#include "pdfcpp/kernel/pdf/pdf_reader.h"

#include <cstring>
#include <set>

#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/source/random_access_source.h"
#include "pdfcpp/kernel/crypto/pdf_encryption.h"
#include "pdfcpp/kernel/pdf/filters/filters.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_xref_table.h"

namespace pdfcpp {
namespace kernel {

namespace {
const Bytes kEndstream = ToBytes("endstream");
const Bytes kEndobj = ToBytes("endobj");

/// A source over a ByteBuffer that can be reused (port of
/// ReusableRandomAccessSource).
class ReusableRandomAccessSource : public io::IRandomAccessSource {
 public:
  explicit ReusableRandomAccessSource(io::ByteBuffer* buffer)
      : buffer_(buffer) {}
  int Get(int64_t offset) override {
    if (offset >= buffer_->size()) return -1;
    return buffer_->GetInternalBuffer()[offset];
  }
  int Get(int64_t offset, uint8_t* bytes, int off, int len) override {
    if (offset >= buffer_->size()) return -1;
    if (offset + len > buffer_->size())
      len = static_cast<int>(buffer_->size() - offset);
    std::memcpy(bytes + off, buffer_->GetInternalBuffer() + offset, len);
    return len;
  }
  int64_t Length() override { return buffer_->size(); }
  void Close() override {}

