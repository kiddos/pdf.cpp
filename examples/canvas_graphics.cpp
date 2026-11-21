// Demonstrates low-level PdfCanvas vector graphics, colors/opacity, FormXObjects, annotations, and page footers.
#include <iostream>

#include "pdfcpp/kernel/pdf/Navigation.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
    std::string dest = argc > 1 ? argv[1] : "canvas_graphics.pdf";
    PdfDocument pdf(std::make_unique<PdfWriter>(dest));
    Document document(pdf, PageSize::A4, /*immediateFlush=*/false);

    auto bold = PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    document.add(make<Paragraph>("Canvas Graphics & Annotations")->setFont(bold).setFontSize(20).setMarginBottom(8));

    // Hyperlink inside a Paragraph.
    auto linkPara = make<Paragraph>("Visit ");
    linkPara->add(make<Link>("the iText repository", "https://github.com/itext/itext-java")
                      ->setFontColor(ColorConstants::BLUE)
                      .setUnderline());
    linkPara->add(" for the Java reference.");
    document.add(linkPara);

    // Draw vector shapes directly on page 1 using PdfCanvas.
    PdfPage* page = pdf.getPage(1);
    PdfCanvas canvas(page);

    // Reusable vector badge as a PdfFormXObject.
    auto badge = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 80, 80));
    PdfCanvas badgeCanvas(*badge, &pdf);
    badgeCanvas.setFillColor(std::make_shared<DeviceCmyk>(0.85f, 0.25f, 0.0f, 0.0f))
        .circle(40, 40, 36)
        .fill()
        .setStrokeColor(ColorConstants::WHITE)
        .setLineWidth(3)
        .moveTo(22, 40)
        .lineTo(35, 27)
        .lineTo(58, 52)
        .stroke();

    // Place the badge twice at different scales/positions.
    canvas.addXObjectAt(*badge, 60, 560);
    canvas.saveState().concatMatrix(0.6, 0, 0, 0.6, 160, 576).addXObject(*badge).restoreState();

    // Dashed Bezier curve + translucent overlapping rectangles.
    canvas.saveState()
        .setStrokeColor(ColorConstants::DARK_GRAY)
        .setLineWidth(2)
        .setLineDash({6, 4}, 0)
        .moveTo(260, 580)
        .curveTo(320, 660, 420, 520, 500, 620)
        .stroke()
        .restoreState();

    PdfExtGState translucent;
    translucent.setFillOpacity(0.55f);
    canvas.saveState().setExtGState(translucent);
    canvas.setFillColor(std::make_shared<DeviceRgb>(230, 80, 60)).rectangle(60, 440, 110, 70).fill();
    canvas.setFillColor(std::make_shared<DeviceRgb>(50, 140, 220)).rectangle(120, 415, 110, 70).fill();
    canvas.restoreState();

    // Add a sticky-note annotation and a square annotation on page 1.
    auto note = PdfAnnotation::createText(Rectangle(260, 470, 24, 24));
    note->setTitle("Reviewer").setContents("Vector shapes drawn with PdfCanvas.").setColor(ColorConstants::YELLOW);
    page->addAnnotation(note->getPdfObject());

    auto box = PdfAnnotation::createSquare(Rectangle(55, 410, 180, 105));
    box->setColor(ColorConstants::GRAY).setBorder(0, 0, 1);
    page->addAnnotation(box->getPdfObject());

    // Add a second page and stamp "Page X of Y" footers across all pages (possible because immediateFlush=false).
    document.add(make<AreaBreak>());
    document.add(make<Paragraph>("Second Page")->setFont(bold).setFontSize(16));
    document.add(make<Paragraph>("Both pages share a dynamically numbered footer."));

    int totalPages = pdf.getNumberOfPages();
    for (int i = 1; i <= totalPages; ++i) {
        Rectangle ps = pdf.getPage(i)->getPageSize();
        auto footer = std::make_shared<Paragraph>("Page " + std::to_string(i) + " of " + std::to_string(totalPages));
        footer->setFontSize(9).setFontColor(ColorConstants::GRAY);
        document.showTextAligned(footer, ps.getWidth() / 2, 24, i, TextAlignment::CENTER, VerticalAlignment::BOTTOM, 0);
    }

    document.close();
    std::cout << "Wrote " << dest << "\n";
    return 0;
}
