// Font handling: the 14 standard fonts, an embedded TrueType font with a
// single-byte encoding, and an Identity-H (Type0) font for full Unicode text.
// The embedded fonts are subset to the glyphs actually used.
//
// usage: fonts [out.pdf] [font.ttf]   (default font: DejaVu Sans)
#include <fstream>
#include <iostream>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "fonts.pdf";
  std::string ttf_path =
      argc > 2 ? argv[2] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";

  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  Document document(pdf);
  auto heading_font =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  auto heading = [&](const std::string& text) {
    document.Add(Make<Paragraph>(text)
                     ->SetFont(heading_font)
                     .SetFontSize(15)
                     .SetMarginTop(12));
  };

  // 1. The standard 14 fonts need no font file and are never embedded.
  heading("Standard Type 1 fonts");
  for (const char* name :
       {io::standard_fonts::kHelvetica, io::standard_fonts::kHelveticaOblique,
        io::standard_fonts::kTimesRoman, io::standard_fonts::kTimesBolditalic,
        io::standard_fonts::kCourier, io::standard_fonts::kCourierBold}) {
    document.Add(
        Make<Paragraph>(std::string(name) +
                        ": The quick brown fox jumps over the lazy dog")
            ->SetFont(kernel::PdfFontFactory::CreateFont(name)));
  }
  // Symbol and ZapfDingbats only contain their own glyphs, so the label is a
  // separate Helvetica run. With the "Symbol" encoding, Unicode Greek and
  // math characters map to Symbol's codes.
  auto symbol = kernel::PdfFontFactory::CreateFont(io::standard_fonts::kSymbol,
                                                   io::PdfEncodings::kSymbol);
  document.Add(Make<Paragraph>()
                   ->Add("Symbol: ")
                   .Add(Make<Text>("\xce\xb1\xce\xb2\xce\xb3 \xe2\x88\x91 "
                                   "\xe2\x88\x9e \xe2\x89\xa0 \xcf\x80")
                            ->SetFont(symbol)));
  // ZapfDingbats is font-specific: like in iText, characters are the font's
  // codes ("4" is a check mark, "(" an airplane, "n" a filled square).
  auto dingbats =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kZapfdingbats);
  document.Add(Make<Paragraph>()
                   ->Add("ZapfDingbats: ")
                   .Add(Make<Text>("4 ( + n u \xc2\xac \xc2\xad \xc2\xae")
                            ->SetFont(dingbats)));

  if (!std::ifstream(ttf_path)) {
    std::cerr << "Font " << ttf_path
              << " not found; skipping the TrueType part.\n";
  } else {
    // 2. TrueType with WinAnsi: a simple font (max 256 characters).
    heading("Embedded TrueType, WinAnsi encoding");
    auto winansi = kernel::PdfFontFactory::CreateFont(
        ttf_path, io::PdfEncodings::kWinansi,
        kernel::EmbeddingStrategy::kForceEmbedded);
    document.Add(
        Make<Paragraph>("Caf\xc3\xa9, na\xc3\xafve, \xc3\xa0 la carte, "
                        "\xe2\x82\xac 42 \xe2\x80\x94 Western European text.")
            ->SetFont(winansi)
            .SetFontSize(13));

    // 3. Identity-H: a Type0 font addressing glyphs directly, so any script
    //    the font covers can be mixed in one string. A ToUnicode CMap is
    //    written so the text stays searchable and copyable.
    heading("Embedded TrueType, Identity-H (Unicode)");
    auto unicode = kernel::PdfFontFactory::CreateFont(
        ttf_path, io::PdfEncodings::kIdentityH,
        kernel::EmbeddingStrategy::kForceEmbedded);
    for (const char* line :
         {"Greek: \xce\x9a\xce\xb1\xce\xbb\xce\xb7\xce\xbc\xce\xad\xcf\x81"
          "\xce\xb1 \xce\xba\xcf\x8c\xcf\x83\xce\xbc\xce\xb5",
          "Russian: \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82, "
          "\xd0\xbc\xd0\xb8\xd1\x80!",
          "Polish: Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4"
          "\x85 ja\xc5\xba\xc5\x84",
          "Math: \xe2\x88\x80x \xe2\x88\x88 \xe2\x84\x9d, x\xc2\xb2 "
          "\xe2\x89\xa5 0  \xe2\x88\xab f(x) dx \xe2\x89\x88 \xcf\x80",
          "Arrows & boxes: \xe2\x86\x90 \xe2\x86\x91 \xe2\x86\x92 "
          "\xe2\x86\x93 \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90"}) {
      document.Add(Make<Paragraph>(line)->SetFont(unicode).SetFontSize(13));
    }

    // Kerning comes from the font's kern table.
    heading("Kerning");
    for (bool kerning : {false, true}) {
      document.Add(Make<Paragraph>(std::string("AVATAR Tower WAVE ") +
                                   (kerning ? "(kerned)" : "(not kerned)"))
                       ->SetFont(unicode)
                       .SetFontSize(24)
                       .SetFontKerning(kerning));
    }
    std::cout << "Embedded " << ttf_path << " ("
              << (unicode->IsSubset() ? "subset" : "full") << ")\n";
  }

  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
