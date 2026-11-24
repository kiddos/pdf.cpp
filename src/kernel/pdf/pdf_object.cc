#include "pdfcpp/kernel/pdf/pdf_object.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/io/util/text_util.h"
#include "pdfcpp/kernel/crypto/pdf_encryption.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_output_stream.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/pdf_xref_table.h"

namespace pdfcpp {
namespace kernel {

NullCopyFilter& NullCopyFilter::GetInstance() {
  static NullCopyFilter instance;
  return instance;
}

// ============================================================ PdfObject
void PdfObject::Flush(bool can_be_in_obj_stm) {
  if (IsFlushed() || GetIndirectReference() == nullptr ||
      GetIndirectReference()->IsFree())
    return;
  PdfDocument* document = GetIndirectReference()->GetDocument();
  if (document != nullptr) {
    if (document->IsAppendMode() && !IsModified()) return;
    document->FlushObject(this,
                          can_be_in_obj_stm && GetType() != kStream &&
                              GetType() != kIndirectReference &&
                              GetIndirectReference()->GetGenNumber() == 0);
  }
}

bool PdfObject::ContainsIndirectReference() const {
  if (IsIndirect()) return true;
  if (GetType() == kDictionary || GetType() == kStream) {
    for (const auto& kv : static_cast<const PdfDictionary*>(this)->EntrySet())
      if (kv.second->ContainsIndirectReference()) return true;
  } else if (GetType() == kArray) {
    for (const auto& v : *static_cast<const PdfArray*>(this))
      if (v->ContainsIndirectReference()) return true;
  }
  return false;
}

bool PdfObject::IsIndirect() const {
  return indirect_reference_ != nullptr || CheckState(kMustBeIndirect);
}

PdfObject* PdfObject::MakeIndirect(PdfDocument* document,
                                   PdfIndirectReference* reference) {
  if (document == nullptr || indirect_reference_ != nullptr) return this;
  if (document->GetWriter() == nullptr)
    throw PdfException(kernel_exception_message_constant::
                           kThereIsNoAssociatePdfWriterForMakingIndirects);
  if (reference == nullptr) {
    indirect_reference_ = document->CreateNextIndirectReference();
    indirect_reference_->SetRefersTo(shared_from_this());
  } else {
    reference->SetState(kModified);
    indirect_reference_ = reference;
    indirect_reference_->SetRefersTo(shared_from_this());
  }
  SetState(kForbidRelease);
  ClearState(kMustBeIndirect);
  return this;
}

bool PdfObject::IsFlushed() const {
  return indirect_reference_ != nullptr &&
         indirect_reference_->CheckState(kFlushed);
}

bool PdfObject::IsModified() const {
  return indirect_reference_ != nullptr &&
         indirect_reference_->CheckState(kModified);
}

PdfObjectPtr PdfObject::Clone(ICopyFilter& filter) const {
  PdfObjectPtr new_object = NewInstance();
  if (indirect_reference_ != nullptr || CheckState(kMustBeIndirect))
    new_object->SetState(kMustBeIndirect);
  new_object->CopyContent(*this, nullptr, filter);
  return new_object;
}

PdfObjectPtr PdfObject::CopyTo(PdfDocument* document, bool allow_duplicating,
                               ICopyFilter& copy_filter) const {
  if (document == nullptr)
    throw PdfException(
        kernel_exception_message_constant::kDocumentForCopyToCannotBeNull);
  if (indirect_reference_ != nullptr) {
    if (indirect_reference_->GetWriter() != nullptr ||
        CheckState(kMustBeIndirect))
      throw PdfException(
          kernel_exception_message_constant::
              kCannotCopyIndirectObjectFromTheDocumentThatIsBeingWritten);
    if (!indirect_reference_->GetReader()->IsOpenedWithFullPermission())
      throw BadPasswordException(
          BadPasswordException::kPdfReaderNotOpenedWithOwnerPassword);
  }
  return ProcessCopying(document, allow_duplicating, copy_filter);
}

PdfObject* PdfObject::SetModified() {
  if (indirect_reference_ != nullptr) {
    indirect_reference_->SetState(kModified);
    SetState(kForbidRelease);
  }
  return this;
}

void PdfObject::Release() {
  if (IsReleaseForbidden()) return;
  if (indirect_reference_ != nullptr &&
      indirect_reference_->GetReader() != nullptr &&
      !indirect_reference_->CheckState(kFlushed)) {
    PdfIndirectReference* ref = indirect_reference_;
    indirect_reference_ = nullptr;
    SetState(kReadOnly);
    ref->SetRefersTo(nullptr);  // may destroy this object; nothing must be
                                // touched afterwards
  }
}

void PdfObject::CopyContent(const PdfObject&, PdfDocument*, ICopyFilter&) {
  if (IsFlushed())
    throw PdfException(
        kernel_exception_message_constant::kCannotCopyFlushedObject);
}

bool PdfObject::EqualContent(const PdfObject* obj1, const PdfObject* obj2) {
  const PdfObject* d1 = obj1;
  const PdfObject* d2 = obj2;
  if (obj1 && obj1->IsIndirectReference())
    d1 = const_cast<PdfIndirectReference*>(
             static_cast<const PdfIndirectReference*>(obj1))
             ->GetRefersTo(true);
  if (obj2 && obj2->IsIndirectReference())
    d2 = const_cast<PdfIndirectReference*>(
             static_cast<const PdfIndirectReference*>(obj2))
             ->GetRefersTo(true);
  return d1 != nullptr && d2 != nullptr && d1->Equals(*d2);
}

PdfObjectPtr PdfObject::ProcessCopying(PdfDocument* document_to,
                                       bool allow_duplicating,
                                       ICopyFilter& filter) const {
  if (document_to != nullptr) {
    PdfWriter* writer = document_to->GetWriter();
    if (writer == nullptr)
      throw PdfException(kernel_exception_message_constant::
                             kCannotCopyToDocumentOpenedInReadingMode);
    return writer->CopyObject(const_cast<PdfObject*>(this), document_to,
                              allow_duplicating, filter);
  }
  const PdfObject* obj = this;
  if (obj->IsIndirectReference()) {
    PdfObject* ref_to = const_cast<PdfIndirectReference*>(
                            static_cast<const PdfIndirectReference*>(obj))
                            ->GetRefersTo();
    obj = ref_to != nullptr ? ref_to : obj;
  }
  if (obj->IsIndirect() && !allow_duplicating)
    return const_cast<PdfObject*>(obj)->shared_from_this();
  return obj->Clone();
}

// ============================================================
// PdfPrimitiveObject
PdfObject* PdfPrimitiveObject::MakeIndirect(PdfDocument* document,
                                            PdfIndirectReference* reference) {
  if (direct_only_) return this;
  return PdfObject::MakeIndirect(document, reference);
}

PdfObject* PdfPrimitiveObject::SetIndirectReference(
    PdfIndirectReference* indirect_reference) {
  if (!direct_only_) PdfObject::SetIndirectReference(indirect_reference);
  return this;
}

const Bytes& PdfPrimitiveObject::GetInternalContent() const {
  if (!content_set_) GenerateContent();
  return content_;
}

void PdfPrimitiveObject::CopyContent(const PdfObject& from,
                                     PdfDocument* document,
                                     ICopyFilter& filter) {
  PdfObject::CopyContent(from, document, filter);
  const auto& object = static_cast<const PdfPrimitiveObject&>(from);
  if (object.content_set_) SetContent(object.content_);
}

int PdfPrimitiveObject::CompareContent(const PdfPrimitiveObject& o) const {
  const Bytes& a = GetInternalContent();
  const Bytes& b = o.GetInternalContent();
  for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
    int8_t x = static_cast<int8_t>(a[i]), y = static_cast<int8_t>(b[i]);
    if (x > y) return 1;
    if (x < y) return -1;
  }
  return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}

// ============================================================ PdfNull /
// PdfBoolean
const PdfNullPtr PdfNull::kPdfNull = [] {
  auto n = std::make_shared<PdfNull>();
  return n;
}();

const PdfBooleanPtr PdfBoolean::kTrueValue =
    std::make_shared<PdfBoolean>(true, true);
const PdfBooleanPtr PdfBoolean::kFalseValue =
    std::make_shared<PdfBoolean>(false, true);

void PdfBoolean::CopyContent(const PdfObject& from, PdfDocument* document,
                             ICopyFilter& filter) {
  PdfPrimitiveObject::CopyContent(from, document, filter);
  value_ = static_cast<const PdfBoolean&>(from).value_;
}

// ============================================================ PdfNumber
PdfNumber::PdfNumber(const Bytes& content) : PdfPrimitiveObject(content) {
  is_double_ = true;
  value_valid_ = false;
}

double PdfNumber::GetValue() const {
  if (!value_valid_) GenerateValue();
  return value_;
}

int PdfNumber::IntValue() const {
  double v = GetValue();
  if (std::isnan(v)) return 0;
  if (v > 2147483647.0) return 2147483647;
  if (v < -2147483648.0) return -2147483647 - 1;
  return static_cast<int>(v);
}

void PdfNumber::SetValue(int value) {
  value_ = value;
  is_double_ = false;
  value_valid_ = true;
  ClearContent();
}

void PdfNumber::SetValue(double value) {
  value_ = value;
  is_double_ = true;
  value_valid_ = true;
  ClearContent();
}

std::string PdfNumber::ToString() const {
  if (content_set_) return ToLatin1String(content_);
  if (is_double_) return ToLatin1String(io::ByteUtils::GetIsoBytes(GetValue()));
  return std::to_string(IntValue());
}

bool PdfNumber::Equals(const PdfObject& o) const {
  if (o.GetType() != kNumber) return false;
  double a = GetValue(), b = static_cast<const PdfNumber&>(o).GetValue();
  return a == b || (std::isnan(a) && std::isnan(b));
}

void PdfNumber::GenerateContent() const {
  if (is_double_)
    SetContent(io::ByteUtils::GetIsoBytes(value_));
  else
    SetContent(io::ByteUtils::GetIsoBytes(static_cast<int>(value_)));
}

void PdfNumber::GenerateValue() const {
  std::string s = ToLatin1String(content_);
  char* end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str()) v = std::nan("");
  value_ = v;
  is_double_ = true;
  value_valid_ = true;
}

