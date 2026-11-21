// Regression tests for the io module (malformed-input guards and encoding helpers; see BUGS.md).
#include "TestHarness.h"
#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/font/CMap.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/image/ImageData.h"
#include "pdfcpp/io/source/RandomAccessSource.h"

using namespace pdfcpp;
using namespace pdfcpp::io;

static Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

TEST(jpegZeroLengthSegmentDoesNotHang) {
    // SOI, then an APP0 marker whose length field is 0 (would never advance), padding after it
    Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_THROW(ImageDataFactory::createJpeg(jpeg));
}

TEST(jpegTruncatedSofIsRejected) {
    // SOF0 claiming 17 bytes but the file ends right after the length field
    Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00};
    EXPECT_THROW(ImageDataFactory::createJpeg(jpeg));
    // a short (but in-bounds) SOF segment
    Bytes shortSof = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x04, 0x08, 0x00, 0xFF, 0xD9};
    EXPECT_THROW(ImageDataFactory::createJpeg(shortSof));
}

static void putBe32(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

static Bytes pngWithHeader(const Bytes& ihdrData) {
    Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    putBe32(png, static_cast<uint32_t>(ihdrData.size()));
    for (char c : std::string("IHDR")) png.push_back(static_cast<uint8_t>(c));
    png.insert(png.end(), ihdrData.begin(), ihdrData.end());
    putBe32(png, 0);  // crc (not checked)
    putBe32(png, 0);
    for (char c : std::string("IEND")) png.push_back(static_cast<uint8_t>(c));
    putBe32(png, 0);
    return png;
}

TEST(pngInvalidHeaderIsRejected) {
    // bit depth 0 used to divide by zero
    Bytes zeroDepth = {0, 0, 0, 1, 0, 0, 0, 1, /*depth*/ 0, /*color*/ 0, 0, 0, 0};
    EXPECT_THROW(ImageDataFactory::createPng(pngWithHeader(zeroDepth)));
    // RGB must be 8 or 16 bits
    Bytes rgb4 = {0, 0, 0, 1, 0, 0, 0, 1, 4, 2, 0, 0, 0};
    EXPECT_THROW(ImageDataFactory::createPng(pngWithHeader(rgb4)));
    // IHDR shorter than 13 bytes; pad the file so the size pre-check passes
    Bytes shortHeader = pngWithHeader({0, 0, 0, 1, 0, 0, 0, 1});
    shortHeader.resize(64, 0);
    EXPECT_THROW(ImageDataFactory::createPng(shortHeader));
}

TEST(cmapTruncatedBfrangeTerminates) {
    // invalid range (end < start) whose destination array is never closed
    std::string cmap = "1 beginbfrange\n<0010> <0005> [<0041> <0042>\n";
    CMapParser::Result r = CMapParser::parse(bytesOf(cmap));
    EXPECT_TRUE(r.bfMap.empty());
}

TEST(identityToUnicodeLookupsAreIndependent) {
    auto identity = CMapToUnicode::getIdentity();
    uint8_t a[2] = {0x00, 0x41};
    uint8_t b[2] = {0x00, 0x42};
    auto first = identity->lookup(a, 2);
    auto second = identity->lookup(b, 2);
    EXPECT_TRUE(first.has_value() && second.has_value());
    // the first result must not have been overwritten by the second lookup
    EXPECT_TRUE(*first == std::u32string(1, U'A'));
    EXPECT_TRUE(*second == std::u32string(1, U'B'));
    EXPECT_TRUE(!identity->lookup(a, 1).has_value());
}

TEST(encodingAliases) {
    // U+20AC (euro) is 0x80 in WinAnsi and 0xA0 in PDFDocEncoding
    const std::string euro = "\xE2\x82\xAC";
    for (const char* enc : {"Cp1252", "WinAnsi", "WinAnsiEncoding"}) {
        EXPECT_TRUE(PdfEncodings::convertToBytes(euro, enc) == Bytes{0x80});
        EXPECT_EQ(PdfEncodings::convertToString(Bytes{0x80}, enc), euro);
    }
    for (const char* enc : {"PDF", "pdf", "PDFDocEncoding"}) {
        EXPECT_TRUE(PdfEncodings::convertToBytes(euro, enc) == Bytes{0xA0});
        EXPECT_EQ(PdfEncodings::convertToString(Bytes{0xA0}, enc), euro);
    }
    // MacRoman 0x80 is A-dieresis
    EXPECT_EQ(PdfEncodings::convertToString(Bytes{0x80}, "MacRomanEncoding"), std::string("\xC3\x84"));
    EXPECT_TRUE(PdfEncodings::convertToBytes("\xC3\x84", "MacRomanEncoding") == Bytes{0x80});
}

TEST(windowAndGroupedSourcesRejectNegativePositions) {
    auto base = std::make_shared<ArrayRandomAccessSource>(Bytes{1, 2, 3, 4, 5, 6});
    WindowRandomAccessSource window(base, 2, 3);
    EXPECT_EQ(window.get(0), 3);
    EXPECT_EQ(window.get(-1), -1);
    uint8_t buf[4];
    EXPECT_EQ(window.get(-2, buf, 0, 4), -1);
    GroupedRandomAccessSource grouped({std::make_shared<ArrayRandomAccessSource>(Bytes{7, 8}),
                                       std::make_shared<ArrayRandomAccessSource>(Bytes{9})});
    EXPECT_EQ(grouped.get(2), 9);
    EXPECT_EQ(grouped.get(-1), -1);
}

TEST_MAIN()
