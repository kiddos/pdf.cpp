#include <filesystem>
#include <set>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::layout;
namespace fs = std::filesystem;

static const char* const kLorem =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore "
    "magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco "
    "laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse "
    "cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia "
    "deserunt mollit anim id est laborum.";

TEST(SimpleParagraphs) {
  std::string path = "layout_paragraphs.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf);
    auto bold = PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
    document.Add(Make<Paragraph>("pdfcpp layout")
                     ->SetFont(bold)
                     .SetFontSize(24)
                     .SetTextAlignment(TextAlignment::kCenter));
    for (int i = 0; i < 12; ++i) {
      auto p = Make<Paragraph>();
      p->Add(Make<Text>("Paragraph " + std::to_string(i + 1) + ". "))
          .Add(Make<Text>(kLorem));
      p->SetTextAlignment(i % 2 ? TextAlignment::kJustified
                                : TextAlignment::kLeft);
      if (i == 3)
        p->SetFontColor(color_constants::kBlue)
            .SetBackgroundColor(color_constants::kLightGray);
      if (i == 4)
        p->SetBorder(std::make_shared<SolidBorder>(color_constants::kRed, 1))
            .SetPadding(5);
      document.Add(p);
    }
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  EXPECT_TRUE(doc.GetNumberOfPages() >= 2);
  std::string text = PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
  EXPECT_TRUE(text.find("pdfcpp layout") != std::string::npos);
  EXPECT_TRUE(text.find("Paragraph 1. Lorem ipsum") != std::string::npos);
}

TEST(ListsTablesImages) {
  std::string path = "layout_complex.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    document.Add(Make<Paragraph>("Lists")->SetFontSize(18).SimulateBold());
    auto list = Make<List>();
    list->Add("First item")
        .Add(
            "Second item with a much longer text that will certainly wrap onto "
            "another line because it is long")
        .Add("Third");
    document.Add(list);
    auto numbered = Make<List>(ListNumberingType::kRomanUpper);
    numbered->Add("Alpha").Add("Beta").Add("Gamma");
    document.Add(numbered);
    document.Add(Make<LineSeparator>(std::make_shared<SolidLine>(1)));
    document.Add(Make<Paragraph>("Table")->SetFontSize(18).SimulateBold());
    auto table = Make<Table>(UnitValue::CreatePercentArray({1, 2, 2}));
    table->UseAllAvailableWidth();
    table->AddHeaderCell(
        Make<Cell>()->Add("#").SetBackgroundColor(color_constants::kLightGray));
    table->AddHeaderCell("Name");
    table->AddHeaderCell("Description");
    for (int i = 1; i <= 60; ++i) {
      table->AddCell(std::to_string(i));
      table->AddCell("Row " + std::to_string(i));
      table->AddCell(i % 7 == 0 ? std::string(kLorem).substr(0, 140) : "Short");
    }
    auto span = Make<Cell>(1, 3);
    span->Add("A cell spanning three columns")
        .SetTextAlignment(TextAlignment::kCenter);
    table->AddCell(span);
    document.Add(table);
    std::string png = pdfcpp_test::ReferenceDir() +
                      "/pdfa/src/test/resources/com/itextpdf/pdfa/itext.png";
    if (fs::exists(png)) {
      auto img = Make<Image>(io::ImageDataFactory::Create(png));
      img->ScaleToFit(200, 200).SetHorizontalAlignment(
          HorizontalAlignment::kCenter);
      document.Add(img);
    }
    document.ShowTextAligned("Rotated watermark", 300, 400,
                             TextAlignment::kCenter, VerticalAlignment::kMiddle,
                             0.785f);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  EXPECT_TRUE(doc.GetNumberOfPages() >= 2);
  std::string p1 = PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
  EXPECT_TRUE(p1.find("First item") != std::string::npos);
  EXPECT_TRUE(p1.find("Name") != std::string::npos);
  std::string p2 = PdfTextExtractor::GetTextFromPage(doc.GetPage(2));
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
  void EventOccurred(const IEventData& data, EventType type) override {
    if (type != EventType::kRenderText) return;
    const auto& info = static_cast<const TextRenderInfo&>(data);
    auto start = info.GetBaseline().GetStartPoint();
    words.push_back({info.GetText(), start.Get(0), start.Get(1)});
  }
  std::set<EventType> GetSupportedEvents() const override {
    return {EventType::kRenderText};
  }
};