 private:
  io::ByteBuffer* buffer_;
};
}  // namespace

PdfReader::PdfReader(const std::string& filename,
                     const ReaderProperties& properties)
    : properties_(properties) {
  InitTokenizer(io::RandomAccessSourceFactory().CreateBestSource(filename));
}

PdfReader::PdfReader(io::RandomAccessSourcePtr byte_source,
                     const ReaderProperties& properties)
    : properties_(properties) {
  InitTokenizer(std::move(byte_source));
}

std::unique_ptr<PdfReader> PdfReader::FromBytes(
    const Bytes& bytes, const ReaderProperties& properties) {
  return std::make_unique<PdfReader>(
      io::RandomAccessSourceFactory().CreateSource(bytes), properties);
}

PdfReader::~PdfReader() = default;

void PdfReader::InitTokenizer(io::RandomAccessSourcePtr byte_source) {
  auto tok = std::make_unique<io::PdfTokenizer>(byte_source);
  int offset = tok->GetHeaderOffset();
  if (offset != 0) {
    auto offset_source =
        std::make_shared<io::WindowRandomAccessSource>(byte_source, offset);
    tok = std::make_unique<io::PdfTokenizer>(offset_source);
  }
  tokens_ = std::move(tok);
}

void PdfReader::Close() { tokens_->Close(); }

bool PdfReader::IsOpenedWithFullPermission() const {
  return !encrypted_ || decrypt_->IsOpenedWithFullPermission() ||
         unethical_reading_;
}

int PdfReader::GetPermissions() const {
  if (encrypted_ && decrypt_->GetPermissions())
    return static_cast<int>(*decrypt_->GetPermissions());
  return 0;
}

int PdfReader::GetCryptoMode() const {
  return decrypt_ ? decrypt_->GetCryptoMode() : -1;
}

Bytes PdfReader::GetOriginalFileId() const {
  auto id = trailer_ ? trailer_->GetAsArray(PdfName::kID) : nullptr;
  if (id && id->size() == 2 && id->GetAsString(0))
    return id->GetAsString(0)->GetValueBytes();
  return Bytes();
}

Bytes PdfReader::GetModifiedFileId() const {
  auto id = trailer_ ? trailer_->GetAsArray(PdfName::kID) : nullptr;
  if (id && id->size() == 2 && id->GetAsString(1))
    return id->GetAsString(1)->GetValueBytes();
  return Bytes();
}

// ------------------------------------------------------------ streams
Bytes PdfReader::ReadStreamBytes(PdfStream& stream, bool decode) {
  Bytes b = ReadStreamBytesRaw(stream);
  if (decode && !b.empty()) return DecodeBytes(b, stream);
  return b;
}

Bytes PdfReader::ReadStreamBytesRaw(PdfStream& stream) {
  auto type = stream.GetAsName(PdfName::kType);
  if (!NameEquals(type, PdfName::kXRef) && !NameEquals(type, PdfName::kObjStm))
    CheckPdfStreamLength(stream);
  int64_t offset = stream.GetOffset();
  if (offset <= 0) return Bytes();
  int length = stream.GetLength();
  if (length <= 0) return Bytes();
  auto file = tokens_->GetSafeFile();
  // /XRef and /ObjStm skip checkPdfStreamLength (as in Java): don't allocate a
  // forged /Length beyond the file.
  if (offset + static_cast<int64_t>(length) > file->Length())
    throw io::IoException(
        io::io_exception_message_constant::kUnexpectedEndOfFile);
  file->Seek(offset);
  Bytes bytes(length);
  file->ReadFully(bytes);
  if (decrypt_ && !decrypt_->IsEmbeddedFilesOnly()) {
    PdfObjectPtr filter = stream.Get(PdfName::kFilter, true);
    bool skip = false;
    if (filter) {
      if (NameEquals(filter, PdfName::kCrypt)) {
        skip = true;
      } else if (filter->GetType() == PdfObject::kArray) {
        auto filters = std::static_pointer_cast<PdfArray>(filter);
        for (int k = 0; k < filters->size(); k++) {
          if (NameEquals(filters->Get(k, true), PdfName::kCrypt)) {
            skip = true;
            break;
          }
        }
      }
    }
    if (!skip && !decrypt_->IsMetadataEncrypted() &&
        NameEquals(type, PdfName::kMetadata))
      skip = true;
    if (!skip && stream.GetIndirectReference()) {
      decrypt_->SetHashKeyForNextObject(
          stream.GetIndirectReference()->GetObjNumber(),
          stream.GetIndirectReference()->GetGenNumber());
      bytes = decrypt_->DecryptByteArray(bytes);
    }
  }
  return bytes;
}

Bytes PdfReader::DecodeBytes(const Bytes& b,
                             const PdfDictionary& stream_dictionary) {
  return DecodeBytes(b, stream_dictionary,
                     FilterHandlers::GetDefaultFilterHandlers());
}

Bytes PdfReader::DecodeBytes(const Bytes& input,
                             const PdfDictionary& stream_dictionary,
                             const FilterHandlerMap& filter_handlers) {
  Bytes b = input;
  PdfObjectPtr filter = stream_dictionary.Get(PdfName::kFilter);
  std::vector<PdfObjectPtr> filters;
  if (filter) {
    if (filter->GetType() == PdfObject::kName)
      filters.push_back(filter);
    else if (filter->GetType() == PdfObject::kArray) {
      auto arr = std::static_pointer_cast<PdfArray>(filter);
      for (int i = 0; i < arr->size(); ++i) filters.push_back(arr->Get(i));
    }
  }
  std::vector<PdfObjectPtr> dp;
  PdfObjectPtr dpo = stream_dictionary.Get(PdfName::kDecodeParms);
  if (!dpo || (dpo->GetType() != PdfObject::kDictionary &&
               dpo->GetType() != PdfObject::kArray))
    dpo = stream_dictionary.Get(PdfName::kDP);
  if (dpo) {
    if (dpo->GetType() == PdfObject::kDictionary)
      dp.push_back(dpo);
    else if (dpo->GetType() == PdfObject::kArray) {
      auto arr = std::static_pointer_cast<PdfArray>(dpo);
      for (int i = 0; i < arr->size(); ++i) dp.push_back(arr->Get(i, true));
    }
  }
  for (size_t j = 0; j < filters.size(); ++j) {
    auto filter_name = PdfCast<PdfName>(filters[j]);
    if (!filter_name)
      throw PdfException(Format(
          kernel_exception_message_constant::kFilterIsNotSupported, "?"));
    auto it = filter_handlers.find(filter_name->GetValue());
    if (it == filter_handlers.end())
      throw PdfException(
          Format(kernel_exception_message_constant::kFilterIsNotSupported,
                 filter_name->GetValue()));
    const PdfDictionary* decode_params = nullptr;
    if (j < dp.size()) {
      const PdfObjectPtr& entry = dp[j];
      if (!entry || entry->GetType() == PdfObject::kNull)
        decode_params = nullptr;
      else if (entry->GetType() == PdfObject::kDictionary)
        decode_params = static_cast<const PdfDictionary*>(entry.get());
      else
        throw PdfException(Format(kernel_exception_message_constant::
                                      kThisDecodeParameterTypeIsNotSupported,
                                  entry->ToString()));
    }
    b = it->second->Decode(b, *filter_name, decode_params, stream_dictionary);
  }
  return b;
}

// ------------------------------------------------------------ reading
void PdfReader::ReadPdf() {
  std::string version = tokens_->CheckPdfHeader();
  try {
    header_pdf_version_ = PdfVersion::FromString(version);
  } catch (...) {
    throw PdfException("PDF version is not valid: " + version);
  }
  try {
    ReadXref();
  } catch (const XrefCycledReferencesException&) {
    throw;
  } catch (const MemoryLimitsAwareException&) {
    throw;
  } catch (const std::exception&) {
    if (IsStricter(StrictnessLevel::kConservative, strictness_level_))
      RebuildXref();
    else
      throw;
  }
  pdf_document_->GetXref()->MarkReadingCompleted();
  ReadDecryptObj();
}

void PdfReader::ReadDecryptObj() {
  if (encrypted_) return;
  auto enc = trailer_->GetAsDictionary(PdfName::kEncrypt);
  if (!enc) return;
  encrypted_ = true;
  auto filter = enc->GetAsName(PdfName::kFilter);
  if (NameEquals(filter, PdfName::kStandard)) {
    decrypt_ = std::make_unique<PdfEncryption>(enc, properties_.password,
                                               GetOriginalFileId());
  } else {
    throw PdfException("Unsupported security handler: " +
                       (filter ? filter->GetValue() : std::string("null")));
  }
}

void PdfReader::ReadObjectStream(PdfStream& object_stream) {
  int object_stream_number =
      object_stream.GetIndirectReference()->GetObjNumber();
  auto first_num = object_stream.GetAsNumber(PdfName::kFirst);
  auto n_num = object_stream.GetAsNumber(PdfName::kN);
  if (!first_num || !n_num)
    throw PdfException("Error while reading object stream.");
  int first = first_num->IntValue();
  int n = n_num->IntValue();
  Bytes bytes = ReadStreamBytes(object_stream, true);
  // Each entry of the header needs at least two numbers ("1 0 "), so /N can't
  // exceed the decoded size.
  if (n < 0 || first < 0 || static_cast<size_t>(n) > bytes.size())
    throw MemoryLimitsAwareException("Object stream /N exceeds the limit.");
  auto save_tokens = std::move(tokens_);
  tokens_ = std::make_unique<io::PdfTokenizer>(
      io::RandomAccessSourceFactory().CreateSource(bytes));
  try {
    std::vector<int> address(n), obj_number(n);
    bool ok = true;
    for (int k = 0; k < n; ++k) {
      ok = tokens_->NextToken();
      if (!ok) break;
      if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber) {
        ok = false;
        break;
      }
      obj_number[k] = tokens_->GetIntValue();
      ok = tokens_->NextToken();
      if (!ok) break;
      if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber) {
        ok = false;
        break;
      }
      address[k] = tokens_->GetIntValue() + first;
    }
    if (!ok) throw PdfException("Error while reading object stream.");
    for (int k = 0; k < n; ++k) {
      tokens_->Seek(address[k]);
      tokens_->NextToken();
      PdfIndirectReference* reference =
          pdf_document_->GetXref()->Get(obj_number[k]);
      if (reference == nullptr || reference->RefersToRaw() != nullptr ||
          reference->GetObjStreamNumber() != object_stream_number)
        continue;
      PdfObjectPtr obj;
      if (tokens_->GetTokenType() == io::PdfTokenizer::TokenType::kNumber) {
        obj = std::make_shared<PdfNumber>(tokens_->GetByteContent());
      } else {
        tokens_->Seek(address[k]);
        obj = ReadObject(false, true);
      }
      if (!obj) continue;
      reference->SetRefersTo(obj);
      obj->SetIndirectReference(reference);
    }
    object_stream.GetIndirectReference()->SetState(
        PdfObject::kOriginalObjectStream);
  } catch (...) {
    tokens_ = std::move(save_tokens);
    throw;
  }
  tokens_ = std::move(save_tokens);
}

