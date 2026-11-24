// A small report: title, paragraphs, a list, a table that spans pages and an
// outline.
#include <iostream>

#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;
namespace color_constants = kernel::color_constants;

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "report.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  pdf.GetDocumentInfo().SetTitle("pdfcpp report").SetAuthor("pdfcpp");
  Document document(pdf, kernel::PageSize::kA4);

  auto bold =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  document.Add(Make<Paragraph>("Quarterly Report")
                   ->SetFont(bold)
                   .SetFontSize(22)
                   .SetTextAlignment(TextAlignment::kCenter));
  document.Add(
      Make<Paragraph>(
          "This document was generated with pdfcpp, a C++ port of iText.")
          ->SetFontColor(color_constants::kDarkGray));

  auto list = Make<List>(ListNumberingType::kDecimal);
  list->Add("Revenue grew steadily")
      .Add("Costs were kept under control")
      .Add("Outlook remains positive");
  document.Add(list);

  auto table = Make<Table>(UnitValue::CreatePercentArray({1, 3, 2}));
  table->UseAllAvailableWidth();
  for (const char* h : {"#", "Item", "Amount"})
    table->AddHeaderCell(Make<Cell>()
                             ->Add(Make<Paragraph>(h)->SetFont(bold))
                             .SetBackgroundColor(color_constants::kLightGray));
  for (int i = 1; i <= 80; ++i) {
    table->AddCell(std::to_string(i));
    table->AddCell("Line item " + std::to_string(i));
    table->AddCell(
        Make<Cell>()->Add(Make<Paragraph>(std::to_string(i * 37 % 1000) + ".00")
                              ->SetTextAlignment(TextAlignment::kRight)));
  }
  document.Add(table);

  auto outlines = kernel::PdfOutline::GetOutlines(pdf);
  outlines->AddOutline("Report")->AddDestination(
      kernel::PdfDestination::CreateFit(pdf.GetPage(1)));
  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
