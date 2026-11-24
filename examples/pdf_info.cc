// Prints a report about an existing PDF: version, encryption, document
// information, pages (size, rotation, fonts, images, annotations), bookmarks
// and form fields. Shows how to walk the object model of a parsed document.
//
// usage: pdf_info <file.pdf> [password]
#include <iomanip>
#include <iostream>
#include <map>
#include <set>

#include "pdfcpp/forms/forms.h"
#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/signatures/signatures.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

namespace {

std::string NameOf(const PdfNamePtr& name) {
  return name ? name->GetValue() : "-";
}

// A font's program is embedded if its descriptor has a FontFile* entry. For
// Type0 fonts the descriptor lives in the descendant CIDFont.
bool IsEmbedded(const PdfDictionaryPtr& font) {
  PdfDictionaryPtr dict = font;
  if (NameOf(font->GetAsName(PdfName::kSubtype)) == "Type3") return true;
  if (auto descendants = font->GetAsArray(PdfName::kDescendantFonts))
    dict = descendants->GetAsDictionary(0);
  auto descriptor =
      dict ? dict->GetAsDictionary(PdfName::kFontDescriptor) : nullptr;
  return descriptor && (descriptor->ContainsKey(PdfName::kFontFile) ||
                        descriptor->ContainsKey(PdfName::kFontFile2) ||
                        descriptor->ContainsKey(PdfName::kFontFile3));
}

void PrintOutlines(PdfDocument& pdf, const PdfOutline& outline, int depth) {
  for (const auto& child : outline.GetAllChildren()) {
    std::cout << std::string(2 + depth * 2, ' ') << child->GetTitle();
    auto dest = child->GetContent()->GetAsArray(PdfName::kDest);
    if (dest && dest->GetAsDictionary(0))
      std::cout << "  -> page "
                << pdf.GetPageNumber(dest->GetAsDictionary(0).get());
    else if (child->GetContent()->ContainsKey(PdfName::kA))
      std::cout << "  -> action";
    std::cout << "\n";
    PrintOutlines(pdf, *child, depth + 1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: pdf_info <file.pdf> [password]\n";
    return 1;
  }
  ReaderProperties props;
  if (argc > 2) props.SetPassword(std::string(argv[2]));
  try {
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1], props));
    PdfReader& reader = *pdf.GetReader();

    std::cout << "File:        " << argv[1] << " (" << reader.GetFileLength()
              << " bytes)\n"
              << "Version:     " << pdf.GetPdfVersion().ToString() << "\n"
              << "Objects:     " << pdf.GetNumberOfPdfObjects() << "\n"
              << "Xref:        "
              << (reader.HasXrefStm() ? "cross-reference stream" : "table")
              << (reader.HasRebuiltXref() ? " (rebuilt: file was damaged)" : "")
              << "\n"
              << "Encrypted:   " << (reader.IsEncrypted() ? "yes" : "no");
    if (reader.IsEncrypted())
      std::cout << " (permissions 0x" << std::hex << reader.GetPermissions()
                << std::dec << ", opened with "
                << (reader.IsOpenedWithFullPermission() ? "owner" : "user")
                << " password)";
    std::cout << "\n";

    PdfDocumentInfo& info = pdf.GetDocumentInfo();
    for (const auto& [label, value] :
         std::vector<std::pair<const char*, std::string>>{
             {"Title", info.GetTitle()},
             {"Author", info.GetAuthor()},
             {"Subject", info.GetSubject()},
             {"Keywords", info.GetKeywords()},
             {"Creator", info.GetCreator()},
             {"Producer", info.GetProducer()}}) {
      if (!value.empty())
        std::cout << std::left << std::setw(13) << (std::string(label) + ":")
                  << value << "\n";
    }
    std::cout << "XMP:         "
              << (pdf.GetXmpMetadataBytes().empty() ? "no" : "yes") << "\n"
              << "Page mode:   " << NameOf(pdf.GetCatalog().GetPageMode())
              << "\n";

    // Pages.
    std::cout << "\nPages: " << pdf.GetNumberOfPages() << "\n";
    std::set<std::string> all_fonts;
    for (int i = 1; i <= pdf.GetNumberOfPages(); ++i) {
      PdfPage* page = pdf.GetPage(i);
      Rectangle box = page->GetPageSize();
      std::cout << "  page " << i << ": " << std::fixed << std::setprecision(0)
                << box.GetWidth() << " x " << box.GetHeight() << " pt ("
                << box.GetWidth() * 25.4 / 72 << " x "
                << box.GetHeight() * 25.4 / 72 << " mm)";
      if (page->GetRotation() != 0)
        std::cout << ", rotated " << page->GetRotation();
      std::cout << ", " << page->GetContentBytes().size()
                << " bytes of content\n";

      PdfResources* resources = page->GetResources();
      if (auto fonts = resources->GetResource(PdfName::kFont)) {
        for (const auto& key : fonts->KeySet()) {
          auto font = fonts->GetAsDictionary(key);
          if (!font) continue;
          std::string desc = NameOf(font->GetAsName(PdfName::kBaseFont)) +
                             " (" + NameOf(font->GetAsName(PdfName::kSubtype)) +
                             (IsEmbedded(font) ? ", embedded" : "") + ")";
          all_fonts.insert(desc);
          std::cout << "    font  /" << key->GetValue() << ": " << desc << "\n";
        }
      }
      if (auto xobjects = resources->GetResource(PdfName::kXObject)) {
        for (const auto& key : xobjects->KeySet()) {
          auto stream = xobjects->GetAsStream(key);
          if (!stream) continue;
          std::cout << "    xobj  /" << key->GetValue() << ": "
                    << NameOf(stream->GetAsName(PdfName::kSubtype));
          if (auto w = stream->GetAsInt(PdfName::kWidth))
            std::cout << " " << *w << "x"
                      << stream->GetAsInt(PdfName::kHeight).value_or(0)
                      << " px";
          std::cout << "\n";
        }
      }
      std::map<std::string, int> annotation_counts;
      for (const auto& annotation : page->GetAnnotations())
        ++annotation_counts[NameOf(annotation->GetAsName(PdfName::kSubtype))];
      for (const auto& [subtype, count] : annotation_counts)
        std::cout << "    annot " << subtype << " x" << count << "\n";
    }

    std::cout << "\nFonts used: " << all_fonts.size() << "\n";
    for (const auto& f : all_fonts) std::cout << "  " << f << "\n";

    // Bookmarks.
    auto outlines = PdfOutline::GetOutlines(pdf, /*update_outlines=*/true);
    if (outlines && !outlines->GetAllChildren().empty()) {
      std::cout << "\nBookmarks:\n";
      PrintOutlines(pdf, *outlines, 0);
    }

    // Form fields and signatures.
    if (auto form = forms::PdfAcroForm::GetAcroForm(pdf, false)) {
      auto fields = form->GetAllFormFields();
      std::cout << "\nForm fields: " << fields.size() << "\n";
      for (const auto& [name, field] : fields) {
        std::cout << "  " << name << " (" << NameOf(field->GetFormType())
                  << ")";
        // A signature field's value is the binary signature dictionary.
        std::string value = NameOf(field->GetFormType()) == "Sig"
                                ? std::string()
                                : field->GetValueAsString();
        if (!value.empty()) std::cout << " = \"" << value << "\"";
        std::cout << "\n";
      }
      signatures::SignatureUtil signatures(pdf);
      for (const auto& name : signatures.GetSignatureNames())
        std::cout << "  signature: " << name << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
