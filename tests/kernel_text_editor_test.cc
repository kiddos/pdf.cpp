#include <filesystem>
#include <functional>
#include <regex>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/editing/pdf_text_editor.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string FontRes(const std::string& name) {
  return pdfcpp_test::ReferenceDir() +
         "/kernel/src/test/resources/com/itextpdf/kernel/fonts/" + name;
}

static std::string ParserRes(const std::string& rel) {
  return pdfcpp_test::ReferenceDir() +
         "/kernel/src/test/resources/com/itextpdf/kernel/parser/" + rel;
}

static std::string ReplaceAll(std::string s, const std::string& from,
                              const std::string& to) {
  for (size_t pos = s.find(from); pos != std::string::npos;
       pos = s.find(from, pos + to.size()))
    s.replace(pos, from.size(), to);
  return s;
}

/// One page whose content stream is `content`; `%F` in it stands for the
/// resource name of `font` (Helvetica if null).
static Bytes MakePdf(const std::string& content,
                     std::shared_ptr<PdfFont> font = nullptr) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfPage* page = doc.AddNewPage();
    if (!font)
      font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
    PdfNamePtr name = page->GetResources()->AddFont(&doc, font.get());
    // let the font know which glyphs are used so a subset gets them
    if (content.find("%T") != std::string::npos) {
      auto start = content.find("%T"), end = content.find("%T", start + 2);
      std::string text = content.substr(start + 2, end - start - 2);
      Bytes encoded = font->ConvertToBytes(text);
      std::string hex = "<";
      static const char* digits = "0123456789ABCDEF";
      for (uint8_t b : encoded) {
        hex += digits[b >> 4];
        hex += digits[b & 15];
      }
      hex += ">";
      std::string c = content.substr(0, start) + hex + content.substr(end + 2);
      page->GetFirstContentStream()->SetData(
          ToBytes(ReplaceAll(c, "%F", "/" + name->GetValue())));
    } else {
      page->GetFirstContentStream()->SetData(
          ToBytes(ReplaceAll(content, "%F", "/" + name->GetValue())));
    }
    doc.Close();
  }
  return baos.ToByteArray();
}

static Bytes EditPdf(const Bytes& in,
                     const std::function<void(PdfDocument&)>& fn,
                     bool append = false) {
  io::ByteArrayOutputStream baos;
  {
    StampingProperties props;
    if (append) props.UseAppendMode();
    PdfDocument doc(PdfReader::FromBytes(in), std::make_unique<PdfWriter>(baos),
                    props);
    fn(doc);
    doc.Close();
  }
  return baos.ToByteArray();
}

static std::string PageText(const Bytes& pdf, int page = 1) {
  PdfDocument doc(PdfReader::FromBytes(pdf));
  return PdfTextExtractor::GetTextFromPage(doc.GetPage(page));
}

static std::string ContentOf(const Bytes& pdf) {
  PdfDocument doc(PdfReader::FromBytes(pdf));
  Bytes b = doc.GetPage(1)->GetContentBytes();
  return std::string(b.begin(), b.end());
}

/// Start x (user space) of every shown string, in content order.
struct StartCollector : IEventListener {
  std::vector<std::pair<std::string, float>> starts;
  void EventOccurred(const IEventData& data, EventType type) override {
    if (type != EventType::kRenderText) return;
    const auto& info = static_cast<const TextRenderInfo&>(data);
    starts.emplace_back(info.GetText(),
                        info.GetBaseline().GetStartPoint().Get(0));
  }
  std::set<EventType> GetSupportedEvents() const override {
    return {EventType::kRenderText};
  }
};

static std::vector<std::pair<std::string, float>> Starts(const Bytes& pdf) {
  PdfDocument doc(PdfReader::FromBytes(pdf));
  StartCollector collector;
  PdfCanvasProcessor processor(collector);
  processor.ProcessPageContent(doc.GetPage(1));
  return collector.starts;
}

