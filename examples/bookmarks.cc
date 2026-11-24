// Document navigation: a clickable table of contents, nested bookmarks
// (outlines), "back to contents" links and a URI action.
//
// The table of contents is drawn on page 1 after all chapters are laid out,
// when their page numbers are known. That is possible because the Document
// is created with immediate_flush = false, so earlier pages stay editable.
#include <iostream>
#include <vector>

#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;
namespace colors = kernel::color_constants;

namespace {

struct Entry {
  std::string title;
  int page;
  int level;  // 0 = chapter, 1 = section
};

const char* const kBody =
    "This paragraph is filler text that makes the chapters long enough to "
    "spread over several pages, so that the table of contents and the "
    "bookmarks point to different pages. ";

// A link annotation over `rect` that jumps to `target`.
void AddLink(kernel::PdfPage* page, const kernel::Rectangle& rect,
             kernel::PdfPage* target) {
  auto link = kernel::PdfAnnotation::CreateLink(rect);
  link->SetDestination(kernel::PdfDestination::CreateFit(target))
      .SetBorder(0, 0, 0)
      .SetHighlightMode(kernel::PdfName::kI);
  page->AddAnnotation(link->GetPdfObject());
}

}  // namespace

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "bookmarks.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  pdf.GetDocumentInfo().SetTitle("Bookmarks and links");
  Document document(pdf, kernel::PageSize::kA4, /*immediate_flush=*/false);
  auto bold =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);

  // Page 1 holds the title; the contents are filled in at the end.
  document.Add(Make<Paragraph>("Field Guide")
                   ->SetFont(bold)
                   .SetFontSize(28)
                   .SetTextAlignment(TextAlignment::kCenter)
                   .SetMarginTop(40));
  document.Add(Make<Paragraph>("Contents")->SetFont(bold).SetFontSize(16));

  // Chapters with sections; record the page each heading lands on.
  std::vector<Entry> entries;
  const std::vector<std::pair<std::string, std::vector<std::string>>> chapters =
      {
          {"Birds", {"Songbirds", "Raptors", "Waterfowl"}},
          {"Mammals", {"Rodents", "Carnivores"}},
          {"Insects", {"Beetles", "Butterflies", "Bees", "Ants"}},
      };
  for (size_t c = 0; c < chapters.size(); ++c) {
    document.Add(Make<AreaBreak>(AreaBreakType::kNextPage));
    std::string title =
        "Chapter " + std::to_string(c + 1) + ": " + chapters[c].first;
    document.Add(Make<Paragraph>(title)->SetFont(bold).SetFontSize(20));
    entries.push_back({title, pdf.GetNumberOfPages(), 0});
    for (const std::string& section : chapters[c].second) {
      document.Add(Make<Paragraph>(section)
                       ->SetFont(bold)
                       .SetFontSize(14)
                       .SetMarginTop(10)
                       .SetKeepWithNext(true));
      entries.push_back({section, pdf.GetNumberOfPages(), 1});
      std::string body;
      for (int i = 0; i < 12; ++i) body += kBody;
      document.Add(
          Make<Paragraph>(body)->SetTextAlignment(TextAlignment::kJustified));
    }
  }

  // Table of contents on page 1: title, dotted leader, page number, and a
  // link annotation covering the whole line.
  kernel::PdfPage* toc_page = pdf.GetPage(1);
  float y = 650;
  for (const Entry& e : entries) {
    float indent = e.level == 0 ? 0 : 20;
    auto line = Make<Paragraph>(e.title);
    line->SetFontSize(e.level == 0 ? 12 : 11);
    if (e.level == 0) line->SetFont(bold);
    document.ShowTextAligned(line, 60 + indent, y, 1, TextAlignment::kLeft,
                             VerticalAlignment::kBottom, 0);
    auto page_number = Make<Paragraph>(std::to_string(e.page));
    page_number->SetFontSize(11);
    document.ShowTextAligned(page_number, 535, y, 1, TextAlignment::kRight,
                             VerticalAlignment::kBottom, 0);
    document.Add(Make<LineSeparator>(std::make_shared<DottedLine>(0.6f, 3))
                     ->SetFixedPosition(1, 220, y + 3, 290));
    AddLink(toc_page, kernel::Rectangle(55 + indent, y - 3, 485 - indent, 17),
            pdf.GetPage(e.page));
    y -= e.level == 0 ? 26 : 19;
  }

  // "Back to contents" link at the foot of every other page.
  for (int i = 2; i <= pdf.GetNumberOfPages(); ++i) {
    auto back = Make<Paragraph>("Back to contents");
    back->SetFontSize(9).SetFontColor(colors::kBlue);
    document.ShowTextAligned(back, 36, 20, i, TextAlignment::kLeft,
                             VerticalAlignment::kBottom, 0);
    AddLink(pdf.GetPage(i), kernel::Rectangle(34, 18, 80, 14), toc_page);
  }

  // Bookmarks: one per chapter (bold, colored) with nested sections. The
  // last chapter starts collapsed.
  auto root = kernel::PdfOutline::GetOutlines(pdf);
  root->AddOutline("Contents")
      ->AddDestination(kernel::PdfDestination::CreateFit(toc_page));
  std::shared_ptr<kernel::PdfOutline> chapter;
  for (const Entry& e : entries) {
    auto destination = kernel::PdfDestination::CreateFit(pdf.GetPage(e.page));
    if (e.level == 0) {
      chapter = root->AddOutline(e.title);
      chapter->AddDestination(destination);
      chapter->SetStyle(kernel::PdfOutline::kFlagBold);
      chapter->SetColor(std::make_shared<kernel::DeviceRgb>(30, 70, 140));
    } else {
      chapter->AddOutline(e.title)->AddDestination(destination);
    }
  }
  chapter->SetOpen(false);
  auto web = root->AddOutline("pdfcpp on the web");
  web->AddAction(
      kernel::PdfAction::CreateUri("https://github.com/itext/itext-java"));
  web->SetStyle(kernel::PdfOutline::kFlagItalic);

  // Ask viewers to show the bookmarks panel when the file opens.
  pdf.GetCatalog().SetPageMode(kernel::PdfName::kUseOutlines);

  int pages = pdf.GetNumberOfPages();
  document.Close();
  std::cout << "Wrote " << dest << " (" << pages << " pages, " << entries.size()
            << " contents entries)\n";
  return 0;
}
