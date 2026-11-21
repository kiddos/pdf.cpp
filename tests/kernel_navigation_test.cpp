#include "TestHarness.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/Navigation.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

static Bytes makeDoc(int pages, const std::string& prefix) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        auto font = PdfFontFactory::createFont();
        for (int i = 1; i <= pages; ++i) {
            PdfCanvas canvas(doc.addNewPage());
            canvas.beginText().setFontAndSize(font, 20).moveText(72, 700).showText(prefix + " page " + std::to_string(i)).endText();
        }
        doc.close();
    }
    return baos.toByteArray();
}

TEST(outlinesAndAnnotations) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        PdfPage* p1 = doc.addNewPage();
        PdfPage* p2 = doc.addNewPage();
        auto root = PdfOutline::getOutlines(doc);
        auto chapter = root->addOutline("Chapter 1");
        chapter->addDestination(PdfDestination::createFit(p1));
        auto section = chapter->addOutline("Section 1.1");
        section->addDestination(PdfDestination::createXYZ(p2, 0, 800, 0));
        root->addOutline("Chapter 2")->addAction(PdfAction::createURI("https://example.com"));
        auto link = PdfAnnotation::createLink(Rectangle(72, 700, 100, 20));
        link->setAction(PdfAction::createGoTo(PdfDestination::createFit(p2)));
        p1->addAnnotation(link->getPdfObject());
        auto note = PdfAnnotation::createText(Rectangle(300, 700, 20, 20));
        note->setContents("A sticky note").setTitle("pdfcpp");
        p1->addAnnotation(note->getPdfObject());
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    auto root = PdfOutline::getOutlines(doc);
    EXPECT_EQ(static_cast<int>(root->getAllChildren().size()), 2);
    EXPECT_EQ(root->getAllChildren()[0]->getTitle(), std::string("Chapter 1"));
    EXPECT_EQ(root->getAllChildren()[0]->getAllChildren()[0]->getTitle(), std::string("Section 1.1"));
    EXPECT_EQ(doc.getPage(1)->getAnnotsSize(), 2);
    auto annots = doc.getPage(1)->getAnnotations();
    EXPECT_TRUE(nameEquals(annots[0]->getAsName(PdfName::Subtype), PdfName::Link));
}

TEST(mergeAndSplit) {
    Bytes a = makeDoc(3, "A"), b = makeDoc(2, "B");
    io::ByteArrayOutputStream out;
    {
        PdfWriter writer(out);
        PdfDocument dest(writer);
        PdfMerger merger(dest);
        PdfDocument docA(PdfReader::fromBytes(a));
        PdfDocument docB(PdfReader::fromBytes(b));
        merger.merge(docA, 1, 3).merge(docB, 1, 2);
        merger.close();
    }
    PdfDocument merged(PdfReader::fromBytes(out.toByteArray()));
    EXPECT_EQ(merged.getNumberOfPages(), 5);
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(merged.getPage(4)), std::string("B page 1"));
    PdfSplitter splitter(merged);
    auto parts = splitter.splitByPageCount(2);
    EXPECT_EQ(static_cast<int>(parts.size()), 3);
    PdfDocument last(PdfReader::fromBytes(parts[2]));
    EXPECT_EQ(last.getNumberOfPages(), 1);
    EXPECT_EQ(PdfTextExtractor::getTextFromPage(last.getPage(1)), std::string("B page 2"));
}

TEST(layoutLink) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        layout::Document document(pdf);
        auto p = layout::make<layout::Paragraph>("Visit ");
        p->add(layout::make<layout::Link>("example.com", "https://example.com"));
        document.add(p);
        document.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    EXPECT_EQ(doc.getPage(1)->getAnnotsSize(), 1);
    auto annot = doc.getPage(1)->getAnnotations()[0];
    auto action = annot->getAsDictionary(PdfName::A);
    EXPECT_EQ(action->getAsString(PdfName::URI)->getValue(), std::string("https://example.com"));
}


// BUGS.md #11: removing an outline the caller holds no reference to must not touch freed memory, and /Count must
// count visible descendants at all levels.
TEST(outlineRemoveAndHierarchicalCount) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument doc(writer);
        doc.addNewPage();
        auto root = PdfOutline::getOutlines(doc);
        auto a = root->addOutline("A");
        a->addOutline("A.1")->addOutline("A.1.a");
        a->addOutline("A.2");
        root->addOutline("B");
        // root: A, A.1, A.1.a, A.2, B
        EXPECT_EQ(root->getContent()->getAsNumber(PdfName::Count)->intValue(), 5);
        EXPECT_EQ(a->getContent()->getAsNumber(PdfName::Count)->intValue(), 3);
        a->setOpen(false);
        EXPECT_EQ(a->getContent()->getAsNumber(PdfName::Count)->intValue(), -3);
        EXPECT_EQ(root->getContent()->getAsNumber(PdfName::Count)->intValue(), 2);
        a->setOpen(true);
        // only the parent's children_ owns "A.1" now
        PdfOutline* a1 = a->getAllChildren().front().get();
        a1->removeOutline();
        EXPECT_EQ(static_cast<int>(a->getAllChildren().size()), 1);
        EXPECT_EQ(root->getContent()->getAsNumber(PdfName::Count)->intValue(), 3);
        doc.close();
    }
    PdfDocument doc(PdfReader::fromBytes(baos.toByteArray()));
    auto root = PdfOutline::getOutlines(doc);
    EXPECT_EQ(static_cast<int>(root->getAllChildren().size()), 2);
    EXPECT_EQ(root->getAllChildren()[0]->getAllChildren()[0]->getTitle(), std::string("A.2"));
    doc.close();
}

// BUGS.md #18
TEST(splitByNonPositivePageCountThrows) {
    PdfDocument doc(PdfReader::fromBytes(makeDoc(3, "S")));
    PdfSplitter splitter(doc);
    EXPECT_THROW(splitter.splitByPageCount(0));
    EXPECT_EQ(static_cast<int>(splitter.splitByPageCount(2).size()), 2);
    doc.close();
}

TEST_MAIN()