std::vector<Word> WordsOnPage(PdfDocument& doc, int page) {
  WordCollector collector;
  PdfCanvasProcessor processor(collector);
  processor.ProcessPageContent(doc.GetPage(page));
  return collector.words;
}

const Word* FindWord(const std::vector<Word>& words, const std::string& text) {
  for (const auto& w : words)
    if (w.text.find(text) != std::string::npos) return &w;
  return nullptr;
}

int CountOccurrences(PdfDocument& doc, const std::string& needle) {
  int n = 0;
  for (int p = 1; p <= doc.GetNumberOfPages(); ++p) {
    std::string text = PdfTextExtractor::GetTextFromPage(doc.GetPage(p));
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + 1))
      ++n;
  }
  return n;
}
}  // namespace

// BUGS.md #33: rows of a row group with a rowspan are stacked, not drawn on top
// of each other.
TEST(TableRowspanRowsAreStacked) {
  std::string path = "layout_rowspan.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf);
    auto t = Make<Table>(UnitValue::CreatePercentArray({1, 1}));
    t->AddCell(Make<Cell>(3, 1)->Add("SPAN"));
    t->AddCell("ROWA");
    t->AddCell("ROWB");
    t->AddCell("ROWC");
    document.Add(t);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  auto words = WordsOnPage(doc, 1);
  auto a = FindWord(words, "ROWA"), b = FindWord(words, "ROWB"),
       c = FindWord(words, "ROWC");
  EXPECT_TRUE(a && b && c);
  if (a && b && c) {
    EXPECT_TRUE(a->y - b->y > 10);
    EXPECT_TRUE(b->y - c->y > 10);
  }
}

// BUGS.md #34 and the related split duplication: cells completed before a row
// split are not repeated on the next page, a first row that does not fit is
// split in place, and header cells keep their column order.
TEST(TableSplitting) {
  std::string path = "layout_table_split.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    auto t = Make<Table>(UnitValue::CreatePercentArray({1, 1}));
    t->AddHeaderCell("HEADLEFT");
    t->AddHeaderCell("HEADRIGHT");
    t->AddCell("SHORTCELL");
    std::string long_text;
    for (int i = 0; i < 400; ++i) long_text += "word" + std::to_string(i) + " ";
    t->AddCell(long_text);
    document.Add(t);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  EXPECT_EQ(doc.GetNumberOfPages(), 2);
  EXPECT_EQ(CountOccurrences(doc, "SHORTCELL"), 1);
  EXPECT_EQ(CountOccurrences(doc, "word399"), 1);
  auto words = WordsOnPage(doc, 1);
  auto left = FindWord(words, "HEADLEFT"), right = FindWord(words, "HEADRIGHT");
  EXPECT_TRUE(left && right && left->x < right->x);
}

TEST(TableFirstRowSplitsInPlace) {
  std::string path = "layout_table_first_row.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    for (int i = 0; i < 26; ++i)
      document.Add(Make<Paragraph>("filler " + std::to_string(i)));
    auto t = Make<Table>(UnitValue::CreatePercentArray({1}));
    std::string long_text;
    for (int i = 0; i < 300; ++i) long_text += "w" + std::to_string(i) + " ";
    t->AddCell(long_text);
    document.Add(t);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  EXPECT_TRUE(PdfTextExtractor::GetTextFromPage(doc.GetPage(1)).find("w0 ") !=
              std::string::npos);
}

TEST(TableHeaderAndFooterTallerThanPageKeepsRows) {
  std::string path = "layout_table_huge_header.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    auto t = Make<Table>(UnitValue::CreatePercentArray({1, 1}));
    for (int i = 0; i < 40; ++i) t->AddHeaderCell("H" + std::to_string(i));
    for (int i = 0; i < 40; ++i) t->AddFooterCell("F" + std::to_string(i));
    for (int i = 0; i < 6; ++i) t->AddCell("BODY" + std::to_string(i));
    document.Add(t);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  for (int i = 0; i < 6; ++i)
    EXPECT_EQ(CountOccurrences(doc, "BODY" + std::to_string(i)), 1);
}

