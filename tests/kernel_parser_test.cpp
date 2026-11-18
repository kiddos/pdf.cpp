#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string parserRes(const std::string& rel) {
    return pdfcpp_test::referenceDir() + "/kernel/src/test/resources/com/itextpdf/kernel/parser/" + rel;
}

// Port of SimpleFontToUnicodeExtractionTest.test01
TEST(simpleFontToUnicode01) {
    std::string path = parserRes("SimpleFontToUnicodeExtractionTest/simpleFontToUnicode01.pdf");
    if (!fs::exists(path)) return;
    PdfDocument pdfDocument(std::make_unique<PdfReader>(path));
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
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(pdfDocument.getPage(1), strategy), expected);
}

// Port of SimpleFontToUnicodeExtractionTest.test02
TEST(simpleFontToUnicode02) {
    std::string path = parserRes("SimpleFontToUnicodeExtractionTest/simpleFontToUnicode02.pdf");
    if (!fs::exists(path)) return;
    PdfDocument pdfDocument(std::make_unique<PdfReader>(path));
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(pdfDocument.getPage(1)), std::string("ffaast"));
}

// Port of PdfTextExtractorUnicodeIdentityTest
TEST(unicodeIdentity) {
    std::string path = parserRes("PdfTextExtractorUnicodeIdentityTest/user10.pdf");
    if (!fs::exists(path)) return;
    PdfDocument pdfDocument(std::make_unique<PdfReader>(path));
    std::string txt = PdfTextExtractor::getTextFromPage(pdfDocument.getPage(1));
    EXPECT_TRUE(txt.find("Pname Dname Email Address") != std::string::npos);
}

TEST(roundTripExtraction) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfCanvas canvas(doc.addNewPage());
        auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
        canvas.beginText().setFontAndSize(font, 12).moveText(72, 700).showText("First line of text").endText();
        canvas.beginText().setFontAndSize(font, 12).moveText(72, 680).showText("Second").endText();
        canvas.beginText().setFontAndSize(font, 12).moveText(200, 680).showText("column").endText();
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(doc.getPage(1)), std::string("First line of text\nSecond column"));
    SimpleTextExtractionStrategy simple;
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(doc.getPage(1), simple), std::string("First line of text\nSecond column"));
}

TEST(extractAllReferenceParserPdfs) {
    fs::path dir = parserRes("PdfTextExtractorTest");
    if (!fs::exists(dir)) return;
    int ok = 0, total = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".pdf") continue;
        ++total;
        try {
            PdfDocument doc(std::make_unique<PdfReader>(entry.path().string()));
            for (int i = 1; i <= doc.getNumberOfPages(); ++i) PdfTextExtractor::getTextFromPage(doc.getPage(i));
            ++ok;
        } catch (const std::exception& e) {
            std::cout << "    " << entry.path().filename() << ": " << e.what() << "\n";
        }
    }
    std::cout << "    extracted " << ok << "/" << total << "\n";
    EXPECT_EQ(ok, total);
}

TEST_MAIN()