PdfObjectPtr PdfReader::ReadObject(PdfIndirectReference* reference) {
  return ReadObject(reference, true);
}

PdfObjectPtr PdfReader::ReadObject(PdfIndirectReference* reference,
                                   bool fix_xref_flag) {
  if (reference == nullptr) return nullptr;
  if (reference->RefersToRaw() != nullptr) return reference->RefersToRaw();
  current_indirect_reference_ = reference;
  if (reference->GetObjStreamNumber() > 0) {
    PdfIndirectReference* os_ref =
        pdf_document_->GetXref()->Get(reference->GetObjStreamNumber());
    PdfObjectPtr os = os_ref ? os_ref->GetRefersToPtr(false) : nullptr;
    auto object_stream = PdfCast<PdfStream>(os);
    if (!object_stream)
      throw PdfException(
          Format(kernel_exception_message_constant::kInvalidObjectStreamNumber,
                 reference->GetObjNumber(), reference->GetObjStreamNumber(),
                 reference->GetIndex()));
    ReadObjectStream(*object_stream);
    return reference->RefersToRaw();
  } else if (reference->GetOffset() > 0) {
    PdfObjectPtr object;
    try {
      tokens_->Seek(reference->GetOffset());
      tokens_->NextValidToken();
      if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kObj ||
          tokens_->GetObjNr() != reference->GetObjNumber() ||
          tokens_->GetGenNr() != reference->GetGenNumber())
        tokens_->ThrowError("Invalid offset for object " +
                            reference->ToString() + ".");
      object = ReadObject(false);
    } catch (const std::exception&) {
      if (fix_xref_flag && reference->GetObjStreamNumber() == 0) {
        FixXref();
        return ReadObject(reference, false);
      }
      throw;
    }
    if (object) object->SetIndirectReference(reference);
    return object;
  }
  return nullptr;
}

