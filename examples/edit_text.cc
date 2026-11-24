// Finds and replaces text in an existing PDF.
//   edit_text <in.pdf> <out.pdf> <search> <replacement> [options]
// Options:
//   --regex            treat <search> as an ECMAScript regex; <replacement> may
//   use $1, $2, ...
//   --preserve-layout  keep text after each edit at its original position
//   --font <file>      TrueType/OpenType fallback font for characters the
//   original font cannot show
//   --append           write the changes as an incremental update
//   --blocks           only print the text blocks of each page, as the editor
//   sees them
#include <iostream>
#include <regex>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/editing/pdf_text_editor.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[2]) == "--blocks") {
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1]));
    PdfTextEditor editor(pdf);
    for (int i = 1; i <= pdf.GetNumberOfPages(); ++i) {
      std::cout << "--- page " << i << "\n";
      for (const auto& block : editor.GetTextBlocks(pdf.GetPage(i)))
        std::cout << "[" << block << "]\n";
    }
    return 0;
  }
  if (argc < 5) {
    std::cerr << "usage: edit_text <in.pdf> <out.pdf> <search> <replacement> "
                 "[--regex] [--preserve-layout]"
                 " [--font <file>] [--append]\n"
                 "       edit_text <in.pdf> --blocks\n";
    return 1;
  }
  bool regex = false, preserve_layout = false;
  std::string font_file;
  StampingProperties props;
  for (int i = 5; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--regex")
      regex = true;
    else if (arg == "--preserve-layout")
      preserve_layout = true;
    else if (arg == "--append")
      props.UseAppendMode();
    else if (arg == "--font" && i + 1 < argc)
      font_file = argv[++i];
    else {
      std::cerr << "unknown option: " << arg << "\n";
      return 1;
    }
  }
  try {
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1]),
                    std::make_unique<PdfWriter>(argv[2]), props);
    PdfTextEditor editor(pdf);
    editor.SetPreserveLayout(preserve_layout);
    if (!font_file.empty())
      editor.SetFallbackFont(
          PdfFontFactory::CreateFont(font_file, io::PdfEncodings::kIdentityH));
    TextEditResult result =
        regex ? editor.ReplaceRegex(std::regex(argv[3]), argv[4])
              : editor.ReplaceText(argv[3], argv[4]);
    pdf.Close();
    std::cout << "Replaced " << result.replaced << " match(es)";
    if (result.used_fallback_font)
      std::cout << ", " << result.used_fallback_font
                << " using the fallback font";
    if (result.skipped)
      std::cout << ", skipped " << result.skipped
                << " (no font can show the replacement)";
    std::cout << "; wrote " << argv[2] << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
