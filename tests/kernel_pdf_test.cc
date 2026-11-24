#include <filesystem>

#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/filters/filters.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

TEST(ByteUtilsFormatsNumbers) {
  EXPECT_EQ(io::ByteUtils::FormatDouble(0), std::string("0"));
  EXPECT_EQ(io::ByteUtils::FormatDouble(1.5), std::string("1.5"));
  EXPECT_EQ(io::ByteUtils::FormatDouble(-0.25), std::string("-0.25"));
  EXPECT_EQ(io::ByteUtils::FormatDouble(595), std::string("595"));
  EXPECT_EQ(io::ByteUtils::FormatDouble(12.345), std::string("12.35"));
  EXPECT_EQ(io::ByteUtils::FormatDouble(100000.4), std::string("100000"));
}

TEST(TokenizerReadsTokens) {
  io::PdfTokenizer tok(io::RandomAccessSourceFactory().CreateSource(
      ToBytes("<< /Name (str\\)) 12 0 R [1 -2.5] <414243> >>")));
  using TT = io::PdfTokenizer::TokenType;
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kStartDic);
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kName);
  EXPECT_EQ(tok.GetStringValue(), std::string("Name"));
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kString);
  EXPECT_EQ(ToLatin1String(tok.GetDecodedStringContent()), std::string("str)"));
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kRef);
  EXPECT_EQ(tok.GetObjNr(), 12);
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kStartArray);
  tok.NextValidToken();
  EXPECT_EQ(tok.GetIntValue(), 1);
  tok.NextValidToken();
  EXPECT_EQ(tok.GetStringValue(), std::string("-2.5"));
  tok.NextValidToken();
  EXPECT_TRUE(tok.GetTokenType() == TT::kEndArray);
  tok.NextValidToken();
  EXPECT_TRUE(tok.IsHexString());
  EXPECT_EQ(ToLatin1String(tok.GetDecodedStringContent()), std::string("ABC"));
}

TEST(PdfNameEscaping) {
  PdfName n("A B#");
  auto content = n.GetInternalContent();
  EXPECT_EQ(ToLatin1String(content), std::string("A#20B#23"));
  PdfName parsed(ToBytes("A#20B#23"));
  EXPECT_EQ(parsed.GetValue(), std::string("A B#"));
  EXPECT_TRUE(PdfName::Of("Type").get() == PdfName::kType.get());
}

TEST(PdfStringUnicode) {
  PdfString s("\xE6\x97\xA5\xE6\x9C\xAC", "UnicodeBig");
  Bytes b = s.GetValueBytes();
  EXPECT_EQ(static_cast<int>(b[0]), 0xFE);
  PdfString parsed(b);
  // A PdfString from raw bytes decodes as PDF text string
  EXPECT_EQ(parsed.ToUnicodeString(), std::string("\xE6\x97\xA5\xE6\x9C\xAC"));
}

static Bytes WriteSimplePdf(bool full_compression, int pages) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(
        baos, WriterProperties().SetFullCompressionMode(full_compression));
    PdfDocument doc(writer);
    doc.GetDocumentInfo().SetTitle("Test title").SetAuthor("pdfcpp");
    for (int i = 0; i < pages; ++i) {
      PdfPage* page = doc.AddNewPage();
      page->GetFirstContentStream()->GetOutputStream()->WriteString(
          "0 0 m 100 100 l S\n");
    }
    doc.Close();
  }
  return baos.ToByteArray();
}