PdfObjectPtr PdfReader::ReadObject(bool read_as_direct, bool obj_stm) {
  tokens_->NextValidToken();
  using TT = io::PdfTokenizer::TokenType;
  TT type = tokens_->GetTokenType();
  switch (type) {
    case TT::kStartDic: {
      PdfDictionaryPtr dict = ReadDictionary(obj_stm);
      int64_t pos = tokens_->GetPosition();
      bool has_next;
      do {
        has_next = tokens_->NextToken();
      } while (has_next && tokens_->GetTokenType() == TT::kComment);
      if (has_next && tokens_->TokenValueEqualsTo(io::PdfTokenizer::kStream)) {
        int ch;
        do {
          ch = tokens_->Read();
        } while (ch == 32 || ch == 9 || ch == 0 || ch == 12);
        if (ch != '\n') ch = tokens_->Read();
        if (ch != '\n') tokens_->BackOnePosition(ch);
        auto pdf_stream =
            std::make_shared<PdfStream>(tokens_->GetPosition(), *dict);
        tokens_->Seek(pdf_stream->GetOffset() + pdf_stream->GetLength());
        return pdf_stream;
      }
      tokens_->Seek(pos);
      return dict;
    }
    case TT::kStartArray:
      return ReadArray(obj_stm);
    case TT::kNumber:
      return std::make_shared<PdfNumber>(tokens_->GetByteContent());
    case TT::kString: {
      auto pdf_string = std::make_shared<PdfString>(tokens_->GetByteContent(),
                                                    tokens_->IsHexString());
      if (encrypted_ && decrypt_ && !decrypt_->IsEmbeddedFilesOnly() &&
          !obj_stm && current_indirect_reference_)
        pdf_string->SetDecryption(current_indirect_reference_->GetObjNumber(),
                                  current_indirect_reference_->GetGenNumber(),
                                  decrypt_.get());
      return pdf_string;
    }
    case TT::kName:
      return ReadPdfName(read_as_direct);
    case TT::kRef:
      return ReadReference(read_as_direct);
    case TT::kEndOfFile:
      throw PdfException(
          kernel_exception_message_constant::kUnexpectedEndOfFile);
    default:
      if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kNull))
        return CreatePdfNullInstance(read_as_direct);
      if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kTrue))
        return read_as_direct ? PdfObjectPtr(PdfBoolean::kTrueValue)
                              : std::make_shared<PdfBoolean>(true);
      if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kFalse))
        return read_as_direct ? PdfObjectPtr(PdfBoolean::kFalseValue)
                              : std::make_shared<PdfBoolean>(false);
      return nullptr;
  }
}

PdfObjectPtr PdfReader::ReadReference(bool read_as_direct) {
  int num = tokens_->GetObjNr();
  if (num < 0) return CreatePdfNullInstance(read_as_direct);
  PdfXrefTable* table = pdf_document_->GetXref();
  PdfIndirectReferencePtr reference = table->GetPtr(num);
  if (reference) {
    if (reference->IsFree()) return CreatePdfNullInstance(read_as_direct);
    if (reference->GetGenNumber() != tokens_->GetGenNr()) {
      if (fixed_xref_) return CreatePdfNullInstance(read_as_direct);
      throw PdfException(
          Format(kernel_exception_message_constant::kInvalidIndirectReference,
                 std::to_string(reference->GetObjNumber()) + " " +
                     std::to_string(reference->GetGenNumber())));
    }
  } else {
    if (table->IsReadingCompleted())
      return CreatePdfNullInstance(read_as_direct);
    if (static_cast<int64_t>(num) >
        std::max<int64_t>(1 << 20, tokens_->Length() * 4))
      return CreatePdfNullInstance(read_as_direct);
    reference = std::make_shared<PdfIndirectReference>(pdf_document_, num,
                                                       tokens_->GetGenNr(), 0);
    reference->SetState(PdfObject::kReading);
    table->Add(reference);
  }
  return reference;
}

PdfNamePtr PdfReader::ReadPdfName(bool read_as_direct) {
  if (read_as_direct) {
    std::string raw = tokens_->GetStringValue();
    if (raw.find('#') == std::string::npos) return PdfName::Of(raw);
  }
  return std::make_shared<PdfName>(tokens_->GetByteContent());
}

PdfDictionaryPtr PdfReader::ReadDictionary(bool obj_stm) {
  auto dic = std::make_shared<PdfDictionary>();
  using TT = io::PdfTokenizer::TokenType;
  while (true) {
    tokens_->NextValidToken();
    if (tokens_->GetTokenType() == TT::kEndDic) break;
    if (tokens_->GetTokenType() != TT::kName)
      tokens_->ThrowError(
          Format(kernel_exception_message_constant::kDictionaryKeyIsNotAName,
                 tokens_->GetStringValue()));
    PdfNamePtr name = ReadPdfName(true);
    PdfObjectPtr obj = ReadObject(true, obj_stm);
    if (!obj) {
      if (tokens_->GetTokenType() == TT::kEndDic)
        tokens_->ThrowError(
            Format(kernel_exception_message_constant::kUnexpectedToken, ">>"));
      if (tokens_->GetTokenType() == TT::kEndArray)
        tokens_->ThrowError(
            Format(kernel_exception_message_constant::kUnexpectedToken, "]"));
    }
    dic->Put(name, obj ? obj : PdfObjectPtr(PdfNull::kPdfNull));
  }
  return dic;
}