TEST(ReplaceInSingleTj) {
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td (Hello World) Tj ET");
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("World", "There");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(r.skipped, 0);
  EXPECT_EQ(r.used_fallback_font, 0);
  EXPECT_EQ(PageText(out), std::string("Hello There"));
}

TEST(ReplaceKeepsUntouchedContentBytes) {
  std::string content =
      "q 1 0 0 1 0 0 cm\n% a comment\nBT %F 12 Tf 72 700 Td (keep) Tj ET\nBT "
      "%F 12 Tf 72 680 Td (change me) Tj ET\nQ";
  Bytes pdf = MakePdf(content);
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).ReplaceText("change", "edited");
  });
  std::string c = ContentOf(out);
  EXPECT_TRUE(c.find("% a comment\nBT ") != std::string::npos);
  EXPECT_TRUE(c.find("(keep) Tj ET") != std::string::npos);
  EXPECT_TRUE(c.find("(edited me) Tj") != std::string::npos);
  EXPECT_EQ(PageText(out), std::string("keep\nedited me"));
}

TEST(ReplaceAcrossTjKerning) {
  Bytes pdf =
      MakePdf("BT %F 12 Tf 72 700 Td [(Hel) 20 (lo W) -15 (orld)] TJ ET");
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("lo World", "p me");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(PageText(out), std::string("Help me"));
}

TEST(VirtualSeparatorsMatchSeparateOperators) {
  Bytes pdf = MakePdf(
      "BT %F 12 Tf 72 700 Td (Hello) Tj 40 0 Td (World) Tj 0 -14 Td (Next) Tj "
      "ET");
  {
    PdfDocument doc(PdfReader::FromBytes(pdf));
    auto blocks = PdfTextEditor(doc).GetTextBlocks(doc.GetPage(1));
    EXPECT_EQ(blocks.size(), size_t(1));
    EXPECT_EQ(blocks[0], std::string("Hello World\nNext"));
  }
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("Hello World", "Goodbye");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(PageText(out), std::string("Goodbye\nNext"));
}

TEST(TjWordGapIsAVirtualSpace) {
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td [(Hello) -300 (World)] TJ ET");
  PdfDocument doc(PdfReader::FromBytes(pdf));
  auto blocks = PdfTextEditor(doc).GetTextBlocks(doc.GetPage(1));
  EXPECT_EQ(blocks.size(), size_t(1));
  EXPECT_EQ(blocks[0], std::string("Hello World"));
}

TEST(QuoteOperatorsAreRewrittenWithTheirLineMove) {
  Bytes pdf = MakePdf(
      "BT %F 12 Tf 14 TL 72 700 Td (first) Tj (second line) ' 1 0.5 (third "
      "line) \" ET");
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).ReplaceText("line", "row");
  });
  EXPECT_EQ(PageText(out), std::string("first\nsecond row\nthird row"));
  std::string c = ContentOf(out);
  EXPECT_TRUE(c.find("T*\n(second row) Tj") != std::string::npos);
  EXPECT_TRUE(c.find("1 Tw 0.5 Tc T*\n(third row) Tj") != std::string::npos);
}

TEST(RegexReplacement) {
  Bytes pdf =
      MakePdf("BT %F 12 Tf 72 700 Td (Due 2023-07, paid 2024-01) Tj ET");
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceRegex(std::regex("(\\d{4})-(\\d{2})"),
                                        "$2/$1");
  });
  EXPECT_EQ(r.replaced, 2);
  EXPECT_EQ(PageText(out), std::string("Due 07/2023, paid 01/2024"));
}

TEST(DeleteText) {
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td (Secret: 1234 end) Tj ET");
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).ReplaceText("1234 ", "");
  });
  EXPECT_EQ(PageText(out), std::string("Secret: end"));
}

