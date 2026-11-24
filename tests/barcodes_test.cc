#include "pdfcpp/barcodes/barcodes.h"

#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::barcodes;
using namespace pdfcpp::kernel;

TEST(EanParity) {
  EXPECT_EQ(BarcodeEAN::CalculateEanParity("400638133393"),
            1);                                             // 4006381333931
  EXPECT_EQ(BarcodeEAN::CalculateEanParity("9638507"), 4);  // EAN-8 96385074
  EXPECT_EQ(static_cast<int>(BarcodeEAN::GetBarsEan13("4006381333931").size()),
            59);
}

TEST(Code128Raw) {
  // "123456" packs into code set C: kStartC, 12, 34, 56
  auto raw = Barcode128::GetRawText("123456", false);
  EXPECT_EQ(static_cast<int>(raw.size()), 4);
  EXPECT_EQ(raw[0], Barcode128::kStartC);
  EXPECT_EQ(raw[1], 12);
  auto mixed = Barcode128::GetRawText("AB12345", false);
  EXPECT_EQ(mixed[0], Barcode128::kStartB);
  auto bars = Barcode128::GetBarsCode128Raw(raw);
  int modules = 0;
  for (auto b : bars) modules += b;
  EXPECT_EQ(modules, static_cast<int>(raw.size() + 1) * 11 + 13);
}

TEST(QrMatchesIText) {
  // Matrix produced by iText 9 (com.itextpdf.barcodes.qrcode.Encoder) for
  // "HELLO WORLD", level M.
  BarcodeQRCode qr("HELLO WORLD", ErrorCorrectionLevel::kM);
  EXPECT_EQ(qr.GetVersion(), 1);
  const auto& m = qr.GetMatrix();
  EXPECT_EQ(static_cast<int>(m.size()), 21 + 8);
  static const char* expected[] = {
      "#######...#.#.#######", "#.....#.###...#.....#", "#.###.#...#.#.#.###.#",
      "#.###.#...#.#.#.###.#", "#.###.#.#.###.#.###.#", "#.....#..###..#.....#",
      "#######.#.#.#.#######", ".....................", "#.#.#.#..#..#...#..#.",
      ".####...#..#....#...#", "...#######.#..#.##...", "####.#.##..###.#.###.",
      ".#..####.#.#..###.#.#", "........#.#...#...#.#", "#######.....#..#.##..",
      "#.....#..##...##.#...", "#.###.#.##..#.#######", "#.###.#...##.#.#...#.",
      "#.###.#.####.###.#..#", "#.....#....###...#.##", "#######.##.#.###....#",
  };
  for (int y = 0; y < 21; ++y)
    for (int x = 0; x < 21; ++x)
      EXPECT_EQ(m[y + 4][x + 4], expected[y][x] == '#');
}

TEST(BarcodesInPdf) {
  std::string path = "barcodes.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    PdfPage* page = pdf.AddNewPage();
    PdfCanvas canvas(page);
    Barcode128 code128(&pdf);
    code128.SetCode("pdfcpp-128");
    auto x1 = code128.CreateFormXObject();
    canvas.AddXObjectAt(*x1, 72, 700);
    BarcodeEAN ean(&pdf);
    ean.SetCode("4006381333931");
    auto x2 = ean.CreateFormXObject();
    canvas.AddXObjectAt(*x2, 72, 600);
    Barcode39 code39(&pdf);
    code39.SetCode("PDFCPP 39");
    auto x3 = code39.CreateFormXObject();
    canvas.AddXObjectAt(*x3, 72, 500);
    BarcodeQRCode qr("https://github.com/itext/itext-java");
    auto x4 = qr.CreateFormXObject(&pdf, color_constants::kBlack, 3);
    canvas.AddXObjectAt(*x4, 300, 500);
    pdf.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  std::string text = PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
  EXPECT_TRUE(text.find("pdfcpp-128") != std::string::npos);
  EXPECT_TRUE(text.find("*PDFCPP 39*") != std::string::npos);
}

// BUGS.md #29: codes of the wrong length or with non-digits are rejected
// instead of read out of bounds.
TEST(EanRejectsInvalidCodes) {
  EXPECT_THROW(BarcodeEAN::GetBarsEan13("123"));
  EXPECT_THROW(BarcodeEAN::GetBarsEan13("40063813339A1"));
  EXPECT_THROW(BarcodeEAN::GetBarsEan8("1234"));
  io::ByteArrayOutputStream baos;
  PdfWriter writer(baos);
  PdfDocument pdf(writer);
  BarcodeEAN upc(&pdf);
  upc.SetCodeType(BarcodeEAN::kUpca);
  upc.SetCode("12");
  EXPECT_THROW(upc.GetBarcodeSize());
  upc.SetCode("036000291452");
  EXPECT_TRUE(upc.GetBarcodeSize().GetWidth() > 0);
  pdf.Close();
}

TEST_MAIN()
