// PdfDocument / PdfResources functionality that depends on the font module.
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace kernel {

PdfFont* PdfDocument::addFont(std::shared_ptr<PdfFont> font) {
    checkClosingStatus();
    if (font->getPdfObject()->getIndirectReference() == nullptr && writer_) font->getPdfObject()->makeIndirect(this);
    PdfFont* raw = font.get();
    documentFonts_[font->getPdfObject().get()] = std::move(font);
    return raw;
}

PdfFont* PdfDocument::getDefaultFont() {
    if (!defaultFont_) {
        defaultFont_ = PdfFontFactory::createFont();
        if (writer_) addFont(defaultFont_);
    }
    return defaultFont_.get();
}

PdfFont* PdfDocument::getFont(const PdfDictionaryPtr& dictionary) {
    auto it = documentFonts_.find(dictionary.get());
    if (it != documentFonts_.end()) return it->second.get();
    auto font = PdfFontFactory::createFont(dictionary);
    PdfFont* raw = font.get();
    documentFonts_[dictionary.get()] = std::move(font);
    return raw;
}

void PdfDocument::flushFonts() {
    for (auto& kv : documentFonts_) {
        PdfFont* font = kv.second.get();
        if (!font->isNewFont()) continue;
        if (properties_.appendMode) {
            auto* ref = font->getPdfObject()->getIndirectReference();
            if (font->getPdfObject()->checkState(PdfObject::MUST_BE_INDIRECT) || (ref && ref->checkState(PdfObject::MODIFIED)))
                font->flush();
        } else {
            font->flush();
        }
    }
}

PdfNamePtr PdfResources::addFont(PdfDocument* pdfDocument, PdfFont* font) {
    if (pdfDocument && pdfDocument->getWriter()) {
        // the document keeps the font alive and flushes it on close
        std::shared_ptr<PdfFont> owned;
        try {
            owned = font->shared_from_this();
        } catch (const std::bad_weak_ptr&) {
        }
        if (owned) pdfDocument->addFont(owned);
        else if (font->getPdfObject()->getIndirectReference() == nullptr) font->getPdfObject()->makeIndirect(pdfDocument);
    }
    return addResource(font->getPdfObject(), PdfName::Font, "F");
}

PdfNamePtr PdfResources::addImage(PdfImageXObject& image) { return addResource(image.getPdfObject(), PdfName::XObject, "Im"); }
PdfNamePtr PdfResources::addForm(PdfFormXObject& form) { return addResource(form.getPdfObject(), PdfName::XObject, "Fm"); }
PdfNamePtr PdfResources::addForm(PdfFormXObject& form, const PdfNamePtr& name) {
    // Port of Java: if the requested name is taken, fall back to a generated one; otherwise register it under the
    // given name (which also records it in resourceToName_ and marks the dictionary modified).
    auto xobjects = getPdfObject()->getAsDictionary(PdfName::XObject);
    if (xobjects && xobjects->containsKey(name)) return addResource(form.getPdfObject(), PdfName::XObject, "Fm");
    addResource(form.getPdfObject(), PdfName::XObject, name);
    return name;
}
PdfNamePtr PdfResources::addExtGState(PdfExtGState& extGState) { return addResource(extGState.getPdfObject(), PdfName::ExtGState, "Gs"); }

}  // namespace kernel
}  // namespace pdfcpp
