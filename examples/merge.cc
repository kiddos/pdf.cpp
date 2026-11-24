// Merges several PDFs into one (port of PdfMerger usage).
#include <iostream>

#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: merge <out.pdf> <in1.pdf> [in2.pdf ...]\n";
    return 1;
  }
  PdfDocument dest(std::make_unique<PdfWriter>(
      argv[1], WriterProperties().SetFullCompressionMode(true)));
  PdfMerger merger(dest);
  for (int i = 2; i < argc; ++i) {
    PdfDocument src(std::make_unique<PdfReader>(argv[i]));
    merger.Merge(src, 1, src.GetNumberOfPages());
  }
  int pages = dest.GetNumberOfPages();
  merger.Close();
  std::cout << "Merged " << pages << " page(s) into " << argv[1] << "\n";
  return 0;
}