void PdfNumber::CopyContent(const PdfObject& from, PdfDocument* document,
                            ICopyFilter& filter) {
  PdfPrimitiveObject::CopyContent(from, document, filter);
  const auto& number = static_cast<const PdfNumber&>(from);
  value_ = number.value_;
  is_double_ = number.is_double_;
  value_valid_ = number.value_valid_;
}

// ============================================================ PdfName
PdfNamePtr PdfName::CreateDirectName(const std::string& name) {
  return PdfNamePtr(new PdfName(name, true));
}

namespace {
const std::map<std::string, PdfNamePtr>& StaticNames();
}

PdfNamePtr PdfName::Of(const std::string& value) {
  const auto& names = StaticNames();
  auto it = names.find(value);
  if (it != names.end()) return it->second;
  return std::make_shared<PdfName>(value);
}

const std::string& PdfName::GetValue() const {
  if (!value_set_) GenerateValue();
  return value_;
}

void PdfName::GenerateValue() const {
  // PdfNameUtil.decodeName: decode #xx escapes
  std::string buf;
  const Bytes& content = content_;
  for (size_t k = 0; k < content.size(); ++k) {
    char c = static_cast<char>(content[k]);
    if (c == '#' && k + 2 < content.size()) {
      int c1 = io::ByteBuffer::GetHex(content[k + 1]);
      int c2 = io::ByteBuffer::GetHex(content[k + 2]);
      if (c1 >= 0 && c2 >= 0) {
        c = static_cast<char>((c1 << 4) + c2);
        k += 2;
      }
    }
    buf += c;
  }
  value_ = buf;
  value_set_ = true;
}

