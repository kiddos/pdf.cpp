#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/kernel/colors/Color.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/filters/Filters.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

TEST(byteUtilsFormatsNumbers) {
    EXPECT_EQ(io::ByteUtils::formatDouble(0), std::string("0"));
    EXPECT_EQ(io::ByteUtils::formatDouble(1.5), std::string("1.5"));
    EXPECT_EQ(io::ByteUtils::formatDouble(-0.25), std::string("-0.25"));
    EXPECT_EQ(io::ByteUtils::formatDouble(595), std::string("595"));
    EXPECT_EQ(io::ByteUtils::formatDouble(12.345), std::string("12.35"));
    EXPECT_EQ(io::ByteUtils::formatDouble(100000.4), std::string("100000"));
}

TEST(tokenizerReadsTokens) {
    io::PdfTokenizer tok(io::RandomAccessSourceFactory().createSource(toBytes("<< /Name (str\\)) 12 0 R [1 -2.5] <414243> >>")));
    using TT = io::PdfTokenizer::TokenType;
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::StartDic);
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::Name);
    EXPECT_EQ(tok.getStringValue(), std::string("Name"));
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::String);
    EXPECT_EQ(toLatin1String(tok.getDecodedStringContent()), std::string("str)"));
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::Ref);
    EXPECT_EQ(tok.getObjNr(), 12);
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::StartArray);
    tok.nextValidToken();
    EXPECT_EQ(tok.getIntValue(), 1);
    tok.nextValidToken();
    EXPECT_EQ(tok.getStringValue(), std::string("-2.5"));
    tok.nextValidToken();
    EXPECT_TRUE(tok.getTokenType() == TT::EndArray);
    tok.nextValidToken();
    EXPECT_TRUE(tok.isHexString());
    EXPECT_EQ(toLatin1String(tok.getDecodedStringContent()), std::string("ABC"));
}

TEST(pdfNameEscaping) {
    PdfName n("A B#");
    auto content = n.getInternalContent();
    EXPECT_EQ(toLatin1String(content), std::string("A#20B#23"));
    PdfName parsed(toBytes("A#20B#23"));
    EXPECT_EQ(parsed.getValue(), std::string("A B#"));
    EXPECT_TRUE(PdfName::of("Type").get() == PdfName::Type.get());
}

TEST(pdfStringUnicode) {
    PdfString s("\xE6\x97\xA5\xE6\x9C\xAC", "UnicodeBig");
    Bytes b = s.getValueBytes();
    EXPECT_EQ(static_cast<int>(b[0]), 0xFE);
    PdfString parsed(b);
    // A PdfString from raw bytes decodes as PDF text string
    EXPECT_EQ(parsed.toUnicodeString(), std::string("\xE6\x97\xA5\xE6\x9C\xAC"));
}

static Bytes writeSimplePdf(bool fullCompression, int pages) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos, WriterProperties().setFullCompressionMode(fullCompression));
        PdfDocument doc(writer);
        doc.getDocumentInfo().setTitle("Test title").setAuthor("pdfcpp");
        for (int i = 0; i < pages; ++i) {
            PdfPage* page = doc.addNewPage();
            page->getFirstContentStream()->getOutputStream()->writeString("0 0 m 100 100 l S\n");
        }
        doc.close();
    }
    return baos.toByteArray();
}

TEST(writeAndReadRoundTrip) {
    for (bool full : {false, true}) {
        Bytes pdf = writeSimplePdf(full, 25);
        EXPECT_TRUE(pdf.size() > 100);
        std::string s(pdf.begin(), pdf.end());
        EXPECT_TRUE(s.rfind("%PDF-1.7", 0) == 0);
        EXPECT_TRUE(s.find("%%EOF") != std::string::npos);
        auto reader = PdfReader::fromBytes(pdf);
        PdfDocument doc(std::move(reader));
        EXPECT_EQ(doc.getNumberOfPages(), 25);
        EXPECT_EQ(doc.getDocumentInfo().getTitle(), std::string("Test title"));
        EXPECT_EQ(doc.getDocumentInfo().getAuthor(), std::string("pdfcpp"));
        Rectangle box = doc.getPage(3)->getPageSize();
        EXPECT_NEAR(box.getWidth(), 595, 0.01);
        EXPECT_NEAR(box.getHeight(), 842, 0.01);
        Bytes content = doc.getPage(25)->getContentBytes();
        EXPECT_EQ(std::string(content.begin(), content.end()), std::string("0 0 m 100 100 l S\n"));
        doc.close();
    }
}

