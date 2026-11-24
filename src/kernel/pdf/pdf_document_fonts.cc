// PdfDocument / PdfResources functionality that depends on the font module.
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace kernel {

PdfFont* PdfDocument::AddFont(std::shared_ptr<PdfFont> font) {
  CheckClosingStatus();
  if (font->GetPdfObject()->GetIndirectReference() == nullptr && writer_)
    font->GetPdfObject()->MakeIndirect(this);
  PdfFont* raw = font.get();
  document_fonts_[font->GetPdfObject().get()] = std::move(font);
  return raw;
}

PdfFont* PdfDocument::GetDefaultFont() {
  if (!default_font_) {
    default_font_ = PdfFontFactory::CreateFont();
    if (writer_) AddFont(default_font_);
  }
  return default_font_.get();
}

PdfFont* PdfDocument::GetFont(const PdfDictionaryPtr& dictionary) {
  auto it = document_fonts_.find(dictionary.get());
  if (it != document_fonts_.end()) return it->second.get();
  auto font = PdfFontFactory::CreateFont(dictionary);
  PdfFont* raw = font.get();
  document_fonts_[dictionary.get()] = std::move(font);
  return raw;
}

void PdfDocument::FlushFonts() {
  for (auto& kv : document_fonts_) {
    PdfFont* font = kv.second.get();
    if (!font->IsNewFont()) continue;
    if (properties_.append_mode) {
      auto* ref = font->GetPdfObject()->GetIndirectReference();
      if (font->GetPdfObject()->CheckState(PdfObject::kMustBeIndirect) ||
          (ref && ref->CheckState(PdfObject::kModified)))
        font->Flush();
    } else {
      font->Flush();
    }
  }
}

PdfNamePtr PdfResources::AddFont(PdfDocument* pdf_document, PdfFont* font) {
  if (pdf_document && pdf_document->GetWriter()) {
    // the document keeps the font alive and flushes it on close
    std::shared_ptr<PdfFont> owned;
    try {
      owned = font->shared_from_this();
    } catch (const std::bad_weak_ptr&) {
    }
    if (owned)
      pdf_document->AddFont(owned);
    else if (font->GetPdfObject()->GetIndirectReference() == nullptr)
      font->GetPdfObject()->MakeIndirect(pdf_document);
  }
  return AddResource(font->GetPdfObject(), PdfName::kFont, "F");
}

PdfNamePtr PdfResources::AddImage(PdfImageXObject& image) {
  return AddResource(image.GetPdfObject(), PdfName::kXObject, "Im");
}
PdfNamePtr PdfResources::AddForm(PdfFormXObject& form) {
  return AddResource(form.GetPdfObject(), PdfName::kXObject, "Fm");
}
PdfNamePtr PdfResources::AddForm(PdfFormXObject& form, const PdfNamePtr& name) {
  // Port of Java: if the requested name is taken, fall back to a generated one;
  // otherwise register it under the given name (which also records it in
  // resource_to_name_ and marks the dictionary modified).
  auto xobjects = GetPdfObject()->GetAsDictionary(PdfName::kXObject);
  if (xobjects && xobjects->ContainsKey(name))
    return AddResource(form.GetPdfObject(), PdfName::kXObject, "Fm");
  AddResource(form.GetPdfObject(), PdfName::kXObject, name);
  return name;
}
PdfNamePtr PdfResources::AddExtGState(PdfExtGState& ext_g_state) {
  return AddResource(ext_g_state.GetPdfObject(), PdfName::kExtGState, "Gs");
}

}  // namespace kernel
}  // namespace pdfcpp