PdfArrayPtr PdfReader::ReadArray(bool obj_stm) {
  auto array = std::make_shared<PdfArray>();
  while (true) {
    PdfObjectPtr obj = ReadObject(true, obj_stm);
    if (!obj) {
      if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kEndArray) {
        if (!IsStricter(StrictnessLevel::kConservative, strictness_level_))
          tokens_->ThrowError(
              Format(kernel_exception_message_constant::kUnexpectedToken,
                     tokens_->GetStringValue()));
      }
      break;
    }
    array->Add(obj);
  }
  return array;
}

void PdfReader::ReadXref() {
  tokens_->Seek(tokens_->GetStartxref());
  tokens_->NextToken();
  if (!tokens_->TokenValueEqualsTo(io::PdfTokenizer::kStartxref))
    throw PdfException("PDF startxref not found.");
  tokens_->NextToken();
  if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber)
    throw PdfException("PDF startxref is not followed by a number.");
  int64_t startxref = tokens_->GetLongValue();
  last_xref_ = startxref;
  eof_pos_ = tokens_->GetPosition();
  try {
    if (ReadXrefStream(startxref)) {
      xref_stm_ = true;
      return;
    }
  } catch (const XrefCycledReferencesException&) {
    throw;
  } catch (const MemoryLimitsAwareException&) {
    throw;
  } catch (const std::exception&) {
  }
  pdf_document_->GetXref()->clear();
  tokens_->Seek(startxref);
  trailer_ = ReadXrefSection();
  PdfDictionaryPtr trailer2 = trailer_;
  std::set<int64_t> visited;
  while (true) {
    visited.insert(startxref);
    PdfNumberPtr prev = GetXrefPrev(trailer2->Get(PdfName::kPrev, false));
    if (!prev) break;
    int64_t prev_offset = prev->LongValue();
    if (visited.count(prev_offset))
      throw XrefCycledReferencesException("Xref table has cycled references.");
    startxref = prev_offset;
    tokens_->Seek(startxref);
    trailer2 = ReadXrefSection();
  }
  if (!trailer_->GetAsInt(PdfName::kSize))
    throw PdfException("Invalid xref table.");
}

PdfDictionaryPtr PdfReader::ReadXrefSection() {
  tokens_->NextValidToken();
  if (!tokens_->TokenValueEqualsTo(io::PdfTokenizer::kXref))
    tokens_->ThrowError("xref subsection not found.");
  PdfXrefTable* xref = pdf_document_->GetXref();
  while (true) {
    tokens_->NextValidToken();
    if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kTrailer)) break;
    if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber)
      tokens_->ThrowError(
          "Object number of the first object in this xref subsection not "
          "found.");
    int start = tokens_->GetIntValue();
    tokens_->NextValidToken();
    if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber)
      tokens_->ThrowError(
          "Number of entries in this xref subsection not found.");
    int end = tokens_->GetIntValue() + start;
    if (start < 0 || end < start ||
        static_cast<int64_t>(end) >
            std::max<int64_t>(1 << 20, tokens_->Length() * 4))
      throw MemoryLimitsAwareException(
          "Xref structure size exceeds the limit.");
    for (int num = start; num < end; num++) {
      tokens_->NextValidToken();
      int64_t pos = tokens_->GetLongValue();
      tokens_->NextValidToken();
      int gen = tokens_->GetIntValue();
      tokens_->NextValidToken();
      if (pos == 0 && gen == 65535 && num == 1 && start != 0) {
        num = 0;
        end--;
        continue;
      }
      PdfIndirectReference* reference = xref->Get(num);
      bool ref_reading_state = reference != nullptr &&
                               reference->CheckState(PdfObject::kReading) &&
                               reference->GetGenNumber() == gen;
      bool ref_first_encountered =
          reference == nullptr ||
          (!ref_reading_state && reference->GetDocument() == nullptr);
      PdfIndirectReferencePtr new_ref;
      if (ref_first_encountered) {
        new_ref = std::make_shared<PdfIndirectReference>(pdf_document_, num,
                                                         gen, pos);
        reference = new_ref.get();
      } else if (ref_reading_state) {
        reference->SetOffset(pos);
        reference->ClearState(PdfObject::kReading);
      } else {
        continue;
      }
      if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kN)) {
        if (pos == 0)
          tokens_->ThrowError(
              "File position 0 cross-reference entry in this xref subsection.");
      } else if (tokens_->TokenValueEqualsTo(io::PdfTokenizer::kF)) {
        if (ref_first_encountered) reference->SetState(PdfObject::kFree);
      } else {
        tokens_->ThrowError(
            "Invalid cross reference entry in this xref subsection.");
      }
      if (ref_first_encountered) xref->Add(new_ref);
    }
  }
  auto trailer = PdfCast<PdfDictionary>(ReadObject(false));
  if (!trailer)
    throw PdfException(kernel_exception_message_constant::kTrailerNotFound);
  PdfObjectPtr xrs = trailer->Get(PdfName::kXRefStm);
  if (xrs && xrs->GetType() == PdfObject::kNumber) {
    int64_t loc = std::static_pointer_cast<PdfNumber>(xrs)->LongValue();
    try {
      ReadXrefStream(loc);
      xref_stm_ = true;
      hybrid_xref_ = true;
    } catch (...) {
      xref->clear();
      throw;
    }
  }
  return trailer;
}

