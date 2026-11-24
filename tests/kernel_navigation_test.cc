#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

static Bytes MakeDoc(int pages, const std::string& prefix) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    auto font = PdfFontFactory::CreateFont();
    for (int i = 1; i <= pages; ++i) {
      PdfCanvas canvas(doc.AddNewPage());
      canvas.BeginText()
          .SetFontAndSize(font, 20)
          .MoveText(72, 700)
          .ShowText(prefix + " page " + std::to_string(i))
          .EndText();
    }
    doc.Close();
  }
  return baos.ToByteArray();
}

TEST(OutlinesAndAnnotations) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    PdfPage* p1 = doc.AddNewPage();
    PdfPage* p2 = doc.AddNewPage();
    auto root = PdfOutline::GetOutlines(doc);
    auto chapter = root->AddOutline("Chapter 1");
    chapter->AddDestination(PdfDestination::CreateFit(p1));
    auto section = chapter->AddOutline("Section 1.1");
    section->AddDestination(PdfDestination::CreateXyz(p2, 0, 800, 0));
    root->AddOutline("Chapter 2")
        ->AddAction(PdfAction::CreateUri("https://example.com"));
    auto link = PdfAnnotation::CreateLink(Rectangle(72, 700, 100, 20));
    link->SetAction(PdfAction::CreateGoTo(PdfDestination::CreateFit(p2)));
    p1->AddAnnotation(link->GetPdfObject());
    auto note = PdfAnnotation::CreateText(Rectangle(300, 700, 20, 20));
    note->SetContents("A sticky note").SetTitle("pdfcpp");
    p1->AddAnnotation(note->GetPdfObject());
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  auto root = PdfOutline::GetOutlines(doc);
  EXPECT_EQ(static_cast<int>(root->GetAllChildren().size()), 2);
  EXPECT_EQ(root->GetAllChildren()[0]->GetTitle(), std::string("Chapter 1"));
  EXPECT_EQ(root->GetAllChildren()[0]->GetAllChildren()[0]->GetTitle(),
            std::string("Section 1.1"));
  EXPECT_EQ(doc.GetPage(1)->GetAnnotsSize(), 2);
  auto annots = doc.GetPage(1)->GetAnnotations();
  EXPECT_TRUE(
      NameEquals(annots[0]->GetAsName(PdfName::kSubtype), PdfName::kLink));
}

TEST(MergeAndSplit) {
  Bytes a = MakeDoc(3, "A"), b = MakeDoc(2, "B");
  io::ByteArrayOutputStream out;
  {
    PdfWriter writer(out);
    PdfDocument dest(writer);
    PdfMerger merger(dest);
    PdfDocument doc_a(PdfReader::FromBytes(a));
    PdfDocument doc_b(PdfReader::FromBytes(b));
    merger.Merge(doc_a, 1, 3).Merge(doc_b, 1, 2);
    merger.Close();
  }
  PdfDocument merged(PdfReader::FromBytes(out.ToByteArray()));
  EXPECT_EQ(merged.GetNumberOfPages(), 5);
  EXPECT_EQ(PdfTextExtractor::GetTextFromPage(merged.GetPage(4)),
            std::string("B page 1"));
  PdfSplitter splitter(merged);
  auto parts = splitter.SplitByPageCount(2);
  EXPECT_EQ(static_cast<int>(parts.size()), 3);
  PdfDocument last(PdfReader::FromBytes(parts[2]));
  EXPECT_EQ(last.GetNumberOfPages(), 1);
  EXPECT_EQ(PdfTextExtractor::GetTextFromPage(last.GetPage(1)),
            std::string("B page 2"));
}

TEST(LayoutLink) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    layout::Document document(pdf);
    auto p = layout::Make<layout::Paragraph>("Visit ");
    p->Add(layout::Make<layout::Link>("example.com", "https://example.com"));
    document.Add(p);
    document.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  EXPECT_EQ(doc.GetPage(1)->GetAnnotsSize(), 1);
  auto annot = doc.GetPage(1)->GetAnnotations()[0];
  auto action = annot->GetAsDictionary(PdfName::kA);
  EXPECT_EQ(action->GetAsString(PdfName::kURI)->GetValue(),
            std::string("https://example.com"));
}

// BUGS.md #11: removing an outline the caller holds no reference to must not
// touch freed memory, and /Count must count visible descendants at all levels.
TEST(OutlineRemoveAndHierarchicalCount) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument doc(writer);
    doc.AddNewPage();
    auto root = PdfOutline::GetOutlines(doc);
    auto a = root->AddOutline("A");
    a->AddOutline("A.1")->AddOutline("A.1.a");
    a->AddOutline("A.2");
    root->AddOutline("B");
    // root: A, A.1, A.1.a, A.2, B
    EXPECT_EQ(root->GetContent()->GetAsNumber(PdfName::kCount)->IntValue(), 5);
    EXPECT_EQ(a->GetContent()->GetAsNumber(PdfName::kCount)->IntValue(), 3);
    a->SetOpen(false);
    EXPECT_EQ(a->GetContent()->GetAsNumber(PdfName::kCount)->IntValue(), -3);
    EXPECT_EQ(root->GetContent()->GetAsNumber(PdfName::kCount)->IntValue(), 2);
    a->SetOpen(true);
    // only the parent's children_ owns "A.1" now
    PdfOutline* a1 = a->GetAllChildren().front().get();
    a1->RemoveOutline();
    EXPECT_EQ(static_cast<int>(a->GetAllChildren().size()), 1);
    EXPECT_EQ(root->GetContent()->GetAsNumber(PdfName::kCount)->IntValue(), 3);
    doc.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  auto root = PdfOutline::GetOutlines(doc);
  EXPECT_EQ(static_cast<int>(root->GetAllChildren().size()), 2);
  EXPECT_EQ(root->GetAllChildren()[0]->GetAllChildren()[0]->GetTitle(),
            std::string("A.2"));
  doc.Close();
}

// BUGS.md #18
TEST(SplitByNonPositivePageCountThrows) {
  PdfDocument doc(PdfReader::FromBytes(MakeDoc(3, "S")));
  PdfSplitter splitter(doc);
  EXPECT_THROW(splitter.SplitByPageCount(0));
  EXPECT_EQ(static_cast<int>(splitter.SplitByPageCount(2).size()), 2);
  doc.Close();
}

TEST_MAIN()
