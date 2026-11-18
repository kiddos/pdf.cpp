#pragma once

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/image/ImageData.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"
#include "pdfcpp/layout/Properties.h"

namespace pdfcpp {
namespace layout {

class IRenderer;
using IRendererPtr = std::shared_ptr<IRenderer>;

/// Port of IPropertyContainer.
class IPropertyContainer {
public:
    virtual ~IPropertyContainer() = default;
    virtual bool hasProperty(int property) const = 0;
    virtual bool hasOwnProperty(int property) const = 0;
    /// Returns a pointer to the stored value or nullptr.
    virtual const std::any* getPropertyAny(int property) const = 0;
    virtual const std::any* getOwnPropertyAny(int property) const = 0;
    virtual std::any getDefaultPropertyAny(int property) const { return std::any(); }
    virtual void setPropertyAny(int property, std::any value) = 0;
    virtual void deleteOwnProperty(int property) = 0;

    template <typename V>
    std::optional<V> getProperty(int property) const {
        const std::any* a = getPropertyAny(property);
        if (a && a->has_value()) {
            if (const V* v = std::any_cast<V>(a)) return *v;
        }
        std::any d = getDefaultPropertyAny(property);
        if (d.has_value())
            if (const V* v = std::any_cast<V>(&d)) return *v;
        return std::nullopt;
    }
};

/// Port of ElementPropertyContainer: properties + fluent setters (CRTP returns the concrete type).
template <typename T>
class ElementPropertyContainer : public virtual IPropertyContainer {
public:
    bool hasProperty(int property) const override { return hasOwnProperty(property); }
    bool hasOwnProperty(int property) const override { return properties_.count(property) > 0; }
    const std::any* getPropertyAny(int property) const override { return getOwnPropertyAny(property); }
    const std::any* getOwnPropertyAny(int property) const override {
        auto it = properties_.find(property);
        return it == properties_.end() ? nullptr : &it->second;
    }
    void setPropertyAny(int property, std::any value) override { properties_[property] = std::move(value); }
    void deleteOwnProperty(int property) override { properties_.erase(property); }

    template <typename V>
    T& setProperty(int property, V value) {
        properties_[property] = std::move(value);
        return self();
    }

