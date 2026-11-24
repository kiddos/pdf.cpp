#include <cstdio>
#include <filesystem>
#include <fstream>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string Res(const std::string& rel) {
  return pdfcpp_test::ReferenceDir() + "/" + rel;
}

/// Runs pdftotext (poppler) on a file if available; returns empty string if
/// not.
static std::string Pdftotext(const std::string& path) {
  if (std::system("which pdftotext > /dev/null 2>&1") != 0)
    return "<no pdftotext>";
  std::string txt = path + ".txt";
  std::string cmd =
      "pdftotext -layout '" + path + "' '" + txt + "' 2>/dev/null";
  if (std::system(cmd.c_str()) != 0) return "<pdftotext failed>";
  std::ifstream in(txt);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

TEST(StandardFontMetrics) {
  auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
  EXPECT_EQ(font->GetWidth('A'), 667);
  EXPECT_EQ(font->GetWidth(' '), 278);
  EXPECT_NEAR(font->GetWidth("Hello", 12),
              (722 + 556 + 222 + 222 + 556) * 12 / 1000.0, 0.001);
  auto times = PdfFontFactory::CreateFont(io::standard_fonts::kTimesRoman);
  EXPECT_TRUE(times->GetFontProgram()->HasKernPairs());
}

TEST(CanvasTextWithStandardFonts) {
  std::string path = "canvas_standard_fonts.pdf";
  {
    PdfDocument doc(std::make_unique<PdfWriter>(path));
    PdfPage* page = doc.AddNewPage();
    PdfCanvas canvas(page);
    auto helv = PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
    auto times =
        PdfFontFactory::CreateFont(io::standard_fonts::kTimesRoman, "Cp1252");
    canvas.BeginText()
        .SetFontAndSize(helv, 24)
        .MoveText(72, 760)
        .ShowText("Hello pdfcpp!")
        .EndText();
    canvas.BeginText()
        .SetFontAndSize(times, 12)
        .MoveText(72, 730)
        .ShowText("Caf\xC3\xA9 \xE2\x82\xAC 100 \xE2\x80\x94 na\xC3\xAFve")
        .EndText();
    canvas.SetStrokeColor(color_constants::kRed)
        .SetLineWidth(2)
        .Rectangle(70, 700, 200, 20)
        .Stroke();
    canvas.SetFillColor(std::make_shared<DeviceCmyk>(0.f, 1.f, 1.f, 0.f))
        .Circle(300, 600, 40)
        .Fill();
    canvas.SaveState()
        .SetFillColorGray(0.5f)
        .RoundRectangle(72, 500, 150, 60, 10)
        .Fill()
        .RestoreState();
    doc.Close();
  }
  std::string text = Pdftotext(path);
  if (text[0] != '<') {
    EXPECT_TRUE(text.find("Hello pdfcpp!") != std::string::npos);
    EXPECT_TRUE(
        text.find("Caf\xC3\xA9 \xE2\x82\xAC 100 \xE2\x80\x94 na\xC3\xAFve") !=
        std::string::npos);
  }
}

TEST(CanvasTextWithTrueTypeFonts) {
  std::string ttf_path =
      Res("kernel/src/test/resources/com/itextpdf/kernel/fonts/"
          "NotoSans-Regular.ttf");
  if (!fs::exists(ttf_path)) return;
  std::string path = "canvas_truetype.pdf";
  std::string sample =
      "\xCE\x9A\xCE\xB1\xCE\xBB\xCE\xB7\xCE\xBC\xCE\xAD\xCF\x81\xCE\xB1 "
      "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
  {
    PdfDocument doc(std::make_unique<PdfWriter>(path));
    PdfCanvas canvas(doc.AddNewPage());
    auto identity =
        PdfFontFactory::CreateFont(ttf_path, io::PdfEncodings::kIdentityH);
    auto simple =
        PdfFontFactory::CreateFont(ttf_path, io::PdfEncodings::kWinansi);
    canvas.BeginText()
        .SetFontAndSize(identity, 18)
        .MoveText(72, 760)
        .ShowText(sample)
        .EndText();
    canvas.BeginText()
        .SetFontAndSize(simple, 14)
        .MoveText(72, 730)
        .ShowText("Simple TrueType font")
        .EndText();
    doc.Close();
  }
  EXPECT_TRUE(fs::file_size(path) < 200000);  // subset, not the whole font
  std::string text = Pdftotext(path);
  if (text[0] != '<') {
    EXPECT_TRUE(text.find(sample) != std::string::npos);
    EXPECT_TRUE(text.find("Simple TrueType font") != std::string::npos);
  }
}

TEST(CanvasImages) {
  std::string jpg = Res("pdfa/src/test/resources/com/itextpdf/pdfa/Desert.jpg");
  std::string png = Res("pdfa/src/test/resources/com/itextpdf/pdfa/itext.png");
  if (!fs::exists(jpg) || !fs::exists(png)) return;
  std::string path = "canvas_images.pdf";
  {
    PdfDocument doc(std::make_unique<PdfWriter>(path));
    PdfCanvas canvas(doc.AddNewPage());
    auto jpg_data = io::ImageDataFactory::Create(jpg);
    EXPECT_TRUE(jpg_data->GetWidth() > 0);
    canvas.AddImageFittedIntoRectangle(jpg_data, Rectangle(36, 400, 300, 225));
    auto png_data = io::ImageDataFactory::Create(png);
    canvas.AddImageAt(png_data, 36, 100);
    doc.Close();
  }
  auto reader = std::make_unique<PdfReader>(path);
  PdfDocument doc(std::move(reader));
  auto xobjects =
      doc.GetPage(1)->GetResources()->GetResource(PdfName::kXObject);
  EXPECT_TRUE(xobjects && xobjects->size() == 2);
}

TEST(FormXObject) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfFormXObject form(Rectangle(0, 0, 100, 100));
    PdfCanvas fc(form, &doc);
    fc.Rectangle(10, 10, 80, 80).Fill();
    PdfCanvas canvas(doc.AddNewPage());
    canvas.AddXObjectAt(form, 100, 100);
    canvas.AddXObjectAt(form, 300, 300);
    doc.Close();
  }
  auto reader = PdfReader::FromBytes(baos.ToByteArray());
  PdfDocument doc(std::move(reader));
  std::string content = ToLatin1String(doc.GetPage(1)->GetContentBytes());
  EXPECT_TRUE(content.find("Do") != std::string::npos);
}

TEST_MAIN()
