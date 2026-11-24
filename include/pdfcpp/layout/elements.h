#ifndef PDFCPP_LAYOUT_ELEMENTS_H_
#define PDFCPP_LAYOUT_ELEMENTS_H_

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "pdfcpp/layout/properties.h"

namespace pdfcpp {
namespace layout {

class IRenderer;
using IRendererPtr = std::shared_ptr<IRenderer>;

/// Port of IPropertyContainer.
class IPropertyContainer {
 public:
  virtual ~IPropertyContainer() = default;
  virtual bool HasProperty(int property) const = 0;
  virtual bool HasOwnProperty(int property) const = 0;
  /// Returns a pointer to the stored value or nullptr.
  virtual const std::any* GetPropertyAny(int property) const = 0;
  virtual const std::any* GetOwnPropertyAny(int property) const = 0;
  virtual std::any GetDefaultPropertyAny(int property) const {
    return std::any();
  }
  virtual void SetPropertyAny(int property, std::any value) = 0;
  virtual void DeleteOwnProperty(int property) = 0;

  template <typename V>
  std::optional<V> GetProperty(int property) const {
    const std::any* a = GetPropertyAny(property);
    if (a && a->has_value()) {
      if (const V* v = std::any_cast<V>(a)) return *v;
    }
    std::any d = GetDefaultPropertyAny(property);
    if (d.has_value())
      if (const V* v = std::any_cast<V>(&d)) return *v;
    return std::nullopt;
  }
};

/// Port of ElementPropertyContainer: properties + fluent setters (CRTP returns
/// the concrete type).
template <typename T>
class ElementPropertyContainer : public virtual IPropertyContainer {
 public:
  bool HasProperty(int property) const override {
    return HasOwnProperty(property);
  }
  bool HasOwnProperty(int property) const override {
    return properties_.count(property) > 0;
  }
  const std::any* GetPropertyAny(int property) const override {
    return GetOwnPropertyAny(property);
  }
  const std::any* GetOwnPropertyAny(int property) const override {
    auto it = properties_.find(property);
    return it == properties_.end() ? nullptr : &it->second;
  }
  void SetPropertyAny(int property, std::any value) override {
    properties_[property] = std::move(value);
  }
  void DeleteOwnProperty(int property) override { properties_.erase(property); }

  template <typename V>
  T& SetProperty(int property, V value) {
    properties_[property] = std::move(value);
    return Self();
  }

