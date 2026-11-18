#include <cstdio>
#include <filesystem>
#include <fstream>

#include "TestHarness.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string res(const std::string& rel) { return pdfcpp_test::referenceDir() + "/" + rel; }

/// Runs pdftotext (poppler) on a file if available; returns empty string if not.
static std::string pdftotext(const std::string& path) {
    if (std::system("which pdftotext > /dev/null 2>&1") != 0) return "<no pdftotext>";
    std::string txt = path + ".txt";
    std::string cmd = "pdftotext -layout '" + path + "' '" + txt + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return "<pdftotext failed>";
    std::ifstream in(txt);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

TEST(standardFontMetrics) {
    auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
    EXPECT_EQ(font->getWidth('A'), 667);
    EXPECT_EQ(font->getWidth(' '), 278);
    EXPECT_NEAR(font->getWidth("Hello", 12), (722 + 556 + 222 + 222 + 556) * 12 / 1000.0, 0.001);
    auto times = PdfFontFactory::createFont(io::StandardFonts::TIMES_ROMAN);
    EXPECT_TRUE(times->getFontProgram()->hasKernPairs());
}

TEST(canvasTextWithStandardFonts) {
    std::string path = "canvas_standard_fonts.pdf";
    {
        PdfDocument doc(std::make_unique<PdfWriter>(path));
        PdfPage* page = doc.addNewPage();
        PdfCanvas canvas(page);
        auto helv = PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
        auto times = PdfFontFactory::createFont(io::StandardFonts::TIMES_ROMAN, "Cp1252");
        canvas.beginText().setFontAndSize(helv, 24).moveText(72, 760).showText("Hello pdfcpp!").endText();
        canvas.beginText().setFontAndSize(times, 12).moveText(72, 730).showText("Caf\xC3\xA9 \xE2\x82\xAC 100 \xE2\x80\x94 na\xC3\xAFve").endText();
        canvas.setStrokeColor(ColorConstants::RED).setLineWidth(2).rectangle(70, 700, 200, 20).stroke();
        canvas.setFillColor(std::make_shared<DeviceCmyk>(0.f, 1.f, 1.f, 0.f)).circle(300, 600, 40).fill();
        canvas.saveState().setFillColorGray(0.5f).roundRectangle(72, 500, 150, 60, 10).fill().restoreState();
        doc.close();
    }
    std::string text = pdftotext(path);
    if (text[0] != '<') {
        EXPECT_TRUE(text.find("Hello pdfcpp!") != std::string::npos);
        EXPECT_TRUE(text.find("Caf\xC3\xA9 \xE2\x82\xAC 100 \xE2\x80\x94 na\xC3\xAFve") != std::string::npos);
    }
}

TEST(canvasTextWithTrueTypeFonts) {
    std::string ttfPath = res("kernel/src/test/resources/com/itextpdf/kernel/fonts/NotoSans-Regular.ttf");
    if (!fs::exists(ttfPath)) return;
    std::string path = "canvas_truetype.pdf";
    std::string sample = "\xCE\x9A\xCE\xB1\xCE\xBB\xCE\xB7\xCE\xBC\xCE\xAD\xCF\x81\xCE\xB1 \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    {
        PdfDocument doc(std::make_unique<PdfWriter>(path));
        PdfCanvas canvas(doc.addNewPage());
        auto identity = PdfFontFactory::createFont(ttfPath, io::PdfEncodings::IDENTITY_H);
        auto simple = PdfFontFactory::createFont(ttfPath, io::PdfEncodings::WINANSI);
        canvas.beginText().setFontAndSize(identity, 18).moveText(72, 760).showText(sample).endText();
        canvas.beginText().setFontAndSize(simple, 14).moveText(72, 730).showText("Simple TrueType font").endText();
        doc.close();
    }
    EXPECT_TRUE(fs::file_size(path) < 200000);  // subset, not the whole font
    std::string text = pdftotext(path);
    if (text[0] != '<') {
        EXPECT_TRUE(text.find(sample) != std::string::npos);
        EXPECT_TRUE(text.find("Simple TrueType font") != std::string::npos);
    }
}

TEST(canvasImages) {
    std::string jpg = res("pdfa/src/test/resources/com/itextpdf/pdfa/Desert.jpg");
    std::string png = res("pdfa/src/test/resources/com/itextpdf/pdfa/itext.png");
    if (!fs::exists(jpg) || !fs::exists(png)) return;
    std::string path = "canvas_images.pdf";
    {
        PdfDocument doc(std::make_unique<PdfWriter>(path));
        PdfCanvas canvas(doc.addNewPage());
        auto jpgData = io::ImageDataFactory::create(jpg);
        EXPECT_TRUE(jpgData->getWidth() > 0);
        canvas.addImageFittedIntoRectangle(jpgData, Rectangle(36, 400, 300, 225));
        auto pngData = io::ImageDataFactory::create(png);
        canvas.addImageAt(pngData, 36, 100);
        doc.close();
    }
    auto reader = std::make_unique<PdfReader>(path);
    PdfDocument doc(std::move(reader));
    auto xobjects = doc.getPage(1)->getResources()->getResource(PdfName::XObject);
    EXPECT_TRUE(xobjects && xobjects->size() == 2);
}

TEST(formXObject) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfFormXObject form(Rectangle(0, 0, 100, 100));
        PdfCanvas fc(form, &doc);
        fc.rectangle(10, 10, 80, 80).fill();
        PdfCanvas canvas(doc.addNewPage());
        canvas.addXObjectAt(form, 100, 100);
        canvas.addXObjectAt(form, 300, 300);
        doc.close();
    }
    auto reader = PdfReader::fromBytes(baos.toByteArray());
    PdfDocument doc(std::move(reader));
    std::string content = toLatin1String(doc.getPage(1)->getContentBytes());
    EXPECT_TRUE(content.find("Do") != std::string::npos);
}

TEST_MAIN()
