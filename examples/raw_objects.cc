// Works directly with the PDF object model, below PdfCanvas and layout:
//   1. builds pages from hand-written content-stream operators and
//      hand-made dictionaries (a font, resources, a named-destination tree,
//      viewer preferences, custom catalog data);
//   2. reopens the file and dumps its object graph, then decodes a content
//      stream.
#include <iostream>
#include <set>

#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

namespace {

PdfNamePtr Name(const std::string& value) { return PdfName::Of(value); }

void SetContent(PdfPage* page, const std::string& operators) {
  // A new page already has one empty content stream.
  PdfStreamPtr stream = page->GetFirstContentStream();
  if (!stream) stream = page->NewContentStreamAfter();
  stream->SetData(Bytes(operators.begin(), operators.end()));
}

// Prints an object tree. Indirect references are followed once each, so
// cycles (e.g. /Parent) are printed as plain "N 0 R".
void Dump(const PdfObjectPtr& object, int depth, std::set<int>& visited) {
  std::string indent(depth * 2, ' ');
  switch (object->GetType()) {
    case PdfObject::kIndirectReference: {
      auto ref = PdfCast<PdfIndirectReference>(object);
      std::cout << ref->GetObjNumber() << " " << ref->GetGenNumber() << " R";
      if (depth > 12 || !visited.insert(ref->GetObjNumber()).second) return;
      std::cout << " => ";
      Dump(ref->GetRefersToPtr(), depth, visited);
      return;
    }
    case PdfObject::kDictionary:
    case PdfObject::kStream: {
      auto dict = PdfCast<PdfDictionary>(object);
      if (object->GetType() == PdfObject::kStream)
        std::cout << "stream (" << PdfCast<PdfStream>(object)->GetLength()
                  << " bytes) ";
      std::cout << "<<\n";
      for (const auto& [key, value] : dict->EntrySet()) {
        std::cout << indent << "  /" << key->GetValue() << " ";
        Dump(value, depth + 1, visited);
        std::cout << "\n";
      }
      std::cout << indent << ">>";
      return;
    }
    case PdfObject::kArray: {
      auto array = PdfCast<PdfArray>(object);
      std::cout << "[";
      for (int i = 0; i < array->size(); ++i) {
        std::cout << (i ? " " : "");
        Dump(array->Get(i, /*as_direct=*/false), depth + 1, visited);
      }
      std::cout << "]";
      return;
    }
    case PdfObject::kString:
      std::cout << "(" << PdfCast<PdfString>(object)->ToUnicodeString() << ")";
      return;
    default:
      std::cout << object->ToString();
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "raw_objects.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(dest));
    PdfPage* page1 = pdf.AddNewPage(PageSize::kA5);
    PdfPage* page2 = pdf.AddNewPage(PageSize::kA5);

    // A standard Type1 font dictionary written by hand, made indirect so both
    // pages share one object.
    auto font = std::make_shared<PdfDictionary>();
    font->Put(PdfName::kType, PdfName::kFont);
    font->Put(PdfName::kSubtype, PdfName::kType1);
    font->Put(PdfName::kBaseFont, Name("Helvetica"));
    font->Put(PdfName::kEncoding, PdfName::kWinAnsiEncoding);
    font->MakeIndirect(&pdf);
    // AddResource picks a free name (/F1, ...) in the page's /Font dictionary.
    PdfNamePtr f1 =
        page1->GetResources()->AddResource(font, PdfName::kFont, "F");
    PdfNamePtr f2 =
        page2->GetResources()->AddResource(font, PdfName::kFont, "F");

    // Content streams are plain operator text (ISO 32000-1, section 8/9).
    SetContent(
        page1,
        "0.12 0.27 0.55 rg\n"
        "40 460 340 90 re f\n"
        "BT /" +
            f1->GetValue() +
            " 22 Tf 1 g 55 495 Td "
            "(Raw content stream) Tj ET\n"
            "BT /" +
            f1->GetValue() +
            " 12 Tf 0 g 40 420 Td 16 TL "
            "(Every byte on this page was written by hand:) Tj "
            "T* (re/f paint a rectangle, BT/ET wrap the text,) Tj "
            "T* (Tf selects the font, Td and T* move the cursor.) Tj ET\n"
            "0 0 1 RG 40 340 m 380 340 l S\n"
            "BT /" +
            f1->GetValue() +
            " 12 Tf 0 0 1 rg 40 320 Td "
            "(Click here to jump to the named destination \\(page 2\\)) Tj "
            "ET\n");
    SetContent(page2, "BT /" + f2->GetValue() +
                          " 18 Tf 40 520 Td "
                          "(Page 2: the \"details\" destination) Tj ET\n"
                          "0.9 0.3 0.2 rg 200 250 m 300 450 l 100 450 l h f\n");

    // A link to a *named* destination, resolved through the catalog's
    // /Names /Dests name tree.
    auto link = PdfAnnotation::CreateLink(Rectangle(38, 314, 345, 18));
    link->SetDestination(PdfDestination::CreateNamed("details"))
        .SetBorder(0, 0, 0);
    page1->AddAnnotation(link->GetPdfObject());

    auto names = std::make_shared<PdfArray>();
    names->Add(std::make_shared<PdfString>("details"));
    names->Add(PdfDestination::CreateFit(page2)->GetPdfObject());
    auto dests = std::make_shared<PdfDictionary>();
    dests->Put(PdfName::kNames, names);
    auto names_dict = std::make_shared<PdfDictionary>();
    names_dict->Put(PdfName::kDests, dests);
    pdf.GetCatalog().Put(PdfName::kNames, names_dict);

    // Viewer preferences and page layout.
    auto prefs = std::make_shared<PdfDictionary>();
    prefs->Put(PdfName::kDisplayDocTitle, PdfBoolean::ValueOf(true));
    prefs->Put(PdfName::kFitWindow, PdfBoolean::ValueOf(true));
    pdf.GetCatalog()
        .Put(PdfName::kViewerPreferences, prefs)
        .SetPageLayout(PdfName::kTwoPageLeft);
    pdf.GetDocumentInfo().SetTitle("Raw objects");

    // Private application data: second-class names should carry a
    // registered prefix; readers ignore keys they don't know.
    auto app_data = std::make_shared<PdfDictionary>();
    app_data->Put(Name("Version"), MakeNumber(3));
    app_data->Put(Name("Tags"), std::make_shared<PdfArray>(
                                    std::vector<std::string>{"demo", "raw"},
                                    /*as_names=*/true));
    app_data->Put(Name("Ratio"), MakeNumber(0.75));
    app_data->MakeIndirect(&pdf);
    pdf.GetCatalog().Put(Name("PDFCPP_AppData"), app_data);
    pdf.Close();
  }
  std::cout << "Wrote " << dest << "\n\n";

  // Read the file back and walk the object graph from the catalog.
  PdfDocument pdf(std::make_unique<PdfReader>(dest));
  std::set<int> visited;
  std::cout << "Catalog: ";
  Dump(pdf.GetCatalog().GetPdfObject(), 0, visited);
  std::cout << "\n\nDecoded content of page 2:\n";
  Bytes content = pdf.GetPage(2)->GetContentBytes();
  std::cout << std::string(content.begin(), content.end());
  return 0;
}
