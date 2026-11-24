#include "pdfcpp/kernel/pdf/pdf_output_stream.h"

#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/kernel/crypto/pdf_encryption.h"
#include "pdfcpp/kernel/pdf/filters/filters.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

namespace pdfcpp {
namespace kernel {

std::shared_ptr<io::OutputStream>
FlateCompressionStrategy::CreateNewOutputStream(
    std::shared_ptr<io::OutputStream> original, PdfStream& stream) {
  return std::make_shared<io::DeflaterOutputStream>(
      std::move(original), stream.GetCompressionLevel());
}

PdfOutputStream::PdfOutputStream(
    std::shared_ptr<io::OutputStream> output_stream)
    : io::HighPrecisionOutputStream(std::move(output_stream)) {}

IStreamCompressionStrategy& PdfOutputStream::GetCompressionStrategy() {
  if (!compression_strategy_)
    compression_strategy_ = std::make_shared<FlateCompressionStrategy>();
  return *compression_strategy_;
}

void PdfOutputStream::WriteBigEndian(int64_t bytes, int size) {
  while (--size >= 0) Write(static_cast<int>((bytes >> (8 * size)) & 0xff));
}

PdfOutputStream& PdfOutputStream::Write(PdfObject& obj) {
  PdfObject* pdf_object = &obj;
  if (pdf_object->CheckState(PdfObject::kMustBeIndirect) &&
      document_ != nullptr) {
    pdf_object->MakeIndirect(document_);
    pdf_object = pdf_object->GetIndirectReference();
  }
  if (pdf_object->CheckState(PdfObject::kReadOnly))
    throw PdfException(kernel_exception_message_constant::
                           kCannotWriteObjectAfterItWasReleased);
  switch (pdf_object->GetType()) {
    case PdfObject::kArray:
      WriteArray(static_cast<PdfArray&>(*pdf_object));
      break;
    case PdfObject::kDictionary:
      WriteDictionary(static_cast<PdfDictionary&>(*pdf_object));
      break;
    case PdfObject::kIndirectReference:
      WriteReference(static_cast<PdfIndirectReference&>(*pdf_object));
      break;
    case PdfObject::kName:
      WriteName(static_cast<PdfName&>(*pdf_object));
      break;
    case PdfObject::kNull:
    case PdfObject::kBoolean:
      WriteBytes(
          static_cast<PdfPrimitiveObject&>(*pdf_object).GetInternalContent());
      break;
    case PdfObject::kLiteral: {
      auto& literal = static_cast<PdfLiteral&>(*pdf_object);
      literal.SetPosition(GetCurrentPos());
      WriteBytes(literal.GetInternalContent());
      break;
    }
    case PdfObject::kString:
      WritePdfString(static_cast<PdfString&>(*pdf_object));
      break;
    case PdfObject::kNumber:
      WriteNumber(static_cast<PdfNumber&>(*pdf_object));
      break;
    case PdfObject::kStream:
      WriteStream(static_cast<PdfStream&>(*pdf_object));
      break;
  }
  return *this;
}

void PdfOutputStream::WriteArray(PdfArray& pdf_array) {
  WriteByte('[');
  for (int i = 0; i < pdf_array.size(); i++) {
    PdfObjectPtr value = pdf_array.Get(i, false);
    if (PdfIndirectReference* ref = value->GetIndirectReference())
      WriteReference(*ref);
    else
      Write(*value);
    if (i < pdf_array.size() - 1) WriteSpace();
  }
  WriteByte(']');
}

void PdfOutputStream::WriteDictionary(PdfDictionary& pdf_dictionary) {
  WriteByte('<');
  WriteByte('<');
  // copy entries: writing may make nested objects indirect but never changes
  // the key set
  std::vector<std::pair<PdfNamePtr, PdfObjectPtr>> entries(
      pdf_dictionary.EntrySet().begin(), pdf_dictionary.EntrySet().end());
  for (auto& entry : entries) {
    bool space_written = false;
    WriteName(*entry.first);
    PdfObjectPtr value = entry.second;
    if (!value) value = PdfNull::kPdfNull;
    uint8_t t = value->GetType();
    if (t == PdfObject::kNumber || t == PdfObject::kLiteral ||
        t == PdfObject::kBoolean || t == PdfObject::kNull ||
        t == PdfObject::kIndirectReference ||
        value->CheckState(PdfObject::kMustBeIndirect)) {
      space_written = true;
      WriteSpace();
    }
    if (PdfIndirectReference* ref = value->GetIndirectReference()) {
      if (!space_written) WriteSpace();
      WriteReference(*ref);
    } else {
      Write(*value);
    }
  }
  WriteByte('>');
  WriteByte('>');
}

void PdfOutputStream::WriteReference(PdfIndirectReference& ref) {
  if (document_ != nullptr && ref.GetDocument() != document_)
    throw PdfException(kernel_exception_message_constant::
                           kPdfIndirectObjectBelongsToOtherPdfDocument);
  if (ref.IsFree()) {
    WriteBytes(PdfNull::kPdfNull->GetInternalContent());
  } else if (ref.RefersToRaw() == nullptr &&
             (ref.CheckState(PdfObject::kModified) ||
              ref.GetReader() == nullptr ||
              !(ref.GetOffset() > 0 || ref.GetIndex() >= 0))) {
    WriteBytes(PdfNull::kPdfNull->GetInternalContent());
  } else if (ref.GetGenNumber() == 0) {
    WriteInteger(ref.GetObjNumber());
    WriteString(" 0 R");
  } else {
    WriteInteger(ref.GetObjNumber())
        .WriteSpace()
        .WriteInteger(ref.GetGenNumber());
    WriteString(" R");
  }
}

void PdfOutputStream::WritePdfString(PdfString& pdf_string) {
  pdf_string.Encrypt(crypto_);
  if (pdf_string.IsHexWriting()) {
    WriteByte('<');
    WriteBytes(pdf_string.GetInternalContent());
    WriteByte('>');
  } else {
    WriteByte('(');
    WriteBytes(pdf_string.GetInternalContent());
    WriteByte(')');
  }
}

void PdfOutputStream::WriteName(PdfName& name) {
  WriteByte('/');
  WriteBytes(name.GetInternalContent());
}

void PdfOutputStream::WriteNumber(PdfNumber& pdf_number) {
  if (pdf_number.HasContent())
    WriteBytes(pdf_number.GetInternalContent());
  else if (pdf_number.IsDoubleNumber())
    WriteDouble(pdf_number.GetValue());
  else
    WriteInteger(pdf_number.IntValue());
}

bool PdfOutputStream::CheckEncryption(PdfStream& pdf_stream) {
  if (crypto_ == nullptr || crypto_->IsEmbeddedFilesOnly()) return false;
  if (NameEquals(pdf_stream.GetAsName(PdfName::kType), PdfName::kXRef))
    return false;
  PdfObjectPtr filter = pdf_stream.Get(PdfName::kFilter, true);
  if (!filter) return true;
  if (NameEquals(filter, PdfName::kCrypt)) return false;
  if (filter->GetType() == PdfObject::kArray) {
    auto filters = std::static_pointer_cast<PdfArray>(filter);
    if (filters->IsEmpty()) return true;
    return !NameEquals(filters->Get(0, true), PdfName::kCrypt);
  }
  return true;
}

bool PdfOutputStream::ContainsFlateFilter(PdfStream& pdf_stream) {
  PdfNamePtr compression_filter = GetCompressionStrategy().GetFilterName();
  PdfObjectPtr filter = pdf_stream.Get(PdfName::kFilter);
  if (!filter) return false;
  if (filter->IsFlushed()) return true;
  if (filter->GetType() != PdfObject::kName &&
      filter->GetType() != PdfObject::kArray)
    throw PdfException(
        kernel_exception_message_constant::kFilterIsNotANameOrArray);
  if (filter->GetType() == PdfObject::kName)
    return NameEquals(filter, compression_filter);
  for (const auto& obj : *std::static_pointer_cast<PdfArray>(filter))
    if (obj->IsFlushed()) return true;
  return std::static_pointer_cast<PdfArray>(filter)->Contains(
      compression_filter);
}

void PdfOutputStream::UpdateCompressionFilter(PdfStream& pdf_stream) {
  PdfObjectPtr existing_filter = pdf_stream.Get(PdfName::kFilter);
  PdfNamePtr new_filter = GetCompressionStrategy().GetFilterName();
  if (!existing_filter) {
    existing_filter = new_filter;
    pdf_stream.Remove(PdfName::kDecodeParms);
  } else if (existing_filter->GetType() == PdfObject::kArray) {
    std::static_pointer_cast<PdfArray>(existing_filter)->Add(0, new_filter);
  } else if (existing_filter->GetType() == PdfObject::kName) {
    auto filter_array = std::make_shared<PdfArray>();
    filter_array->Add(new_filter);
    filter_array->Add(existing_filter);
    existing_filter = filter_array;
  } else {
    throw PdfException(
        kernel_exception_message_constant::kFilterIsNotANameOrArray);
  }
  pdf_stream.Put(PdfName::kFilter, existing_filter);
  PdfObjectPtr existing_decode_parms = pdf_stream.Get(PdfName::kDecodeParms);
  PdfObjectPtr new_decode_params = GetCompressionStrategy().GetDecodeParams();
  if (!new_decode_params) new_decode_params = std::make_shared<PdfNull>();
  if (!existing_decode_parms) {
    if (!new_decode_params->IsNull())
      pdf_stream.Put(PdfName::kDecodeParms, new_decode_params);
  } else if (existing_decode_parms->GetType() == PdfObject::kDictionary) {
    auto array = std::make_shared<PdfArray>();
    array->Add(new_decode_params);
    array->Add(existing_decode_parms);
    pdf_stream.Put(PdfName::kDecodeParms, array);
  } else if (existing_decode_parms->GetType() == PdfObject::kArray) {
    std::static_pointer_cast<PdfArray>(existing_decode_parms)
        ->Add(0, new_decode_params);
  } else {
    throw PdfException(Format(kernel_exception_message_constant::
                                  kThisDecodeParameterTypeIsNotSupported,
                              "?"));
  }
}

Bytes PdfOutputStream::DecodeFlateBytes(PdfStream& stream, const Bytes& input) {
  PdfObjectPtr filter_object = stream.Get(PdfName::kFilter);
  if (!filter_object) return input;
  PdfNamePtr filter_name;
  PdfArrayPtr filters_array;
  if (filter_object->GetType() == PdfObject::kName) {
    filter_name = std::static_pointer_cast<PdfName>(filter_object);
  } else if (filter_object->GetType() == PdfObject::kArray) {
    filters_array = std::static_pointer_cast<PdfArray>(filter_object);
    filter_name = filters_array->GetAsName(0);
  } else {
    throw PdfException(
        kernel_exception_message_constant::kFilterIsNotANameOrArray);
  }
  if (!filter_name || !(*filter_name == *PdfName::kFlateDecode)) return input;
  PdfDictionaryPtr decode_params;
  PdfArrayPtr decode_params_array;
  PdfObjectPtr decode_params_object = stream.Get(PdfName::kDecodeParms);
  if (decode_params_object) {
    if (decode_params_object->GetType() == PdfObject::kDictionary) {
      decode_params =
          std::static_pointer_cast<PdfDictionary>(decode_params_object);
    } else if (decode_params_object->GetType() == PdfObject::kArray) {
      decode_params_array =
          std::static_pointer_cast<PdfArray>(decode_params_object);
      decode_params = decode_params_array->GetAsDictionary(0);
    }
  }
  Bytes res = FlateDecodeFilter::FlateDecode(input, true);
  if (res.empty() && !input.empty())
    res = FlateDecodeFilter::FlateDecode(input, false);
  Bytes bytes = FlateDecodeFilter::DecodePredictor(res, decode_params.get());
  filter_object = nullptr;
  if (filters_array) {
    filters_array->Remove(0);
    if (filters_array->size() == 1)
      filter_object = filters_array->Get(0);
    else if (!filters_array->IsEmpty())
      filter_object = filters_array;
  }
  decode_params_object = nullptr;
  if (decode_params_array) {
    decode_params_array->Remove(0);
    if (decode_params_array->size() == 1 &&
        decode_params_array->Get(0)->GetType() != PdfObject::kNull)
      decode_params_object = decode_params_array->Get(0);
    else if (!decode_params_array->IsEmpty())
      decode_params_object = decode_params_array;
  }
  if (!filter_object)
    stream.Remove(PdfName::kFilter);
  else
    stream.Put(PdfName::kFilter, filter_object);
  if (!decode_params_object)
    stream.Remove(PdfName::kDecodeParms);
  else
    stream.Put(PdfName::kDecodeParms, decode_params_object);
  return bytes;
}

void PdfOutputStream::WriteStream(PdfStream& pdf_stream) {
  bool user_defined_compression = pdf_stream.GetCompressionLevel() !=
                                  compression_constants::kUndefinedCompression;
  if (!user_defined_compression) {
    int default_level =
        document_ != nullptr && document_->GetWriter() != nullptr
            ? document_->GetWriter()->GetCompressionLevel()
            : compression_constants::kDefaultCompression;
    pdf_stream.SetCompressionLevel(default_level);
  }
  bool to_compress =
      pdf_stream.GetCompressionLevel() != compression_constants::kNoCompression;
  auto type_name = pdf_stream.GetAsName(PdfName::kType);
  bool not_metadata = !type_name || !(*type_name == *PdfName::kMetadata);
  bool allow_compression =
      !pdf_stream.ContainsKey(PdfName::kFilter) && not_metadata;

  if (pdf_stream.GetOutputStream() == nullptr &&
      pdf_stream.GetIndirectReference() != nullptr &&
      pdf_stream.GetIndirectReference()->GetReader() != nullptr) {
    Bytes bytes =
        pdf_stream.GetIndirectReference()->GetReader()->ReadStreamBytes(
            pdf_stream, false);
    if (user_defined_compression) bytes = DecodeFlateBytes(pdf_stream, bytes);
    pdf_stream.InitOutputStream();
    pdf_stream.GetOutputStream()->WriteBytes(bytes);
  }
  if (pdf_stream.GetOutputStream() == nullptr) pdf_stream.InitOutputStream();

  auto* object_stream = dynamic_cast<PdfObjectStream*>(&pdf_stream);
  auto raw_bytes = [&]() {
    Bytes all;
    if (object_stream) {
      const Bytes& idx = object_stream->GetIndexBytes()->GetBuffer();
      all.insert(all.end(), idx.begin(), idx.end());
    }
    const Bytes& data = pdf_stream.GetByteArrayOutputStream()->GetBuffer();
    all.insert(all.end(), data.begin(), data.end());
    return all;
  };

  Bytes result;
  PdfArrayPtr decode_params = pdf_stream.GetAsArray(PdfName::kDecodeParms);
  bool decode_params_not_flushed =
      !decode_params || !decode_params->IsFlushed();
  if (to_compress && !ContainsFlateFilter(pdf_stream) &&
      decode_params_not_flushed &&
      (allow_compression || user_defined_compression)) {
    UpdateCompressionFilter(pdf_stream);
    auto baos = std::make_shared<io::ByteArrayOutputStream>();
    auto zip = GetCompressionStrategy().CreateNewOutputStream(baos, pdf_stream);
    zip->Write(raw_bytes());
    if (auto* fin = dynamic_cast<io::IFinishable*>(zip.get())) fin->Finish();
    result = std::move(baos->GetBuffer());
  } else {
    result = raw_bytes();
  }
  if (CheckEncryption(pdf_stream)) {
    auto encoded = std::make_shared<io::ByteArrayOutputStream>();
    auto ose = crypto_->GetEncryptionStream(encoded);
    ose->Write(result);
    dynamic_cast<io::IFinishable*>(ose.get())->Finish();
    result = std::move(encoded->GetBuffer());
  }
  pdf_stream.Put(PdfName::kLength, MakeNumber(static_cast<int>(result.size())));
  pdf_stream.UpdateLength(static_cast<int>(result.size()));
  WriteDictionary(pdf_stream);
  WriteString("stream\n");
  WriteBytes(result);
  WriteString("\nendstream");
}

}  // namespace kernel
}  // namespace pdfcpp
