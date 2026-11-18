#pragma once

#include <memory>

#include "pdfcpp/io/image/ImageData.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfXObject.
class PdfXObject : public PdfObjectWrapper<PdfStream> {
public:
    /// Wraps an existing XObject stream (form or image).
    static std::shared_ptr<PdfXObject> makeXObject(const PdfStreamPtr& stream);
    virtual float getWidth() const = 0;
    virtual float getHeight() const = 0;
    bool isImage() const;

protected:
    PdfXObject() = default;
    explicit PdfXObject(PdfStreamPtr pdfObject) : PdfObjectWrapper<PdfStream>(std::move(pdfObject)) {}
};

/// Port of PdfImageXObject.
class PdfImageXObject : public PdfXObject {
public:
    explicit PdfImageXObject(const std::shared_ptr<io::ImageData>& image);
    explicit PdfImageXObject(PdfStreamPtr pdfStream);
    float getWidth() const override;
    float getHeight() const override;
    /// Raw (filtered) image bytes; decoded through non-image filters when decoded=true.
    Bytes getImageBytes(bool decoded = true);
    /// Identifies the format of getImageBytes(true): "jpg", "jp2", "png" (raw samples) etc.
    std::string identifyImageFileExtension() const;
    PdfImageXObject& put(const PdfNamePtr& key, const PdfObjectPtr& value) {
        getPdfObject()->put(key, value);
        return *this;
    }

private:
    static PdfStreamPtr createPdfStream(const std::shared_ptr<io::ImageData>& image);
};

/// Port of PdfFormXObject.
class PdfFormXObject : public PdfXObject {
public:
    explicit PdfFormXObject(const Rectangle& bBox);
    explicit PdfFormXObject(PdfStreamPtr pdfStream);
    PdfResources* getResources();
    std::optional<Rectangle> getBBox() const { return getPdfObject()->getAsRectangle(PdfName::BBox); }
    PdfFormXObject& setBBox(const Rectangle& bbox) {
        getPdfObject()->put(PdfName::BBox, std::make_shared<PdfArray>(bbox));
        return *this;
    }
    float getWidth() const override;
    float getHeight() const override;
    PdfFormXObject& put(const PdfNamePtr& key, const PdfObjectPtr& value) {
        getPdfObject()->put(key, value);
        return *this;
    }
    void flush() override;

private:
    std::shared_ptr<PdfResources> resources_;
};

/// Port of PdfExtGState.
class PdfExtGState : public PdfObjectWrapper<PdfDictionary> {
public:
    static const PdfNamePtr& BM_NORMAL();
    static const PdfNamePtr& BM_MULTIPLY();
    static const PdfNamePtr& BM_SCREEN();

    PdfExtGState() : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) {}
    explicit PdfExtGState(PdfDictionaryPtr dict) : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
    PdfExtGState& setLineWidth(float lineWidth) { return put(PdfName::LW, makeNumber(static_cast<double>(lineWidth))); }
    PdfExtGState& setLineCapStyle(int style) { return put(PdfName::LC, makeNumber(style)); }
    PdfExtGState& setLineJoinStyle(int style) { return put(PdfName::LJ, makeNumber(style)); }
    PdfExtGState& setMiterLimit(float v) { return put(PdfName::ML, makeNumber(static_cast<double>(v))); }
    PdfExtGState& setStrokeOpacity(float v) { return put(PdfName::CA, makeNumber(static_cast<double>(v))); }
    PdfExtGState& setFillOpacity(float v) { return put(PdfName::ca, makeNumber(static_cast<double>(v))); }
    PdfExtGState& setBlendMode(const PdfNamePtr& bm) { return put(PdfName::BM, bm); }
    PdfExtGState& setStrokeOverPrintFlag(bool v) { return put(PdfName::OP, PdfBoolean::valueOf(v)); }
    PdfExtGState& setFillOverPrintFlag(bool v) { return put(PdfName::op, PdfBoolean::valueOf(v)); }
    PdfExtGState& setAlphaSourceFlag(bool v) { return put(PdfName::AIS, PdfBoolean::valueOf(v)); }
    PdfExtGState& setSoftMask(const PdfObjectPtr& sm) { return put(PdfName::SMask, sm); }
    std::optional<float> getFillOpacity() const { return getPdfObject()->getAsFloat(PdfName::ca); }
    std::optional<float> getStrokeOpacity() const { return getPdfObject()->getAsFloat(PdfName::CA); }
    std::optional<float> getLineWidth() const { return getPdfObject()->getAsFloat(PdfName::LW); }
    PdfExtGState& put(const PdfNamePtr& key, const PdfObjectPtr& value) {
        getPdfObject()->put(key, value);
        return *this;
    }
};

}  // namespace kernel
}  // namespace pdfcpp
