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

TEST_MAIN()