void PdfName::GenerateContent() const {
  static const char* hex = "0123456789abcdef";
  Bytes buf;
  buf.reserve(value_.size() + 20);
  for (char ch : value_) {
    uint8_t c = static_cast<uint8_t>(ch);
    switch (c) {
      case ' ':
      case '%':
      case '(':
      case ')':
      case '<':
      case '>':
      case '[':
      case ']':
      case '{':
      case '}':
      case '/':
      case '#':
        buf.push_back('#');
        buf.push_back(hex[c >> 4]);
        buf.push_back(hex[c & 0xf]);
        break;
      default:
        if (c >= 32 && c <= 126) {
          buf.push_back(c);
        } else {
          buf.push_back('#');
          buf.push_back(hex[c >> 4]);
          buf.push_back(hex[c & 0xf]);
        }
    }
  }
  SetContent(std::move(buf));
}

std::string PdfName::ToString() const {
  if (content_set_) return "/" + ToLatin1String(content_);
  return "/" + GetValue();
}

void PdfName::CopyContent(const PdfObject& from, PdfDocument* document,
                          ICopyFilter& filter) {
  PdfPrimitiveObject::CopyContent(from, document, filter);
  const auto& name = static_cast<const PdfName&>(from);
  value_ = name.value_;
  value_set_ = name.value_set_;
}

// ============================================================ PdfString
PdfString::PdfString(const Bytes& content) {
  value_.clear();
  for (uint8_t b : content) io::TextUtil::AppendUtf8(value_, b);
  value_set_ = true;
}

PdfString::PdfString(const Bytes& content, bool hex_writing)
    : PdfPrimitiveObject(content), hex_writing_(hex_writing) {}

PdfString* PdfString::SetHexWriting(bool hex_writing) {
  if (!value_set_) GenerateValue();
  ClearContent();
  hex_writing_ = hex_writing;
  return this;
}

const std::string& PdfString::GetValue() const {
  if (!value_set_) GenerateValue();
  return value_;
}

std::string PdfString::ToUnicodeString() const {
  if (!encoding_.empty()) return GetValue();
  if (!content_set_) GenerateContent();
  Bytes b = DecodeContent();
  if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF)
    return io::PdfEncodings::ConvertToString(b, io::PdfEncodings::kUnicodeBig);
  if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF)
    return io::PdfEncodings::ConvertToString(b, io::PdfEncodings::kUtf8);
  return io::PdfEncodings::ConvertToString(b,
                                           io::PdfEncodings::kPdfDocEncoding);
}

Bytes PdfString::GetValueBytes() const {
  if (!value_set_) GenerateValue();
  if (encoding_ == io::PdfEncodings::kUnicodeBig &&
      io::PdfEncodings::IsPdfDocEncoding(value_))
    return io::PdfEncodings::ConvertToBytes(value_,
                                            io::PdfEncodings::kPdfDocEncoding);
  return io::PdfEncodings::ConvertToBytes(value_, encoding_);
}

bool PdfString::Equals(const PdfObject& o) const {
  if (this == &o) return true;
  if (o.GetType() != kString) return false;
  const auto& that = static_cast<const PdfString&>(o);
  return GetValue() == that.GetValue() && GetEncoding() == that.GetEncoding();
}

std::string PdfString::ToString() const {
  if (!value_set_) return ToLatin1String(DecodeContent());
  return GetValue();
}

void PdfString::SetDecryption(int decrypt_info_num, int decrypt_info_gen,
                              PdfEncryption* decryption) {
  decrypt_info_num_ = decrypt_info_num;
  decrypt_info_gen_ = decrypt_info_gen;
  decryption_ = decryption;
}