bool PdfReader::ReadXrefStream(int64_t ptr) {
  std::set<int64_t> visited;
  while (ptr != -1) {
    tokens_->Seek(ptr);
    if (!tokens_->NextToken()) return false;
    if (tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber)
      return false;
    if (!tokens_->NextToken() ||
        tokens_->GetTokenType() != io::PdfTokenizer::TokenType::kNumber)
      return false;
    if (!tokens_->NextToken() ||
        !tokens_->TokenValueEqualsTo(io::PdfTokenizer::kObj))
      return false;
    visited.insert(ptr);
    PdfXrefTable* xref = pdf_document_->GetXref();
    PdfObjectPtr object = ReadObject(false);
    auto xref_stream = PdfCast<PdfStream>(object);
    if (!xref_stream ||
        !NameEquals(xref_stream->Get(PdfName::kType), PdfName::kXRef))
      return false;
    if (!trailer_) {
      trailer_ = std::make_shared<PdfDictionary>();
      trailer_->PutAll(*xref_stream);
      trailer_->Remove(PdfName::kDecodeParms);
      trailer_->Remove(PdfName::kFilter);
      trailer_->Remove(PdfName::kPrev);
      trailer_->Remove(PdfName::kLength);
    }
    auto size_num = PdfCast<PdfNumber>(xref_stream->Get(PdfName::kSize));
    if (!size_num)
      throw PdfException(kernel_exception_message_constant::kInvalidXrefStream);
    int size = size_num->IntValue();
    PdfArrayPtr index;
    PdfObjectPtr obj = xref_stream->Get(PdfName::kIndex);
    if (!obj) {
      index = std::make_shared<PdfArray>();
      index->Add(MakeNumber(0));
      index->Add(MakeNumber(size));
    } else {
      index = PdfCast<PdfArray>(obj);
    }
    PdfArrayPtr w = xref_stream->GetAsArray(PdfName::kW);
    if (!w || !index)
      throw PdfException(kernel_exception_message_constant::kInvalidXrefStream);
    int64_t prev = -1;
    auto prev_num = GetXrefPrev(xref_stream->Get(PdfName::kPrev, false));
    if (prev_num) prev = prev_num->LongValue();
    // Guard against absurd /Size values (port of MemoryLimitsAwareHandler
    // checks).
    const int64_t max_elements =
        std::max<int64_t>(1 << 20, tokens_->Length() * 4);
    if (size < 0 || size > max_elements)
      throw MemoryLimitsAwareException(
          "Xref structure size exceeds the limit.");
    xref->SetCapacity(size);
    // the xref stream object must be readable before references are resolved:
    // read directly
    xref_stream->UpdateLength(
        xref_stream->GetAsNumber(PdfName::kLength)
            ? xref_stream->GetAsNumber(PdfName::kLength)->IntValue()
            : 0);
    Bytes b;
    {
      auto file = tokens_->GetSafeFile();
      if (xref_stream->GetOffset() + std::max(0, xref_stream->GetLength()) >
          file->Length())
        throw io::IoException(
            io::io_exception_message_constant::kUnexpectedEndOfFile);
      file->Seek(xref_stream->GetOffset());
      b.resize(std::max(0, xref_stream->GetLength()));
      file->ReadFully(b);
      b = DecodeBytes(b, *xref_stream);
    }
    size_t bptr = 0;
    int wc[3];
    for (int k = 0; k < 3; ++k) {
      wc[k] = w->GetAsNumber(k) ? w->GetAsNumber(k)->IntValue() : 0;
      // Field widths beyond 8 bytes can't be represented (and negative ones
      // would defeat the bounds check).
      if (wc[k] < 0 || wc[k] > 8)
        throw PdfException(
            kernel_exception_message_constant::kInvalidXrefStream);
    }
    for (int idx = 0; idx + 1 < index->size(); idx += 2) {
      auto start_num = index->GetAsNumber(idx);
      auto length_num = index->GetAsNumber(idx + 1);
      if (!start_num || !length_num)
        throw PdfException(
            kernel_exception_message_constant::kInvalidXrefStream);
      int start = start_num->IntValue();
      int length = length_num->IntValue();
      if (start < 0 || length < 0 ||
          static_cast<int64_t>(start) + length > max_elements)
        throw MemoryLimitsAwareException(
            "Xref structure size exceeds the limit.");
      xref->SetCapacity(start + length);
      while (length-- > 0) {
        if (bptr + wc[0] + wc[1] + wc[2] > b.size()) break;
        // Accumulate unsigned so wide fields can't overflow a signed int;
        // narrow afterwards.
        uint64_t type_field = 1;
        if (wc[0] > 0) {
          type_field = 0;
          for (int k = 0; k < wc[0]; ++k)
            type_field = (type_field << 8) + b[bptr++];
        }
        int type = type_field > 2 ? -1 : static_cast<int>(type_field);
        uint64_t f2 = 0;
        for (int k = 0; k < wc[1]; ++k) f2 = (f2 << 8) + b[bptr++];
        int64_t field2 = static_cast<int64_t>(f2);
        uint64_t f3 = 0;
        for (int k = 0; k < wc[2]; ++k) f3 = (f3 << 8) + b[bptr++];
        int field3 = static_cast<int>(static_cast<uint32_t>(f3));
        int base = start;
        PdfIndirectReferencePtr new_reference;
        switch (type) {
          case 0:
            new_reference = std::make_shared<PdfIndirectReference>(
                pdf_document_, base, field3, field2);
            new_reference->SetState(PdfObject::kFree);
            break;
          case 1:
            new_reference = std::make_shared<PdfIndirectReference>(
                pdf_document_, base, field3, field2);
            break;
          case 2:
            new_reference = std::make_shared<PdfIndirectReference>(
                pdf_document_, base, 0, field3);
            new_reference->SetObjStreamNumber(static_cast<int>(field2));
            break;
          default:
            throw PdfException(
                kernel_exception_message_constant::kInvalidXrefStream);
        }
        PdfIndirectReference* reference = xref->Get(base);
        bool ref_reading_state =
            reference != nullptr &&
            reference->CheckState(PdfObject::kReading) &&
            reference->GetGenNumber() == new_reference->GetGenNumber();
        bool ref_first_encountered =
            reference == nullptr ||
            (!ref_reading_state && reference->GetDocument() == nullptr);
        if (ref_first_encountered) {
          xref->Add(new_reference);
        } else if (ref_reading_state) {
          if (new_reference->GetObjStreamNumber() != 0) {
            reference->SetIndex(new_reference->GetIndex());
            reference->SetObjStreamNumber(new_reference->GetObjStreamNumber());
          } else {
            reference->SetOffset(new_reference->GetOffset());
          }
          reference->ClearState(PdfObject::kReading);
        }
        ++start;
      }
    }
    ptr = prev;
    if (visited.count(ptr))
      throw XrefCycledReferencesException("Xref stream has cycled references.");
  }
  return true;
}

