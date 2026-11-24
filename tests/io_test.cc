// Regression tests for the io module (malformed-input guards and encoding
// helpers; see BUGS.md).
#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/font/cmap.h"
#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/io/source/random_access_source.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::io;

static Bytes BytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

TEST(JpegZeroLengthSegmentDoesNotHang) {
  // SOI, then an APP0 marker whose length field is 0 (would never advance),
  // padding after it
  Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_THROW(ImageDataFactory::CreateJpeg(jpeg));
}

TEST(JpegTruncatedSofIsRejected) {
  // SOF0 claiming 17 bytes but the file ends right after the length field
  Bytes jpeg = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00};
  EXPECT_THROW(ImageDataFactory::CreateJpeg(jpeg));
  // a short (but in-bounds) SOF segment
  Bytes short_sof = {0xFF, 0xD8, 0xFF, 0xC0, 0x00,
                     0x04, 0x08, 0x00, 0xFF, 0xD9};
  EXPECT_THROW(ImageDataFactory::CreateJpeg(short_sof));
}

static void PutBe32(Bytes& b, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

static Bytes PngWithHeader(const Bytes& ihdr_data) {
  Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  PutBe32(png, static_cast<uint32_t>(ihdr_data.size()));
  for (char c : std::string("IHDR")) png.push_back(static_cast<uint8_t>(c));
  png.insert(png.end(), ihdr_data.begin(), ihdr_data.end());
  PutBe32(png, 0);  // crc (not checked)
  PutBe32(png, 0);
  for (char c : std::string("IEND")) png.push_back(static_cast<uint8_t>(c));
  PutBe32(png, 0);
  return png;
}

TEST(PngInvalidHeaderIsRejected) {
  // bit depth 0 used to divide by zero
  Bytes zero_depth = {0, 0,           0,           1, 0, 0, 0,
                      1, /*depth*/ 0, /*color*/ 0, 0, 0, 0};
  EXPECT_THROW(ImageDataFactory::CreatePng(PngWithHeader(zero_depth)));
  // RGB must be 8 or 16 bits
  Bytes rgb4 = {0, 0, 0, 1, 0, 0, 0, 1, 4, 2, 0, 0, 0};
  EXPECT_THROW(ImageDataFactory::CreatePng(PngWithHeader(rgb4)));
  // IHDR shorter than 13 bytes; pad the file so the size pre-check passes
  Bytes short_header = PngWithHeader({0, 0, 0, 1, 0, 0, 0, 1});
  short_header.resize(64, 0);
  EXPECT_THROW(ImageDataFactory::CreatePng(short_header));
}

TEST(CmapTruncatedBfrangeTerminates) {
  // invalid range (end < start) whose destination array is never closed
  std::string cmap = "1 beginbfrange\n<0010> <0005> [<0041> <0042>\n";
  CMapParser::Result r = CMapParser::Parse(BytesOf(cmap));
  EXPECT_TRUE(r.bf_map.empty());
}

TEST(IdentityToUnicodeLookupsAreIndependent) {
  auto identity = CMapToUnicode::GetIdentity();
  uint8_t a[2] = {0x00, 0x41};
  uint8_t b[2] = {0x00, 0x42};
  auto first = identity->Lookup(a, 2);
  auto second = identity->Lookup(b, 2);
  EXPECT_TRUE(first.has_value() && second.has_value());
  // the first result must not have been overwritten by the second lookup
  EXPECT_TRUE(*first == std::u32string(1, U'A'));
  EXPECT_TRUE(*second == std::u32string(1, U'B'));
  EXPECT_TRUE(!identity->Lookup(a, 1).has_value());
}

TEST(EncodingAliases) {
  // U+20AC (euro) is 0x80 in WinAnsi and 0xA0 in PDFDocEncoding
  const std::string euro = "\xE2\x82\xAC";
  for (const char* enc : {"Cp1252", "WinAnsi", "WinAnsiEncoding"}) {
    EXPECT_TRUE(PdfEncodings::ConvertToBytes(euro, enc) == Bytes{0x80});
    EXPECT_EQ(PdfEncodings::ConvertToString(Bytes{0x80}, enc), euro);
  }
  for (const char* enc : {"PDF", "pdf", "PDFDocEncoding"}) {
    EXPECT_TRUE(PdfEncodings::ConvertToBytes(euro, enc) == Bytes{0xA0});
    EXPECT_EQ(PdfEncodings::ConvertToString(Bytes{0xA0}, enc), euro);
  }
  // MacRoman 0x80 is A-dieresis
  EXPECT_EQ(PdfEncodings::ConvertToString(Bytes{0x80}, "MacRomanEncoding"),
            std::string("\xC3\x84"));
  EXPECT_TRUE(PdfEncodings::ConvertToBytes("\xC3\x84", "MacRomanEncoding") ==
              Bytes{0x80});
}

TEST(WindowAndGroupedSourcesRejectNegativePositions) {
  auto base =
      std::make_shared<ArrayRandomAccessSource>(Bytes{1, 2, 3, 4, 5, 6});
  WindowRandomAccessSource window(base, 2, 3);
  EXPECT_EQ(window.Get(0), 3);
  EXPECT_EQ(window.Get(-1), -1);
  uint8_t buf[4];
  EXPECT_EQ(window.Get(-2, buf, 0, 4), -1);
  GroupedRandomAccessSource grouped(
      {std::make_shared<ArrayRandomAccessSource>(Bytes{7, 8}),
       std::make_shared<ArrayRandomAccessSource>(Bytes{9})});
  EXPECT_EQ(grouped.Get(2), 9);
  EXPECT_EQ(grouped.Get(-1), -1);
}

TEST_MAIN()
