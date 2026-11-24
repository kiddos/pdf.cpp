#include <cstring>
#include <filesystem>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/filters/filters.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
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

// ------------------------------------------------- hand-assembled documents

/// `<< entries /Length n >> stream ... endstream` for an unfiltered stream.
static std::string StreamObject(const std::string& entries,
                                const std::string& data) {
  return "<< " + entries + " /Length " + std::to_string(data.size()) +
         " >>\nstream\n" + data + "\nendstream";
}

/// A PDF made of the given objects (number -> body) whose cross-reference is an
/// uncompressed xref stream, written as the last object. Its entries always use
/// field widths 1 4 2; `xref_entries` (normally "/W [1 4 2]") lets tests
/// declare other /W or /Index values. `compressed` adds type 2 entries:
/// object number -> {object stream number, index}.
static Bytes AssemblePdf(
    const std::map<int, std::string>& objects,
    const std::string& xref_entries = "/W [1 4 2]",
    const std::map<int, std::pair<int, int>>& compressed = {}) {
  std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
  std::map<int, size_t> offsets;
  int max_number = 0;
  for (const auto& [number, body] : objects) {
    offsets[number] = pdf.size();
    pdf += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
    max_number = std::max(max_number, number);
  }
  for (const auto& kv : compressed) max_number = std::max(max_number, kv.first);
  const int xref_number = max_number + 1;
  offsets[xref_number] = pdf.size();
  std::string rows;
  auto put = [&rows](uint32_t v, int width) {
    for (int k = width - 1; k >= 0; --k)
      rows.push_back(static_cast<char>(v >> (8 * k)));
  };
  for (int i = 0; i <= xref_number; ++i) {
    if (offsets.count(i)) {
      put(1, 1);
      put(static_cast<uint32_t>(offsets[i]), 4);
      put(0, 2);
    } else if (compressed.count(i)) {
      put(2, 1);
      put(static_cast<uint32_t>(compressed.at(i).first), 4);
      put(static_cast<uint32_t>(compressed.at(i).second), 2);
    } else {
      put(0, 1);
      put(0, 4);
      put(i == 0 ? 0xFFFF : 0, 2);
    }
  }
  pdf += std::to_string(xref_number) + " 0 obj\n" +
         StreamObject("/Type /XRef /Size " + std::to_string(xref_number + 1) +
                          " /Root 1 0 R " + xref_entries,
                      rows) +
         "\nendobj\n";
  pdf += "startxref\n" + std::to_string(offsets[xref_number]) + "\n%%EOF\n";
  return Bytes(pdf.begin(), pdf.end());
}

