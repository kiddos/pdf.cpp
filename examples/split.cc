// Splits a PDF into smaller PDFs of N pages each (port of PdfSplitter usage).
#include <iostream>
#include <string>

#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: split <in.pdf> <out_prefix> [pages_per_part]\n";
    return 1;
  }
  std::string prefix = argv[2];
  int pages_per_part = argc > 3 ? std::stoi(argv[3]) : 1;
  if (pages_per_part < 1) {
    std::cerr << "pages_per_part must be >= 1\n";
    return 1;
  }

  PdfDocument src(std::make_unique<PdfReader>(argv[1]));
  PdfSplitter splitter(src);
  std::vector<Bytes> parts = splitter.SplitByPageCount(pages_per_part);

  for (size_t i = 0; i < parts.size(); ++i) {
    std::string out_path = prefix + "_" + std::to_string(i + 1) + ".pdf";
    io::FileOutputStream out(out_path);
    out.Write(parts[i]);
    out.Close();
    std::cout << "Wrote " << out_path << " (" << parts[i].size() << " bytes)\n";
  }
  return 0;
}