    T& setRelativePosition(float left, float top, float right, float bottom);
    T& setFixedPosition(float left, float bottom, float width) {
        return setFixedPosition(left, bottom, UnitValue::createPointValue(width));
    }
    T& setFixedPosition(float left, float bottom, UnitValue width) {
        setProperty(Property::POSITION, 4);  // LayoutPosition.FIXED
        setProperty(Property::LEFT, left);
        setProperty(Property::BOTTOM, bottom);
        return setProperty(Property::WIDTH, width);
    }
    T& setFixedPosition(int pageNumber, float left, float bottom, float width) {
        setFixedPosition(left, bottom, width);
        return setProperty(Property::PAGE_NUMBER, pageNumber);
    }
    T& setFont(std::shared_ptr<kernel::PdfFont> font) { return setProperty(Property::FONT, std::move(font)); }
    T& setFontColor(ColorPtr fontColor) { return setProperty(Property::FONT_COLOR, std::move(fontColor)); }
    T& setFontColor(ColorPtr fontColor, float opacity) {
        setProperty(Property::OPACITY, opacity);
        return setFontColor(std::move(fontColor));
    }
    T& setFontSize(float fontSize) { return setProperty(Property::FONT_SIZE, UnitValue::createPointValue(fontSize)); }
    T& setTextAlignment(TextAlignment alignment) { return setProperty(Property::TEXT_ALIGNMENT, alignment); }
    T& setCharacterSpacing(float charSpacing) { return setProperty(Property::CHARACTER_SPACING, charSpacing); }
    T& setWordSpacing(float wordSpacing) { return setProperty(Property::WORD_SPACING, wordSpacing); }
    T& setFontKerning(bool kerning) { return setProperty(Property::FONT_KERNING, kerning); }
    T& setBackgroundColor(ColorPtr color) { return setProperty(Property::BACKGROUND, Background(std::move(color))); }
    T& setBackgroundColor(ColorPtr color, float opacity) {
        return setProperty(Property::BACKGROUND, Background(std::move(color), opacity));
    }
    T& setBorder(BorderPtr border) {
        setProperty(Property::BORDER_TOP, border);
        setProperty(Property::BORDER_RIGHT, border);
        setProperty(Property::BORDER_BOTTOM, border);
        return setProperty(Property::BORDER_LEFT, border);
    }
    T& setBorderTop(BorderPtr border) { return setProperty(Property::BORDER_TOP, std::move(border)); }
    T& setBorderRight(BorderPtr border) { return setProperty(Property::BORDER_RIGHT, std::move(border)); }
    T& setBorderBottom(BorderPtr border) { return setProperty(Property::BORDER_BOTTOM, std::move(border)); }
    T& setBorderLeft(BorderPtr border) { return setProperty(Property::BORDER_LEFT, std::move(border)); }
    T& setSplitCharacters(int) { return self(); }
    T& setTextRenderingMode(int mode) { return setProperty(Property::TEXT_RENDERING_MODE, mode); }
    T& setStrokeColor(ColorPtr color) { return setProperty(Property::STROKE_COLOR, std::move(color)); }
    T& setStrokeWidth(float width) { return setProperty(Property::STROKE_WIDTH, width); }
    T& simulateBold() { return setProperty(Property::BOLD_SIMULATION, true); }
    T& setBold() { return simulateBold(); }
    T& simulateItalic() { return setProperty(Property::ITALIC_SIMULATION, true); }
    T& setItalic() { return simulateItalic(); }
    T& setLineThrough() { return setUnderline(nullptr, .75f, 0, 0, 1 / 4.f); }
    T& setUnderline() { return setUnderline(nullptr, .75f, 0, 0, -1 / 8.f); }
    T& setUnderline(float thickness, float yPosition) { return setUnderline(nullptr, thickness, 0, yPosition, 0); }
    T& setUnderline(ColorPtr color, float thickness, float thicknessMul, float yPosition, float yPositionMul) {
        auto it = properties_.find(Property::UNDERLINE);
        std::vector<Underline> list;
        if (it != properties_.end())
            if (auto* l = std::any_cast<std::vector<Underline>>(&it->second)) list = *l;
        list.emplace_back(std::move(color), thickness, thicknessMul, yPosition, yPositionMul);
        return setProperty(Property::UNDERLINE, list);
    }
    T& setTextRise(float textRise) { return setProperty(Property::TEXT_RISE, textRise); }
    T& setHorizontalScaling(float scale) { return setProperty(Property::HORIZONTAL_SCALING, scale); }
    T& setDestination(const std::string& destination) { return setProperty(Property::DESTINATION, destination); }
    T& setOpacity(float opacity) { return setProperty(Property::OPACITY, opacity); }

protected:
    T& self() { return static_cast<T&>(*this); }
    std::map<int, std::any> properties_;
};

/// Port of IElement.
class IElement : public virtual IPropertyContainer, public std::enable_shared_from_this<IElement> {
public:
    /// Shared pointer to this element (elements must be created with std::make_shared / layout::make).
    std::shared_ptr<IElement> shared() { return shared_from_this(); }
    virtual IRendererPtr getRenderer() = 0;
    virtual IRendererPtr createRendererSubTree() = 0;
};

using IElementPtr = std::shared_ptr<IElement>;

/// Port of AbstractElement.
template <typename T>
class AbstractElement : public ElementPropertyContainer<T>, public IElement {
public:
    bool hasProperty(int property) const override { return ElementPropertyContainer<T>::hasProperty(property); }
    bool hasOwnProperty(int property) const override { return ElementPropertyContainer<T>::hasOwnProperty(property); }
    const std::any* getPropertyAny(int property) const override { return ElementPropertyContainer<T>::getPropertyAny(property); }
    const std::any* getOwnPropertyAny(int property) const override { return ElementPropertyContainer<T>::getOwnPropertyAny(property); }
    void setPropertyAny(int property, std::any value) override { ElementPropertyContainer<T>::setPropertyAny(property, std::move(value)); }
    void deleteOwnProperty(int property) override { ElementPropertyContainer<T>::deleteOwnProperty(property); }

