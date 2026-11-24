#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

#include "pdfcpp/kernel/pdf/pdf_reader.h"

namespace pdfcpp {
namespace kernel {

std::shared_ptr<PdfXObject> PdfXObject::MakeXObject(
    const PdfStreamPtr& stream) {
  if (NameEquals(stream->GetAsName(PdfName::kSubtype), PdfName::kImage))
    return std::make_shared<PdfImageXObject>(stream);
  return std::make_shared<PdfFormXObject>(stream);
}

bool PdfXObject::IsImage() const {
  return NameEquals(GetPdfObject()->GetAsName(PdfName::kSubtype),
                    PdfName::kImage);
}

// ============================================================ PdfImageXObject
PdfImageXObject::PdfImageXObject(const std::shared_ptr<io::ImageData>& image)
    : PdfXObject(CreatePdfStream(image)) {}

PdfImageXObject::PdfImageXObject(PdfStreamPtr pdf_stream)
    : PdfXObject(std::move(pdf_stream)) {}

PdfStreamPtr PdfImageXObject::CreatePdfStream(
    const std::shared_ptr<io::ImageData>& image) {
  auto stream = std::make_shared<PdfStream>(image->GetData());
  stream->Put(PdfName::kType, PdfName::kXObject);
  stream->Put(PdfName::kSubtype, PdfName::kImage);
  stream->Put(PdfName::kWidth, MakeNumber(static_cast<int>(image->GetWidth())));
  stream->Put(PdfName::kHeight,
              MakeNumber(static_cast<int>(image->GetHeight())));
  if (image->IsMask()) {
    if (image->GetBpc() == 1) {
      stream->Put(PdfName::kImageMask, PdfBoolean::kTrueValue);
    } else {
      stream->Put(PdfName::kColorSpace, PdfName::kDeviceGray);
      stream->Put(PdfName::kBitsPerComponent, MakeNumber(image->GetBpc()));
    }
  } else {
    stream->Put(PdfName::kBitsPerComponent, MakeNumber(image->GetBpc()));
    int n = image->GetColorEncodingComponentsNumber();
    PdfObjectPtr cs = n == 1   ? PdfName::kDeviceGray
                      : n == 3 ? PdfName::kDeviceRGB
                               : PdfName::kDeviceCMYK;
    if (!image->GetIccProfile().empty()) {
      auto icc = std::make_shared<PdfStream>(image->GetIccProfile());
      icc->Put(PdfName::kN, MakeNumber(n));
      auto arr = std::make_shared<PdfArray>();
      arr->Add(PdfName::kICCBased);
      arr->Add(icc);
      cs = arr;
    }
    if (!image->GetPalette().empty()) {
      auto arr = std::make_shared<PdfArray>();
      arr->Add(PdfName::kIndexed);
      arr->Add(PdfName::kDeviceRGB);
      arr->Add(
          MakeNumber(static_cast<int>(image->GetPalette().size() / 3) - 1));
      auto lookup = std::make_shared<PdfString>(image->GetPalette());
      lookup->SetHexWriting(true);
      arr->Add(lookup);
      cs = arr;
    }
    stream->Put(PdfName::kColorSpace, cs);
  }
  if (!image->GetFilter().empty()) {
    stream->Put(PdfName::kFilter, PdfName::Of(image->GetFilter()));
    stream->SetCompressionLevel(compression_constants::kNoCompression);
    if (!image->GetDecodeParms().empty()) {
      auto dp = std::make_shared<PdfDictionary>();
      for (const auto& kv : image->GetDecodeParms())
        dp->Put(PdfName::Of(kv.first), MakeNumber(kv.second));
      stream->Put(PdfName::kDecodeParms, dp);
    }
  }
  if (!image->GetDecode().empty())
    stream->Put(PdfName::kDecode,
                std::make_shared<PdfArray>(image->GetDecode()));
  if (!image->GetColorKeyMask().empty())
    stream->Put(PdfName::kMask,
                std::make_shared<PdfArray>(image->GetColorKeyMask()));
  if (image->GetImageMask()) {
    auto mask = CreatePdfStream(image->GetImageMask());
    if (image->GetImageMask()->GetBpc() == 1)
      stream->Put(PdfName::kMask, mask);
    else
      stream->Put(PdfName::kSMask, mask);
  }
  return stream;
}

float PdfImageXObject::GetWidth() const {
  auto w = GetPdfObject()->GetAsNumber(PdfName::kWidth);
  return w ? w->FloatValue() : 0;
}

float PdfImageXObject::GetHeight() const {
  auto h = GetPdfObject()->GetAsNumber(PdfName::kHeight);
  return h ? h->FloatValue() : 0;
}

Bytes PdfImageXObject::GetImageBytes(bool decoded) {
  return GetPdfObject()->GetBytes(decoded);
}

std::string PdfImageXObject::IdentifyImageFileExtension() const {
  PdfObjectPtr filter = GetPdfObject()->Get(PdfName::kFilter);
  PdfNamePtr last;
  if (filter && filter->IsName())
    last = std::static_pointer_cast<PdfName>(filter);
  else if (filter && filter->IsArray()) {
    auto arr = std::static_pointer_cast<PdfArray>(filter);
    if (arr->size() > 0) last = arr->GetAsName(arr->size() - 1);
  }
  if (last) {
    if (last->GetValue() == "DCTDecode") return "jpg";
    if (last->GetValue() == "JPXDecode") return "jp2";
    if (last->GetValue() == "JBIG2Decode") return "jbig2";
    if (last->GetValue() == "CCITTFaxDecode") return "tif";
  }
  return "png";
}

// ============================================================ PdfFormXObject
PdfFormXObject::PdfFormXObject(const Rectangle& b_box)
    : PdfXObject(std::make_shared<PdfStream>()) {
  GetPdfObject()->Put(PdfName::kType, PdfName::kXObject);
  GetPdfObject()->Put(PdfName::kSubtype, PdfName::kForm);
  GetPdfObject()->Put(PdfName::kBBox, std::make_shared<PdfArray>(b_box));
}

PdfFormXObject::PdfFormXObject(PdfStreamPtr pdf_stream)
    : PdfXObject(std::move(pdf_stream)) {}

PdfResources* PdfFormXObject::GetResources() {
  if (!resources_) {
    auto resources_dict = GetPdfObject()->GetAsDictionary(PdfName::kResources);
    if (!resources_dict) {
      resources_dict = std::make_shared<PdfDictionary>();
      GetPdfObject()->Put(PdfName::kResources, resources_dict);
    }
    resources_ = std::make_shared<PdfResources>(resources_dict);
  }
  return resources_.get();
}

float PdfFormXObject::GetWidth() const {
  auto b = GetBBox();
  return b ? b->GetWidth() : 0;
}

float PdfFormXObject::GetHeight() const {
  auto b = GetBBox();
  return b ? b->GetHeight() : 0;
}

void PdfFormXObject::Flush() {
  resources_.reset();
  PdfXObject::Flush();
}

// ============================================================ PdfExtGState
const PdfNamePtr& PdfExtGState::BmNormal() { return PdfName::kNormal; }
const PdfNamePtr& PdfExtGState::BmMultiply() { return PdfName::kMultiply; }
const PdfNamePtr& PdfExtGState::BmScreen() { return PdfName::kScreen; }

}  // namespace kernel
}  // namespace pdfcpp
