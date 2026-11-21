// Splits a PDF into smaller PDFs of N pages each (port of PdfSplitter usage).
#include <iostream>
#include <string>

#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/kernel/pdf/Navigation.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: split <in.pdf> <out_prefix> [pages_per_part]\n";
        return 1;
    }
    std::string prefix = argv[2];
    int pagesPerPart = argc > 3 ? std::stoi(argv[3]) : 1;
    if (pagesPerPart < 1) {
        std::cerr << "pages_per_part must be >= 1\n";
        return 1;
    }

    PdfDocument src(std::make_unique<PdfReader>(argv[1]));
    PdfSplitter splitter(src);
    std::vector<Bytes> parts = splitter.splitByPageCount(pagesPerPart);

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string outPath = prefix + "_" + std::to_string(i + 1) + ".pdf";
        io::FileOutputStream out(outPath);
        out.write(parts[i]);
        out.close();
        std::cout << "Wrote " << outPath << " (" << parts[i].size() << " bytes)\n";
    }
    return 0;
}