TEST(WriteAndReadRoundTrip) {
  for (bool full : {false, true}) {
    Bytes pdf = WriteSimplePdf(full, 25);
    EXPECT_TRUE(pdf.size() > 100);
    std::string s(pdf.begin(), pdf.end());
    EXPECT_TRUE(s.rfind("%PDF-1.7", 0) == 0);
    EXPECT_TRUE(s.find("%%EOF") != std::string::npos);
    auto reader = PdfReader::FromBytes(pdf);
    PdfDocument doc(std::move(reader));
    EXPECT_EQ(doc.GetNumberOfPages(), 25);
    EXPECT_EQ(doc.GetDocumentInfo().GetTitle(), std::string("Test title"));
    EXPECT_EQ(doc.GetDocumentInfo().GetAuthor(), std::string("pdfcpp"));
    Rectangle box = doc.GetPage(3)->GetPageSize();
    EXPECT_NEAR(box.GetWidth(), 595, 0.01);
    EXPECT_NEAR(box.GetHeight(), 842, 0.01);
    Bytes content = doc.GetPage(25)->GetContentBytes();
    EXPECT_EQ(std::string(content.begin(), content.end()),
              std::string("0 0 m 100 100 l S\n"));
    doc.Close();
  }
}

TEST(StampingAppendMode) {
  Bytes original = WriteSimplePdf(false, 2);
  io::ByteArrayOutputStream out;
  {
    auto reader = PdfReader::FromBytes(original);
    PdfWriter writer(out);
    PdfDocument doc(*reader, writer, StampingProperties().UseAppendMode());
    doc.GetDocumentInfo().SetSubject("appended");
    doc.AddNewPage();
    doc.Close();
  }
  Bytes result = out.ToByteArray();
  EXPECT_TRUE(result.size() > original.size());
  EXPECT_TRUE(std::equal(original.begin(), original.end(), result.begin()));
  auto reader = PdfReader::FromBytes(result);
  PdfDocument doc(std::move(reader));
  EXPECT_EQ(doc.GetNumberOfPages(), 3);
  EXPECT_EQ(doc.GetDocumentInfo().GetSubject(), std::string("appended"));
  EXPECT_EQ(doc.GetDocumentInfo().GetTitle(), std::string("Test title"));
}

TEST(StampingRewrite) {
  Bytes original = WriteSimplePdf(true, 3);
  io::ByteArrayOutputStream out;
  {
    auto reader = PdfReader::FromBytes(original);
    PdfWriter writer(out);
    PdfDocument doc(*reader, writer);
    doc.RemovePage(2);
    doc.Close();
  }
  auto reader = PdfReader::FromBytes(out.ToByteArray());
  PdfDocument doc(std::move(reader));
  EXPECT_EQ(doc.GetNumberOfPages(), 2);
}

TEST(EncryptionRoundTrip) {
  for (int alg : {encryption_constants::kStandardEncryption40,
                  encryption_constants::kStandardEncryption128,
                  encryption_constants::kEncryptionAes128,
                  encryption_constants::kEncryptionAes256}) {
    io::ByteArrayOutputStream baos;
    {
      PdfWriter writer(baos, WriterProperties().SetStandardEncryption(
                                 ToBytes("user"), ToBytes("owner"),
                                 encryption_constants::kAllowPrinting, alg));
      PdfDocument doc(writer);
      doc.GetDocumentInfo().SetTitle("Secret");
      doc.AddNewPage()->GetFirstContentStream()->GetOutputStream()->WriteString(
          "BT ET");
      doc.Close();
    }
    Bytes pdf = baos.ToByteArray();
    for (const char* pwd : {"user", "owner"}) {
      auto reader =
          PdfReader::FromBytes(pdf, ReaderProperties().SetPassword(pwd));
      PdfDocument doc(std::move(reader));
      EXPECT_EQ(doc.GetDocumentInfo().GetTitle(), std::string("Secret"));
      Bytes content = doc.GetPage(1)->GetContentBytes();
      EXPECT_EQ(std::string(content.begin(), content.end()),
                std::string("BT ET"));
    }
    EXPECT_THROW({
      auto reader =
          PdfReader::FromBytes(pdf, ReaderProperties().SetPassword("wrong"));
      PdfDocument doc(std::move(reader));
    });
  }
}

TEST(FiltersDecode) {
  Bytes hex = ASCIIHexDecodeFilter::ASCIIHexDecode(ToBytes("48 65 6C6C6F>"));
  EXPECT_EQ(ToLatin1String(hex), std::string("Hello"));
  Bytes a85 = ASCII85DecodeFilter::ASCII85Decode(ToBytes("87cURD]i,\"Ebo80~>"));
  EXPECT_EQ(ToLatin1String(a85), std::string("Hello World!"));
}