void PdfString::GenerateValue() const {
  Bytes decoded = DecodeContent();
  value_.clear();
  for (uint8_t b : decoded) io::TextUtil::AppendUtf8(value_, b);
  value_set_ = true;
  if (decryption_ != nullptr) {
    decryption_ = nullptr;
    ClearContent();
  }
}

void PdfString::GenerateContent() const {
  SetContent(EncodeBytes(GetValueBytes()));
}

bool PdfString::Encrypt(PdfEncryption* encrypt) {
  if (CheckState(kUnencrypted)) return false;
  if (encrypt != decryption_) {
    if (decryption_ != nullptr) GenerateValue();
    if (encrypt != nullptr && !encrypt->IsEmbeddedFilesOnly()) {
      Bytes b = encrypt->EncryptByteArray(GetValueBytes());
      SetContent(EncodeBytes(b));
      return true;
    }
  }
  return false;
}

Bytes PdfString::DecodeContent() const {
  Bytes decoded = io::PdfTokenizer::DecodeStringContent(content_, hex_writing_);
  if (decryption_ != nullptr && !CheckState(kUnencrypted)) {
    decryption_->SetHashKeyForNextObject(decrypt_info_num_, decrypt_info_gen_);
    decoded = decryption_->DecryptByteArray(decoded);
  }
  return decoded;
}

Bytes PdfString::EncodeBytes(const Bytes& bytes) const {
  if (bytes.empty()) return bytes;
  if (hex_writing_) {
    io::ByteBuffer buf(static_cast<int>(bytes.size()) * 2);
    for (uint8_t b : bytes) buf.AppendHex(b);
    return buf.ToByteArray();
  }
  io::ByteBuffer buf = io::StreamUtil::CreateBufferedEscapedString(bytes);
  return buf.ToByteArray(1, buf.size() - 2);
}

void PdfString::CopyContent(const PdfObject& from, PdfDocument* document,
                            ICopyFilter& filter) {
  PdfPrimitiveObject::CopyContent(from, document, filter);
  const auto& s = static_cast<const PdfString&>(from);
  value_ = s.value_;
  value_set_ = s.value_set_;
  hex_writing_ = s.hex_writing_;
  decryption_ = s.decryption_;
  decrypt_info_num_ = s.decrypt_info_num_;
  decrypt_info_gen_ = s.decrypt_info_gen_;
  encoding_ = s.encoding_;
}

// ============================================================ PdfLiteral
void PdfLiteral::CopyContent(const PdfObject& from, PdfDocument* document,
                             ICopyFilter& filter) {
  PdfPrimitiveObject::CopyContent(from, document, filter);
  position_ = static_cast<const PdfLiteral&>(from).position_;
}

// ============================================================
// PdfIndirectReference
PdfObject* PdfIndirectReference::GetRefersTo(bool recursively) {
  return GetRefersToPtr(recursively).get();
}

PdfObjectPtr PdfIndirectReference::GetRefersToPtr(bool recursively) {
  if (!recursively) {
    if (refers_to_ == nullptr && !CheckState(kFlushed) &&
        !CheckState(kModified) && !CheckState(kFree) &&
        GetReader() != nullptr) {
      if (resolving_reference_in_process_)
        throw XrefCycledReferencesException(
            Format(kernel_exception_message_constant::
                       kXrefStreamHasSelfReferencedObject,
                   obj_nr_));
      resolving_reference_in_process_ = true;
      try {
        refers_to_ = GetReader()->ReadObject(this);
      } catch (...) {
        resolving_reference_in_process_ = false;
        throw;
      }
      resolving_reference_in_process_ = false;
    }
    return refers_to_;
  }
  PdfObjectPtr current = GetRefersToPtr(false);
  for (int i = 0; i < kLengthOfIndirectsChain; i++) {
    if (current && current->GetType() == kIndirectReference)
      current = static_cast<PdfIndirectReference*>(current.get())
                    ->GetRefersToPtr(false);
    else
      break;
  }
  return current;
}

void PdfIndirectReference::SetFree() {
  GetDocument()->GetXref()->FreeReference(this);
}

std::string PdfIndirectReference::ToString() const {
  std::string states = " ";
  if (CheckState(kFree)) states += "Free; ";
  if (CheckState(kModified)) states += "Modified; ";
  if (CheckState(kMustBeFlushed)) states += "MustBeFlushed; ";
  if (CheckState(kReading)) states += "Reading; ";
  if (CheckState(kFlushed)) states += "Flushed; ";
  if (CheckState(kOriginalObjectStream)) states += "OriginalObjectStream; ";
  if (CheckState(kForbidRelease)) states += "ForbidRelease; ";
  if (CheckState(kReadOnly)) states += "ReadOnly; ";
  return std::to_string(obj_nr_) + " " + std::to_string(gen_nr_) + " R" +
         states.substr(0, states.size() - 1);
}