void PdfReader::FixXref() {
  fixed_xref_ = true;
  PdfXrefTable* xref = pdf_document_->GetXref();
  tokens_->Seek(0);
  io::ByteBuffer buffer(24);
  io::PdfTokenizer line_tokenizer(
      std::make_shared<ReusableRandomAccessSource>(&buffer));
  for (;;) {
    int64_t pos = tokens_->GetPosition();
    buffer.Reset();
    if (!tokens_->ReadLineSegment(buffer, true)) break;
    if (buffer.Get(0) >= '0' && buffer.Get(0) <= '9') {
      auto obj = io::PdfTokenizer::CheckObjectStart(line_tokenizer);
      if (!obj) continue;
      PdfIndirectReference* reference = xref->Get((*obj)[0]);
      if (reference != nullptr && reference->GetGenNumber() == (*obj)[1])
        reference->FixOffset(pos);
    }
  }
}

bool PdfReader::IsCurrentObjectATrailer() {
  try {
    auto dic = PdfCast<PdfDictionary>(ReadObject(false));
    return dic && dic->Get(PdfName::kRoot, false) != nullptr;
  } catch (...) {
    return false;
  }
}

void PdfReader::RebuildXref() {
  xref_stm_ = false;
  hybrid_xref_ = false;
  rebuilt_xref_ = true;
  PdfXrefTable* xref = pdf_document_->GetXref();
  xref->clear();
  tokens_->Seek(0);
  trailer_ = nullptr;
  io::ByteBuffer buffer(24);
  io::PdfTokenizer line_tokenizer(
      std::make_shared<ReusableRandomAccessSource>(&buffer));
  std::optional<int64_t> trailer_index;
  for (;;) {
    int64_t pos = tokens_->GetPosition();
    buffer.Reset();
    if (!tokens_->ReadLineSegment(buffer, true)) break;
    if (buffer.Get(0) == 't') {
      if (!io::PdfTokenizer::CheckTrailer(buffer)) continue;
      tokens_->Seek(pos);
      tokens_->NextToken();
      pos = tokens_->GetPosition();
      if (IsCurrentObjectATrailer())
        trailer_index = pos;
      else
        tokens_->Seek(pos);
    } else if (buffer.Get(0) >= '0' && buffer.Get(0) <= '9') {
      auto obj = io::PdfTokenizer::CheckObjectStart(line_tokenizer);
      if (!obj) continue;
      int num = (*obj)[0], gen = (*obj)[1];
      if (xref->Get(num) == nullptr || xref->Get(num)->GetGenNumber() <= gen)
        xref->Add(std::make_shared<PdfIndirectReference>(pdf_document_, num,
                                                         gen, pos));
    }
  }
  if (!trailer_index) {
    // Try to find a cross-reference stream acting as trailer
    for (int i = 1; i < xref->size(); ++i) {
      PdfIndirectReference* ref = xref->Get(i);
      if (!ref) continue;
      try {
        auto s = PdfCast<PdfStream>(ref->GetRefersToPtr(false));
        if (s && NameEquals(s->GetAsName(PdfName::kType), PdfName::kXRef) &&
            s->Get(PdfName::kRoot, false)) {
          trailer_ = std::make_shared<PdfDictionary>();
          trailer_->PutAll(*s);
          trailer_->Remove(PdfName::kDecodeParms);
          trailer_->Remove(PdfName::kFilter);
          trailer_->Remove(PdfName::kPrev);
          trailer_->Remove(PdfName::kLength);
        }
      } catch (...) {
      }
    }
    if (!trailer_)
      throw PdfException(kernel_exception_message_constant::kTrailerNotFound);
    return;
  }
  tokens_->Seek(*trailer_index);
  auto dic = PdfCast<PdfDictionary>(ReadObject(false));
  if (dic && dic->Get(PdfName::kRoot, false)) trailer_ = dic;
  if (!trailer_)
    throw PdfException(kernel_exception_message_constant::kTrailerNotFound);
}

