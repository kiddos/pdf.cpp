#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

#include "pdfcpp/kernel/pdf/PdfReader.h"

namespace pdfcpp {
namespace kernel {

std::shared_ptr<PdfXObject> PdfXObject::makeXObject(const PdfStreamPtr& stream) {
    if (nameEquals(stream->getAsName(PdfName::Subtype), PdfName::Image)) return std::make_shared<PdfImageXObject>(stream);
    return std::make_shared<PdfFormXObject>(stream);
}

bool PdfXObject::isImage() const { return nameEquals(getPdfObject()->getAsName(PdfName::Subtype), PdfName::Image); }

// ============================================================ PdfImageXObject
PdfImageXObject::PdfImageXObject(const std::shared_ptr<io::ImageData>& image) : PdfXObject(createPdfStream(image)) {}

PdfImageXObject::PdfImageXObject(PdfStreamPtr pdfStream) : PdfXObject(std::move(pdfStream)) {}

PdfStreamPtr PdfImageXObject::createPdfStream(const std::shared_ptr<io::ImageData>& image) {
    auto stream = std::make_shared<PdfStream>(image->getData());
    stream->put(PdfName::Type, PdfName::XObject);
    stream->put(PdfName::Subtype, PdfName::Image);
    stream->put(PdfName::Width, makeNumber(static_cast<int>(image->getWidth())));
    stream->put(PdfName::Height, makeNumber(static_cast<int>(image->getHeight())));
    if (image->isMask()) {
        if (image->getBpc() == 1) {
            stream->put(PdfName::ImageMask, PdfBoolean::TRUE_VALUE);
        } else {
            stream->put(PdfName::ColorSpace, PdfName::DeviceGray);
            stream->put(PdfName::BitsPerComponent, makeNumber(image->getBpc()));
        }
    } else {
        stream->put(PdfName::BitsPerComponent, makeNumber(image->getBpc()));
        int n = image->getColorEncodingComponentsNumber();
        PdfObjectPtr cs = n == 1 ? PdfName::DeviceGray : n == 3 ? PdfName::DeviceRGB : PdfName::DeviceCMYK;
        if (!image->getIccProfile().empty()) {
            auto icc = std::make_shared<PdfStream>(image->getIccProfile());
            icc->put(PdfName::N, makeNumber(n));
            auto arr = std::make_shared<PdfArray>();
            arr->add(PdfName::ICCBased);
            arr->add(icc);
            cs = arr;
        }
        if (!image->getPalette().empty()) {
            auto arr = std::make_shared<PdfArray>();
            arr->add(PdfName::Indexed);
            arr->add(PdfName::DeviceRGB);
            arr->add(makeNumber(static_cast<int>(image->getPalette().size() / 3) - 1));
            auto lookup = std::make_shared<PdfString>(image->getPalette());
            lookup->setHexWriting(true);
            arr->add(lookup);
            cs = arr;
        }
        stream->put(PdfName::ColorSpace, cs);
    }
    if (!image->getFilter().empty()) {
        stream->put(PdfName::Filter, PdfName::of(image->getFilter()));
        stream->setCompressionLevel(CompressionConstants::NO_COMPRESSION);
        if (!image->getDecodeParms().empty()) {
            auto dp = std::make_shared<PdfDictionary>();
            for (const auto& kv : image->getDecodeParms()) dp->put(PdfName::of(kv.first), makeNumber(kv.second));
            stream->put(PdfName::DecodeParms, dp);
        }
    }
    if (!image->getDecode().empty()) stream->put(PdfName::Decode, std::make_shared<PdfArray>(image->getDecode()));
    if (!image->getColorKeyMask().empty()) stream->put(PdfName::Mask, std::make_shared<PdfArray>(image->getColorKeyMask()));
    if (image->getImageMask()) {
        auto mask = createPdfStream(image->getImageMask());
        if (image->getImageMask()->getBpc() == 1) stream->put(PdfName::Mask, mask);
        else stream->put(PdfName::SMask, mask);
    }
    return stream;
}

float PdfImageXObject::getWidth() const {
    auto w = getPdfObject()->getAsNumber(PdfName::Width);
    return w ? w->floatValue() : 0;
}

float PdfImageXObject::getHeight() const {
    auto h = getPdfObject()->getAsNumber(PdfName::Height);
    return h ? h->floatValue() : 0;
}

Bytes PdfImageXObject::getImageBytes(bool decoded) { return getPdfObject()->getBytes(decoded); }

std::string PdfImageXObject::identifyImageFileExtension() const {
    PdfObjectPtr filter = getPdfObject()->get(PdfName::Filter);
    PdfNamePtr last;
    if (filter && filter->isName()) last = std::static_pointer_cast<PdfName>(filter);
    else if (filter && filter->isArray()) {
        auto arr = std::static_pointer_cast<PdfArray>(filter);
        if (arr->size() > 0) last = arr->getAsName(arr->size() - 1);
    }
    if (last) {
        if (last->getValue() == "DCTDecode") return "jpg";
        if (last->getValue() == "JPXDecode") return "jp2";
        if (last->getValue() == "JBIG2Decode") return "jbig2";
        if (last->getValue() == "CCITTFaxDecode") return "tif";
    }
    return "png";
}

// ============================================================ PdfFormXObject
PdfFormXObject::PdfFormXObject(const Rectangle& bBox) : PdfXObject(std::make_shared<PdfStream>()) {
    getPdfObject()->put(PdfName::Type, PdfName::XObject);
    getPdfObject()->put(PdfName::Subtype, PdfName::Form);
    getPdfObject()->put(PdfName::BBox, std::make_shared<PdfArray>(bBox));
}

PdfFormXObject::PdfFormXObject(PdfStreamPtr pdfStream) : PdfXObject(std::move(pdfStream)) {}

PdfResources* PdfFormXObject::getResources() {
    if (!resources_) {
        auto resourcesDict = getPdfObject()->getAsDictionary(PdfName::Resources);
        if (!resourcesDict) {
            resourcesDict = std::make_shared<PdfDictionary>();
            getPdfObject()->put(PdfName::Resources, resourcesDict);
        }
        resources_ = std::make_shared<PdfResources>(resourcesDict);
    }
    return resources_.get();
}

float PdfFormXObject::getWidth() const {
    auto b = getBBox();
    return b ? b->getWidth() : 0;
}

float PdfFormXObject::getHeight() const {
    auto b = getBBox();
    return b ? b->getHeight() : 0;
}

void PdfFormXObject::flush() {
    resources_.reset();
    PdfXObject::flush();
}

// ============================================================ PdfExtGState
const PdfNamePtr& PdfExtGState::BM_NORMAL() { return PdfName::Normal; }
const PdfNamePtr& PdfExtGState::BM_MULTIPLY() { return PdfName::Multiply; }
const PdfNamePtr& PdfExtGState::BM_SCREEN() { return PdfName::Screen; }

}  // namespace kernel
}  // namespace pdfcpp