/// Catalog (1), page tree (2) and one empty page (3).
static std::map<int, std::string> OnePageObjects() {
  return {{1, "<< /Type /Catalog /Pages 2 0 R >>"},
          {2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"},
          {3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>"}};
}

// BUGS.md #12: malformed /Index and /W entries of an xref stream make the
// reader rebuild the cross-reference instead of crashing or misreading it.
TEST(XrefStreamWithBadIndexOrWidths) {
  {
    PdfDocument doc(PdfReader::FromBytes(AssemblePdf(OnePageObjects())));
    EXPECT_TRUE(!doc.GetReader()->HasRebuiltXref());
    EXPECT_EQ(doc.GetNumberOfPages(), 1);
  }
  for (const char* entries :
       {"/W [1 4 2] /Index [(a) 5]", "/W [1 4 2] /Index [0 /Five]",
        "/W [1 -4 2]", "/W [1 9 2]"}) {
    PdfDocument doc(
        PdfReader::FromBytes(AssemblePdf(OnePageObjects(), entries)));
    EXPECT_TRUE(doc.GetReader()->HasRebuiltXref());
    EXPECT_EQ(doc.GetNumberOfPages(), 1);
  }
}

// BUGS.md #12: an object stream's /N is checked before the offset tables are
// allocated.
TEST(ObjectStreamCountIsBounded) {
  const std::string content = "5 0 << /Title (in stream) >>";
  auto with_count = [&](const std::string& n) {
    auto objects = OnePageObjects();
    objects[4] = StreamObject("/Type /ObjStm /N " + n + " /First 4", content);
    return AssemblePdf(objects, "/W [1 4 2]", {{5, {4, 0}}});
  };
  {
    PdfDocument doc(PdfReader::FromBytes(with_count("1")));
    auto obj = PdfCast<PdfDictionary>(doc.GetPdfObject(5));
    EXPECT_TRUE(obj && obj->GetAsString(PdfName::kTitle));
  }
  for (const char* n : {"2000000000", "-1"}) {
    PdfDocument doc(PdfReader::FromBytes(with_count(n)));
    bool limited = false;
    try {
      doc.GetPdfObject(5);
    } catch (const MemoryLimitsAwareException&) {
      limited = true;
    }
    EXPECT_TRUE(limited);
  }
}

/// Number of references reachable from `obj` that do not resolve to an object.
static int CountDanglingReferences(PdfDocument& doc, const PdfObjectPtr& obj,
                                   std::set<int>& seen) {
  if (!obj) return 0;
  if (obj->IsIndirectReference()) {
    int number = static_cast<PdfIndirectReference&>(*obj).GetObjNumber();
    if (!seen.insert(number).second) return 0;
    PdfObjectPtr target = doc.GetPdfObject(number);
    return target ? CountDanglingReferences(doc, target, seen) : 1;
  }
  int dangling = 0;
  if (obj->IsDictionary() || obj->IsStream()) {
    auto& dict = static_cast<PdfDictionary&>(*obj);
    for (const auto& key : dict.KeySet())
      dangling += CountDanglingReferences(doc, dict.Get(key, false), seen);
  } else if (obj->IsArray()) {
    auto& array = static_cast<PdfArray&>(*obj);
    for (int i = 0; i < array.size(); ++i)
      dangling += CountDanglingReferences(doc, array.Get(i, false), seen);
  }
  return dangling;
}

// BUGS.md #16: in append mode every object created or modified before close()
// (including those created while flushing, e.g. extra object streams under full
// compression) is written; nothing referenced is left free.
TEST(AppendModeWritesAllModifiedObjects) {
  Bytes original = WriteSimplePdf(true, 1);
  io::ByteArrayOutputStream out;
  {
    auto reader = PdfReader::FromBytes(original);
    PdfWriter writer(out, WriterProperties().SetFullCompressionMode(true));
    PdfDocument doc(*reader, writer, StampingProperties().UseAppendMode());
    auto list = std::make_shared<PdfArray>();
    for (int i = 0; i < 300; ++i) {
      auto item = std::make_shared<PdfDictionary>();
      item->Put(PdfName::kTitle,
                std::make_shared<PdfString>("item " + std::to_string(i)));
      item->MakeIndirect(&doc);
      list->Add(item);
    }
    list->MakeIndirect(&doc);
    doc.GetCatalog().GetPdfObject()->Put(PdfName::Of("TestItems"), list);
    doc.GetCatalog().GetPdfObject()->SetModified();
    PdfCanvas canvas(doc.AddNewPage());
    canvas.BeginText()
        .SetFontAndSize(PdfFontFactory::CreateFont(), 12)
        .MoveText(72, 700)
        .ShowText("appended page")
        .EndText();
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(out.ToByteArray()));
  EXPECT_EQ(doc.GetNumberOfPages(), 2);
  std::set<int> seen;
  EXPECT_EQ(CountDanglingReferences(doc, doc.GetTrailer(), seen), 0);
  auto list =
      doc.GetCatalog().GetPdfObject()->GetAsArray(PdfName::Of("TestItems"));
  EXPECT_TRUE(list != nullptr);
  if (list) {
    EXPECT_EQ(list->size(), 300);
    auto last = list->GetAsDictionary(299);
    EXPECT_TRUE(last && last->GetAsString(PdfName::kTitle)->ToUnicodeString() ==
                            "item 299");
  }
}

// BUGS.md #17: a Type0 font with a TrueType program is always embedded:
// kForceNotEmbedded and licensing restrictions are errors.
TEST(Type0TrueTypeIsAlwaysEmbedded) {
  std::string path = pdfcpp_test::ReferenceDir() +
                     "/kernel/src/test/resources/com/itextpdf/kernel/fonts/"
                     "NotoSans-Regular.ttf";
  if (!std::filesystem::exists(path)) return;
  Bytes ttf = io::StreamUtil::ReadFile(path);
  auto font = PdfFontFactory::CreateFont(ttf, io::PdfEncodings::kIdentityH,
                                         EmbeddingStrategy::kPreferNotEmbedded);
  EXPECT_TRUE(font->IsEmbedded());
  EXPECT_THROW(PdfFontFactory::CreateFont(
      ttf, io::PdfEncodings::kIdentityH, EmbeddingStrategy::kForceNotEmbedded));
  // OS/2 fsType 2: restricted license embedding
  Bytes restricted = ttf;
  int num_tables = (restricted[4] << 8) | restricted[5];
  bool patched = false;
  for (int k = 0; k < num_tables; ++k) {
    size_t rec = 12 + 16 * static_cast<size_t>(k);
    if (std::memcmp(&restricted[rec], "OS/2", 4) != 0) continue;
    size_t os2 = (size_t(restricted[rec + 8]) << 24) |
                 (size_t(restricted[rec + 9]) << 16) |
                 (size_t(restricted[rec + 10]) << 8) | restricted[rec + 11];
    restricted[os2 + 8] = 0;
    restricted[os2 + 9] = 2;
    patched = true;
  }
  EXPECT_TRUE(patched);
  EXPECT_THROW(
      PdfFontFactory::CreateFont(restricted, io::PdfEncodings::kIdentityH));
}

// BUGS.md #20: a form added under a chosen name is registered like any other
// resource (found by GetResourceName, not handed out twice) and the change to
// an indirect resource dictionary survives an append-mode update.
TEST(AddFormWithNameRegistersResource) {
  auto objects = OnePageObjects();
  objects[3] =
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] "
      "/Resources 4 0 R >>";
  objects[4] = "<< >>";
  Bytes original = AssemblePdf(objects);
  io::ByteArrayOutputStream out;
  {
    auto reader = PdfReader::FromBytes(original);
    PdfWriter writer(out);
    PdfDocument doc(*reader, writer, StampingProperties().UseAppendMode());
    PdfResources* resources = doc.GetPage(1)->GetResources();
    PdfFormXObject form(Rectangle(0, 0, 10, 10));
    auto name = resources->AddForm(form, PdfName::Of("Stamp"));
    EXPECT_EQ(name->GetValue(), std::string("Stamp"));
    auto found = resources->GetResourceName(form.GetPdfObject().get());
    EXPECT_TRUE(found && found->GetValue() == "Stamp");
    PdfFormXObject other(Rectangle(0, 0, 5, 5));
    EXPECT_TRUE(resources->AddForm(other, PdfName::Of("Stamp"))->GetValue() !=
                "Stamp");
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(out.ToByteArray()));
  auto xobjects =
      doc.GetPage(1)->GetResources()->GetPdfObject()->GetAsDictionary(
          PdfName::kXObject);
  EXPECT_TRUE(xobjects && xobjects->GetAsStream(PdfName::Of("Stamp")));
}

// BUGS.md #22: excluded keys are put back when cloning fails, and null entries
// print as "null".
TEST(DictionaryCloneRestoresExcludedKeys) {
  auto objects = OnePageObjects();
  // An object stream whose /Length runs past the end of the file: reading its
  // bytes (as cloning does) throws.
  objects[4] =
      "<< /Type /ObjStm /N 0 /First 0 /Title (kept) /Length 999999 >>\n"
      "stream\nabc\nendstream";
  PdfDocument doc(PdfReader::FromBytes(AssemblePdf(objects)));
  auto stream = PdfCast<PdfStream>(doc.GetPdfObject(4));
  EXPECT_TRUE(stream != nullptr);
  if (stream) {
    const PdfDictionary& dict = *stream;
    EXPECT_THROW(dict.Clone(std::vector<PdfNamePtr>{PdfName::kTitle}));
    EXPECT_TRUE(stream->ContainsKey(PdfName::kTitle));
  }
  PdfArray array;
  array.Add(nullptr);
  EXPECT_EQ(array.ToString(), std::string("[null ]"));
  PdfDictionary with_null;
  with_null.Put(PdfName::kTitle, nullptr);
  EXPECT_TRUE(with_null.ToString().find("null") != std::string::npos);
}

// BUGS.md #25: a form XObject whose stream cannot be decoded does not leave
// the nesting depth raised: after many failures, forms are still processed.
TEST(UndecodableFormDoesNotLeakDepth) {
  std::map<int, std::string> objects = {
      {1, "<< /Type /Catalog /Pages 2 0 R >>"},
      {2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"},
      {3,
       "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 5 0 R "
       "/Resources << /XObject << /Bad 7 0 R >> >> >>"},
      {4,
       "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 6 0 R "
       "/Resources << /XObject << /Good 8 0 R >> >> >>"},
      {5, StreamObject("", "/Bad Do")},
      {6, StreamObject("", "/Good Do")},
      {7, StreamObject("/Type /XObject /Subtype /Form /BBox [0 0 10 10] "
                       "/Filter /NoSuchFilter",
                       "abc")},
      {8, StreamObject("/Type /XObject /Subtype /Form /BBox [0 0 100 100] "
                       "/Resources << /Font << /F1 9 0 R >> >>",
                       "BT /F1 12 Tf 10 10 Td (Hi) Tj ET")},
      {9, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"}};
  PdfDocument doc(PdfReader::FromBytes(AssemblePdf(objects)));
  BaselineCollector collector;
  PdfCanvasProcessor processor(collector);
  int failures = 0;
  for (int i = 0; i < 40; ++i) {
    try {
      processor.ProcessPageContent(doc.GetPage(1));
    } catch (const std::exception&) {
      ++failures;
    }
  }
  EXPECT_EQ(failures, 40);
  processor.ProcessPageContent(doc.GetPage(2));
  EXPECT_EQ(static_cast<int>(collector.widths.size()), 1);
}

TEST_MAIN()
