#include <filesystem>
#include <functional>
#include <regex>

#include "TestHarness.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/editing/PdfTextEditor.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
namespace fs = std::filesystem;

static std::string fontRes(const std::string& name) {
    return pdfcpp_test::referenceDir() + "/kernel/src/test/resources/com/itextpdf/kernel/fonts/" + name;
}

static std::string parserRes(const std::string& rel) {
    return pdfcpp_test::referenceDir() + "/kernel/src/test/resources/com/itextpdf/kernel/parser/" + rel;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

/// One page whose content stream is `content`; `%F` in it stands for the resource name of `font` (Helvetica if null).
static Bytes makePdf(const std::string& content, std::shared_ptr<PdfFont> font = nullptr) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfPage* page = doc.addNewPage();
        if (!font) font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
        PdfNamePtr name = page->getResources()->addFont(&doc, font.get());
        // let the font know which glyphs are used so a subset gets them
        if (content.find("%T") != std::string::npos) {
            auto start = content.find("%T"), end = content.find("%T", start + 2);
            std::string text = content.substr(start + 2, end - start - 2);
            Bytes encoded = font->convertToBytes(text);
            std::string hex = "<";
            static const char* digits = "0123456789ABCDEF";
            for (uint8_t b : encoded) {
                hex += digits[b >> 4];
                hex += digits[b & 15];
            }
            hex += ">";
            std::string c = content.substr(0, start) + hex + content.substr(end + 2);
            page->getFirstContentStream()->setData(toBytes(replaceAll(c, "%F", "/" + name->getValue())));
        } else {
            page->getFirstContentStream()->setData(toBytes(replaceAll(content, "%F", "/" + name->getValue())));
        }
        doc.close();
    }
    return baos.toByteArray();
}

static Bytes editPdf(const Bytes& in, const std::function<void(PdfDocument&)>& fn, bool append = false) {
    io::ByteArrayOutputStream baos;
    {
        StampingProperties props;
        if (append) props.useAppendMode();
        PdfDocument doc(PdfReader::fromBytes(in), std::make_unique<PdfWriter>(baos), props);
        fn(doc);
        doc.close();
    }
    return baos.toByteArray();
}

static std::string pageText(const Bytes& pdf, int page = 1) {
    PdfDocument doc(PdfReader::fromBytes(pdf));
    return PdfTextExtractor::getTextFromPage(doc.getPage(page));
}

static std::string contentOf(const Bytes& pdf) {
    PdfDocument doc(PdfReader::fromBytes(pdf));
    Bytes b = doc.getPage(1)->getContentBytes();
    return std::string(b.begin(), b.end());
}

/// Start x (user space) of every shown string, in content order.
struct StartCollector : IEventListener {
    std::vector<std::pair<std::string, float>> starts;
    void eventOccurred(const IEventData& data, EventType type) override {
        if (type != EventType::RENDER_TEXT) return;
        const auto& info = static_cast<const TextRenderInfo&>(data);
        starts.emplace_back(info.getText(), info.getBaseline().getStartPoint().get(0));
    }
    std::set<EventType> getSupportedEvents() const override { return {EventType::RENDER_TEXT}; }
};

static std::vector<std::pair<std::string, float>> starts(const Bytes& pdf) {
    PdfDocument doc(PdfReader::fromBytes(pdf));
    StartCollector collector;
    PdfCanvasProcessor processor(collector);
    processor.processPageContent(doc.getPage(1));
    return collector.starts;
}

TEST(replaceInSingleTj) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Hello World) Tj ET");
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("World", "There"); });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(r.skipped, 0);
    EXPECT_EQ(r.usedFallbackFont, 0);
    EXPECT_EQ(pageText(out), std::string("Hello There"));
}

TEST(replaceKeepsUntouchedContentBytes) {
    std::string content = "q 1 0 0 1 0 0 cm\n% a comment\nBT %F 12 Tf 72 700 Td (keep) Tj ET\nBT %F 12 Tf 72 680 Td (change me) Tj ET\nQ";
    Bytes pdf = makePdf(content);
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("change", "edited"); });
    std::string c = contentOf(out);
    EXPECT_TRUE(c.find("% a comment\nBT ") != std::string::npos);
    EXPECT_TRUE(c.find("(keep) Tj ET") != std::string::npos);
    EXPECT_TRUE(c.find("(edited me) Tj") != std::string::npos);
    EXPECT_EQ(pageText(out), std::string("keep\nedited me"));
}

TEST(replaceAcrossTjKerning) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td [(Hel) 20 (lo W) -15 (orld)] TJ ET");
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("lo World", "p me"); });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(pageText(out), std::string("Help me"));
}

