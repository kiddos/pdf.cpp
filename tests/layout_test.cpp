#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::layout;
namespace fs = std::filesystem;

static const char* LOREM =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore "
    "magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

TEST(simpleParagraphs) {
    std::string path = "layout_paragraphs.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf);
        auto bold = PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
        document.add(make<Paragraph>("pdfcpp layout")->setFont(bold).setFontSize(24).setTextAlignment(TextAlignment::CENTER));
        for (int i = 0; i < 12; ++i) {
            auto p = make<Paragraph>();
            p->add(make<Text>("Paragraph " + std::to_string(i + 1) + ". ")).add(make<Text>(LOREM));
            p->setTextAlignment(i % 2 ? TextAlignment::JUSTIFIED : TextAlignment::LEFT);
            if (i == 3) p->setFontColor(ColorConstants::BLUE).setBackgroundColor(ColorConstants::LIGHT_GRAY);
            if (i == 4) p->setBorder(std::make_shared<SolidBorder>(ColorConstants::RED, 1)).setPadding(5);
            document.add(p);
        }
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    EXPECT_TRUE(doc.getNumberOfPages() >= 2);
    std::string text = PdfTextExtractor::getTextFromPage(doc.getPage(1));
    EXPECT_TRUE(text.find("pdfcpp layout") != std::string::npos);
    EXPECT_TRUE(text.find("Paragraph 1. Lorem ipsum") != std::string::npos);
}

TEST(listsTablesImages) {
    std::string path = "layout_complex.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        document.add(make<Paragraph>("Lists")->setFontSize(18).simulateBold());
        auto list = make<List>();
        list->add("First item").add("Second item with a much longer text that will certainly wrap onto another line because it is long").add("Third");
        document.add(list);
        auto numbered = make<List>(ListNumberingType::ROMAN_UPPER);
        numbered->add("Alpha").add("Beta").add("Gamma");
        document.add(numbered);
        document.add(make<LineSeparator>(std::make_shared<SolidLine>(1)));
        document.add(make<Paragraph>("Table")->setFontSize(18).simulateBold());
        auto table = make<Table>(UnitValue::createPercentArray({1, 2, 2}));
        table->useAllAvailableWidth();
        table->addHeaderCell(make<Cell>()->add("#").setBackgroundColor(ColorConstants::LIGHT_GRAY));
        table->addHeaderCell("Name");
        table->addHeaderCell("Description");
        for (int i = 1; i <= 60; ++i) {
            table->addCell(std::to_string(i));
            table->addCell("Row " + std::to_string(i));
            table->addCell(i % 7 == 0 ? std::string(LOREM).substr(0, 140) : "Short");
        }
        auto span = make<Cell>(1, 3);
        span->add("A cell spanning three columns").setTextAlignment(TextAlignment::CENTER);
        table->addCell(span);
        document.add(table);
        std::string png = pdfcpp_test::referenceDir() + "/pdfa/src/test/resources/com/itextpdf/pdfa/itext.png";
        if (fs::exists(png)) {
            auto img = make<Image>(io::ImageDataFactory::create(png));
            img->scaleToFit(200, 200).setHorizontalAlignment(HorizontalAlignment::CENTER);
            document.add(img);
        }
        document.showTextAligned("Rotated watermark", 300, 400, TextAlignment::CENTER, VerticalAlignment::MIDDLE, 0.785f);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    EXPECT_TRUE(doc.getNumberOfPages() >= 2);
    std::string p1 = PdfTextExtractor::getTextFromPage(doc.getPage(1));
    EXPECT_TRUE(p1.find("First item") != std::string::npos);
    EXPECT_TRUE(p1.find("Name") != std::string::npos);
    std::string p2 = PdfTextExtractor::getTextFromPage(doc.getPage(2));
    // header repeats on the second page
    EXPECT_TRUE(p2.find("Description") != std::string::npos);
}


namespace {
struct Word {
    std::string text;
    float x, y;
};
struct WordCollector : IEventListener {
    std::vector<Word> words;
    void eventOccurred(const IEventData& data, EventType type) override {
        if (type != EventType::RENDER_TEXT) return;
        const auto& info = static_cast<const TextRenderInfo&>(data);
        auto start = info.getBaseline().getStartPoint();
        words.push_back({info.getText(), start.get(0), start.get(1)});
    }
    std::set<EventType> getSupportedEvents() const override { return {EventType::RENDER_TEXT}; }
};

std::vector<Word> wordsOnPage(PdfDocument& doc, int page) {
    WordCollector collector;
    PdfCanvasProcessor processor(collector);
    processor.processPageContent(doc.getPage(page));
    return collector.words;
}

const Word* findWord(const std::vector<Word>& words, const std::string& text) {
    for (const auto& w : words)
        if (w.text.find(text) != std::string::npos) return &w;
    return nullptr;
}

int countOccurrences(PdfDocument& doc, const std::string& needle) {
    int n = 0;
    for (int p = 1; p <= doc.getNumberOfPages(); ++p) {
        std::string text = PdfTextExtractor::getTextFromPage(doc.getPage(p));
        for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) ++n;
    }
    return n;
}
}  // namespace

