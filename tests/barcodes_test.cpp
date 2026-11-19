#include "TestHarness.h"
#include "pdfcpp/barcodes/Barcodes.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::barcodes;
using namespace pdfcpp::kernel;

TEST(eanParity) {
    EXPECT_EQ(BarcodeEAN::calculateEANParity("400638133393"), 1);  // 4006381333931
    EXPECT_EQ(BarcodeEAN::calculateEANParity("9638507"), 4);       // EAN-8 96385074
    EXPECT_EQ(static_cast<int>(BarcodeEAN::getBarsEAN13("4006381333931").size()), 59);
}

TEST(code128Raw) {
    // "123456" packs into code set C: START_C, 12, 34, 56
    auto raw = Barcode128::getRawText("123456", false);
    EXPECT_EQ(static_cast<int>(raw.size()), 4);
    EXPECT_EQ(raw[0], Barcode128::START_C);
    EXPECT_EQ(raw[1], 12);
    auto mixed = Barcode128::getRawText("AB12345", false);
    EXPECT_EQ(mixed[0], Barcode128::START_B);
    auto bars = Barcode128::getBarsCode128Raw(raw);
    int modules = 0;
    for (auto b : bars) modules += b;
    EXPECT_EQ(modules, static_cast<int>(raw.size() + 1) * 11 + 13);
}

TEST(qrMatchesIText) {
    // Matrix produced by iText 9 (com.itextpdf.barcodes.qrcode.Encoder) for "HELLO WORLD", level M.
    BarcodeQRCode qr("HELLO WORLD", ErrorCorrectionLevel::M);
    EXPECT_EQ(qr.getVersion(), 1);
    const auto& m = qr.getMatrix();
    EXPECT_EQ(static_cast<int>(m.size()), 21 + 8);
    static const char* expected[] = {
        "#######...#.#.#######",
        "#.....#.###...#.....#",
        "#.###.#...#.#.#.###.#",
        "#.###.#...#.#.#.###.#",
        "#.###.#.#.###.#.###.#",
        "#.....#..###..#.....#",
        "#######.#.#.#.#######",
        ".....................",
        "#.#.#.#..#..#...#..#.",
        ".####...#..#....#...#",
        "...#######.#..#.##...",
        "####.#.##..###.#.###.",
        ".#..####.#.#..###.#.#",
        "........#.#...#...#.#",
        "#######.....#..#.##..",
        "#.....#..##...##.#...",
        "#.###.#.##..#.#######",
        "#.###.#...##.#.#...#.",
        "#.###.#.####.###.#..#",
        "#.....#....###...#.##",
        "#######.##.#.###....#",
    };
    for (int y = 0; y < 21; ++y)
        for (int x = 0; x < 21; ++x) EXPECT_EQ(m[y + 4][x + 4], expected[y][x] == '#');
}

TEST(barcodesInPdf) {
    std::string path = "barcodes.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        PdfPage* page = pdf.addNewPage();
        PdfCanvas canvas(page);
        Barcode128 code128(&pdf);
        code128.setCode("pdfcpp-128");
        auto x1 = code128.createFormXObject();
        canvas.addXObjectAt(*x1, 72, 700);
        BarcodeEAN ean(&pdf);
        ean.setCode("4006381333931");
        auto x2 = ean.createFormXObject();
        canvas.addXObjectAt(*x2, 72, 600);
        Barcode39 code39(&pdf);
        code39.setCode("PDFCPP 39");
        auto x3 = code39.createFormXObject();
        canvas.addXObjectAt(*x3, 72, 500);
        BarcodeQRCode qr("https://github.com/itext/itext-java");
        auto x4 = qr.createFormXObject(&pdf, ColorConstants::BLACK, 3);
        canvas.addXObjectAt(*x4, 300, 500);
        pdf.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    std::string text = PdfTextExtractor::getTextFromPage(doc.getPage(1));
    EXPECT_TRUE(text.find("pdfcpp-128") != std::string::npos);
    EXPECT_TRUE(text.find("*PDFCPP 39*") != std::string::npos);
}

TEST_MAIN()