    const std::vector<IElementPtr>& getChildren() const { return childElements_; }
    IRendererPtr getRenderer() override { return nextRenderer_ ? nextRenderer_ : makeNewRenderer(); }
    void setNextRenderer(IRendererPtr renderer) { nextRenderer_ = std::move(renderer); }
    IRendererPtr createRendererSubTree() override;
    bool isEmpty() const { return childElements_.empty(); }
    T& addStyle(const std::map<int, std::any>& style) {
        for (const auto& kv : style) this->properties_[kv.first] = kv.second;
        return this->self();
    }

protected:
    virtual IRendererPtr makeNewRenderer() = 0;
    std::vector<IElementPtr> childElements_;
    IRendererPtr nextRenderer_;
};

/// Port of BlockElement: margins, paddings, sizes.
template <typename T>
class BlockElement : public AbstractElement<T> {
public:
    T& setMarginLeft(float v) { return this->setProperty(Property::MARGIN_LEFT, UnitValue::createPointValue(v)); }
    T& setMarginRight(float v) { return this->setProperty(Property::MARGIN_RIGHT, UnitValue::createPointValue(v)); }
    T& setMarginTop(float v) { return this->setProperty(Property::MARGIN_TOP, UnitValue::createPointValue(v)); }
    T& setMarginBottom(float v) { return this->setProperty(Property::MARGIN_BOTTOM, UnitValue::createPointValue(v)); }
    T& setMargin(float v) { return setMargins(v, v, v, v); }
    T& setMargins(float top, float right, float bottom, float left) {
        setMarginTop(top);
        setMarginRight(right);
        setMarginBottom(bottom);
        return setMarginLeft(left);
    }
    T& setPaddingLeft(float v) { return this->setProperty(Property::PADDING_LEFT, UnitValue::createPointValue(v)); }
    T& setPaddingRight(float v) { return this->setProperty(Property::PADDING_RIGHT, UnitValue::createPointValue(v)); }
    T& setPaddingTop(float v) { return this->setProperty(Property::PADDING_TOP, UnitValue::createPointValue(v)); }
    T& setPaddingBottom(float v) { return this->setProperty(Property::PADDING_BOTTOM, UnitValue::createPointValue(v)); }
    T& setPadding(float v) { return setPaddings(v, v, v, v); }
    T& setPaddings(float top, float right, float bottom, float left) {
        setPaddingTop(top);
        setPaddingRight(right);
        setPaddingBottom(bottom);
        return setPaddingLeft(left);
    }
    T& setVerticalAlignment(VerticalAlignment v) { return this->setProperty(Property::VERTICAL_ALIGNMENT, v); }
    T& setSpacingRatio(float ratio) { return this->setProperty(Property::SPACING_RATIO, ratio); }
    T& setKeepTogether(bool v) { return this->setProperty(Property::KEEP_TOGETHER, v); }
    bool isKeepTogether() const {
        auto v = this->template getProperty<bool>(Property::KEEP_TOGETHER);
        return v && *v;
    }
    T& setKeepWithNext(bool v) { return this->setProperty(Property::KEEP_WITH_NEXT, v); }
    T& setRotationAngle(double radAngle) { return this->setProperty(Property::ROTATION_ANGLE, static_cast<float>(radAngle)); }
    T& setWidth(float width) { return this->setProperty(Property::WIDTH, UnitValue::createPointValue(width)); }
    T& setWidth(UnitValue width) { return this->setProperty(Property::WIDTH, width); }
    T& setHeight(float height) { return this->setProperty(Property::HEIGHT, UnitValue::createPointValue(height)); }
    T& setMaxHeight(float v) { return this->setProperty(Property::MAX_HEIGHT, UnitValue::createPointValue(v)); }
    T& setMinHeight(float v) { return this->setProperty(Property::MIN_HEIGHT, UnitValue::createPointValue(v)); }
    T& setMaxWidth(float v) { return this->setProperty(Property::MAX_WIDTH, UnitValue::createPointValue(v)); }
    T& setMinWidth(float v) { return this->setProperty(Property::MIN_WIDTH, UnitValue::createPointValue(v)); }
    T& setHorizontalAlignment(HorizontalAlignment v) { return this->setProperty(Property::HORIZONTAL_ALIGNMENT, v); }
    std::any getDefaultPropertyAny(int property) const override;
};

class Paragraph;
class Image;

/// Port of Text: a leaf element holding a chunk of text (UTF-8).
class Text : public AbstractElement<Text> {
public:
    explicit Text(std::string text) : text_(std::move(text)) {}
    const std::string& getText() const { return text_; }
    void setText(std::string text) { text_ = std::move(text); }
    float getTextRise() const;
    float getHorizontalScaling() const;
    Text& setSkew(float alphaAngle, float betaAngle) {
        return setProperty(Property::SKEW, std::vector<float>{alphaAngle, betaAngle});
    }

protected:
    IRendererPtr makeNewRenderer() override;

private:
    std::string text_;
};
using TextPtr = std::shared_ptr<Text>;

/// Port of Link (a Text with an URI action).
class Link : public Text {
public:
    Link(std::string text, std::string uri) : Text(std::move(text)), uri_(std::move(uri)) {}
    const std::string& getUri() const { return uri_; }

protected:
    IRendererPtr makeNewRenderer() override;

private:
    std::string uri_;
};

/// Port of Image.
class Image : public AbstractElement<Image> {
public:
    explicit Image(std::shared_ptr<io::ImageData> img);
    explicit Image(std::shared_ptr<kernel::PdfImageXObject> xObject);
    explicit Image(std::shared_ptr<kernel::PdfFormXObject> xObject);
    Image(std::shared_ptr<io::ImageData> img, float left, float bottom);
    const std::shared_ptr<kernel::PdfXObject>& getXObject() const { return xObject_; }
    Image& setRotationAngle(double radAngle) { return setProperty(Property::ROTATION_ANGLE, static_cast<float>(radAngle)); }
    Image& setMarginLeft(float v) { return setProperty(Property::MARGIN_LEFT, UnitValue::createPointValue(v)); }
    Image& setMarginRight(float v) { return setProperty(Property::MARGIN_RIGHT, UnitValue::createPointValue(v)); }
    Image& setMarginTop(float v) { return setProperty(Property::MARGIN_TOP, UnitValue::createPointValue(v)); }
    Image& setMarginBottom(float v) { return setProperty(Property::MARGIN_BOTTOM, UnitValue::createPointValue(v)); }
    Image& setMargins(float top, float right, float bottom, float left) {
        setMarginTop(top);
        setMarginRight(right);
        setMarginBottom(bottom);
        return setMarginLeft(left);
    }
    Image& scale(float horizontalScaling, float verticalScaling) {
        setProperty(Property::HORIZONTAL_SCALING, horizontalScaling);
        return setProperty(Property::VERTICAL_SCALING, verticalScaling);
    }
    Image& scaleToFit(float fitWidth, float fitHeight);
    Image& scaleAbsolute(float fitWidth, float fitHeight) {
        float horizontalScaling = fitWidth / xObject_->getWidth();
        float verticalScaling = fitHeight / xObject_->getHeight();
        return scale(horizontalScaling, verticalScaling);
    }
    Image& setAutoScale(bool autoScale) {
        setProperty(Property::AUTO_SCALE_WIDTH, autoScale);
        setProperty(Property::AUTO_SCALE_HEIGHT, autoScale);
        return setProperty(Property::AUTO_SCALE, autoScale);
    }
    Image& setAutoScaleHeight(bool v) { return setProperty(Property::AUTO_SCALE_HEIGHT, v); }
    Image& setAutoScaleWidth(bool v) { return setProperty(Property::AUTO_SCALE_WIDTH, v); }
    Image& setWidth(float width) { return setProperty(Property::WIDTH, UnitValue::createPointValue(width)); }
    Image& setWidth(UnitValue width) { return setProperty(Property::WIDTH, width); }
    Image& setHeight(float height) { return setProperty(Property::HEIGHT, UnitValue::createPointValue(height)); }
    Image& setHorizontalAlignment(HorizontalAlignment v) { return setProperty(Property::HORIZONTAL_ALIGNMENT, v); }
    Image& setPadding(float v) {
        setProperty(Property::PADDING_TOP, UnitValue::createPointValue(v));
        setProperty(Property::PADDING_RIGHT, UnitValue::createPointValue(v));
        setProperty(Property::PADDING_BOTTOM, UnitValue::createPointValue(v));
        return setProperty(Property::PADDING_LEFT, UnitValue::createPointValue(v));
    }
    float getImageWidth() const { return xObject_->getWidth(); }
    float getImageHeight() const { return xObject_->getHeight(); }
    float getImageScaledWidth() const;
    float getImageScaledHeight() const;

protected:
    IRendererPtr makeNewRenderer() override;

private:
    std::shared_ptr<kernel::PdfXObject> xObject_;
};
using ImagePtr = std::shared_ptr<Image>;

/// Port of Paragraph.
class Paragraph : public BlockElement<Paragraph> {
public:
    Paragraph() = default;
    explicit Paragraph(const std::string& text) { add(IElementPtr(std::make_shared<Text>(text))); }
    explicit Paragraph(TextPtr text) { add(IElementPtr(std::move(text))); }
    Paragraph& add(const std::string& text) { return add(IElementPtr(std::make_shared<Text>(text))); }
    template <typename E>
    Paragraph& add(std::shared_ptr<E> element) {
        childElements_.push_back(std::static_pointer_cast<IElement>(std::move(element)));
        return *this;
    }
    Paragraph& add(IElementPtr element) {
        childElements_.push_back(std::move(element));
        return *this;
    }
    Paragraph& add(IElement& element) { return add(element.shared()); }
    Paragraph& setFirstLineIndent(float indent) { return setProperty(Property::FIRST_LINE_INDENT, indent); }
    Paragraph& setFixedLeading(float leading) { return setProperty(Property::LEADING, Leading(Leading::FIXED, leading)); }
    Paragraph& setMultipliedLeading(float leading) { return setProperty(Property::LEADING, Leading(Leading::MULTIPLIED, leading)); }
    std::any getDefaultPropertyAny(int property) const override;

protected:
    IRendererPtr makeNewRenderer() override;
};
using ParagraphPtr = std::shared_ptr<Paragraph>;

/// Port of Div: a block container.
class Div : public BlockElement<Div> {
public:
    Div& add(IElementPtr element) {
        childElements_.push_back(std::move(element));
        return *this;
    }
    Div& add(IElement& element) { return add(element.shared()); }
    Div& setFillAvailableArea(bool v) { return setProperty(Property::FILL_AVAILABLE_AREA, v); }

protected:
    IRendererPtr makeNewRenderer() override;
};
using DivPtr = std::shared_ptr<Div>;

/// Port of AreaBreak.
class AreaBreak : public AbstractElement<AreaBreak> {
public:
    explicit AreaBreak(AreaBreakType type = AreaBreakType::NEXT_AREA) : type_(type) {}
    AreaBreak(const kernel::PageSize& pageSize) : type_(AreaBreakType::NEXT_PAGE), pageSize_(pageSize) {}
    AreaBreakType getType() const { return type_; }
    const std::optional<kernel::PageSize>& getPageSize() const { return pageSize_; }

protected:
    IRendererPtr makeNewRenderer() override;

private:
    AreaBreakType type_;
    std::optional<kernel::PageSize> pageSize_;
};

/// Port of LineSeparator.
class LineSeparator : public BlockElement<LineSeparator> {
public:
    explicit LineSeparator(std::shared_ptr<ILineDrawer> lineDrawer) { setProperty(Property::LINE_DRAWER, std::move(lineDrawer)); }

protected:
    IRendererPtr makeNewRenderer() override;
};

/// Port of Cell.
class Cell : public BlockElement<Cell> {
public:
    Cell(int rowspan = 1, int colspan = 1) : rowspan_(std::max(rowspan, 1)), colspan_(std::max(colspan, 1)) {}
    Cell& add(IElementPtr element) {
        childElements_.push_back(std::move(element));
        return *this;
    }
    Cell& add(IElement& element) { return add(element.shared()); }
    Cell& add(const std::string& text) { return add(std::make_shared<Paragraph>(text)); }
    Cell& add(ImagePtr image) { return add(IElementPtr(std::move(image))); }
    int getRowspan() const { return rowspan_; }
    int getColspan() const { return colspan_; }
    int getRow() const { return row_; }
    int getCol() const { return col_; }
    void setCellIndexes(int row, int col) {
        row_ = row;
        col_ = col;
    }
    std::any getDefaultPropertyAny(int property) const override;

protected:
    IRendererPtr makeNewRenderer() override;

private:
    int rowspan_, colspan_;
    int row_ = -1, col_ = -1;
};
using CellPtr = std::shared_ptr<Cell>;

/// Port of Table.
class Table : public BlockElement<Table> {
public:
    /// Columns with relative (point) widths.
    explicit Table(const std::vector<float>& pointColumnWidths, bool largeTable = false);
    /// Columns with unit-value widths (points or percents).
    explicit Table(const std::vector<UnitValue>& columnWidths, bool largeTable = false);
    /// numColumns equal-width columns.
    explicit Table(int numColumns, bool largeTable = false);