PdfNumberPtr PdfReader::GetXrefPrev(const PdfObjectPtr& prev_object_to_check) {
  if (!prev_object_to_check) return nullptr;
  if (prev_object_to_check->GetType() == PdfObject::kNumber)
    return std::static_pointer_cast<PdfNumber>(prev_object_to_check);
  if (prev_object_to_check->GetType() == PdfObject::kIndirectReference &&
      IsStricter(StrictnessLevel::kConservative, strictness_level_)) {
    auto value = static_cast<PdfIndirectReference*>(prev_object_to_check.get())
                     ->GetRefersToPtr(true);
    if (value && value->GetType() == PdfObject::kNumber)
      return std::static_pointer_cast<PdfNumber>(value);
  }
  throw PdfException(
      "Prev pointer in xref structure shall be direct number object.");
}

void PdfReader::CheckPdfStreamLength(PdfStream& pdf_stream) {
  int64_t file_length = tokens_->Length();
  int64_t start = pdf_stream.GetOffset();
  bool calc = false;
  int stream_length = 0;
  PdfNumberPtr pdf_number = pdf_stream.GetAsNumber(PdfName::kLength);
  if (pdf_number) {
    stream_length = pdf_number->IntValue();
    if (stream_length + start > file_length - 20) {
      calc = true;
    } else {
      tokens_->Seek(start + stream_length);
      std::string line = tokens_->ReadString(20);
      if (line.rfind("\nendstream", 0) != 0 &&
          line.rfind("\r\nendstream", 0) != 0 &&
          line.rfind("\rendstream", 0) != 0 && line.rfind("endstream", 0) != 0)
        calc = true;
    }
  } else {
    pdf_number = MakeNumber(0);
    pdf_stream.Put(PdfName::kLength, pdf_number);
    calc = true;
  }
  if (calc) {
    io::ByteBuffer line(16);
    tokens_->Seek(start);
    int64_t pos;
    while (true) {
      pos = tokens_->GetPosition();
      line.Reset();
      if (!tokens_->ReadLineSegment(line, false)) {
        if (!IsStricter(StrictnessLevel::kConservative, strictness_level_))
          throw PdfException("Stream shall end with endstream keyword.");
        break;
      }
      if (line.StartsWith(kEndstream)) break;
      if (line.StartsWith(kEndobj)) {
        int64_t back = std::max<int64_t>(0, pos - 16);
        tokens_->Seek(back);
        std::string s = tokens_->ReadString(static_cast<int>(pos - back));
        auto index = s.find("endstream");
        if (index != std::string::npos)
          pos = back + static_cast<int64_t>(index);
        break;
      }
    }
    stream_length = static_cast<int>(pos - start);
    if (pos >= 2) {
      tokens_->Seek(pos - 2);
      if (tokens_->Read() == 13) stream_length--;
    }
    if (pos >= 1) {
      tokens_->Seek(pos - 1);
      if (tokens_->Read() == 10) stream_length--;
    }
    if (stream_length < 0) stream_length = 0;
    pdf_number->SetValue(stream_length);
    pdf_stream.UpdateLength(stream_length);
  }
}

PdfObjectPtr PdfReader::CreatePdfNullInstance(bool read_as_direct) {
  if (read_as_direct) return PdfNull::kPdfNull;
  return std::make_shared<PdfNull>();
}

}  // namespace kernel
}  // namespace pdfcpp