  T& SetRelativePosition(float left, float top, float right, float bottom);
  T& SetFixedPosition(float left, float bottom, float width) {
    return SetFixedPosition(left, bottom, UnitValue::CreatePointValue(width));
  }
  T& SetFixedPosition(float left, float bottom, UnitValue width) {
    SetProperty(property::kPosition, 4);  // LayoutPosition.FIXED
    SetProperty(property::kLeft, left);
    SetProperty(property::kBottom, bottom);
    return SetProperty(property::kWidth, width);
  }
  T& SetFixedPosition(int page_number, float left, float bottom, float width) {
    SetFixedPosition(left, bottom, width);
    return SetProperty(property::kPageNumber, page_number);
  }
  T& SetFont(std::shared_ptr<kernel::PdfFont> font) {
    return SetProperty(property::kFont, std::move(font));
  }
  T& SetFontColor(ColorPtr font_color) {
    return SetProperty(property::kFontColor, std::move(font_color));
  }
  T& SetFontColor(ColorPtr font_color, float opacity) {
    SetProperty(property::kOpacity, opacity);
    return SetFontColor(std::move(font_color));
  }
  T& SetFontSize(float font_size) {
    return SetProperty(property::kFontSize,
                       UnitValue::CreatePointValue(font_size));
  }
  T& SetTextAlignment(TextAlignment alignment) {
    return SetProperty(property::kTextAlignment, alignment);
  }
  T& SetCharacterSpacing(float char_spacing) {
    return SetProperty(property::kCharacterSpacing, char_spacing);
  }
  T& SetWordSpacing(float word_spacing) {
    return SetProperty(property::kWordSpacing, word_spacing);
  }
  T& SetFontKerning(bool kerning) {
    return SetProperty(property::kFontKerning, kerning);
  }
  T& SetBackgroundColor(ColorPtr color) {
    return SetProperty(property::kBackground, Background(std::move(color)));
  }
  T& SetBackgroundColor(ColorPtr color, float opacity) {
    return SetProperty(property::kBackground,
                       Background(std::move(color), opacity));
  }
  T& SetBorder(BorderPtr border) {
    SetProperty(property::kBorderTop, border);
    SetProperty(property::kBorderRight, border);
    SetProperty(property::kBorderBottom, border);
    return SetProperty(property::kBorderLeft, border);
  }
  T& SetBorderTop(BorderPtr border) {
    return SetProperty(property::kBorderTop, std::move(border));
  }
  T& SetBorderRight(BorderPtr border) {
    return SetProperty(property::kBorderRight, std::move(border));
  }
  T& SetBorderBottom(BorderPtr border) {
    return SetProperty(property::kBorderBottom, std::move(border));
  }
  T& SetBorderLeft(BorderPtr border) {
    return SetProperty(property::kBorderLeft, std::move(border));
  }
  T& SetSplitCharacters(int) { return Self(); }
  T& SetTextRenderingMode(int mode) {
    return SetProperty(property::kTextRenderingMode, mode);
  }
  T& SetStrokeColor(ColorPtr color) {
    return SetProperty(property::kStrokeColor, std::move(color));
  }
  T& SetStrokeWidth(float width) {
    return SetProperty(property::kStrokeWidth, width);
  }
  T& SimulateBold() { return SetProperty(property::kBoldSimulation, true); }
  T& SetBold() { return SimulateBold(); }
  T& SimulateItalic() { return SetProperty(property::kItalicSimulation, true); }
  T& SetItalic() { return SimulateItalic(); }
  T& SetLineThrough() { return SetUnderline(nullptr, .75f, 0, 0, 1 / 4.f); }
  T& SetUnderline() { return SetUnderline(nullptr, .75f, 0, 0, -1 / 8.f); }
  T& SetUnderline(float thickness, float y_position) {
    return SetUnderline(nullptr, thickness, 0, y_position, 0);
  }
  T& SetUnderline(ColorPtr color, float thickness, float thickness_mul,
                  float y_position, float y_position_mul) {
    auto it = properties_.find(property::kUnderline);
    std::vector<Underline> list;
    if (it != properties_.end())
      if (auto* l = std::any_cast<std::vector<Underline>>(&it->second))
        list = *l;
    list.emplace_back(std::move(color), thickness, thickness_mul, y_position,
                      y_position_mul);
    return SetProperty(property::kUnderline, list);
  }
  T& SetTextRise(float text_rise) {
    return SetProperty(property::kTextRise, text_rise);
  }
  T& SetHorizontalScaling(float scale) {
    return SetProperty(property::kHorizontalScaling, scale);
  }
  T& SetDestination(const std::string& destination) {
    return SetProperty(property::kDestination, destination);
  }
  T& SetOpacity(float opacity) {
    return SetProperty(property::kOpacity, opacity);
  }

 protected:
  T& Self() { return static_cast<T&>(*this); }
  std::map<int, std::any> properties_;
};

/// Port of IElement.
class IElement : public virtual IPropertyContainer,
                 public std::enable_shared_from_this<IElement> {
 public:
  /// Shared pointer to this element (elements must be created with
  /// std::make_shared / layout::make).
  std::shared_ptr<IElement> Shared() { return shared_from_this(); }
  virtual IRendererPtr GetRenderer() = 0;
  virtual IRendererPtr CreateRendererSubTree() = 0;
};

using IElementPtr = std::shared_ptr<IElement>;

/// Port of AbstractElement.
template <typename T>
class AbstractElement : public ElementPropertyContainer<T>, public IElement {
 public:
  bool HasProperty(int property) const override {
    return ElementPropertyContainer<T>::HasProperty(property);
  }
  bool HasOwnProperty(int property) const override {
    return ElementPropertyContainer<T>::HasOwnProperty(property);
  }
  const std::any* GetPropertyAny(int property) const override {
    return ElementPropertyContainer<T>::GetPropertyAny(property);
  }
  const std::any* GetOwnPropertyAny(int property) const override {
    return ElementPropertyContainer<T>::GetOwnPropertyAny(property);
  }
  void SetPropertyAny(int property, std::any value) override {
    ElementPropertyContainer<T>::SetPropertyAny(property, std::move(value));
  }
  void DeleteOwnProperty(int property) override {
    ElementPropertyContainer<T>::DeleteOwnProperty(property);
  }