bool PdfIndirectReference::Equals(const PdfObject& o) const {
  if (this == &o) return true;
  if (o.GetType() != kIndirectReference) return false;
  const auto& that = static_cast<const PdfIndirectReference&>(o);
  bool docs_equal = pdf_document_ == that.pdf_document_;
  if (!docs_equal)
    docs_equal =
        pdf_document_ && that.pdf_document_ &&
        pdf_document_->GetDocumentId() == that.pdf_document_->GetDocumentId();
  return obj_nr_ == that.obj_nr_ && gen_nr_ == that.gen_nr_ && docs_equal;
}

int PdfIndirectReference::CompareTo(const PdfIndirectReference& o) const {
  if (obj_nr_ == o.obj_nr_) {
    if (gen_nr_ == o.gen_nr_) {
      if (pdf_document_ == o.pdf_document_) return 0;
      if (!pdf_document_) return -1;
      if (!o.pdf_document_) return 1;
      auto a = pdf_document_->GetDocumentId(),
           b = o.pdf_document_->GetDocumentId();
      return a == b ? 0 : (a > b ? 1 : -1);
    }
    return gen_nr_ > o.gen_nr_ ? 1 : -1;
  }
  return obj_nr_ > o.obj_nr_ ? 1 : -1;
}

PdfWriter* PdfIndirectReference::GetWriter() const {
  return pdf_document_ ? pdf_document_->GetWriter() : nullptr;
}
PdfReader* PdfIndirectReference::GetReader() const {
  return pdf_document_ ? pdf_document_->GetReader() : nullptr;
}
PdfObjectPtr PdfIndirectReference::NewInstance() const {
  return PdfNull::kPdfNull;
}

// ============================================================ PdfArray
PdfArray::PdfArray(const Rectangle& r) {
  list_ = {MakeNumber(static_cast<double>(r.GetLeft())),
           MakeNumber(static_cast<double>(r.GetBottom())),
           MakeNumber(static_cast<double>(r.GetRight())),
           MakeNumber(static_cast<double>(r.GetTop()))};
}

PdfArray::PdfArray(const std::vector<float>& numbers) {
  for (float f : numbers) list_.push_back(MakeNumber(static_cast<double>(f)));
}
PdfArray::PdfArray(const std::vector<double>& numbers) {
  for (double f : numbers) list_.push_back(MakeNumber(f));
}
PdfArray::PdfArray(const std::vector<int>& numbers) {
  for (int f : numbers) list_.push_back(MakeNumber(f));
}
PdfArray::PdfArray(const std::vector<bool>& values) {
  for (bool b : values) list_.push_back(PdfBoolean::ValueOf(b));
}
PdfArray::PdfArray(const std::vector<std::string>& strings, bool as_names) {
  for (const auto& s : strings) {
    if (as_names)
      list_.push_back(std::make_shared<PdfName>(s));
    else
      list_.push_back(std::make_shared<PdfString>(s));
  }
}

static bool SameOrEqual(const PdfObject* a, const PdfObject* b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return a->Equals(*b);
}

bool PdfArray::Contains(const PdfObject* o) const {
  for (const auto& e : list_) {
    if (SameOrEqual(e.get(), o)) return true;
    if (o && e && o->GetIndirectReference() &&
        o->GetIndirectReference()->Equals(*e))
      return true;
    if (o && e && e->GetIndirectReference() &&
        e->GetIndirectReference()->Equals(*o))
      return true;
  }
  return false;
}

PdfObjectPtr PdfArray::Set(int index, PdfObjectPtr element) {
  PdfObjectPtr old = list_.at(index);
  list_[index] = std::move(element);
  return old;
}

PdfObjectPtr PdfArray::Get(int index, bool as_direct) const {
  if (index < 0 || index >= static_cast<int>(list_.size())) return nullptr;
  const PdfObjectPtr& obj = list_[index];
  if (!as_direct || !obj || obj->GetType() != kIndirectReference) return obj;
  return static_cast<PdfIndirectReference*>(obj.get())->GetRefersToPtr(true);
}

void PdfArray::Remove(const PdfObject* o) {
  int i = IndexOf(o);
  if (i >= 0) list_.erase(list_.begin() + i);
}

int PdfArray::IndexOf(const PdfObject* o) const {
  if (o == nullptr) return -1;
  for (size_t i = 0; i < list_.size(); ++i) {
    const PdfObject* e = list_[i].get();
    if (SameOrEqual(e, o)) return static_cast<int>(i);
    if (o->GetIndirectReference() && o->GetIndirectReference()->Equals(*e))
      return static_cast<int>(i);
  }
  return -1;
}

std::string PdfArray::ToString() const {
  std::string s = "[";
  for (const auto& e : list_) {
    PdfIndirectReference* ref = e ? e->GetIndirectReference() : nullptr;
    s += e == nullptr     ? std::string("null")
         : ref == nullptr ? e->ToString()
                          : ref->ToString();
    s += " ";
  }
  s += "]";
  return s;
}

bool PdfArray::Equals(const PdfObject& o) const {
  if (this == &o) return true;
  if (o.GetType() != kArray) return false;
  const auto& that = static_cast<const PdfArray&>(o);
  if (that.size() != size()) return false;
  for (int i = 0; i < size(); ++i)
    if (!EqualContent(list_[i].get(), that.list_[i].get())) return false;
  return true;
}