TEST(virtualSeparatorsMatchSeparateOperators) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Hello) Tj 40 0 Td (World) Tj 0 -14 Td (Next) Tj ET");
    {
        PdfDocument doc(PdfReader::fromBytes(pdf));
        auto blocks = PdfTextEditor(doc).getTextBlocks(doc.getPage(1));
        EXPECT_EQ(blocks.size(), size_t(1));
        EXPECT_EQ(blocks[0], std::string("Hello World\nNext"));
    }
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("Hello World", "Goodbye"); });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(pageText(out), std::string("Goodbye\nNext"));
}

TEST(tjWordGapIsAVirtualSpace) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td [(Hello) -300 (World)] TJ ET");
    PdfDocument doc(PdfReader::fromBytes(pdf));
    auto blocks = PdfTextEditor(doc).getTextBlocks(doc.getPage(1));
    EXPECT_EQ(blocks.size(), size_t(1));
    EXPECT_EQ(blocks[0], std::string("Hello World"));
}

TEST(quoteOperatorsAreRewrittenWithTheirLineMove) {
    Bytes pdf = makePdf("BT %F 12 Tf 14 TL 72 700 Td (first) Tj (second line) ' 1 0.5 (third line) \" ET");
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("line", "row"); });
    EXPECT_EQ(pageText(out), std::string("first\nsecond row\nthird row"));
    std::string c = contentOf(out);
    EXPECT_TRUE(c.find("T*\n(second row) Tj") != std::string::npos);
    EXPECT_TRUE(c.find("1 Tw 0.5 Tc T*\n(third row) Tj") != std::string::npos);
}

TEST(regexReplacement) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Due 2023-07, paid 2024-01) Tj ET");
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) {
        r = PdfTextEditor(doc).replaceRegex(std::regex("(\\d{4})-(\\d{2})"), "$2/$1");
    });
    EXPECT_EQ(r.replaced, 2);
    EXPECT_EQ(pageText(out), std::string("Due 07/2023, paid 01/2024"));
}

TEST(deleteText) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Secret: 1234 end) Tj ET");
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("1234 ", ""); });
    EXPECT_EQ(pageText(out), std::string("Secret: end"));
}

TEST(preserveLayoutKeepsFollowingTextInPlace) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td [(Total: 5 items) -2000 (end)] TJ ET");
    auto before = starts(pdf);
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) {
        PdfTextEditor(doc).setPreserveLayout(true).replaceText("5", "12345");
    });
    auto after = starts(out);
    EXPECT_EQ(pageText(out), std::string("Total: 12345 items end"));
    EXPECT_TRUE(!before.empty() && !after.empty());
    // "end" starts at the same place; so does " items"
    EXPECT_EQ(before.back().first, std::string("end"));
    EXPECT_EQ(after.back().first, std::string("end"));
    EXPECT_TRUE(std::fabs(before.back().second - after.back().second) < 0.01f);

    // without preserveLayout the following text moves by the width difference
    Bytes moved = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("5", "12345"); });
    auto movedStarts = starts(moved);
    EXPECT_TRUE(movedStarts.back().second > before.back().second + 20);
}

TEST(nothingMatchedLeavesDocumentUnchanged) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Hello) Tj ET");
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("absent", "x"); });
    EXPECT_EQ(r.replaced, 0);
    EXPECT_EQ(contentOf(out), contentOf(pdf));
}

TEST(appendModeKeepsOriginalBytes) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (Version 1) Tj ET");
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("1", "2"); }, true);
    EXPECT_TRUE(out.size() > pdf.size());
    EXPECT_TRUE(std::equal(pdf.begin(), pdf.end(), out.begin()));
    EXPECT_EQ(pageText(out), std::string("Version 2"));
}

TEST(fallbackFontForUnencodableCharacters) {
    // WinAnsi Helvetica cannot show Cyrillic; the fallback (a TrueType font) can
    std::string ttf = fontRes("PTSerif-Regular.ttf");
    if (!fs::exists(ttf)) return;
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (angle alpha here) Tj ET");
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) {
        auto fallback = PdfFontFactory::createFont(ttf, io::PdfEncodings::IDENTITY_H);
        r = PdfTextEditor(doc).setFallbackFont(fallback).replaceText("alpha", "\xD0\xB4=1");
    });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(r.usedFallbackFont, 1);
    EXPECT_EQ(pageText(out), std::string("angle \xD0\xB4=1 here"));
    std::string c = contentOf(out);
    // the original font is restored after the fallback run
    EXPECT_TRUE(c.find("Tf\n(=1 here) Tj") != std::string::npos || c.find("Tf (=1 here) Tj") != std::string::npos ||
                c.find("Tf\n[(=1 here)] TJ") != std::string::npos);

    // without a usable font the match is skipped (the default Helvetica fallback has no Greek)
    Bytes skipped = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("alpha", "\xCE\xB1"); });
    EXPECT_EQ(r.replaced, 0);
    EXPECT_EQ(r.skipped, 1);
    EXPECT_EQ(pageText(skipped), std::string("angle alpha here"));
}

