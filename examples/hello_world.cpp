// Port of the classic iText "Hello World" (layout API).
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
    std::string dest = argc > 1 ? argv[1] : "hello_world.pdf";
    kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
    Document document(pdf);
    document.add(make<Paragraph>("Hello World!"));
    document.close();
    return 0;
}