// BUGS.md #35: the symbol of a list item split across pages stays left of the
// text.
TEST(ListItemSplitKeepsSymbolLeft) {
  std::string path = "layout_list_split.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    for (int i = 0; i < 25; ++i)
      document.Add(Make<Paragraph>("filler " + std::to_string(i)));
    auto list = Make<List>(ListNumberingType::kDecimal);
    std::string long_text;
    for (int i = 0; i < 300; ++i) long_text += "w" + std::to_string(i) + " ";
    list->Add(long_text);
    document.Add(list);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  EXPECT_EQ(doc.GetNumberOfPages(), 2);
  auto words = WordsOnPage(doc, 1);
  auto symbol = FindWord(words, "1."), text = FindWord(words, "w0");
  EXPECT_TRUE(symbol && text);
  if (symbol && text) EXPECT_TRUE(symbol->x + 5 < text->x);
}

// Greek list symbols are drawn in the Symbol font (Java's
// ConstantFontTextRenderer); the alphabet has 24 letters without final sigma
// and continues with two-letter numbers. Dingbat lists use ZapfDingbats.
TEST(GreekAndDingbatListSymbolsUseTheirFonts) {
  std::string path = "layout_list_symbol_fonts.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    auto greek = Make<List>(ListNumberingType::kGreekLower);
    for (int i = 1; i <= 26; ++i) greek->Add("item" + std::to_string(i));
    document.Add(greek);
    auto dingbats = Make<List>(ListNumberingType::kZapfDingbats1);
    dingbats->Add("first").Add("second");
    document.Add(dingbats);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  std::string text = PdfTextExtractor::GetTextFromPage(doc.GetPage(1));
  EXPECT_TRUE(text.find("\xce\xb1. item1") != std::string::npos);   // α
  EXPECT_TRUE(text.find("\xcf\x83. item18") != std::string::npos);  // σ
  EXPECT_TRUE(text.find("\xcf\x89. item24") != std::string::npos);  // ω
  EXPECT_TRUE(text.find("\xce\xb1\xce\xb2. item26") != std::string::npos);
  EXPECT_TRUE(text.find("\xcf\x82") == std::string::npos);  // no final ς
  std::set<std::string> fonts;
  auto font_dict = doc.GetPage(1)->GetResources()->GetResource(PdfName::kFont);
  for (const auto& key : font_dict->KeySet())
    fonts.insert(font_dict->GetAsDictionary(key)
                     ->GetAsName(PdfName::kBaseFont)
                     ->GetValue());
  EXPECT_TRUE(fonts.count("Symbol") == 1);
  EXPECT_TRUE(fonts.count("ZapfDingbats") == 1);
}

// BUGS.md #36: a percentage width given to SetFixedPosition is applied once.
TEST(FixedPositionPercentWidth) {
  std::string path = "layout_fixed_width.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    auto p = Make<Paragraph>("RIGHTALIGNED");
    p->SetTextAlignment(TextAlignment::kRight);
    p->SetFixedPosition(100, 500, UnitValue::CreatePercentValue(50));
    document.Add(p);
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  auto words = WordsOnPage(doc, 1);
  auto w = FindWord(words, "RIGHTALIGNED");
  EXPECT_TRUE(w != nullptr);
  // 50% of the 523pt wide area: the ~90pt wide text ends at x = 100 + 261.5,
  // i.e. starts near 271 (applying the percentage twice made it end at 100 +
  // 130.75, starting near 140)
  if (w) EXPECT_NEAR(w->x, 271.5f, 3.f);
}

// BUGS.md #37: a zero-sized form XObject image does not produce NaN/Inf sizes.
TEST(ZeroSizedImage) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    Document document(pdf);
    auto xobject = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 0, 0));
    auto img = Make<Image>(xobject);
    img->ScaleToFit(100, 100);
    img->SetWidth(50);
    document.Add(img);
    document.Close();
  }
  Bytes pdf = baos.ToByteArray();
  std::string s(pdf.begin(), pdf.end());
  EXPECT_TRUE(s.find("nan") == std::string::npos &&
              s.find("inf") == std::string::npos);
}