PdfArrayPtr PdfArray::GetAsArray(int index) const {
  return PdfCast<PdfArray>(Get(index));
}
PdfDictionaryPtr PdfArray::GetAsDictionary(int index) const {
  auto o = Get(index);
  return (o && o->GetType() == kDictionary)
             ? std::static_pointer_cast<PdfDictionary>(o)
             : nullptr;
}
PdfStreamPtr PdfArray::GetAsStream(int index) const {
  return PdfCast<PdfStream>(Get(index));
}
PdfNumberPtr PdfArray::GetAsNumber(int index) const {
  return PdfCast<PdfNumber>(Get(index));
}
PdfNamePtr PdfArray::GetAsName(int index) const {
  return PdfCast<PdfName>(Get(index));
}
PdfStringPtr PdfArray::GetAsString(int index) const {
  return PdfCast<PdfString>(Get(index));
}
PdfBooleanPtr PdfArray::GetAsBoolean(int index) const {
  return PdfCast<PdfBoolean>(Get(index));
}

Rectangle PdfArray::ToRectangle() const {
  if (size() != 4) throw PdfException("Cannot convert PdfArray to Rectangle.");
  float x1 = GetAsNumber(0) ? GetAsNumber(0)->FloatValue() : 0;
  float y1 = GetAsNumber(1) ? GetAsNumber(1)->FloatValue() : 0;
  float x2 = GetAsNumber(2) ? GetAsNumber(2)->FloatValue() : 0;
  float y2 = GetAsNumber(3) ? GetAsNumber(3)->FloatValue() : 0;
  float llx = std::min(x1, x2), lly = std::min(y1, y2), urx = std::max(x1, x2),
        ury = std::max(y1, y2);
  return Rectangle(llx, lly, urx - llx, ury - lly);
}

std::vector<float> PdfArray::ToFloatArray() const {
  std::vector<float> r;
  for (int i = 0; i < size(); ++i) {
    auto n = GetAsNumber(i);
    r.push_back(n ? n->FloatValue() : 0.f);
  }
  return r;
}
std::vector<double> PdfArray::ToDoubleArray() const {
  std::vector<double> r;
  for (int i = 0; i < size(); ++i) {
    auto n = GetAsNumber(i);
    r.push_back(n ? n->DoubleValue() : 0.0);
  }
  return r;
}
std::vector<int64_t> PdfArray::ToLongArray() const {
  std::vector<int64_t> r;
  for (int i = 0; i < size(); ++i) {
    auto n = GetAsNumber(i);
    r.push_back(n ? n->LongValue() : 0);
  }
  return r;
}
std::vector<int> PdfArray::ToIntArray() const {
  std::vector<int> r;
  for (int i = 0; i < size(); ++i) {
    auto n = GetAsNumber(i);
    r.push_back(n ? n->IntValue() : 0);
  }
  return r;
}
std::vector<bool> PdfArray::ToBooleanArray() const {
  std::vector<bool> r;
  for (int i = 0; i < size(); ++i) {
    auto b = GetAsBoolean(i);
    r.push_back(b ? b->GetValue() : false);
  }
  return r;
}

void PdfArray::CopyContent(const PdfObject& from, PdfDocument* document,
                           ICopyFilter& filter) {
  PdfObject::CopyContent(from, document, filter);
  const auto& array = static_cast<const PdfArray&>(from);
  for (const auto& entry : array.list_) {
    if (filter.ShouldProcess(this, nullptr, entry.get()))
      list_.push_back(entry->ProcessCopying(document, false, filter));
  }
}

// ============================================================ PdfDictionary
bool PdfDictionary::ContainsValue(const PdfObject* value) const {
  for (const auto& kv : map_)
    if (SameOrEqual(kv.second.get(), value)) return true;
  return false;
}

PdfObjectPtr PdfDictionary::Get(const PdfNamePtr& key, bool as_direct) const {
  auto it = map_.find(key);
  if (it == map_.end()) return nullptr;
  const PdfObjectPtr& obj = it->second;
  if (!as_direct || !obj || obj->GetType() != kIndirectReference) return obj;
  return static_cast<PdfIndirectReference*>(obj.get())->GetRefersToPtr(true);
}

PdfArrayPtr PdfDictionary::GetAsArray(const PdfNamePtr& key) const {
  return PdfCast<PdfArray>(Get(key));
}
PdfDictionaryPtr PdfDictionary::GetAsDictionary(const PdfNamePtr& key) const {
  auto o = Get(key);
  return (o && o->GetType() == kDictionary)
             ? std::static_pointer_cast<PdfDictionary>(o)
             : nullptr;
}
PdfStreamPtr PdfDictionary::GetAsStream(const PdfNamePtr& key) const {
  return PdfCast<PdfStream>(Get(key));
}
PdfNumberPtr PdfDictionary::GetAsNumber(const PdfNamePtr& key) const {
  return PdfCast<PdfNumber>(Get(key));
}
PdfNamePtr PdfDictionary::GetAsName(const PdfNamePtr& key) const {
  return PdfCast<PdfName>(Get(key));
}
PdfStringPtr PdfDictionary::GetAsString(const PdfNamePtr& key) const {
  return PdfCast<PdfString>(Get(key));
}
PdfBooleanPtr PdfDictionary::GetAsBoolean(const PdfNamePtr& key) const {
  return PdfCast<PdfBoolean>(Get(key));
}

