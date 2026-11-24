// Adds a watermark to every page of an existing PDF (stamping, optionally in
// append mode).
#include <iostream>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: stamp <in.pdf> <out.pdf> [--append]\n";
    return 1;
  }
  StampingProperties props;
  if (argc > 3 && std::string(argv[3]) == "--append") props.UseAppendMode();
  PdfDocument pdf(std::make_unique<PdfReader>(argv[1]),
                  std::make_unique<PdfWriter>(argv[2]), props);
  auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  PdfExtGState gs;
  gs.SetFillOpacity(0.3f);
  for (int i = 1; i <= pdf.GetNumberOfPages(); ++i) {
    PdfPage* page = pdf.GetPage(i);
    Rectangle size = page->GetPageSize();
    PdfCanvas canvas(page);
    canvas.SaveState().SetExtGState(gs).SetFillColor(color_constants::kRed);
    canvas.BeginText().SetFontAndSize(font, 60);
    canvas.SetTextMatrix(AffineTransform(0.7071, 0.7071, -0.7071, 0.7071,
                                         size.GetWidth() / 2 - 150,
                                         size.GetHeight() / 2 - 100));
    canvas.ShowText("CONFIDENTIAL").EndText().RestoreState();
  }
  int pages = pdf.GetNumberOfPages();
  pdf.Close();
  std::cout << "Stamped " << pages << " page(s) into " << argv[2] << "\n";
  return 0;
}