// BUGS.md #37: when a line wraps at a break whose x is 0 (everything before it
// has zero width), the pieces from the break on are dropped from the line, not
// drawn there and again on the next line.
TEST(WrapAfterZeroWidthStartDoesNotDuplicate) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    Document document(pdf, PageSize::kA4);
    auto p = Make<Paragraph>();
    // a zero-sized inline image, then one word made of two chunks that is
    // too wide for the rest of the line
    p->Add(
        Make<Image>(std::make_shared<PdfFormXObject>(Rectangle(0, 0, 0, 0))));
    p->Add(Make<Text>("HEADX"));
    p->Add(Make<Text>(std::string(50, 'W')));
    document.Add(p);
    document.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  EXPECT_EQ(CountOccurrences(doc, "HEADX"), 1);
}

// BUGS.md #37: an inline image takes the room of its paddings and borders in
// the line (ImageRenderer draws the image inside them).
TEST(InlineImageIncludesBordersAndPaddings) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    Document document(pdf, PageSize::kA4);
    auto data = io::ImageDataFactory::Create(2, 2, 1, 8, Bytes(4, 0x80));
    auto image = Make<Image>(data);
    image->ScaleAbsolute(20, 20);
    image->SetBorder(std::make_shared<SolidBorder>(color_constants::kRed, 5));
    image->SetPadding(3);
    auto p = Make<Paragraph>();
    p->Add(image);
    p->Add(Make<Text>("AFTER"));
    document.Add(p);
    document.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  auto words = WordsOnPage(doc, 1);
  auto after = FindWord(words, "AFTER");
  EXPECT_TRUE(after != nullptr);
  // left margin 36 + 3 + 5 + 20 + 5 + 3
  if (after) EXPECT_NEAR(after->x, 72.f, 0.5f);
}

// BUGS.md #37: a paragraph with a fixed height that is split keeps only the
// rest of that height for its continuation.
TEST(FixedHeightParagraphSplit) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    Document document(pdf, PageSize::kA4);
    std::string text;
    for (int i = 0; i < 50; ++i) text += "line " + std::to_string(i) + "\n";
    auto p = Make<Paragraph>(text);
    p->SetHeight(1000);
    document.Add(p);
    document.Add(Make<Paragraph>("NEXTPARA"));
    document.Close();
  }
  PdfDocument doc(PdfReader::FromBytes(baos.ToByteArray()));
  // The page holds about 770pt, so 230pt of the 1000pt remain for page 2 and
  // the next paragraph follows on that page (re-applying the whole height
  // pushed it to page 3).
  EXPECT_EQ(doc.GetNumberOfPages(), 2);
  auto words = WordsOnPage(doc, 2);
  auto next = FindWord(words, "NEXTPARA");
  EXPECT_TRUE(next != nullptr);
  if (next) EXPECT_TRUE(next->y < 806 - 200 && next->y > 806 - 270);
}

TEST_MAIN()

// A rotated image occupies the bounding box of the rotated rectangle, so the
// content after it starts below that box (Java ImageRenderer).
TEST(RotatedImageOccupiesRotatedBoundingBox) {
  std::string path = "layout_rotated_image.pdf";
  {
    PdfDocument pdf(std::make_unique<PdfWriter>(path));
    Document document(pdf, PageSize::kA4);
    auto data = io::ImageDataFactory::Create(4, 2, 1, 8, Bytes(8, 0x80));
    // 100 x 50 pt turned by 90 degrees: the box is 50 wide and 100 high.
    document.Add(Make<Image>(data)->ScaleAbsolute(100, 50).SetRotationAngle(
        std::acos(-1.0) / 2));
    document.Add(Make<Paragraph>("after"));
    document.Close();
  }
  PdfDocument doc(std::make_unique<PdfReader>(path));
  auto words = WordsOnPage(doc, 1);
  const Word* after = FindWord(words, "after");
  EXPECT_TRUE(after != nullptr);
  // The top margin is 36 pt, so the image box ends at 842 - 36 - 100 = 706.
  if (after) EXPECT_TRUE(after->y < 706);
}
