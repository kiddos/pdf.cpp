// Port of the classic iText "Hello World" (layout API).
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "hello_world.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  Document document(pdf);
  document.Add(Make<Paragraph>("Hello World!"));
  document.Close();
  return 0;
}