TEST(subsetFontOnlyReusesGlyphsItContains) {
    std::string ttf = fontRes("PTSerif-Regular.ttf");
    if (!fs::exists(ttf)) return;
    auto font = PdfFontFactory::createFont(ttf, io::PdfEncodings::IDENTITY_H);
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td %Tcab here%T Tj ET", font);
    EXPECT_EQ(pageText(pdf), std::string("cab here"));

    // "bac" only needs glyphs already in the subset
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("cab", "bac"); });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(r.usedFallbackFont, 0);
    EXPECT_EQ(pageText(out), std::string("bac here"));

    // "z" is not in the subset: it comes from the fallback font
    Bytes withZ = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("cab", "zab"); });
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(r.usedFallbackFont, 1);
    EXPECT_EQ(pageText(withZ), std::string("zab here"));
    PdfDocument check(PdfReader::fromBytes(withZ));
    auto fonts = check.getPage(1)->getResources()->getResource(PdfName::Font);
    EXPECT_EQ(fonts->size(), 2);
}

TEST(editsTextInFormXObjects) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfFormXObject form(Rectangle(0, 0, 200, 50));
        PdfCanvas formCanvas(form, &doc);
        auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
        formCanvas.beginText().setFontAndSize(font, 12).moveText(5, 20).showText("Draft copy").endText();
        for (int i = 0; i < 2; ++i) {
            PdfCanvas canvas(doc.addNewPage());
            canvas.addXObjectAt(form, 72, 700);
        }
        doc.close();
    }
    Bytes pdf = baos.toByteArray();
    TextEditResult r;
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText("Draft", "Final"); });
    EXPECT_EQ(r.replaced, 1);  // the shared form is edited once
    EXPECT_EQ(pageText(out, 1), std::string("Final copy"));
    EXPECT_EQ(pageText(out, 2), std::string("Final copy"));

    Bytes skipped = editPdf(pdf, [&](PdfDocument& doc) {
        r = PdfTextEditor(doc).setProcessFormXObjects(false).replaceText("Draft", "Final");
    });
    EXPECT_EQ(r.replaced, 0);
    EXPECT_EQ(pageText(skipped, 1), std::string("Draft copy"));
}

TEST(multipleContentStreamsAreMerged) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfPage* page = doc.addNewPage();
        auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
        PdfNamePtr name = page->getResources()->addFont(&doc, font.get());
        // streams split at a token boundary, with the BT block spanning both
        page->getFirstContentStream()->setData(toBytes("BT /" + name->getValue() + " 12 Tf 72 700 Td (first part)"));
        page->newContentStreamAfter()->setData(toBytes("Tj ET"));
        doc.close();
    }
    Bytes pdf = baos.toByteArray();
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) { PdfTextEditor(doc).replaceText("first part", "whole"); });
    EXPECT_EQ(pageText(out), std::string("whole"));
}

TEST(customFinder) {
    Bytes pdf = makePdf("BT %F 12 Tf 72 700 Td (abc) Tj ET BT %F 12 Tf 72 680 Td (def) Tj ET");
    Bytes out = editPdf(pdf, [&](PdfDocument& doc) {
        PdfTextEditor(doc).edit([](const std::string& text) {
            // upper-case the first letter of every block
            return std::vector<PdfTextEditor::Replacement>{{0, 1, std::string(1, static_cast<char>(text[0] - 32))}};
        });
    });
    EXPECT_EQ(pageText(out), std::string("Abc\nDef"));
}

// Edits real-world files from the iText parser tests: every match must disappear from the extracted text.
TEST(editReferenceParserPdfs) {
    const char* files[] = {"PdfTextExtractorTest/test.pdf", "SimpleFontToUnicodeExtractionTest/simpleFontToUnicode01.pdf",
                           "PdfTextExtractorUnicodeIdentityTest/user10.pdf"};
    for (const char* rel : files) {
        std::string path = parserRes(rel);
        if (!fs::exists(path)) continue;
        Bytes in = io::StreamUtil::readFile(path);
        std::string before;
        {
            PdfDocument doc(PdfReader::fromBytes(in));
            before = PdfTextExtractor::getTextFromPage(doc.getPage(1));
        }
        // pick the first word of 4+ letters
        std::smatch m;
        std::string word;
        if (std::regex_search(before, m, std::regex("[A-Za-z]{4,}"))) word = m.str();
        if (word.empty()) continue;
        TextEditResult r;
        Bytes out = editPdf(in, [&](PdfDocument& doc) { r = PdfTextEditor(doc).replaceText(doc.getPage(1), word, "ZZ"); });
        std::string after = pageText(out);
        EXPECT_TRUE(r.replaced > 0);
        EXPECT_TRUE(after.find(word) == std::string::npos || r.skipped > 0);
        EXPECT_TRUE(after.find("ZZ") != std::string::npos);
    }
}

TEST_MAIN()