TEST(stampingAppendMode) {
    Bytes original = writeSimplePdf(false, 2);
    io::ByteArrayOutputStream out;
    {
        auto reader = PdfReader::fromBytes(original);
        PdfWriter writer(out);
        PdfDocument doc(*reader, writer, StampingProperties().useAppendMode());
        doc.getDocumentInfo().setSubject("appended");
        doc.addNewPage();
        doc.close();
    }
    Bytes result = out.toByteArray();
    EXPECT_TRUE(result.size() > original.size());
    EXPECT_TRUE(std::equal(original.begin(), original.end(), result.begin()));
    auto reader = PdfReader::fromBytes(result);
    PdfDocument doc(std::move(reader));
    EXPECT_EQ(doc.getNumberOfPages(), 3);
    EXPECT_EQ(doc.getDocumentInfo().getSubject(), std::string("appended"));
    EXPECT_EQ(doc.getDocumentInfo().getTitle(), std::string("Test title"));
}

TEST(stampingRewrite) {
    Bytes original = writeSimplePdf(true, 3);
    io::ByteArrayOutputStream out;
    {
        auto reader = PdfReader::fromBytes(original);
        PdfWriter writer(out);
        PdfDocument doc(*reader, writer);
        doc.removePage(2);
        doc.close();
    }
    auto reader = PdfReader::fromBytes(out.toByteArray());
    PdfDocument doc(std::move(reader));
    EXPECT_EQ(doc.getNumberOfPages(), 2);
}

TEST(encryptionRoundTrip) {
    for (int alg : {EncryptionConstants::STANDARD_ENCRYPTION_40, EncryptionConstants::STANDARD_ENCRYPTION_128,
                    EncryptionConstants::ENCRYPTION_AES_128, EncryptionConstants::ENCRYPTION_AES_256}) {
        io::ByteArrayOutputStream baos;
        {
            PdfWriter writer(baos, WriterProperties().setStandardEncryption(toBytes("user"), toBytes("owner"),
                                                                            EncryptionConstants::ALLOW_PRINTING, alg));
            PdfDocument doc(writer);
            doc.getDocumentInfo().setTitle("Secret");
            doc.addNewPage()->getFirstContentStream()->getOutputStream()->writeString("BT ET");
            doc.close();
        }
        Bytes pdf = baos.toByteArray();
        for (const char* pwd : {"user", "owner"}) {
            auto reader = PdfReader::fromBytes(pdf, ReaderProperties().setPassword(pwd));
            PdfDocument doc(std::move(reader));
            EXPECT_EQ(doc.getDocumentInfo().getTitle(), std::string("Secret"));
            Bytes content = doc.getPage(1)->getContentBytes();
            EXPECT_EQ(std::string(content.begin(), content.end()), std::string("BT ET"));
        }
        EXPECT_THROW({
            auto reader = PdfReader::fromBytes(pdf, ReaderProperties().setPassword("wrong"));
            PdfDocument doc(std::move(reader));
        });
    }
}

TEST(filtersDecode) {
    Bytes hex = ASCIIHexDecodeFilter::ASCIIHexDecode(toBytes("48 65 6C6C6F>"));
    EXPECT_EQ(toLatin1String(hex), std::string("Hello"));
    Bytes a85 = ASCII85DecodeFilter::ASCII85Decode(toBytes("87cURD]i,\"Ebo80~>"));
    EXPECT_EQ(toLatin1String(a85), std::string("Hello World!"));
}

TEST(readReferencePdfs) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(pdfcpp_test::referenceDir()) / "kernel/src/test/resources/com/itextpdf/kernel/pdf/PdfReaderTest";
    if (!fs::exists(dir)) return;
    int ok = 0, total = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".pdf") continue;
        ++total;
        try {
            PdfReader reader(entry.path().string());
            PdfDocument doc(reader);
            int n = doc.getNumberOfPages();
            for (int i = 1; i <= n; ++i) doc.getPage(i)->getContentBytes();
            ++ok;
        } catch (const std::exception& e) {
            // some files are intentionally broken or encrypted
            if (std::getenv("PDFCPP_VERBOSE")) std::cout << "    " << entry.path().filename() << ": " << e.what() << "\n";
        }
    }
    std::cout << "    read " << ok << "/" << total << " reference PDFs\n";
    EXPECT_TRUE(total == 0 || ok * 10 >= total * 7);
}


