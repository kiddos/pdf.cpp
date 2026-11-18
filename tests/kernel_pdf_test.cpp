#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
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

TEST_MAIN()
