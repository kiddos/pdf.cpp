// Finds and replaces text in an existing PDF.
//   edit_text <in.pdf> <out.pdf> <search> <replacement> [options]
// Options:
//   --regex            treat <search> as an ECMAScript regex; <replacement> may use $1, $2, ...
//   --preserve-layout  keep text after each edit at its original position
//   --font <file>      TrueType/OpenType fallback font for characters the original font cannot show
//   --append           write the changes as an incremental update
//   --blocks           only print the text blocks of each page, as the editor sees them
#include <iostream>
#include <regex>

#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/editing/PdfTextEditor.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[2]) == "--blocks") {
        PdfDocument pdf(std::make_unique<PdfReader>(argv[1]));
        PdfTextEditor editor(pdf);
        for (int i = 1; i <= pdf.getNumberOfPages(); ++i) {
            std::cout << "--- page " << i << "\n";
            for (const auto& block : editor.getTextBlocks(pdf.getPage(i))) std::cout << "[" << block << "]\n";
        }
        return 0;
    }
    if (argc < 5) {
        std::cerr << "usage: edit_text <in.pdf> <out.pdf> <search> <replacement> [--regex] [--preserve-layout]"
                     " [--font <file>] [--append]\n"
                     "       edit_text <in.pdf> --blocks\n";
        return 1;
    }
    bool regex = false, preserveLayout = false;
    std::string fontFile;
    StampingProperties props;
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--regex") regex = true;
        else if (arg == "--preserve-layout") preserveLayout = true;
        else if (arg == "--append") props.useAppendMode();
        else if (arg == "--font" && i + 1 < argc) fontFile = argv[++i];
        else {
            std::cerr << "unknown option: " << arg << "\n";
            return 1;
        }
    }
    try {
        PdfDocument pdf(std::make_unique<PdfReader>(argv[1]), std::make_unique<PdfWriter>(argv[2]), props);
        PdfTextEditor editor(pdf);
        editor.setPreserveLayout(preserveLayout);
        if (!fontFile.empty()) editor.setFallbackFont(PdfFontFactory::createFont(fontFile, io::PdfEncodings::IDENTITY_H));
        TextEditResult result = regex ? editor.replaceRegex(std::regex(argv[3]), argv[4]) : editor.replaceText(argv[3], argv[4]);
        pdf.close();
        std::cout << "Replaced " << result.replaced << " match(es)";
        if (result.usedFallbackFont) std::cout << ", " << result.usedFallbackFont << " using the fallback font";
        if (result.skipped) std::cout << ", skipped " << result.skipped << " (no font can show the replacement)";
        std::cout << "; wrote " << argv[2] << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