// BUGS.md #15: unused objects flushed at close() under full compression must land in a flushed object stream.
TEST(fullCompressionFlushesUnusedObjects) {
    io::ByteArrayOutputStream baos;
    int unusedNumber = 0;
    {
        PdfWriter writer(baos, WriterProperties().setFullCompressionMode(true));
        PdfDocument doc(writer);
        doc.setFlushUnusedObjects(true);
        doc.addNewPage();
        auto unused = std::make_shared<PdfDictionary>();
        unused->put(PdfName::Title, std::make_shared<PdfString>("orphan"));
        unused->makeIndirect(&doc);
        unusedNumber = unused->getIndirectReference()->getObjNumber();
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    auto obj = pdf_cast<PdfDictionary>(doc.getPdfObject(unusedNumber));
    EXPECT_TRUE(obj != nullptr);
    if (obj) EXPECT_EQ(obj->getAsString(PdfName::Title)->toUnicodeString(), std::string("orphan"));
    doc.close();
}


// BUGS.md #14: malformed LZW streams must not index out of bounds.
TEST(lzwMalformedStreams) {
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
    // clear, clear, then the "next code" special case referencing the empty clear entry
    out = LZWDecodeFilter::LZWDecode(pack9({256, 256, 258, 257}));
    EXPECT_TRUE(out.empty());
    // regular KwKwK case still works: A, then code 258 = "AA"
    out = LZWDecodeFilter::LZWDecode(pack9({256, 0x41, 258, 257}));
    EXPECT_TRUE(out == (Bytes{0x41, 0x41, 0x41}));
}

// BUGS.md #23: negative predictor parameters leave the data untouched instead of misbehaving.
TEST(predictorRejectsBadParameters) {
    Bytes data = {2, 1, 1, 1};
    PdfDictionary parms;
    parms.put(PdfName::Predictor, makeNumber(12));
    parms.put(PdfName::Colors, makeNumber(-1));
    parms.put(PdfName::Columns, makeNumber(-3));
    EXPECT_TRUE(FlateDecodeFilter::decodePredictor(data, &parms) == data);
    parms.put(PdfName::Colors, makeNumber(1));
    parms.put(PdfName::Columns, makeNumber(2000000000));
    EXPECT_TRUE(FlateDecodeFilter::decodePredictor(data, &parms) == data);
    parms.put(PdfName::Columns, makeNumber(3));
    EXPECT_TRUE(FlateDecodeFilter::decodePredictor(data, &parms) == (Bytes{1, 1, 1}));
}

// BUGS.md #13: EMBEDDED_FILES_ONLY writes /Identity string and stream filters (content stays readable).
TEST(embeddedFilesOnlyEncryptionDictionary) {
    for (int algorithm : {EncryptionConstants::ENCRYPTION_AES_128, EncryptionConstants::ENCRYPTION_AES_256}) {
        io::ByteArrayOutputStream baos;
        {
            PdfWriter writer(baos, WriterProperties().setStandardEncryption(
                                       toBytes("user"), toBytes("owner"), EncryptionConstants::ALLOW_PRINTING,
                                       algorithm | EncryptionConstants::EMBEDDED_FILES_ONLY));
            PdfDocument doc(writer);
            doc.getDocumentInfo().setTitle("Clear title");
            doc.addNewPage()->getFirstContentStream()->getOutputStream()->writeString("0 0 m 10 10 l S\n");
            doc.close();
        }
        Bytes pdf = baos.toByteArray();
        // Before the fix the dictionary declared /StdCF filters while strings and streams were written in clear, so
        // reading them back "decrypted" plain text into garbage.
        PdfDocument doc(PdfReader::fromBytes(pdf, ReaderProperties().setPassword("user")));
        auto enc = doc.getTrailer()->getAsDictionary(PdfName::Encrypt);
        EXPECT_TRUE(enc != nullptr);
        if (enc) {
            EXPECT_TRUE(nameEquals(enc->getAsName(PdfName::StmF), PdfName::Identity));
            EXPECT_TRUE(nameEquals(enc->getAsName(PdfName::StrF), PdfName::Identity));
            EXPECT_TRUE(nameEquals(enc->getAsName(PdfName::EFF), PdfName::StdCF));
        }
        EXPECT_EQ(doc.getDocumentInfo().getTitle(), std::string("Clear title"));
        Bytes content = doc.getPage(1)->getContentBytes();
        EXPECT_EQ(std::string(content.begin(), content.end()), std::string("0 0 m 10 10 l S\n"));
        doc.close();
    }
}

// BUGS.md #19: the indexed addPage overload must refuse pages of another document.
TEST(addPageAtIndexRejectsForeignPage) {
    io::ByteArrayOutputStream a, b;
    PdfWriter wa(a), wb(b);
    PdfDocument docA(wa), docB(wb);
    docA.addNewPage();
    docB.addNewPage();
    auto foreign = std::make_shared<PdfPage>(&docB, PageSize::A4);
    EXPECT_THROW(docA.addPage(1, foreign));
    docA.close();
    docB.close();
}

// BUGS.md #21: copying a page keeps markup popups consistent and only drops /P links to source pages.
TEST(copyPageKeepsPopups) {
    io::ByteArrayOutputStream src;
    {
        PdfWriter writer(src);
        PdfDocument doc(writer);
        PdfPage* page = doc.addNewPage();
        auto text = std::make_shared<PdfDictionary>();
        text->put(PdfName::Type, PdfName::Annot);
        text->put(PdfName::Subtype, PdfName::Text);
        text->put(PdfName::Rect, std::make_shared<PdfArray>(Rectangle(100, 100, 20, 20)));
        text->put(PdfName::Contents, std::make_shared<PdfString>("note"));
        text->makeIndirect(&doc);
        auto popup = std::make_shared<PdfDictionary>();
        popup->put(PdfName::Type, PdfName::Annot);
        popup->put(PdfName::Subtype, PdfName::Popup);
        popup->put(PdfName::Rect, std::make_shared<PdfArray>(Rectangle(120, 120, 100, 50)));
        popup->put(PdfName::Parent, text);
        popup->makeIndirect(&doc);
        text->put(PdfName::Popup, popup);
        page->addAnnotation(text);
        page->addAnnotation(popup);
        doc.close();
    }
    io::ByteArrayOutputStream dst;
    {
        PdfDocument from(PdfReader::fromBytes(src.toByteArray()));
        PdfWriter writer(dst);
        PdfDocument to(writer);
        from.copyPagesTo(1, 1, to);
        to.close();
        from.close();
    }
    PdfDocument doc(PdfReader::fromBytes(dst.toByteArray()));
    EXPECT_EQ(doc.getNumberOfPages(), 1);
    auto pageDict = doc.getPage(1)->getPdfObject();
    auto annots = pageDict->getAsArray(PdfName::Annots);
    EXPECT_TRUE(annots && annots->size() == 2);
    if (annots && annots->size() == 2) {
        auto markup = annots->getAsDictionary(0);
        auto popup = annots->getAsDictionary(1);
        EXPECT_TRUE(markup->getAsDictionary(PdfName::Popup).get() == popup.get());
        EXPECT_TRUE(popup->getAsDictionary(PdfName::Parent).get() == markup.get());
        EXPECT_TRUE(markup->getAsDictionary(PdfName::P).get() == pageDict.get());
        EXPECT_TRUE(popup->getAsDictionary(PdfName::P).get() == pageDict.get());
    }
    doc.close();
}

// BUGS.md #24
TEST(makeColorWithShortRgbValues) {
    auto rgb = PdfColorSpace::makeColorSpace(PdfName::DeviceRGB);
    auto c = Color::makeColor(rgb, {0.5f});
    EXPECT_TRUE(c != nullptr);
    auto full = Color::makeColor(rgb, {0.1f, 0.2f, 0.3f});
    EXPECT_TRUE(dynamic_cast<DeviceRgb*>(full.get()) != nullptr);
}

// BUGS.md #26: a zero-extent arc must not write NaN into the content stream.
TEST(zeroExtentArcHasNoNaN) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfCanvas canvas(doc.addNewPage());
        canvas.arc(100, 100, 200, 200, 30, 0).stroke();
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    Bytes content = doc.getPage(1)->getContentBytes();
    std::string s(content.begin(), content.end());
    EXPECT_TRUE(s.find("nan") == std::string::npos && s.find("NaN") == std::string::npos);
    EXPECT_TRUE(s.find(" c") != std::string::npos);
    doc.close();
}

// BUGS.md #27: an empty string has no trailing spacing to subtract from its baseline.
struct BaselineCollector : IEventListener {
    std::vector<float> widths;
    void eventOccurred(const IEventData& data, EventType type) override {
        if (type != EventType::RENDER_TEXT) return;
        const auto& info = static_cast<const TextRenderInfo&>(data);
        auto base = info.getBaseline();
        widths.push_back(base.getEndPoint().get(0) - base.getStartPoint().get(0));
    }
    std::set<EventType> getSupportedEvents() const override { return {EventType::RENDER_TEXT}; }
};

TEST(emptyStringBaselineIsNotNegative) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfCanvas canvas(doc.addNewPage());
        canvas.beginText().setFontAndSize(PdfFontFactory::createFont(), 12).setCharacterSpacing(5).moveText(72, 700);
        canvas.showText("").endText();
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    BaselineCollector collector;
    PdfCanvasProcessor processor(collector);
    processor.processPageContent(doc.getPage(1));
    for (float w : collector.widths) EXPECT_TRUE(w >= 0);
    doc.close();
}

TEST_MAIN()