  const std::vector<IElementPtr>& GetChildren() const {
    return child_elements_;
  }
  IRendererPtr GetRenderer() override {
    return next_renderer_ ? next_renderer_ : MakeNewRenderer();
  }
  void SetNextRenderer(IRendererPtr renderer) {
    next_renderer_ = std::move(renderer);
  }
  IRendererPtr CreateRendererSubTree() override;
  bool IsEmpty() const { return child_elements_.empty(); }
  T& AddStyle(const std::map<int, std::any>& style) {
    for (const auto& kv : style) this->properties_[kv.first] = kv.second;
    return this->Self();
  }

 protected:
  virtual IRendererPtr MakeNewRenderer() = 0;
  std::vector<IElementPtr> child_elements_;
  IRendererPtr next_renderer_;
};

/// Port of BlockElement: margins, paddings, sizes.
template <typename T>
class BlockElement : public AbstractElement<T> {
 public:
  T& SetMarginLeft(float v) {
    return this->SetProperty(property::kMarginLeft,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMarginRight(float v) {
    return this->SetProperty(property::kMarginRight,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMarginTop(float v) {
    return this->SetProperty(property::kMarginTop,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMarginBottom(float v) {
    return this->SetProperty(property::kMarginBottom,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMargin(float v) { return SetMargins(v, v, v, v); }
  T& SetMargins(float top, float right, float bottom, float left) {
    SetMarginTop(top);
    SetMarginRight(right);
    SetMarginBottom(bottom);
    return SetMarginLeft(left);
  }
  T& SetPaddingLeft(float v) {
    return this->SetProperty(property::kPaddingLeft,
                             UnitValue::CreatePointValue(v));
  }
  T& SetPaddingRight(float v) {
    return this->SetProperty(property::kPaddingRight,
                             UnitValue::CreatePointValue(v));
  }
  T& SetPaddingTop(float v) {
    return this->SetProperty(property::kPaddingTop,
                             UnitValue::CreatePointValue(v));
  }
  T& SetPaddingBottom(float v) {
    return this->SetProperty(property::kPaddingBottom,
                             UnitValue::CreatePointValue(v));
  }
  T& SetPadding(float v) { return SetPaddings(v, v, v, v); }
  T& SetPaddings(float top, float right, float bottom, float left) {
    SetPaddingTop(top);
    SetPaddingRight(right);
    SetPaddingBottom(bottom);
    return SetPaddingLeft(left);
  }
  T& SetVerticalAlignment(VerticalAlignment v) {
    return this->SetProperty(property::kVerticalAlignment, v);
  }
  T& SetSpacingRatio(float ratio) {
    return this->SetProperty(property::kSpacingRatio, ratio);
  }
  T& SetKeepTogether(bool v) {
    return this->SetProperty(property::kKeepTogether, v);
  }
  bool IsKeepTogether() const {
    auto v = this->template GetProperty<bool>(property::kKeepTogether);
    return v && *v;
  }
  T& SetKeepWithNext(bool v) {
    return this->SetProperty(property::kKeepWithNext, v);
  }
  T& SetRotationAngle(double rad_angle) {
    return this->SetProperty(property::kRotationAngle,
                             static_cast<float>(rad_angle));
  }
  T& SetWidth(float width) {
    return this->SetProperty(property::kWidth,
                             UnitValue::CreatePointValue(width));
  }
  T& SetWidth(UnitValue width) {
    return this->SetProperty(property::kWidth, width);
  }
  T& SetHeight(float height) {
    return this->SetProperty(property::kHeight,
                             UnitValue::CreatePointValue(height));
  }
  T& SetMaxHeight(float v) {
    return this->SetProperty(property::kMaxHeight,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMinHeight(float v) {
    return this->SetProperty(property::kMinHeight,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMaxWidth(float v) {
    return this->SetProperty(property::kMaxWidth,
                             UnitValue::CreatePointValue(v));
  }
  T& SetMinWidth(float v) {
    return this->SetProperty(property::kMinWidth,
                             UnitValue::CreatePointValue(v));
  }
  T& SetHorizontalAlignment(HorizontalAlignment v) {
    return this->SetProperty(property::kHorizontalAlignment, v);
  }
  std::any GetDefaultPropertyAny(int property) const override;
};

class Paragraph;
class Image;

/// Port of Text: a leaf element holding a chunk of text (UTF-8).
class Text : public AbstractElement<Text> {
 public:
  explicit Text(std::string text) : text_(std::move(text)) {}
  const std::string& GetText() const { return text_; }
  void SetText(std::string text) { text_ = std::move(text); }
  float GetTextRise() const;
  float GetHorizontalScaling() const;
  Text& SetSkew(float alpha_angle, float beta_angle) {
    return SetProperty(property::kSkew,
                       std::vector<float>{alpha_angle, beta_angle});
  }

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  std::string text_;
};
using TextPtr = std::shared_ptr<Text>;

/// Port of Link (a Text with an URI action).
class Link : public Text {
 public:
  Link(std::string text, std::string uri)
      : Text(std::move(text)), uri_(std::move(uri)) {}
  const std::string& GetUri() const { return uri_; }

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  std::string uri_;
};

/// Port of Image.
class Image : public AbstractElement<Image> {
 public:
  explicit Image(std::shared_ptr<io::ImageData> img);
  explicit Image(std::shared_ptr<kernel::PdfImageXObject> x_object);
  explicit Image(std::shared_ptr<kernel::PdfFormXObject> x_object);
  Image(std::shared_ptr<io::ImageData> img, float left, float bottom);
  const std::shared_ptr<kernel::PdfXObject>& GetXObject() const {
    return x_object_;
  }
  Image& SetRotationAngle(double rad_angle) {
    return SetProperty(property::kRotationAngle, static_cast<float>(rad_angle));
  }
  Image& SetMarginLeft(float v) {
    return SetProperty(property::kMarginLeft, UnitValue::CreatePointValue(v));
  }
  Image& SetMarginRight(float v) {
    return SetProperty(property::kMarginRight, UnitValue::CreatePointValue(v));
  }
  Image& SetMarginTop(float v) {
    return SetProperty(property::kMarginTop, UnitValue::CreatePointValue(v));
  }
  Image& SetMarginBottom(float v) {
    return SetProperty(property::kMarginBottom, UnitValue::CreatePointValue(v));
  }
  Image& SetMargins(float top, float right, float bottom, float left) {
    SetMarginTop(top);
    SetMarginRight(right);
    SetMarginBottom(bottom);
    return SetMarginLeft(left);
  }
  Image& Scale(float horizontal_scaling, float vertical_scaling) {
    SetProperty(property::kHorizontalScaling, horizontal_scaling);
    return SetProperty(property::kVerticalScaling, vertical_scaling);
  }
  Image& ScaleToFit(float fit_width, float fit_height);
  Image& ScaleAbsolute(float fit_width, float fit_height) {
    float horizontal_scaling = fit_width / x_object_->GetWidth();
    float vertical_scaling = fit_height / x_object_->GetHeight();
    return Scale(horizontal_scaling, vertical_scaling);
  }
  Image& SetAutoScale(bool auto_scale) {
    SetProperty(property::kAutoScaleWidth, auto_scale);
    SetProperty(property::kAutoScaleHeight, auto_scale);
    return SetProperty(property::kAutoScale, auto_scale);
  }
  Image& SetAutoScaleHeight(bool v) {
    return SetProperty(property::kAutoScaleHeight, v);
  }
  Image& SetAutoScaleWidth(bool v) {
    return SetProperty(property::kAutoScaleWidth, v);
  }
  Image& SetWidth(float width) {
    return SetProperty(property::kWidth, UnitValue::CreatePointValue(width));
  }
  Image& SetWidth(UnitValue width) {
    return SetProperty(property::kWidth, width);
  }
  Image& SetHeight(float height) {
    return SetProperty(property::kHeight, UnitValue::CreatePointValue(height));
  }
  Image& SetHorizontalAlignment(HorizontalAlignment v) {
    return SetProperty(property::kHorizontalAlignment, v);
  }
  Image& SetPadding(float v) {
    SetProperty(property::kPaddingTop, UnitValue::CreatePointValue(v));
    SetProperty(property::kPaddingRight, UnitValue::CreatePointValue(v));
    SetProperty(property::kPaddingBottom, UnitValue::CreatePointValue(v));
    return SetProperty(property::kPaddingLeft, UnitValue::CreatePointValue(v));
  }
  float GetImageWidth() const { return x_object_->GetWidth(); }
  float GetImageHeight() const { return x_object_->GetHeight(); }
  float GetImageScaledWidth() const;
  float GetImageScaledHeight() const;

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  std::shared_ptr<kernel::PdfXObject> x_object_;
};
using ImagePtr = std::shared_ptr<Image>;

/// Port of Paragraph.
class Paragraph : public BlockElement<Paragraph> {
 public:
  Paragraph() = default;
  explicit Paragraph(const std::string& text) {
    Add(IElementPtr(std::make_shared<Text>(text)));
  }
  explicit Paragraph(TextPtr text) { Add(IElementPtr(std::move(text))); }
  Paragraph& Add(const std::string& text) {
    return Add(IElementPtr(std::make_shared<Text>(text)));
  }
  template <typename E>
  Paragraph& Add(std::shared_ptr<E> element) {
    child_elements_.push_back(
        std::static_pointer_cast<IElement>(std::move(element)));
    return *this;
  }
  Paragraph& Add(IElementPtr element) {
    child_elements_.push_back(std::move(element));
    return *this;
  }
  Paragraph& Add(IElement& element) { return Add(element.Shared()); }
  Paragraph& SetFirstLineIndent(float indent) {
    return SetProperty(property::kFirstLineIndent, indent);
  }
  Paragraph& SetFixedLeading(float leading) {
    return SetProperty(property::kLeading, Leading(Leading::kFixed, leading));
  }
  Paragraph& SetMultipliedLeading(float leading) {
    return SetProperty(property::kLeading,
                       Leading(Leading::kMultiplied, leading));
  }
  std::any GetDefaultPropertyAny(int property) const override;

 protected:
  IRendererPtr MakeNewRenderer() override;
};
using ParagraphPtr = std::shared_ptr<Paragraph>;

/// Port of Div: a block container.
class Div : public BlockElement<Div> {
 public:
  Div& Add(IElementPtr element) {
    child_elements_.push_back(std::move(element));
    return *this;
  }
  Div& Add(IElement& element) { return Add(element.Shared()); }
  Div& SetFillAvailableArea(bool v) {
    return SetProperty(property::kFillAvailableArea, v);
  }

 protected:
  IRendererPtr MakeNewRenderer() override;
};
using DivPtr = std::shared_ptr<Div>;

/// Port of AreaBreak.
class AreaBreak : public AbstractElement<AreaBreak> {
 public:
  explicit AreaBreak(AreaBreakType type = AreaBreakType::kNextArea)
      : type_(type) {}
  AreaBreak(const kernel::PageSize& page_size)
      : type_(AreaBreakType::kNextPage), page_size_(page_size) {}
  AreaBreakType GetType() const { return type_; }
  const std::optional<kernel::PageSize>& GetPageSize() const {
    return page_size_;
  }

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  AreaBreakType type_;
  std::optional<kernel::PageSize> page_size_;
};

/// Port of LineSeparator.
class LineSeparator : public BlockElement<LineSeparator> {
 public:
  explicit LineSeparator(std::shared_ptr<ILineDrawer> line_drawer) {
    SetProperty(property::kLineDrawer, std::move(line_drawer));
  }

 protected:
  IRendererPtr MakeNewRenderer() override;
};

/// Port of Cell.
class Cell : public BlockElement<Cell> {
 public:
  Cell(int rowspan = 1, int colspan = 1)
      : rowspan_(std::max(rowspan, 1)), colspan_(std::max(colspan, 1)) {}
  Cell& Add(IElementPtr element) {
    child_elements_.push_back(std::move(element));
    return *this;
  }
  Cell& Add(IElement& element) { return Add(element.Shared()); }
  Cell& Add(const std::string& text) {
    return Add(std::make_shared<Paragraph>(text));
  }
  Cell& Add(ImagePtr image) { return Add(IElementPtr(std::move(image))); }
  int GetRowspan() const { return rowspan_; }
  int GetColspan() const { return colspan_; }
  int GetRow() const { return row_; }
  int GetCol() const { return col_; }
  void SetCellIndexes(int row, int col) {
    row_ = row;
    col_ = col;
  }
  std::any GetDefaultPropertyAny(int property) const override;

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  int rowspan_, colspan_;
  int row_ = -1, col_ = -1;
};
using CellPtr = std::shared_ptr<Cell>;

/// Port of Table.
class Table : public BlockElement<Table> {
 public:
  /// Columns with relative (point) widths.
  explicit Table(const std::vector<float>& point_column_widths,
                 bool large_table = false);
  /// Columns with unit-value widths (points or percents).
  explicit Table(const std::vector<UnitValue>& column_widths,
                 bool large_table = false);
  /// num_columns equal-width columns.
  explicit Table(int num_columns, bool large_table = false);

  Table& UseAllAvailableWidth() {
    return SetWidth(UnitValue::CreatePercentValue(100));
  }
  Table& AddCell(CellPtr cell);
  Table& AddCell(const std::string& content);
  Table& AddCell(IElementPtr element);
  /// Adds a Cell (if the element is a Cell) or wraps the element into a new
  /// cell.
  Table& AddCell(IElement& element);
  Table& AddHeaderCell(IElement& element);
  Table& AddHeaderCell(CellPtr header_cell);
  Table& AddHeaderCell(const std::string& content);
  Table& AddFooterCell(CellPtr footer_cell);
  Table& AddFooterCell(const std::string& content);
  Table& StartNewRow();
  int GetNumberOfColumns() const {
    return static_cast<int>(column_widths_.size());
  }
  int GetNumberOfRows() const { return static_cast<int>(rows_.size()); }
  const std::vector<UnitValue>& GetColumnWidths() const {
    return column_widths_;
  }
  CellPtr GetCell(int row, int column) const;
  const std::vector<std::vector<CellPtr>>& GetRows() const { return rows_; }
  const std::shared_ptr<Table>& GetHeader() const { return header_; }
  const std::shared_ptr<Table>& GetFooter() const { return footer_; }
  Table& SetSkipFirstHeader(bool v) {
    skip_first_header_ = v;
    return *this;
  }
  bool IsSkipFirstHeader() const { return skip_first_header_; }
  Table& SetSkipLastFooter(bool v) {
    skip_last_footer_ = v;
    return *this;
  }
  bool IsSkipLastFooter() const { return skip_last_footer_; }
  std::any GetDefaultPropertyAny(int property) const override;

 protected:
  IRendererPtr MakeNewRenderer() override;

 private:
  void EnsureHeader();
  void EnsureFooter();
  std::vector<UnitValue> column_widths_;
  std::vector<std::vector<CellPtr>>
      rows_;  // rows_[r][c] = cell starting at (r,c) or nullptr
  int current_row_ = -1;
  int current_column_ = 0;
  std::vector<int>
      rowspans_left_;  // per column, remaining rows covered by a rowspan
  std::shared_ptr<Table> header_, footer_;
  bool skip_first_header_ = false, skip_last_footer_ = false;
};
using TablePtr = std::shared_ptr<Table>;

/// Port of ListItem.
class ListItem : public BlockElement<ListItem> {
 public:
  ListItem() = default;
  explicit ListItem(const std::string& text) {
    Add(std::make_shared<Paragraph>(text));
  }
  ListItem& Add(IElementPtr element) {
    child_elements_.push_back(std::move(element));
    return *this;
  }
  ListItem& Add(IElement& element) { return Add(element.Shared()); }
  ListItem& SetListSymbol(const std::string& symbol) {
    return SetProperty(property::kListSymbol, std::make_shared<Text>(symbol));
  }
  ListItem& SetListSymbol(ListNumberingType type) {
    return SetProperty(property::kListSymbol, type);
  }

 protected:
  IRendererPtr MakeNewRenderer() override;
};
using ListItemPtr = std::shared_ptr<ListItem>;

/// Port of List.
class List : public BlockElement<List> {
 public:
  static constexpr const char* kDefaultListSymbol = "- ";
  List() = default;
  explicit List(ListNumberingType list_numbering_type) {
    SetListSymbol(list_numbering_type);
  }
  List& Add(ListItemPtr item) {
    child_elements_.push_back(std::move(item));
    return *this;
  }
  List& Add(const std::string& text) {
    return Add(std::make_shared<ListItem>(text));
  }
  List& Add(ListItem& item) {
    return Add(std::static_pointer_cast<ListItem>(item.Shared()));
  }
  List& SetItemStartIndex(int start) {
    return SetProperty(property::kListStart, start);
  }
  List& SetListSymbol(const std::string& symbol) {
    return SetProperty(property::kListSymbol, std::make_shared<Text>(symbol));
  }
  List& SetListSymbol(TextPtr text) {
    return SetProperty(property::kListSymbol, std::move(text));
  }
  List& SetListSymbol(ListNumberingType type);
  List& SetSymbolIndent(float indent) {
    return SetProperty(property::kListSymbolIndent, indent);
  }
  List& SetPreSymbolText(const std::string& s) {
    return SetProperty(property::kListSymbolPreText, s);
  }
  List& SetPostSymbolText(const std::string& s) {
    return SetProperty(property::kListSymbolPostText, s);
  }
  std::any GetDefaultPropertyAny(int property) const override;

 protected:
  IRendererPtr MakeNewRenderer() override;
};
using ListPtr = std::shared_ptr<List>;

/// Shared-pointer helper: elements are always used through shared_ptr.
template <typename T, typename... Args>
std::shared_ptr<T> Make(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

}  // namespace layout
}  // namespace pdfcpp

#endif  // PDFCPP_LAYOUT_ELEMENTS_H_