TEST(PreserveLayoutKeepsFollowingTextInPlace) {
  Bytes pdf =
      MakePdf("BT %F 12 Tf 72 700 Td [(Total: 5 items) -2000 (end)] TJ ET");
  auto before = Starts(pdf);
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).SetPreserveLayout(true).ReplaceText("5", "12345");
  });
  auto after = Starts(out);
  EXPECT_EQ(PageText(out), std::string("Total: 12345 items end"));
  EXPECT_TRUE(!before.empty() && !after.empty());
  // "end" starts at the same place; so does " items"
  EXPECT_EQ(before.back().first, std::string("end"));
  EXPECT_EQ(after.back().first, std::string("end"));
  EXPECT_TRUE(std::fabs(before.back().second - after.back().second) < 0.01f);

  // without preserve_layout the following text moves by the width difference
  Bytes moved = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).ReplaceText("5", "12345");
  });
  auto moved_starts = Starts(moved);
  EXPECT_TRUE(moved_starts.back().second > before.back().second + 20);
}

TEST(NothingMatchedLeavesDocumentUnchanged) {
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td (Hello) Tj ET");
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("absent", "x");
  });
  EXPECT_EQ(r.replaced, 0);
  EXPECT_EQ(ContentOf(out), ContentOf(pdf));
}

TEST(AppendModeKeepsOriginalBytes) {
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td (Version 1) Tj ET");
  Bytes out = EditPdf(
      pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).ReplaceText("1", "2"); },
      true);
  EXPECT_TRUE(out.size() > pdf.size());
  EXPECT_TRUE(std::equal(pdf.begin(), pdf.end(), out.begin()));
  EXPECT_EQ(PageText(out), std::string("Version 2"));
}

TEST(FallbackFontForUnencodableCharacters) {
  // WinAnsi Helvetica cannot show Cyrillic; the fallback (a TrueType font) can
  std::string ttf = FontRes("PTSerif-Regular.ttf");
  if (!fs::exists(ttf)) return;
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td (angle alpha here) Tj ET");
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    auto fallback =
        PdfFontFactory::CreateFont(ttf, io::PdfEncodings::kIdentityH);
    r = PdfTextEditor(doc).SetFallbackFont(fallback).ReplaceText("alpha",
                                                                 "\xD0\xB4=1");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(r.used_fallback_font, 1);
  EXPECT_EQ(PageText(out), std::string("angle \xD0\xB4=1 here"));
  std::string c = ContentOf(out);
  // the original font is restored after the fallback run
  EXPECT_TRUE(c.find("Tf\n(=1 here) Tj") != std::string::npos ||
              c.find("Tf (=1 here) Tj") != std::string::npos ||
              c.find("Tf\n[(=1 here)] TJ") != std::string::npos);

  // without a usable font the match is skipped (the default Helvetica fallback
  // has no Greek)
  Bytes skipped = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("alpha", "\xCE\xB1");
  });
  EXPECT_EQ(r.replaced, 0);
  EXPECT_EQ(r.skipped, 1);
  EXPECT_EQ(PageText(skipped), std::string("angle alpha here"));
}

TEST(SubsetFontOnlyReusesGlyphsItContains) {
  std::string ttf = FontRes("PTSerif-Regular.ttf");
  if (!fs::exists(ttf)) return;
  auto font = PdfFontFactory::CreateFont(ttf, io::PdfEncodings::kIdentityH);
  Bytes pdf = MakePdf("BT %F 12 Tf 72 700 Td %Tcab here%T Tj ET", font);
  EXPECT_EQ(PageText(pdf), std::string("cab here"));

  // "bac" only needs glyphs already in the subset
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("cab", "bac");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(r.used_fallback_font, 0);
  EXPECT_EQ(PageText(out), std::string("bac here"));

  // "z" is not in the subset: it comes from the fallback font
  Bytes with_z = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("cab", "zab");
  });
  EXPECT_EQ(r.replaced, 1);
  EXPECT_EQ(r.used_fallback_font, 1);
  EXPECT_EQ(PageText(with_z), std::string("zab here"));
  PdfDocument check(PdfReader::FromBytes(with_z));
  auto fonts = check.GetPage(1)->GetResources()->GetResource(PdfName::kFont);
  EXPECT_EQ(fonts->size(), 2);
}

