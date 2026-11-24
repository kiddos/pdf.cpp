#ifndef PDFCPP_KERNEL_PDF_XOBJECT_PDF_XOBJECT_H_
#define PDFCPP_KERNEL_PDF_XOBJECT_PDF_XOBJECT_H_

#include <memory>

#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfXObject.
class PdfXObject : public PdfObjectWrapper<PdfStream> {
 public:
  /// Wraps an existing XObject stream (form or image).
  static std::shared_ptr<PdfXObject> MakeXObject(const PdfStreamPtr& stream);
  virtual float GetWidth() const = 0;
  virtual float GetHeight() const = 0;
  bool IsImage() const;

 protected:
  PdfXObject() = default;
  explicit PdfXObject(PdfStreamPtr pdf_object)
      : PdfObjectWrapper<PdfStream>(std::move(pdf_object)) {}
};

/// Port of PdfImageXObject.
class PdfImageXObject : public PdfXObject {
 public:
  explicit PdfImageXObject(const std::shared_ptr<io::ImageData>& image);
  explicit PdfImageXObject(PdfStreamPtr pdf_stream);
  float GetWidth() const override;
  float GetHeight() const override;
  /// Raw (filtered) image bytes; decoded through non-image filters when
  /// decoded=true.
  Bytes GetImageBytes(bool decoded = true);
  /// Identifies the format of GetImageBytes(true): "jpg", "jp2", "png" (raw
  /// samples) etc.
  std::string IdentifyImageFileExtension() const;
  PdfImageXObject& Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    GetPdfObject()->Put(key, value);
    return *this;
  }

 private:
  static PdfStreamPtr CreatePdfStream(
      const std::shared_ptr<io::ImageData>& image);
};

/// Port of PdfFormXObject.
class PdfFormXObject : public PdfXObject {
 public:
  explicit PdfFormXObject(const Rectangle& b_box);
  explicit PdfFormXObject(PdfStreamPtr pdf_stream);
  PdfResources* GetResources();
  std::optional<Rectangle> GetBBox() const {
    return GetPdfObject()->GetAsRectangle(PdfName::kBBox);
  }
  PdfFormXObject& SetBBox(const Rectangle& bbox) {
    GetPdfObject()->Put(PdfName::kBBox, std::make_shared<PdfArray>(bbox));
    return *this;
  }
  float GetWidth() const override;
  float GetHeight() const override;
  PdfFormXObject& Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    GetPdfObject()->Put(key, value);
    return *this;
  }
  void Flush() override;

 private:
  std::shared_ptr<PdfResources> resources_;
};

/// Port of PdfExtGState.
class PdfExtGState : public PdfObjectWrapper<PdfDictionary> {
 public:
  static const PdfNamePtr& BmNormal();
  static const PdfNamePtr& BmMultiply();
  static const PdfNamePtr& BmScreen();

  PdfExtGState()
      : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) {}
  explicit PdfExtGState(PdfDictionaryPtr dict)
      : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
  PdfExtGState& SetLineWidth(float line_width) {
    return Put(PdfName::kLW, MakeNumber(static_cast<double>(line_width)));
  }
  PdfExtGState& SetLineCapStyle(int style) {
    return Put(PdfName::kLC, MakeNumber(style));
  }
  PdfExtGState& SetLineJoinStyle(int style) {
    return Put(PdfName::kLJ, MakeNumber(style));
  }
  PdfExtGState& SetMiterLimit(float v) {
    return Put(PdfName::kML, MakeNumber(static_cast<double>(v)));
  }
  PdfExtGState& SetStrokeOpacity(float v) {
    return Put(PdfName::kCA, MakeNumber(static_cast<double>(v)));
  }
  PdfExtGState& SetFillOpacity(float v) {
    return Put(PdfName::kCa, MakeNumber(static_cast<double>(v)));
  }
  PdfExtGState& SetBlendMode(const PdfNamePtr& bm) {
    return Put(PdfName::kBM, bm);
  }
  PdfExtGState& SetStrokeOverPrintFlag(bool v) {
    return Put(PdfName::kOP, PdfBoolean::ValueOf(v));
  }
  PdfExtGState& SetFillOverPrintFlag(bool v) {
    return Put(PdfName::kOp, PdfBoolean::ValueOf(v));
  }
  PdfExtGState& SetAlphaSourceFlag(bool v) {
    return Put(PdfName::kAIS, PdfBoolean::ValueOf(v));
  }
  PdfExtGState& SetSoftMask(const PdfObjectPtr& sm) {
    return Put(PdfName::kSMask, sm);
  }
  std::optional<float> GetFillOpacity() const {
    return GetPdfObject()->GetAsFloat(PdfName::kCa);
  }
  std::optional<float> GetStrokeOpacity() const {
    return GetPdfObject()->GetAsFloat(PdfName::kCA);
  }
  std::optional<float> GetLineWidth() const {
    return GetPdfObject()->GetAsFloat(PdfName::kLW);
  }
  PdfExtGState& Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    GetPdfObject()->Put(key, value);
    return *this;
  }
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_XOBJECT_PDF_XOBJECT_H_
