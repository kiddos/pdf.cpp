#include <filesystem>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string ParserRes(const std::string& rel) {
  return pdfcpp_test::ReferenceDir() +
         "/kernel/src/test/resources/com/itextpdf/kernel/parser/" + rel;
}

// Port of SimpleFontToUnicodeExtractionTest.test01
TEST(SimpleFontToUnicode01) {
  std::string path =
      ParserRes("SimpleFontToUnicodeExtractionTest/simpleFontToUnicode01.pdf");
  if (!fs::exists(path)) return;
  PdfDocument pdf_document(std::make_unique<PdfReader>(path));
  std::string expected =
      "Information plays a central role in soci-\n"
      "ety today, and it is becoming more and \n"
      "more common for that information to \n"
      "be offered in digital form alone. The re-\n"
      "liable, user-friendly Portable Document \n"
      "Format (PDF) has become the world\xE2\x80\x99s \n"
      "file type of choice for providing infor-\n"
      "mation as a digital document. \n"
      "Tags can be added to a PDF in order \n"
      "to structure the content of a document. \n"
      "These tags are a critical requirement if \n"
      "any form of assistive technology (such \n"
      "as screen readers, specialist mice, and \n"
      "speech recognition and text-to-speech \n"
      "software) is to gain access to this con-\n"
      "tent. To date, PDF documents have rare-\n"
      "ly been tagged, and not all software can \n"
      "make use of PDF tags. In practical terms, \n"
      "this particularly reduces information\xE2\x80\x98s \n"
      "accessibility for people with disabilities \n"
      "who rely on assistive technology.";
  LocationTextExtractionStrategy strategy;
  EXPECT_EQ(
      PdfTextExtractor::GetTextFromPage(pdf_document.GetPage(1), strategy),
      expected);
}

// Port of SimpleFontToUnicodeExtractionTest.test02
TEST(SimpleFontToUnicode02) {
  std::string path =
      ParserRes("SimpleFontToUnicodeExtractionTest/simpleFontToUnicode02.pdf");
  if (!fs::exists(path)) return;
  PdfDocument pdf_document(std::make_unique<PdfReader>(path));
  EXPECT_EQ(PdfTextExtractor::GetTextFromPage(pdf_document.GetPage(1)),
            std::string("ffaast"));
}

// Port of PdfTextExtractorUnicodeIdentityTest
TEST(UnicodeIdentity) {
  std::string path =
      ParserRes("PdfTextExtractorUnicodeIdentityTest/user10.pdf");
  if (!fs::exists(path)) return;
  PdfDocument pdf_document(std::make_unique<PdfReader>(path));
  std::string txt = PdfTextExtractor::GetTextFromPage(pdf_document.GetPage(1));
  EXPECT_TRUE(txt.find("Pname Dname Email Address") != std::string::npos);
}

TEST(RoundTripExtraction) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfCanvas canvas(doc.AddNewPage());
    auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
    canvas.BeginText()
        .SetFontAndSize(font, 12)
        .MoveText(72, 700)
        .ShowText("First line of text")
        .EndText();
    canvas.BeginText()
        .SetFontAndSize(font, 12)
        .MoveText(72, 680)
        .ShowText("Second")
        .EndText();
    canvas.BeginText()
        .SetFontAndSize(font, 12)
        .MoveText(200, 680)
        .ShowText("column")
        .EndText();
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  EXPECT_EQ(PdfTextExtractor::GetTextFromPage(doc.GetPage(1)),
            std::string("First line of text\nSecond column"));
  SimpleTextExtractionStrategy simple;
  EXPECT_EQ(PdfTextExtractor::GetTextFromPage(doc.GetPage(1), simple),
            std::string("First line of text\nSecond column"));
}

// Ports of PdfTextExtractorTest assertions
static std::string Extract(const std::string& file) {
  PdfDocument doc(
      std::make_unique<PdfReader>(ParserRes("PdfTextExtractorTest/" + file)));
  return PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
}

TEST(PdfTextExtractorJavaCases) {
  if (!fs::exists(ParserRes("PdfTextExtractorTest"))) return;
  EXPECT_EQ(Extract("noSpecifiedDictionaryInProperties.pdf"), std::string(""));
  EXPECT_EQ(Extract("noPropertiesInResources.pdf"), std::string(""));
  EXPECT_EQ(Extract("type3NoCMap.pdf"), std::string("*0*"));
  EXPECT_EQ(Extract("noBaseEncoding.pdf"), std::string("HELLO WORLD"));
  EXPECT_EQ(Extract("simpleFontWithoutEncodingToUnicode.pdf"),
            std::string("MyriadPro-Bold font."));
  EXPECT_EQ(Extract("simpleFontWithPartialToUnicode.pdf"),
            std::string("Registered"));
  EXPECT_EQ(Extract("type0FontToUnicode.pdf"), std::string("\xE2\x82\xAC 390"));
  EXPECT_EQ(
      Extract("diacriticShiftedLessThanTwo.pdf"),
      std::string("\xE0\xA4\xB8\xE0\xA4\x82\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\x95"
                  "\xE0\xA5\x83\xE0\xA4\xA4 \xE0\xA4\xAE\xE0\xA5\x8D"));
  EXPECT_EQ(
      Extract("diacriticShiftedMoreThanTwo.pdf"),
      std::string(
          "\xE0\xA5\x83\n\xE0\xA4\xB8\xE0\xA4\x82\xE0\xA4\xB8\xE0\xA5\x8D\xE0"
          "\xA4\x95\xE0\xA5\x83\xE0\xA4\xA4 \xE0\xA4\xAE\xE0\xA5\x8D"));
  EXPECT_EQ(Extract("shortOctalDataAsText.pdf"), std::string("EC"));
  std::string cyr = Extract("notDefaultCodespacesCyrillic.pdf");
  EXPECT_TRUE(cyr.find("\xD0\xB1\xD1\x80\xD0\xBE\xD0\xBD\xD0\xB8\xD1\x80\xD0"
                       "\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5") !=
              std::string::npos);
  EXPECT_TRUE(cyr.find("From") != std::string::npos);
  std::string mixed = Extract("SameCidForDifferentCodes.pdf");
  EXPECT_TRUE(mixed.find("18\xE4\xB8\xAA\xE6\x9C\x88") != std::string::npos);
}

TEST(ExtractAllReferenceParserPdfs) {
  fs::path dir = ParserRes("PdfTextExtractorTest");
  if (!fs::exists(dir)) return;
  int ok = 0, total = 0;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".pdf") continue;
    ++total;
    try {
      PdfDocument doc(std::make_unique<PdfReader>(entry.path().string()));
      for (int i = 1; i <= doc.GetNumberOfPages(); ++i)
        PdfTextExtractor::GetTextFromPage(doc.GetPage(i));
      ++ok;
    } catch (const std::exception& e) {
      std::cout << "    " << entry.path().filename() << ": " << e.what()
                << "\n";
    }
  }
  std::cout << "    extracted " << ok << "/" << total << "\n";
  EXPECT_EQ(ok, total);
}

TEST_MAIN()