TEST(EditsTextInFormXObjects) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfFormXObject form(Rectangle(0, 0, 200, 50));
    PdfCanvas form_canvas(form, &doc);
    auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
    form_canvas.BeginText()
        .SetFontAndSize(font, 12)
        .MoveText(5, 20)
        .ShowText("Draft copy")
        .EndText();
    for (int i = 0; i < 2; ++i) {
      PdfCanvas canvas(doc.AddNewPage());
      canvas.AddXObjectAt(form, 72, 700);
    }
    doc.Close();
  }
  Bytes pdf = baos.ToByteArray();
  TextEditResult r;
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).ReplaceText("Draft", "Final");
  });
  EXPECT_EQ(r.replaced, 1);  // the shared form is edited once
  EXPECT_EQ(PageText(out, 1), std::string("Final copy"));
  EXPECT_EQ(PageText(out, 2), std::string("Final copy"));

  Bytes skipped = EditPdf(pdf, [&](PdfDocument& doc) {
    r = PdfTextEditor(doc).SetProcessFormXObjects(false).ReplaceText("Draft",
                                                                     "Final");
  });
  EXPECT_EQ(r.replaced, 0);
  EXPECT_EQ(PageText(skipped, 1), std::string("Draft copy"));
}

TEST(MultipleContentStreamsAreMerged) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfPage* page = doc.AddNewPage();
    auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
    PdfNamePtr name = page->GetResources()->AddFont(&doc, font.get());
    // streams split at a token boundary, with the BT block spanning both
    page->GetFirstContentStream()->SetData(
        ToBytes("BT /" + name->GetValue() + " 12 Tf 72 700 Td (first part)"));
    page->NewContentStreamAfter()->SetData(ToBytes("Tj ET"));
    doc.Close();
  }
  Bytes pdf = baos.ToByteArray();
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).ReplaceText("first part", "whole");
  });
  EXPECT_EQ(PageText(out), std::string("whole"));
}

TEST(CustomFinder) {
  Bytes pdf = MakePdf(
      "BT %F 12 Tf 72 700 Td (abc) Tj ET BT %F 12 Tf 72 680 Td (def) Tj ET");
  Bytes out = EditPdf(pdf, [&](PdfDocument& doc) {
    PdfTextEditor(doc).Edit([](const std::string& text) {
      // upper-case the first letter of every block
      return std::vector<PdfTextEditor::Replacement>{
          {0, 1, std::string(1, static_cast<char>(text[0] - 32))}};
    });
  });
  EXPECT_EQ(PageText(out), std::string("Abc\nDef"));
}

// Edits real-world files from the iText parser tests: every match must
// disappear from the extracted text.
TEST(EditReferenceParserPdfs) {
  const char* files[] = {
      "PdfTextExtractorTest/test.pdf",
      "SimpleFontToUnicodeExtractionTest/simpleFontToUnicode01.pdf",
      "PdfTextExtractorUnicodeIdentityTest/user10.pdf"};
  for (const char* rel : files) {
    std::string path = ParserRes(rel);
    if (!fs::exists(path)) continue;
    Bytes in = io::StreamUtil::ReadFile(path);
    std::string before;
    {
      PdfDocument doc(PdfReader::FromBytes(in));
      before = PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
    }
    // pick the first word of 4+ letters
    std::smatch m;
    std::string word;
    if (std::regex_search(before, m, std::regex("[A-Za-z]{4,}")))
      word = m.str();
    if (word.empty()) continue;
    TextEditResult r;
    Bytes out = EditPdf(in, [&](PdfDocument& doc) {
      r = PdfTextEditor(doc).ReplaceText(doc.GetPage(1), word, "ZZ");
    });
    std::string after = PageText(out);
    EXPECT_TRUE(r.replaced > 0);
    EXPECT_TRUE(after.find(word) == std::string::npos || r.skipped > 0);
    EXPECT_TRUE(after.find("ZZ") != std::string::npos);
  }
}

TEST_MAIN()