std::optional<Rectangle> PdfDictionary::GetAsRectangle(
    const PdfNamePtr& key) const {
  auto a = GetAsArray(key);
  if (!a) return std::nullopt;
  return a->ToRectangle();
}
std::optional<float> PdfDictionary::GetAsFloat(const PdfNamePtr& key) const {
  auto n = GetAsNumber(key);
  if (!n) return std::nullopt;
  return n->FloatValue();
}
std::optional<int> PdfDictionary::GetAsInt(const PdfNamePtr& key) const {
  auto n = GetAsNumber(key);
  if (!n) return std::nullopt;
  return n->IntValue();
}
std::optional<bool> PdfDictionary::GetAsBool(const PdfNamePtr& key) const {
  auto b = GetAsBoolean(key);
  if (!b) return std::nullopt;
  return b->GetValue();
}

PdfObjectPtr PdfDictionary::Put(const PdfNamePtr& key, PdfObjectPtr value) {
  auto it = map_.find(key);
  if (it != map_.end()) {
    PdfObjectPtr old = it->second;
    it->second = std::move(value);
    return old;
  }
  map_.emplace(key, std::move(value));
  return nullptr;
}

PdfObjectPtr PdfDictionary::Remove(const PdfNamePtr& key) {
  auto it = map_.find(key);
  if (it == map_.end()) return nullptr;
  PdfObjectPtr old = it->second;
  map_.erase(it);
  return old;
}

void PdfDictionary::PutAll(const PdfDictionary& d) {
  for (const auto& kv : d.map_) map_[kv.first] = kv.second;
}

std::vector<PdfNamePtr> PdfDictionary::KeySet() const {
  std::vector<PdfNamePtr> keys;
  keys.reserve(map_.size());
  for (const auto& kv : map_) keys.push_back(kv.first);
  return keys;
}

std::vector<PdfObjectPtr> PdfDictionary::Values(bool as_directs) const {
  std::vector<PdfObjectPtr> v;
  v.reserve(map_.size());
  for (const auto& kv : map_) {
    if (as_directs && kv.second && kv.second->GetType() == kIndirectReference)
      v.push_back(static_cast<PdfIndirectReference*>(kv.second.get())
                      ->GetRefersToPtr(true));
    else
      v.push_back(kv.second);
  }
  return v;
}

std::string PdfDictionary::ToString() const {
  if (!IsFlushed()) {
    std::string s = "<<";
    for (const auto& kv : map_) {
      PdfIndirectReference* ref =
          kv.second ? kv.second->GetIndirectReference() : nullptr;
      std::string value = kv.second == nullptr ? std::string("null")
                          : ref == nullptr     ? kv.second->ToString()
                                               : ref->ToString();
      s += kv.first->ToString() + " " + value + " ";
    }
    s += ">>";
    return s;
  }
  return indirect_reference_->ToString();
}

bool PdfDictionary::Equals(const PdfObject& o) const {
  if (this == &o) return true;
  if (o.GetType() != GetType()) return false;
  const auto& that = static_cast<const PdfDictionary&>(o);
  if (that.size() != size()) return false;
  for (const auto& kv : map_) {
    auto it = that.map_.find(kv.first);
    if (it == that.map_.end()) return false;
    if (!EqualContent(kv.second.get(), it->second.get())) return false;
  }
  return true;
}

PdfDictionaryPtr PdfDictionary::Clone(
    const std::vector<PdfNamePtr>& exclude_keys) const {
  Map excluded;
  auto self = const_cast<PdfDictionary*>(this);
  for (const auto& key : exclude_keys) {
    auto it = self->map_.find(key);
    if (it != self->map_.end()) {
      excluded.insert(*it);
      self->map_.erase(it);
    }
  }
  PdfDictionaryPtr dictionary;
  try {
    dictionary = std::static_pointer_cast<PdfDictionary>(PdfObject::Clone());
  } catch (...) {
    for (const auto& kv : excluded) self->map_.insert(kv);
    throw;
  }
  for (const auto& kv : excluded) self->map_.insert(kv);
  return dictionary;
}

PdfDictionaryPtr PdfDictionary::CopyTo(
    PdfDocument* document, const std::vector<PdfNamePtr>& exclude_keys,
    bool allow_duplicating) const {
  Map excluded;
  auto self = const_cast<PdfDictionary*>(this);
  for (const auto& key : exclude_keys) {
    auto it = self->map_.find(key);
    if (it != self->map_.end()) {
      excluded.insert(*it);
      self->map_.erase(it);
    }
  }
  PdfDictionaryPtr dictionary;
  try {
    dictionary = std::static_pointer_cast<PdfDictionary>(
        PdfObject::CopyTo(document, allow_duplicating));
  } catch (...) {
    for (const auto& kv : excluded) self->map_.insert(kv);
    throw;
  }
  for (const auto& kv : excluded) self->map_.insert(kv);
  return dictionary;
}

void PdfDictionary::MergeDifferent(const PdfDictionary& other) {
  for (const auto& kv : other.map_)
    if (!ContainsKey(kv.first)) Put(kv.first, kv.second);
}

