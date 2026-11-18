// Prints the text of every page of a PDF (port of PdfTextExtractor usage).
#include <iostream>

#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"

using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: extract_text <file.pdf> [password]\n";
        return 1;
    }
    ReaderProperties props;
    if (argc > 2) props.setPassword(std::string(argv[2]));
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1], props));
    for (int i = 1; i <= pdf.getNumberOfPages(); ++i) {
        std::cout << "--- page " << i << " ---\n" << PdfTextExtractor::getTextFromPage(pdf.getPage(i)) << "\n";
    }
    return 0;
}
