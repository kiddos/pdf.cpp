// Regression tests for the io module (malformed-input guards and encoding
// helpers; see BUGS.md).
#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/font/cmap.h"
#include "pdfcpp/io/font/font_program.h"
#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/font/true_type_font.h"
#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/io/source/pdf_tokenizer.h"
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

static void PutBe16(Bytes& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

/// Minimal sfnt container holding the given tables (checksums are not
/// verified by the parser).
static Bytes MakeSfnt(const std::map<std::string, Bytes>& tables) {
  Bytes f;
  PutBe32(f, 0x00010000);
  PutBe16(f, static_cast<uint32_t>(tables.size()));
  PutBe16(f, 0);
  PutBe16(f, 0);
  PutBe16(f, 0);
  uint32_t offset = 12 + 16 * static_cast<uint32_t>(tables.size());
  for (const auto& [tag, data] : tables) {
    for (char c : tag) f.push_back(static_cast<uint8_t>(c));
    PutBe32(f, 0);
    PutBe32(f, offset);
    PutBe32(f, static_cast<uint32_t>(data.size()));
    offset += (static_cast<uint32_t>(data.size()) + 3) & ~3u;
  }
  for (const auto& kv : tables) {
    f.insert(f.end(), kv.second.begin(), kv.second.end());
    while (f.size() % 4) f.push_back(0);
  }
  return f;
}

/// A three-glyph TrueType font: cmap format 12 maps 'A' to glyph 1 and has a
/// second group at the top of the 32-bit range; the kern table has a pair whose
/// left glyph id is >= 32768.
static Bytes MakeTestFont() {
  Bytes head(54, 0);
  head[18] = 0x03;  // unitsPerEm 1000
  head[19] = 0xE8;
  Bytes hhea(36, 0);
  hhea[35] = 1;  // numberOfHMetrics
  Bytes maxp;
  PutBe32(maxp, 0x00005000);
  PutBe16(maxp, 3);  // numGlyphs
  Bytes hmtx;
  PutBe16(hmtx, 500);
  PutBe16(hmtx, 0);
  Bytes cmap;
  PutBe16(cmap, 0);   // version
  PutBe16(cmap, 1);   // numTables
  PutBe16(cmap, 3);   // platform: Windows
  PutBe16(cmap, 10);  // encoding: UCS-4
  PutBe32(cmap, 12);  // subtable offset
  PutBe16(cmap, 12);  // format 12
  PutBe16(cmap, 0);
  PutBe32(cmap, 16 + 2 * 12);  // length
  PutBe32(cmap, 0);            // language
  PutBe32(cmap, 2);            // numGroups
  for (uint32_t v : {0x41u, 0x41u, 1u, 0xFFFF0000u, 0xFFFFFFFFu, 2u})
    PutBe32(cmap, v);
  Bytes kern;
  PutBe16(kern, 0);  // version
  PutBe16(kern, 1);  // nTables
  PutBe16(kern, 0);  // subtable version
  PutBe16(kern, 14 + 2 * 6);
  PutBe16(kern, 0x0001);  // horizontal, format 0
  PutBe16(kern, 2);       // nPairs
  PutBe16(kern, 0);
  PutBe16(kern, 0);
  PutBe16(kern, 0);
  for (uint32_t v : {0x8001u, 2u, static_cast<uint32_t>(-50 & 0xFFFF), 1u, 2u,
                     static_cast<uint32_t>(-30 & 0xFFFF)})
    PutBe16(kern, v);
  return MakeSfnt({{"head", head},
                   {"hhea", hhea},
                   {"maxp", maxp},
                   {"hmtx", hmtx},
                   {"cmap", cmap},
                   {"kern", kern}});
}

// BUGS.md #4: kerning pairs with a left glyph id >= 32768 are found, a format
// 12 group ending at 0xFFFFFFFF does not wrap around (and hang), and a table
// directory running past the data is rejected.
TEST(TrueTypeKerningCmap12AndDirectoryBounds) {
  TrueTypeFont font(MakeTestFont());
  const auto& cmap = font.GetActiveCmap();
  EXPECT_TRUE(cmap.count(0x41) == 1 && cmap.at(0x41).first == 1);
  EXPECT_EQ(static_cast<int>(cmap.size()), 1);
  EXPECT_EQ(font.GetKerning(Glyph(0x8001, 0, -1), Glyph(2, 0, -1)), -50);
  EXPECT_EQ(font.GetKerning(Glyph(1, 0, -1), Glyph(2, 0, -1)), -30);

  Bytes truncated = MakeTestFont();
  truncated[5] = 50;  // numTables
  truncated.resize(12 + 16 * 3);
  EXPECT_THROW(TrueTypeFont{truncated});
}

// BUGS.md #6: negative codes in custom encodings are ignored instead of
// written before the start of the tables.
TEST(CustomFontEncodingIgnoresNegativeCodes) {
  auto simple = FontEncoding::CreateFontEncoding("# -1 0041 0042");
  EXPECT_TRUE(!simple->CanEncode(0x41));
  EXPECT_TRUE(!simple->CanEncode(0x42));
  auto full = FontEncoding::CreateFontEncoding("# full -3 A 0041 66 B 0042");
  EXPECT_TRUE(!full->CanEncode(0x41));
  EXPECT_TRUE(full->CanEncode(0x42));
  EXPECT_EQ(full->ConvertToByte(0x42), 66);
  EXPECT_EQ(full->GetDifference(66), std::string("B"));
  EXPECT_EQ(full->GetDifference(-1), std::string());
  EXPECT_EQ(full->GetDifference(256), std::string());
}

// BUGS.md #9: searching for %%EOF in a file shorter than the look-back window
// never seeks before the start.
TEST(GetNextEofOnTinyInput) {
  PdfTokenizer tiny(RandomAccessSourceFactory().CreateSource(BytesOf("ab")));
  EXPECT_THROW(tiny.GetNextEof());
  EXPECT_TRUE(tiny.GetPosition() >= 0);
  PdfTokenizer eof(RandomAccessSourceFactory().CreateSource(BytesOf("%%EOF")));
  EXPECT_EQ(eof.GetNextEof(), 5);
}

// BUGS.md #10: only a real "StartFontMetrics" header selects the AFM parser,
// and inputs shorter than the magic are not over-read.
TEST(FontProgramFactoryChecksWholeAfmMagic) {
  std::string message;
  try {
    FontProgramFactory::CreateFont(BytesOf("Starfish, not a font at all"));
  } catch (const IoException& e) {
    message = e.what();
  }
  EXPECT_TRUE(message.find("true type or open type") != std::string::npos);
  EXPECT_THROW(FontProgramFactory::CreateFont(BytesOf("St")));
}

TEST_MAIN()