void PdfDictionary::CopyContent(const PdfObject& from, PdfDocument* document,
                                ICopyFilter& filter) {
  PdfObject::CopyContent(from, document, filter);
  const auto& dictionary = static_cast<const PdfDictionary&>(from);
  for (const auto& kv : dictionary.map_) {
    if (filter.ShouldProcess(this, kv.first.get(), kv.second.get()))
      map_[kv.first] = kv.second->ProcessCopying(document, false, filter);
  }
}

// ============================================================ PdfStream
PdfStream::PdfStream(const Bytes& bytes, int compression_level)
    : compression_level_(compression_level) {
  SetState(kMustBeIndirect);
  baos_ = std::make_shared<io::ByteArrayOutputStream>(bytes.size());
  output_stream_ = std::make_unique<PdfOutputStream>(baos_);
  if (!bytes.empty()) output_stream_->WriteBytes(bytes);
}

PdfStream::PdfStream(int64_t offset, const PdfDictionary& keys)
    : offset_(offset) {
  compression_level_ = compression_constants::kUndefinedCompression;
  PutAll(keys);
  auto length = GetAsNumber(PdfName::kLength);
  length_ = length ? length->IntValue() : 0;
}

PdfStream::PdfStream(std::shared_ptr<io::ByteArrayOutputStream> output_stream) {
  baos_ = std::move(output_stream);
  output_stream_ = std::make_unique<PdfOutputStream>(baos_);
  compression_level_ = compression_constants::kUndefinedCompression;
  SetState(kMustBeIndirect);
}

PdfStream::~PdfStream() = default;

Bytes PdfStream::GetBytes(bool decoded) {
  if (IsFlushed())
    throw PdfException(
        kernel_exception_message_constant::kCannotOperateWithFlushedPdfStream);
  Bytes bytes;
  if (output_stream_ && baos_) {
    bytes = baos_->ToByteArray();
    if (decoded && ContainsKey(PdfName::kFilter))
      bytes = PdfReader::DecodeBytes(bytes, *this);
  } else if (GetIndirectReference() != nullptr) {
    PdfReader* reader = GetIndirectReference()->GetReader();
    if (reader != nullptr) bytes = reader->ReadStreamBytes(*this, decoded);
  }
  return bytes;
}

void PdfStream::SetData(const Bytes& bytes, bool append) {
  if (IsFlushed())
    throw PdfException(
        kernel_exception_message_constant::kCannotOperateWithFlushedPdfStream);
  bool uninitialized = output_stream_ == nullptr;
  if (uninitialized) InitOutputStream();
  if (append) {
    if ((uninitialized && GetIndirectReference() != nullptr &&
         GetIndirectReference()->GetReader() != nullptr) ||
        (!uninitialized && ContainsKey(PdfName::kFilter))) {
      Bytes old_bytes;
      if (uninitialized) {
        // read original data from the reader (temporarily without the new
        // output stream)
        auto saved_out = std::move(output_stream_);
        auto saved_baos = std::move(baos_);
        try {
          old_bytes = GetBytes();
        } catch (const PdfException&) {
          throw PdfException(kernel_exception_message_constant::
                                 kCannotReadAStreamInOrderToAppendNewBytes);
        }
        output_stream_ = std::move(saved_out);
        baos_ = std::move(saved_baos);
      } else {
        old_bytes = GetBytes();
      }
      output_stream_->AssignBytes(old_bytes,
                                  static_cast<int>(old_bytes.size()));
    }
    output_stream_->WriteBytes(bytes);
  } else {
    output_stream_->AssignBytes(bytes, static_cast<int>(bytes.size()));
  }
  offset_ = 0;
  Remove(PdfName::kFilter);
  Remove(PdfName::kDecodeParms);
}

void PdfStream::CopyContent(const PdfObject& from, PdfDocument* document,
                            ICopyFilter& filter) {
  PdfDictionary::CopyContent(from, document, filter);
  auto& stream = const_cast<PdfStream&>(static_cast<const PdfStream&>(from));
  Bytes bytes = stream.GetBytes(false);
  if (!output_stream_) InitOutputStream();
  output_stream_->WriteBytes(bytes);
}

void PdfStream::InitOutputStream(std::shared_ptr<io::OutputStream> stream) {
  if (output_stream_ == nullptr) {
    auto b = std::dynamic_pointer_cast<io::ByteArrayOutputStream>(stream);
    baos_ = b ? b : std::make_shared<io::ByteArrayOutputStream>();
    output_stream_ = std::make_unique<PdfOutputStream>(baos_);
  }
}

void PdfStream::ReleaseContent() {
  PdfDictionary::ReleaseContent();
  output_stream_.reset();
  baos_.reset();
}

// ============================================================ static names
namespace {
const std::map<std::string, PdfNamePtr>& StaticNames() {
  static const std::map<std::string, PdfNamePtr> kNames = [] {
    std::map<std::string, PdfNamePtr> m;
    for (const PdfNamePtr* p : PdfName::AllStaticNames())
      m[(*p)->GetValue()] = *p;
    return m;
  }();
  return kNames;
}
}  // namespace

}  // namespace kernel
}  // namespace pdfcpp
