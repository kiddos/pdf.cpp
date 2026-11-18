// A small report: title, paragraphs, a list, a table that spans pages and an outline.
#include <iostream>

#include "pdfcpp/kernel/pdf/Navigation.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;
namespace ColorConstants = kernel::ColorConstants;

int main(int argc, char** argv) {
    std::string dest = argc > 1 ? argv[1] : "report.pdf";
    kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
    pdf.getDocumentInfo().setTitle("pdfcpp report").setAuthor("pdfcpp");
    Document document(pdf, kernel::PageSize::A4);

    auto bold = kernel::PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    document.add(make<Paragraph>("Quarterly Report")->setFont(bold).setFontSize(22).setTextAlignment(TextAlignment::CENTER));
    document.add(make<Paragraph>("This document was generated with pdfcpp, a C++ port of iText.")
                     ->setFontColor(ColorConstants::DARK_GRAY));

    auto list = make<List>(ListNumberingType::DECIMAL);
    list->add("Revenue grew steadily").add("Costs were kept under control").add("Outlook remains positive");
    document.add(list);

    auto table = make<Table>(UnitValue::createPercentArray({1, 3, 2}));
    table->useAllAvailableWidth();
    for (const char* h : {"#", "Item", "Amount"})
        table->addHeaderCell(make<Cell>()->add(make<Paragraph>(h)->setFont(bold)).setBackgroundColor(ColorConstants::LIGHT_GRAY));
    for (int i = 1; i <= 80; ++i) {
        table->addCell(std::to_string(i));
        table->addCell("Line item " + std::to_string(i));
        table->addCell(make<Cell>()->add(make<Paragraph>(std::to_string(i * 37 % 1000) + ".00")->setTextAlignment(TextAlignment::RIGHT)));
    }
    document.add(table);

    auto outlines = kernel::PdfOutline::getOutlines(pdf);
    outlines->addOutline("Report")->addDestination(kernel::PdfDestination::createFit(pdf.getPage(1)));
    document.close();
    std::cout << "Wrote " << dest << "\n";
    return 0;
}
