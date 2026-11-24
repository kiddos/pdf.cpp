// A tour of the layout module: styled text runs, paragraph formatting,
// borders and backgrounds, lists, tables with spans, separators and fixed or
// rotated elements.
#include <cmath>
#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;
namespace colors = kernel::color_constants;

namespace {

std::shared_ptr<kernel::PdfFont> Font(const char* name) {
  return kernel::PdfFontFactory::CreateFont(name);
}

ParagraphPtr Heading(const std::string& text) {
  auto p = Make<Paragraph>(text);
  p->SetFont(Font(io::standard_fonts::kHelveticaBold))
      .SetFontSize(14)
      .SetFontColor(std::make_shared<kernel::DeviceRgb>(30, 70, 140))
      .SetMarginTop(14)
      .SetMarginBottom(4);
  return p;
}

const char* const kLorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat.";

}  // namespace

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "layout_showcase.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  Document document(pdf, kernel::PageSize::kA4);
  document.SetMargins(50, 50, 50, 50);

  document.Add(Make<Paragraph>("Layout showcase")
                   ->SetFont(Font(io::standard_fonts::kTimesBold))
                   .SetFontSize(26)
                   .SetTextAlignment(TextAlignment::kCenter));

  // --- Text runs: every Text in a Paragraph can carry its own style.
  document.Add(Heading("Styled text"));
  auto styled = Make<Paragraph>();
  styled->Add(Make<Text>("Bold, ")->SetBold())
      .Add(Make<Text>("italic, ")->SetItalic())
      .Add(Make<Text>("underlined, ")->SetUnderline())
      .Add(Make<Text>("struck through, ")->SetLineThrough())
      .Add(Make<Text>("colored, ")->SetFontColor(colors::kRed))
      .Add(Make<Text>("highlighted")->SetBackgroundColor(colors::kYellow))
      .Add(", ")
      .Add(Make<Text>("outlined")
               ->SetTextRenderingMode(
                   kernel::pdf_canvas_constants::text_rendering_mode::kStroke)
               .SetStrokeColor(colors::kBlue)
               .SetStrokeWidth(0.5f)
               .SetFontSize(16))
      .Add(", ")
      .Add(Make<Text>("w i d e")->SetCharacterSpacing(2))
      .Add(" and ")
      .Add(Make<Text>("condensed")->SetHorizontalScaling(0.7f))
      .Add(". Also E = mc")
      .Add(Make<Text>("2")->SetTextRise(5).SetFontSize(8))
      .Add(" and H")
      .Add(Make<Text>("2")->SetTextRise(-3).SetFontSize(8))
      .Add("O.");
  document.Add(styled);

  // --- Paragraph formatting.
  document.Add(Heading("Alignment, indentation and leading"));
  struct {
    TextAlignment alignment;
    const char* name;
  } alignments[] = {{TextAlignment::kLeft, "Left"},
                    {TextAlignment::kCenter, "Centered"},
                    {TextAlignment::kRight, "Right"},
                    {TextAlignment::kJustified, "Justified"}};
  auto alignment_table = Make<Table>(2);
  alignment_table->UseAllAvailableWidth();
  for (const auto& a : alignments) {
    alignment_table->AddCell(
        Make<Cell>()->Add(Make<Paragraph>(std::string(a.name) + ". " + kLorem)
                              ->SetTextAlignment(a.alignment)
                              .SetFontSize(8.5f)));
  }
  document.Add(alignment_table);
  document.Add(Make<Paragraph>(std::string("First-line indent of 30pt and a "
                                           "leading of 1.6 times the font "
                                           "size. ") +
                               kLorem)
                   ->SetFirstLineIndent(30)
                   .SetMultipliedLeading(1.6f)
                   .SetFontSize(10)
                   .SetMarginTop(6));

  // --- Borders and backgrounds on Div containers.
  document.Add(Heading("Borders and backgrounds"));
  auto boxes = Make<Table>(4);
  boxes->UseAllAvailableWidth();
  struct {
    BorderPtr border;
    const char* name;
  } borders[] = {
      {std::make_shared<SolidBorder>(colors::kDarkGray, 1.5f), "solid"},
      {std::make_shared<DashedBorder>(colors::kRed, 1.5f), "dashed"},
      {std::make_shared<DottedBorder>(colors::kBlue, 1.5f), "dotted"},
      {std::make_shared<DoubleBorder>(
           std::make_shared<kernel::DeviceRgb>(30, 140, 60), 3),
       "double"}};
  for (const auto& b : borders) {
    auto div = Make<Div>();
    div->SetBorder(b.border)
        .SetBackgroundColor(std::make_shared<kernel::DeviceRgb>(240, 244, 250))
        .SetPadding(8)
        .SetMargin(4);
    div->Add(Make<Paragraph>(b.name)->SetTextAlignment(TextAlignment::kCenter));
    boxes->AddCell(Make<Cell>()->Add(div).SetBorder(Border::kNoBorder));
  }
  document.Add(boxes);

  // --- Lists, including a nested one.
  document.Add(Heading("Lists"));
  auto lists = Make<Table>(3);
  lists->UseAllAvailableWidth();
  for (ListNumberingType type :
       {ListNumberingType::kRomanUpper, ListNumberingType::kEnglishLower,
        ListNumberingType::kGreekLower}) {
    auto list = Make<List>(type);
    list->SetPostSymbolText(") ");
    list->Add("Planning").Add("Execution").Add("Review");
    lists->AddCell(Make<Cell>()->Add(list).SetBorder(Border::kNoBorder));
  }
  document.Add(lists);

  auto nested = Make<List>();
  nested->SetListSymbol("\xe2\x80\xa2 ");  // bullet
  auto fruit = Make<ListItem>("Fruit");
  auto fruit_kinds = Make<List>(ListNumberingType::kDecimal);
  fruit_kinds->SetMarginLeft(12);
  fruit_kinds->Add("Apples").Add("Pears");
  fruit->Add(fruit_kinds);
  nested->Add(fruit).Add("Vegetables").Add("Grains");
  document.Add(nested);

  // --- Tables with column and row spans.
  document.Add(Heading("Table with spans"));
  auto table = Make<Table>(UnitValue::CreatePercentArray({2, 1, 1, 1, 1}));
  table->UseAllAvailableWidth();
  auto header_bg = std::make_shared<kernel::DeviceRgb>(30, 70, 140);
  auto header = [&](const std::string& text, int rowspan, int colspan) {
    auto cell = Make<Cell>(rowspan, colspan);
    cell->Add(Make<Paragraph>(text)
                  ->SetFontColor(colors::kWhite)
                  .SetBold()
                  .SetTextAlignment(TextAlignment::kCenter))
        .SetBackgroundColor(header_bg)
        .SetVerticalAlignment(VerticalAlignment::kMiddle);
    table->AddHeaderCell(cell);
  };
  header("Region", 2, 1);
  header("2025", 1, 2);
  header("2026", 1, 2);
  for (const char* h : {"H1", "H2", "H1", "H2"}) header(h, 1, 1);
  const char* regions[] = {"North", "South", "East", "West"};
  for (int r = 0; r < 4; ++r) {
    table->AddCell(regions[r]);
    for (int c = 0; c < 4; ++c) {
      table->AddCell(Make<Cell>()->Add(
          Make<Paragraph>(std::to_string(100 + (r * 37 + c * 53) % 400))
              ->SetTextAlignment(TextAlignment::kRight)));
    }
  }
  auto footer = Make<Cell>(1, 5);
  footer->Add(
      Make<Paragraph>("Figures in thousands")->SetFontSize(8).SetItalic());
  table->AddFooterCell(footer);
  document.Add(table);

  // --- Separators.
  document.Add(Make<LineSeparator>(std::make_shared<DashedLine>(1))
                   ->SetMarginTop(12)
                   .SetMarginBottom(6));
  document.Add(Make<Paragraph>("Text after a dashed line separator."));

  // --- Fixed position and rotation: a "DRAFT" badge in the corner of page 1.
  auto badge = Make<Paragraph>("DRAFT");
  badge->SetFont(Font(io::standard_fonts::kHelveticaBold))
      .SetFontSize(18)
      .SetFontColor(colors::kRed, 0.6f)
      .SetBorder(std::make_shared<SolidBorder>(colors::kRed, 2))
      .SetPaddings(2, 8, 2, 8)
      .SetRotationAngle(M_PI / 8)
      .SetFixedPosition(1, 470, 770, 90);
  document.Add(badge);

  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