    Table& useAllAvailableWidth() { return setWidth(UnitValue::createPercentValue(100)); }
    Table& addCell(CellPtr cell);
    Table& addCell(const std::string& content);
    Table& addCell(IElementPtr element);
    /// Adds a Cell (if the element is a Cell) or wraps the element into a new cell.
    Table& addCell(IElement& element);
    Table& addHeaderCell(IElement& element);
    Table& addHeaderCell(CellPtr headerCell);
    Table& addHeaderCell(const std::string& content);
    Table& addFooterCell(CellPtr footerCell);
    Table& addFooterCell(const std::string& content);
    Table& startNewRow();
    int getNumberOfColumns() const { return static_cast<int>(columnWidths_.size()); }
    int getNumberOfRows() const { return static_cast<int>(rows_.size()); }
    const std::vector<UnitValue>& getColumnWidths() const { return columnWidths_; }
    CellPtr getCell(int row, int column) const;
    const std::vector<std::vector<CellPtr>>& getRows() const { return rows_; }
    const std::shared_ptr<Table>& getHeader() const { return header_; }
    const std::shared_ptr<Table>& getFooter() const { return footer_; }
    Table& setSkipFirstHeader(bool v) {
        skipFirstHeader_ = v;
        return *this;
    }
    bool isSkipFirstHeader() const { return skipFirstHeader_; }
    Table& setSkipLastFooter(bool v) {
        skipLastFooter_ = v;
        return *this;
    }
    bool isSkipLastFooter() const { return skipLastFooter_; }
    std::any getDefaultPropertyAny(int property) const override;

protected:
    IRendererPtr makeNewRenderer() override;

private:
    void ensureHeader();
    void ensureFooter();
    std::vector<UnitValue> columnWidths_;
    std::vector<std::vector<CellPtr>> rows_;  // rows_[r][c] = cell starting at (r,c) or nullptr
    int currentRow_ = -1;
    int currentColumn_ = 0;
    std::vector<int> rowspansLeft_;  // per column, remaining rows covered by a rowspan
    std::shared_ptr<Table> header_, footer_;
    bool skipFirstHeader_ = false, skipLastFooter_ = false;
};
using TablePtr = std::shared_ptr<Table>;

/// Port of ListItem.
class ListItem : public BlockElement<ListItem> {
public:
    ListItem() = default;
    explicit ListItem(const std::string& text) { add(std::make_shared<Paragraph>(text)); }
    ListItem& add(IElementPtr element) {
        childElements_.push_back(std::move(element));
        return *this;
    }
    ListItem& add(IElement& element) { return add(element.shared()); }
    ListItem& setListSymbol(const std::string& symbol) { return setProperty(Property::LIST_SYMBOL, std::make_shared<Text>(symbol)); }
    ListItem& setListSymbol(ListNumberingType type) { return setProperty(Property::LIST_SYMBOL, type); }

protected:
    IRendererPtr makeNewRenderer() override;
};
using ListItemPtr = std::shared_ptr<ListItem>;

/// Port of List.
class List : public BlockElement<List> {
public:
    static constexpr const char* DEFAULT_LIST_SYMBOL = "- ";
    List() = default;
    explicit List(ListNumberingType listNumberingType) { setListSymbol(listNumberingType); }
    List& add(ListItemPtr item) {
        childElements_.push_back(std::move(item));
        return *this;
    }
    List& add(const std::string& text) { return add(std::make_shared<ListItem>(text)); }
    List& add(ListItem& item) { return add(std::static_pointer_cast<ListItem>(item.shared())); }
    List& setItemStartIndex(int start) { return setProperty(Property::LIST_START, start); }
    List& setListSymbol(const std::string& symbol) { return setProperty(Property::LIST_SYMBOL, std::make_shared<Text>(symbol)); }
    List& setListSymbol(TextPtr text) { return setProperty(Property::LIST_SYMBOL, std::move(text)); }
    List& setListSymbol(ListNumberingType type);
    List& setSymbolIndent(float indent) { return setProperty(Property::LIST_SYMBOL_INDENT, indent); }
    List& setPreSymbolText(const std::string& s) { return setProperty(Property::LIST_SYMBOL_PRE_TEXT, s); }
    List& setPostSymbolText(const std::string& s) { return setProperty(Property::LIST_SYMBOL_POST_TEXT, s); }
    std::any getDefaultPropertyAny(int property) const override;

protected:
    IRendererPtr makeNewRenderer() override;
};
using ListPtr = std::shared_ptr<List>;

/// Shared-pointer helper: elements are always used through shared_ptr.
template <typename T, typename... Args>
std::shared_ptr<T> make(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

}  // namespace layout
}  // namespace pdfcpp
