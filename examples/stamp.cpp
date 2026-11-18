// Adds a watermark to every page of an existing PDF (stamping, optionally in append mode).
#include <iostream>

#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: stamp <in.pdf> <out.pdf> [--append]\n";
        return 1;
    }
    StampingProperties props;
    if (argc > 3 && std::string(argv[3]) == "--append") props.useAppendMode();
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1]), std::make_unique<PdfWriter>(argv[2]), props);
    auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    PdfExtGState gs;
    gs.setFillOpacity(0.3f);
    for (int i = 1; i <= pdf.getNumberOfPages(); ++i) {
        PdfPage* page = pdf.getPage(i);
        Rectangle size = page->getPageSize();
        PdfCanvas canvas(page);
        canvas.saveState().setExtGState(gs).setFillColor(ColorConstants::RED);
        canvas.beginText().setFontAndSize(font, 60);
        canvas.setTextMatrix(AffineTransform(0.7071, 0.7071, -0.7071, 0.7071, size.getWidth() / 2 - 150, size.getHeight() / 2 - 100));
        canvas.showText("CONFIDENTIAL").endText().restoreState();
    }
    int pages = pdf.getNumberOfPages();
    pdf.close();
    std::cout << "Stamped " << pages << " page(s) into " << argv[2] << "\n";
    return 0;
}