// BUGS.md #33: rows of a row group with a rowspan are stacked, not drawn on top of each other.
TEST(tableRowspanRowsAreStacked) {
    std::string path = "layout_rowspan.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf);
        auto t = make<Table>(UnitValue::createPercentArray({1, 1}));
        t->addCell(make<Cell>(3, 1)->add("SPAN"));
        t->addCell("ROWA");
        t->addCell("ROWB");
        t->addCell("ROWC");
        document.add(t);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    auto words = wordsOnPage(doc, 1);
    auto a = findWord(words, "ROWA"), b = findWord(words, "ROWB"), c = findWord(words, "ROWC");
    EXPECT_TRUE(a && b && c);
    if (a && b && c) {
        EXPECT_TRUE(a->y - b->y > 10);
        EXPECT_TRUE(b->y - c->y > 10);
    }
}

// BUGS.md #34 and the related split duplication: cells completed before a row split are not repeated on the next
// page, a first row that does not fit is split in place, and header cells keep their column order.
TEST(tableSplitting) {
    std::string path = "layout_table_split.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        auto t = make<Table>(UnitValue::createPercentArray({1, 1}));
        t->addHeaderCell("HEADLEFT");
        t->addHeaderCell("HEADRIGHT");
        t->addCell("SHORTCELL");
        std::string longText;
        for (int i = 0; i < 400; ++i) longText += "word" + std::to_string(i) + " ";
        t->addCell(longText);
        document.add(t);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    EXPECT_EQ(doc.getNumberOfPages(), 2);
    EXPECT_EQ(countOccurrences(doc, "SHORTCELL"), 1);
    EXPECT_EQ(countOccurrences(doc, "word399"), 1);
    auto words = wordsOnPage(doc, 1);
    auto left = findWord(words, "HEADLEFT"), right = findWord(words, "HEADRIGHT");
    EXPECT_TRUE(left && right && left->x < right->x);
}

TEST(tableFirstRowSplitsInPlace) {
    std::string path = "layout_table_first_row.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        for (int i = 0; i < 26; ++i) document.add(make<Paragraph>("filler " + std::to_string(i)));
        auto t = make<Table>(UnitValue::createPercentArray({1}));
        std::string longText;
        for (int i = 0; i < 300; ++i) longText += "w" + std::to_string(i) + " ";
        t->addCell(longText);
        document.add(t);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    EXPECT_TRUE(PdfTextExtractor::getTextFromPage(doc.getPage(1)).find("w0 ") != std::string::npos);
}

TEST(tableHeaderAndFooterTallerThanPageKeepsRows) {
    std::string path = "layout_table_huge_header.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        auto t = make<Table>(UnitValue::createPercentArray({1, 1}));
        for (int i = 0; i < 40; ++i) t->addHeaderCell("H" + std::to_string(i));
        for (int i = 0; i < 40; ++i) t->addFooterCell("F" + std::to_string(i));
        for (int i = 0; i < 6; ++i) t->addCell("BODY" + std::to_string(i));
        document.add(t);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    for (int i = 0; i < 6; ++i) EXPECT_EQ(countOccurrences(doc, "BODY" + std::to_string(i)), 1);
}

// BUGS.md #35: the symbol of a list item split across pages stays left of the text.
TEST(listItemSplitKeepsSymbolLeft) {
    std::string path = "layout_list_split.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        for (int i = 0; i < 25; ++i) document.add(make<Paragraph>("filler " + std::to_string(i)));
        auto list = make<List>(ListNumberingType::DECIMAL);
        std::string longText;
        for (int i = 0; i < 300; ++i) longText += "w" + std::to_string(i) + " ";
        list->add(longText);
        document.add(list);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    EXPECT_EQ(doc.getNumberOfPages(), 2);
    auto words = wordsOnPage(doc, 1);
    auto symbol = findWord(words, "1."), text = findWord(words, "w0");
    EXPECT_TRUE(symbol && text);
    if (symbol && text) EXPECT_TRUE(symbol->x + 5 < text->x);
}

// BUGS.md #36: a percentage width given to setFixedPosition is applied once.
TEST(fixedPositionPercentWidth) {
    std::string path = "layout_fixed_width.pdf";
    {
        PdfDocument pdf(std::make_unique<PdfWriter>(path));
        Document document(pdf, PageSize::A4);
        auto p = make<Paragraph>("RIGHTALIGNED");
        p->setTextAlignment(TextAlignment::RIGHT);
        p->setFixedPosition(100, 500, UnitValue::createPercentValue(50));
        document.add(p);
        document.close();
    }
    PdfDocument doc(std::make_unique<PdfReader>(path));
    auto words = wordsOnPage(doc, 1);
    auto w = findWord(words, "RIGHTALIGNED");
    EXPECT_TRUE(w != nullptr);
    // 50% of the 523pt wide area: the ~90pt wide text ends at x = 100 + 261.5, i.e. starts near 271 (applying the
    // percentage twice made it end at 100 + 130.75, starting near 140)
    if (w) EXPECT_NEAR(w->x, 271.5f, 3.f);
}

// BUGS.md #37: a zero-sized form XObject image does not produce NaN/Inf sizes.
TEST(zeroSizedImage) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        Document document(pdf);
        auto xobject = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 0, 0));
        auto img = make<Image>(xobject);
        img->scaleToFit(100, 100);
        img->setWidth(50);
        document.add(img);
        document.close();
    }
    Bytes pdf = baos.toByteArray();
    std::string s(pdf.begin(), pdf.end());
    EXPECT_TRUE(s.find("nan") == std::string::npos && s.find("inf") == std::string::npos);
}

TEST_MAIN()