TEST(ReadReferencePdfs) {
  namespace fs = std::filesystem;
  fs::path dir =
      fs::path(pdfcpp_test::ReferenceDir()) /
      "kernel/src/test/resources/com/itextpdf/kernel/pdf/PdfReaderTest";
  if (!fs::exists(dir)) return;
  int ok = 0, total = 0;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".pdf") continue;
    ++total;
    try {
      PdfReader reader(entry.path().string());
      PdfDocument doc(reader);
      int n = doc.GetNumberOfPages();
      for (int i = 1; i <= n; ++i) doc.GetPage(i)->GetContentBytes();
      ++ok;
    } catch (const std::exception& e) {
      // some files are intentionally broken or encrypted
      if (std::getenv("PDFCPP_VERBOSE"))
        std::cout << "    " << entry.path().filename() << ": " << e.what()
                  << "\n";
    }
  }
  std::cout << "    read " << ok << "/" << total << " reference PDFs\n";
  EXPECT_TRUE(total == 0 || ok * 10 >= total * 7);
}

// BUGS.md #15: unused objects flushed at close() under full compression must
// land in a flushed object stream.
TEST(FullCompressionFlushesUnusedObjects) {
  io::ByteArrayOutputStream baos;
  int unused_number = 0;
  {
    PdfWriter writer(baos, WriterProperties().SetFullCompressionMode(true));
    PdfDocument doc(writer);
    doc.SetFlushUnusedObjects(true);
    doc.AddNewPage();
    auto unused = std::make_shared<PdfDictionary>();
    unused->Put(PdfName::kTitle, std::make_shared<PdfString>("orphan"));
    unused->MakeIndirect(&doc);
    unused_number = unused->GetIndirectReference()->GetObjNumber();
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  auto obj = PdfCast<PdfDictionary>(doc.GetPdfObject(unused_number));
  EXPECT_TRUE(obj != nullptr);
  if (obj)
    EXPECT_EQ(obj->GetAsString(PdfName::kTitle)->ToUnicodeString(),
              std::string("orphan"));
  doc.Close();
}

// BUGS.md #14: malformed LZW streams must not index out of bounds.
TEST(LzwMalformedStreams) {
  // 9-bit codes: 0x41 ('A'), then code 300 (> next free slot 258)
  auto pack9 = [](const std::vector<int>& codes) {
    Bytes out;
    uint32_t acc = 0;
    int bits = 0;
    for (int c : codes) {
      acc = (acc << 9) | static_cast<uint32_t>(c);
      bits += 9;
      while (bits >= 8) {
        out.push_back(static_cast<uint8_t>(acc >> (bits - 8)));
        bits -= 8;
      }
    }
    if (bits > 0) out.push_back(static_cast<uint8_t>(acc << (8 - bits)));
    return out;
  };
  Bytes out = LZWDecodeFilter::LZWDecode(pack9({256, 0x41, 300, 257}));
  EXPECT_TRUE(out == Bytes{0x41});
  // clear, clear, then the "next code" special case referencing the empty clear
  // entry
  out = LZWDecodeFilter::LZWDecode(pack9({256, 256, 258, 257}));
  EXPECT_TRUE(out.empty());
  // regular KwKwK case still works: A, then code 258 = "AA"
  out = LZWDecodeFilter::LZWDecode(pack9({256, 0x41, 258, 257}));
  EXPECT_TRUE(out == (Bytes{0x41, 0x41, 0x41}));
}

// BUGS.md #23: negative predictor parameters leave the data untouched instead
// of misbehaving.
TEST(PredictorRejectsBadParameters) {
  Bytes data = {2, 1, 1, 1};
  PdfDictionary parms;
  parms.Put(PdfName::kPredictor, MakeNumber(12));
  parms.Put(PdfName::kColors, MakeNumber(-1));
  parms.Put(PdfName::kColumns, MakeNumber(-3));
  EXPECT_TRUE(FlateDecodeFilter::DecodePredictor(data, &parms) == data);
  parms.Put(PdfName::kColors, MakeNumber(1));
  parms.Put(PdfName::kColumns, MakeNumber(2000000000));
  EXPECT_TRUE(FlateDecodeFilter::DecodePredictor(data, &parms) == data);
  parms.Put(PdfName::kColumns, MakeNumber(3));
  EXPECT_TRUE(FlateDecodeFilter::DecodePredictor(data, &parms) ==
              (Bytes{1, 1, 1}));
}

// BUGS.md #13: kEmbeddedFilesOnly writes /Identity string and stream filters
// (content stays readable).
TEST(EmbeddedFilesOnlyEncryptionDictionary) {
  for (int algorithm : {encryption_constants::kEncryptionAes128,
                        encryption_constants::kEncryptionAes256}) {
    io::ByteArrayOutputStream baos;
    {
      PdfWriter writer(
          baos, WriterProperties().SetStandardEncryption(
                    ToBytes("user"), ToBytes("owner"),
                    encryption_constants::kAllowPrinting,
                    algorithm | encryption_constants::kEmbeddedFilesOnly));
      PdfDocument doc(writer);
      doc.GetDocumentInfo().SetTitle("Clear title");
      doc.AddNewPage()->GetFirstContentStream()->GetOutputStream()->WriteString(
          "0 0 m 10 10 l S\n");
      doc.Close();
    }
    Bytes pdf = baos.ToByteArray();
    // Before the fix the dictionary declared /StdCF filters while strings and
    // streams were written in clear, so reading them back "decrypted" plain
    // text into garbage.
    PdfDocument doc(
        PdfReader::FromBytes(pdf, ReaderProperties().SetPassword("user")));
    auto enc = doc.GetTrailer()->GetAsDictionary(PdfName::kEncrypt);
    EXPECT_TRUE(enc != nullptr);
    if (enc) {
      EXPECT_TRUE(
          NameEquals(enc->GetAsName(PdfName::kStmF), PdfName::kIdentity));
      EXPECT_TRUE(
          NameEquals(enc->GetAsName(PdfName::kStrF), PdfName::kIdentity));
      EXPECT_TRUE(NameEquals(enc->GetAsName(PdfName::kEFF), PdfName::kStdCF));
    }
    EXPECT_EQ(doc.GetDocumentInfo().GetTitle(), std::string("Clear title"));
    Bytes content = doc.GetPage(1)->GetContentBytes();
    EXPECT_EQ(std::string(content.begin(), content.end()),
              std::string("0 0 m 10 10 l S\n"));
    doc.Close();
  }
}

// BUGS.md #19: the indexed AddPage overload must refuse pages of another
// document.
TEST(AddPageAtIndexRejectsForeignPage) {
  io::ByteArrayOutputStream a, b;
  PdfWriter wa(a), wb(b);
  PdfDocument doc_a(wa), doc_b(wb);
  doc_a.AddNewPage();
  doc_b.AddNewPage();
  auto foreign = std::make_shared<PdfPage>(&doc_b, PageSize::kA4);
  EXPECT_THROW(doc_a.AddPage(1, foreign));
  doc_a.Close();
  doc_b.Close();
}

// BUGS.md #21: copying a page keeps markup popups consistent and only drops /P
// links to source pages.
TEST(CopyPageKeepsPopups) {
  io::ByteArrayOutputStream src;
  {
    PdfWriter writer(src);
    PdfDocument doc(writer);
    PdfPage* page = doc.AddNewPage();
    auto text = std::make_shared<PdfDictionary>();
    text->Put(PdfName::kType, PdfName::kAnnot);
    text->Put(PdfName::kSubtype, PdfName::kText);
    text->Put(PdfName::kRect,
              std::make_shared<PdfArray>(Rectangle(100, 100, 20, 20)));
    text->Put(PdfName::kContents, std::make_shared<PdfString>("note"));
    text->MakeIndirect(&doc);
    auto popup = std::make_shared<PdfDictionary>();
    popup->Put(PdfName::kType, PdfName::kAnnot);
    popup->Put(PdfName::kSubtype, PdfName::kPopup);
    popup->Put(PdfName::kRect,
               std::make_shared<PdfArray>(Rectangle(120, 120, 100, 50)));
    popup->Put(PdfName::kParent, text);
    popup->MakeIndirect(&doc);
    text->Put(PdfName::kPopup, popup);
    page->AddAnnotation(text);
    page->AddAnnotation(popup);
    doc.Close();
  }
  io::ByteArrayOutputStream dst;
  {
    PdfDocument from(PdfReader::FromBytes(src.ToByteArray()));
    PdfWriter writer(dst);
    PdfDocument to(writer);
    from.CopyPagesTo(1, 1, to);
    to.Close();
    from.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(dst.ToByteArray()));
  EXPECT_EQ(doc.GetNumberOfPages(), 1);
  auto page_dict = doc.GetPage(1)->GetPdfObject();
  auto annots = page_dict->GetAsArray(PdfName::kAnnots);
  EXPECT_TRUE(annots && annots->size() == 2);
  if (annots && annots->size() == 2) {
    auto markup = annots->GetAsDictionary(0);
    auto popup = annots->GetAsDictionary(1);
    EXPECT_TRUE(markup->GetAsDictionary(PdfName::kPopup).get() == popup.get());
    EXPECT_TRUE(popup->GetAsDictionary(PdfName::kParent).get() == markup.get());
    EXPECT_TRUE(markup->GetAsDictionary(PdfName::kP).get() == page_dict.get());
    EXPECT_TRUE(popup->GetAsDictionary(PdfName::kP).get() == page_dict.get());
  }
  doc.Close();
}

// BUGS.md #24
TEST(MakeColorWithShortRgbValues) {
  auto rgb = PdfColorSpace::MakeColorSpace(PdfName::kDeviceRGB);
  auto c = Color::MakeColor(rgb, {0.5f});
  EXPECT_TRUE(c != nullptr);
  auto full = Color::MakeColor(rgb, {0.1f, 0.2f, 0.3f});
  EXPECT_TRUE(dynamic_cast<DeviceRgb*>(full.get()) != nullptr);
}

// BUGS.md #26: a zero-extent arc must not write NaN into the content stream.
TEST(ZeroExtentArcHasNoNaN) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfCanvas canvas(doc.AddNewPage());
    canvas.Arc(100, 100, 200, 200, 30, 0).Stroke();
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  Bytes content = doc.GetPage(1)->GetContentBytes();
  std::string s(content.begin(), content.end());
  EXPECT_TRUE(s.find("nan") == std::string::npos &&
              s.find("NaN") == std::string::npos);
  EXPECT_TRUE(s.find(" c") != std::string::npos);
  doc.Close();
}

// BUGS.md #27: an empty string has no trailing spacing to subtract from its
// baseline.
struct BaselineCollector : IEventListener {
  std::vector<float> widths;
  void EventOccurred(const IEventData& data, EventType type) override {
    if (type != EventType::kRenderText) return;
    const auto& info = static_cast<const TextRenderInfo&>(data);
    auto base = info.GetBaseline();
    widths.push_back(base.GetEndPoint().Get(0) - base.GetStartPoint().Get(0));
  }
  std::set<EventType> GetSupportedEvents() const override {
    return {EventType::kRenderText};
  }
};

TEST(EmptyStringBaselineIsNotNegative) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfCanvas canvas(doc.AddNewPage());
    canvas.BeginText()
        .SetFontAndSize(PdfFontFactory::CreateFont(), 12)
        .SetCharacterSpacing(5)
        .MoveText(72, 700);
    canvas.ShowText("").EndText();
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  BaselineCollector collector;
  PdfCanvasProcessor processor(collector);
  processor.ProcessPageContent(doc.GetPage(1));
  for (float w : collector.widths) EXPECT_TRUE(w >= 0);
  doc.Close();
}

TEST_MAIN()
