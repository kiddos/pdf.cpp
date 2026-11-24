#include "pdfcpp/layout/elements.h"

#include "pdfcpp/layout/renderers.h"

namespace pdfcpp {
namespace layout {

// ------------------------------------------------------------ AbstractElement
template <typename T>
IRendererPtr AbstractElement<T>::CreateRendererSubTree() {
  IRendererPtr renderer_root = GetRenderer();
  for (const auto& child : child_elements_) {
    IRendererPtr child_renderer = child->CreateRendererSubTree();
    if (child_renderer) renderer_root->AddChild(child_renderer);
  }
  return renderer_root;
}

template <typename T>
std::any BlockElement<T>::GetDefaultPropertyAny(int property) const {
  return std::any();
}

// explicit instantiations
template class AbstractElement<Text>;
template class AbstractElement<Image>;
template class AbstractElement<Paragraph>;
template class AbstractElement<Div>;
template class AbstractElement<AreaBreak>;
template class AbstractElement<LineSeparator>;
template class AbstractElement<Cell>;
template class AbstractElement<Table>;
template class AbstractElement<ListItem>;
template class AbstractElement<List>;
template class BlockElement<Paragraph>;
template class BlockElement<Div>;
template class BlockElement<LineSeparator>;
template class BlockElement<Cell>;
template class BlockElement<Table>;
template class BlockElement<ListItem>;
template class BlockElement<List>;

// ------------------------------------------------------------ Text / Link
float Text::GetTextRise() const {
  auto v = GetProperty<float>(property::kTextRise);
  return v ? *v : 0;
}

float Text::GetHorizontalScaling() const {
  auto v = GetProperty<float>(property::kHorizontalScaling);
  return v ? *v : 1;
}

IRendererPtr Text::MakeNewRenderer() {
  std::shared_ptr<IElement> self;
  try {
    self = shared_from_this();
  } catch (const std::bad_weak_ptr&) {
    self = std::shared_ptr<IElement>(std::shared_ptr<IElement>{},
                                     static_cast<IElement*>(this));
  }
  return std::make_shared<TextRenderer>(self);
}

IRendererPtr Link::MakeNewRenderer() { return Text::MakeNewRenderer(); }

// Renderers keep a non-owning alias to the element: elements are owned by the
// user/document tree.
template <typename E>
static std::shared_ptr<IElement> Alias(E* e) {
  try {
    return static_cast<IElement*>(e)->shared_from_this();
  } catch (const std::bad_weak_ptr&) {
    return std::shared_ptr<IElement>(std::shared_ptr<IElement>{},
                                     static_cast<IElement*>(e));
  }
}

// ------------------------------------------------------------ Image
Image::Image(std::shared_ptr<io::ImageData> img)
    : x_object_(std::make_shared<kernel::PdfImageXObject>(img)) {}
Image::Image(std::shared_ptr<kernel::PdfImageXObject> x_object)
    : x_object_(std::move(x_object)) {}
Image::Image(std::shared_ptr<kernel::PdfFormXObject> x_object)
    : x_object_(std::move(x_object)) {}
Image::Image(std::shared_ptr<io::ImageData> img, float left, float bottom)
    : Image(std::move(img)) {
  SetProperty(property::kPosition, 4);
  SetProperty(property::kLeft, left);
  SetProperty(property::kBottom, bottom);
}

Image& Image::ScaleToFit(float fit_width, float fit_height) {
  float w = x_object_->GetWidth(), h = x_object_->GetHeight();
  if (w == 0 && h == 0)
    return *this;  // nothing to scale (avoids NaN/Inf scaling)
  float s = w == 0   ? fit_height / h
            : h == 0 ? fit_width / w
                     : std::min(fit_width / w, fit_height / h);
  return Scale(s, s);
}

float Image::GetImageScaledWidth() const {
  auto s = GetProperty<float>(property::kHorizontalScaling);
  return x_object_->GetWidth() * (s ? *s : 1);
}

float Image::GetImageScaledHeight() const {
  auto s = GetProperty<float>(property::kVerticalScaling);
  return x_object_->GetHeight() * (s ? *s : 1);
}

IRendererPtr Image::MakeNewRenderer() {
  return std::make_shared<ImageRenderer>(Alias(this));
}

// ------------------------------------------------------------ Paragraph / Div
// / AreaBreak / LineSeparator
std::any Paragraph::GetDefaultPropertyAny(int property) const {
  switch (property) {
    case property::kLeading: {
      bool single_image = child_elements_.size() == 1 &&
                          dynamic_cast<Image*>(child_elements_[0].get());
      return Leading(Leading::kMultiplied, single_image ? 1.f : 1.35f);
    }
    case property::kFirstLineIndent:
      return 0.f;
    case property::kMarginTop:
    case property::kMarginBottom:
      return UnitValue::CreatePointValue(4);
    case property::kTabDefault:
      return 50.f;
    default:
      return BlockElement<Paragraph>::GetDefaultPropertyAny(property);
  }
}

IRendererPtr Paragraph::MakeNewRenderer() {
  return std::make_shared<ParagraphRenderer>(Alias(this));
}
IRendererPtr Div::MakeNewRenderer() {
  return std::make_shared<BlockRenderer>(Alias(this));
}
IRendererPtr AreaBreak::MakeNewRenderer() {
  return std::make_shared<AreaBreakRenderer>(Alias(this));
}
IRendererPtr LineSeparator::MakeNewRenderer() {
  return std::make_shared<LineSeparatorRenderer>(Alias(this));
}

// ------------------------------------------------------------ Cell
std::any Cell::GetDefaultPropertyAny(int property) const {
  static const BorderPtr kDefaultBorder = std::make_shared<SolidBorder>(0.5f);
  switch (property) {
    case property::kBorderTop:
    case property::kBorderRight:
    case property::kBorderBottom:
    case property::kBorderLeft:
      return kDefaultBorder;
    case property::kPaddingBottom:
    case property::kPaddingLeft:
    case property::kPaddingRight:
    case property::kPaddingTop:
      return UnitValue::CreatePointValue(2);
    default:
      return BlockElement<Cell>::GetDefaultPropertyAny(property);
  }
}

IRendererPtr Cell::MakeNewRenderer() {
  return std::make_shared<BlockRenderer>(Alias(this));
}

// ------------------------------------------------------------ Table
Table::Table(const std::vector<float>& point_column_widths, bool) {
  if (point_column_widths.empty())
    throw std::invalid_argument(
        "The widths array in table constructor can not be empty.");
  column_widths_ = UnitValue::CreatePointArray(point_column_widths);
  rowspans_left_.assign(column_widths_.size(), 0);
}

Table::Table(const std::vector<UnitValue>& column_widths, bool)
    : column_widths_(column_widths) {
  if (column_widths_.empty())
    throw std::invalid_argument(
        "The widths array in table constructor can not be empty.");
  rowspans_left_.assign(column_widths_.size(), 0);
}

Table::Table(int num_columns, bool) {
  if (num_columns <= 0)
    throw std::invalid_argument(
        "The number of columns in Table constructor must be greater than zero");
  column_widths_ =
      UnitValue::CreatePercentArray(std::vector<float>(num_columns, 1.f));
  rowspans_left_.assign(num_columns, 0);
  UseAllAvailableWidth();
}

std::any Table::GetDefaultPropertyAny(int property) const {
  switch (property) {
    case property::kBorderCollapse:
      return BorderCollapsePropertyValue::kCollapse;
    default:
      return BlockElement<Table>::GetDefaultPropertyAny(property);
  }
}

Table& Table::StartNewRow() {
  current_column_ = static_cast<int>(column_widths_.size());
  return *this;
}

Table& Table::AddCell(CellPtr cell) {
  int num_columns = static_cast<int>(column_widths_.size());
  // find next free position
  while (true) {
    if (current_row_ < 0 || current_column_ >= num_columns) {
      ++current_row_;
      current_column_ = 0;
      rows_.emplace_back(num_columns);
      for (int c = 0; c < num_columns; ++c)
        if (rowspans_left_[c] > 0) --rowspans_left_[c];
    }
    if (rowspans_left_[current_column_] > 0) {
      ++current_column_;
      continue;
    }
    break;
  }
  int colspan = std::min(cell->GetColspan(), num_columns - current_column_);
  if (colspan < cell->GetColspan())
    cell = std::make_shared<Cell>(*cell);  // shrink colspan conservatively
  cell->SetCellIndexes(current_row_, current_column_);
  rows_[current_row_][current_column_] = cell;
  for (int c = current_column_; c < current_column_ + colspan; ++c)
    rowspans_left_[c] = cell->GetRowspan();
  // rowspans_left_ counts rows including the current one; decrement happens on
  // new row
  current_column_ += colspan;
  return *this;
}

Table& Table::AddCell(const std::string& content) {
  auto cell = std::make_shared<Cell>();
  cell->Add(content);
  return AddCell(cell);
}

Table& Table::AddCell(IElementPtr element) {
  auto cell = std::make_shared<Cell>();
  cell->Add(std::move(element));
  return AddCell(cell);
}

Table& Table::AddCell(IElement& element) {
  if (auto cell = std::dynamic_pointer_cast<Cell>(element.Shared()))
    return AddCell(cell);
  return AddCell(element.Shared());
}

Table& Table::AddHeaderCell(IElement& element) {
  if (auto cell = std::dynamic_pointer_cast<Cell>(element.Shared()))
    return AddHeaderCell(cell);
  auto cell = std::make_shared<Cell>();
  cell->Add(element.Shared());
  return AddHeaderCell(cell);
}

void Table::EnsureHeader() {
  if (!header_) {
    header_ = std::make_shared<Table>(column_widths_);
    header_->DeleteOwnProperty(property::kWidth);
  }
}

void Table::EnsureFooter() {
  if (!footer_) {
    footer_ = std::make_shared<Table>(column_widths_);
    footer_->DeleteOwnProperty(property::kWidth);
  }
}

Table& Table::AddHeaderCell(CellPtr header_cell) {
  EnsureHeader();
  header_->AddCell(std::move(header_cell));
  return *this;
}

Table& Table::AddHeaderCell(const std::string& content) {
  EnsureHeader();
  header_->AddCell(content);
  return *this;
}

Table& Table::AddFooterCell(CellPtr footer_cell) {
  EnsureFooter();
  footer_->AddCell(std::move(footer_cell));
  return *this;
}

Table& Table::AddFooterCell(const std::string& content) {
  EnsureFooter();
  footer_->AddCell(content);
  return *this;
}

CellPtr Table::GetCell(int row, int column) const {
  if (row < 0 || row >= static_cast<int>(rows_.size()) || column < 0 ||
      column >= GetNumberOfColumns())
    return nullptr;
  return rows_[row][column];
}

IRendererPtr Table::MakeNewRenderer() {
  return std::make_shared<TableRenderer>(Alias(this));
}

// ------------------------------------------------------------ List / ListItem
List& List::SetListSymbol(ListNumberingType type) {
  if (type == ListNumberingType::kZapfDingbats1) {
    SetPostSymbolText(" ");
  }
  return SetProperty(property::kListSymbol, type);
}

std::any List::GetDefaultPropertyAny(int property) const {
  switch (property) {
    case property::kListSymbol:
      return std::make_shared<Text>(kDefaultListSymbol);
    case property::kListSymbolPreText:
      return std::string();
    case property::kListSymbolPostText:
      return std::string(". ");
    case property::kListStart:
      return 1;
    case property::kListSymbolIndent:
      return 0.f;
    default:
      return BlockElement<List>::GetDefaultPropertyAny(property);
  }
}

IRendererPtr List::MakeNewRenderer() {
  return std::make_shared<ListRenderer>(Alias(this));
}
IRendererPtr ListItem::MakeNewRenderer() {
  return std::make_shared<ListItemRenderer>(Alias(this));
}

}  // namespace layout
}  // namespace pdfcpp
