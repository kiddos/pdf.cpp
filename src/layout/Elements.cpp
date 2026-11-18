#include "pdfcpp/layout/Elements.h"

#include "pdfcpp/layout/Renderers.h"

namespace pdfcpp {
namespace layout {

// ------------------------------------------------------------ AbstractElement
template <typename T>
IRendererPtr AbstractElement<T>::createRendererSubTree() {
    IRendererPtr rendererRoot = getRenderer();
    for (const auto& child : childElements_) {
        IRendererPtr childRenderer = child->createRendererSubTree();
        if (childRenderer) rendererRoot->addChild(childRenderer);
    }
    return rendererRoot;
}

template <typename T>
std::any BlockElement<T>::getDefaultPropertyAny(int property) const {
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
float Text::getTextRise() const {
    auto v = getProperty<float>(Property::TEXT_RISE);
    return v ? *v : 0;
}

float Text::getHorizontalScaling() const {
    auto v = getProperty<float>(Property::HORIZONTAL_SCALING);
    return v ? *v : 1;
}

IRendererPtr Text::makeNewRenderer() {
    std::shared_ptr<IElement> self;
    try {
        self = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        self = std::shared_ptr<IElement>(std::shared_ptr<IElement>{}, static_cast<IElement*>(this));
    }
    return std::make_shared<TextRenderer>(self);
}

IRendererPtr Link::makeNewRenderer() { return Text::makeNewRenderer(); }

// Renderers keep a non-owning alias to the element: elements are owned by the user/document tree.
template <typename E>
static std::shared_ptr<IElement> alias(E* e) {
    try {
        return static_cast<IElement*>(e)->shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return std::shared_ptr<IElement>(std::shared_ptr<IElement>{}, static_cast<IElement*>(e));
    }
}

// ------------------------------------------------------------ Image
Image::Image(std::shared_ptr<io::ImageData> img) : xObject_(std::make_shared<kernel::PdfImageXObject>(img)) {}
Image::Image(std::shared_ptr<kernel::PdfImageXObject> xObject) : xObject_(std::move(xObject)) {}
Image::Image(std::shared_ptr<kernel::PdfFormXObject> xObject) : xObject_(std::move(xObject)) {}
Image::Image(std::shared_ptr<io::ImageData> img, float left, float bottom) : Image(std::move(img)) {
    setProperty(Property::POSITION, 4);
    setProperty(Property::LEFT, left);
    setProperty(Property::BOTTOM, bottom);
}

Image& Image::scaleToFit(float fitWidth, float fitHeight) {
    float horizontalScaling = fitWidth / xObject_->getWidth();
    float verticalScaling = fitHeight / xObject_->getHeight();
    float s = std::min(horizontalScaling, verticalScaling);
    return scale(s, s);
}

float Image::getImageScaledWidth() const {
    auto s = getProperty<float>(Property::HORIZONTAL_SCALING);
    return xObject_->getWidth() * (s ? *s : 1);
}

float Image::getImageScaledHeight() const {
    auto s = getProperty<float>(Property::VERTICAL_SCALING);
    return xObject_->getHeight() * (s ? *s : 1);
}

IRendererPtr Image::makeNewRenderer() { return std::make_shared<ImageRenderer>(alias(this)); }

// ------------------------------------------------------------ Paragraph / Div / AreaBreak / LineSeparator
std::any Paragraph::getDefaultPropertyAny(int property) const {
    switch (property) {
        case Property::LEADING: {
            bool singleImage = childElements_.size() == 1 && dynamic_cast<Image*>(childElements_[0].get());
            return Leading(Leading::MULTIPLIED, singleImage ? 1.f : 1.35f);
        }
        case Property::FIRST_LINE_INDENT:
            return 0.f;
        case Property::MARGIN_TOP:
        case Property::MARGIN_BOTTOM:
            return UnitValue::createPointValue(4);
        case Property::TAB_DEFAULT:
            return 50.f;
        default:
            return BlockElement<Paragraph>::getDefaultPropertyAny(property);
    }
}

IRendererPtr Paragraph::makeNewRenderer() { return std::make_shared<ParagraphRenderer>(alias(this)); }
IRendererPtr Div::makeNewRenderer() { return std::make_shared<BlockRenderer>(alias(this)); }
IRendererPtr AreaBreak::makeNewRenderer() { return std::make_shared<AreaBreakRenderer>(alias(this)); }
IRendererPtr LineSeparator::makeNewRenderer() { return std::make_shared<LineSeparatorRenderer>(alias(this)); }

// ------------------------------------------------------------ Cell
std::any Cell::getDefaultPropertyAny(int property) const {
    static const BorderPtr DEFAULT_BORDER = std::make_shared<SolidBorder>(0.5f);
    switch (property) {
        case Property::BORDER_TOP:
        case Property::BORDER_RIGHT:
        case Property::BORDER_BOTTOM:
        case Property::BORDER_LEFT:
            return DEFAULT_BORDER;
        case Property::PADDING_BOTTOM:
        case Property::PADDING_LEFT:
        case Property::PADDING_RIGHT:
        case Property::PADDING_TOP:
            return UnitValue::createPointValue(2);
        default:
            return BlockElement<Cell>::getDefaultPropertyAny(property);
    }
}

IRendererPtr Cell::makeNewRenderer() { return std::make_shared<BlockRenderer>(alias(this)); }

// ------------------------------------------------------------ Table
Table::Table(const std::vector<float>& pointColumnWidths, bool) {
    if (pointColumnWidths.empty()) throw std::invalid_argument("The widths array in table constructor can not be empty.");
    columnWidths_ = UnitValue::createPointArray(pointColumnWidths);
    rowspansLeft_.assign(columnWidths_.size(), 0);
}

Table::Table(const std::vector<UnitValue>& columnWidths, bool) : columnWidths_(columnWidths) {
    if (columnWidths_.empty()) throw std::invalid_argument("The widths array in table constructor can not be empty.");
    rowspansLeft_.assign(columnWidths_.size(), 0);
}

Table::Table(int numColumns, bool) {
    if (numColumns <= 0) throw std::invalid_argument("The number of columns in Table constructor must be greater than zero");
    columnWidths_ = UnitValue::createPercentArray(std::vector<float>(numColumns, 1.f));
    rowspansLeft_.assign(numColumns, 0);
    useAllAvailableWidth();
}

std::any Table::getDefaultPropertyAny(int property) const {
    switch (property) {
        case Property::BORDER_COLLAPSE:
            return BorderCollapsePropertyValue::COLLAPSE;
        default:
            return BlockElement<Table>::getDefaultPropertyAny(property);
    }
}

Table& Table::startNewRow() {
    currentColumn_ = static_cast<int>(columnWidths_.size());
    return *this;
}

Table& Table::addCell(CellPtr cell) {
    int numColumns = static_cast<int>(columnWidths_.size());
    // find next free position
    while (true) {
        if (currentRow_ < 0 || currentColumn_ >= numColumns) {
            ++currentRow_;
            currentColumn_ = 0;
            rows_.emplace_back(numColumns);
            for (int c = 0; c < numColumns; ++c)
                if (rowspansLeft_[c] > 0) --rowspansLeft_[c];
        }
        if (rowspansLeft_[currentColumn_] > 0) {
            ++currentColumn_;
            continue;
        }
        break;
    }
    int colspan = std::min(cell->getColspan(), numColumns - currentColumn_);
    if (colspan < cell->getColspan()) cell = std::make_shared<Cell>(*cell);  // shrink colspan conservatively
    cell->setCellIndexes(currentRow_, currentColumn_);
    rows_[currentRow_][currentColumn_] = cell;
    for (int c = currentColumn_; c < currentColumn_ + colspan; ++c) rowspansLeft_[c] = cell->getRowspan();
    // rowspansLeft_ counts rows including the current one; decrement happens on new row
    currentColumn_ += colspan;
    return *this;
}

Table& Table::addCell(const std::string& content) {
    auto cell = std::make_shared<Cell>();
    cell->add(content);
    return addCell(cell);
}

Table& Table::addCell(IElementPtr element) {
    auto cell = std::make_shared<Cell>();
    cell->add(std::move(element));
    return addCell(cell);
}

Table& Table::addCell(IElement& element) {
    if (auto cell = std::dynamic_pointer_cast<Cell>(element.shared())) return addCell(cell);
    return addCell(element.shared());
}

Table& Table::addHeaderCell(IElement& element) {
    if (auto cell = std::dynamic_pointer_cast<Cell>(element.shared())) return addHeaderCell(cell);
    auto cell = std::make_shared<Cell>();
    cell->add(element.shared());
    return addHeaderCell(cell);
}

void Table::ensureHeader() {
    if (!header_) {
        header_ = std::make_shared<Table>(columnWidths_);
        header_->deleteOwnProperty(Property::WIDTH);
    }
}

void Table::ensureFooter() {
    if (!footer_) {
        footer_ = std::make_shared<Table>(columnWidths_);
        footer_->deleteOwnProperty(Property::WIDTH);
    }
}

Table& Table::addHeaderCell(CellPtr headerCell) {
    ensureHeader();
    header_->addCell(std::move(headerCell));
    return *this;
}

Table& Table::addHeaderCell(const std::string& content) {
    ensureHeader();
    header_->addCell(content);
    return *this;
}

Table& Table::addFooterCell(CellPtr footerCell) {
    ensureFooter();
    footer_->addCell(std::move(footerCell));
    return *this;
}

Table& Table::addFooterCell(const std::string& content) {
    ensureFooter();
    footer_->addCell(content);
    return *this;
}

CellPtr Table::getCell(int row, int column) const {
    if (row < 0 || row >= static_cast<int>(rows_.size()) || column < 0 || column >= getNumberOfColumns()) return nullptr;
    return rows_[row][column];
}

IRendererPtr Table::makeNewRenderer() { return std::make_shared<TableRenderer>(alias(this)); }

// ------------------------------------------------------------ List / ListItem
List& List::setListSymbol(ListNumberingType type) {
    if (type == ListNumberingType::ZAPF_DINGBATS_1) {
        setPostSymbolText(" ");
    }
    return setProperty(Property::LIST_SYMBOL, type);
}

std::any List::getDefaultPropertyAny(int property) const {
    switch (property) {
        case Property::LIST_SYMBOL:
            return std::make_shared<Text>(DEFAULT_LIST_SYMBOL);
        case Property::LIST_SYMBOL_PRE_TEXT:
            return std::string();
        case Property::LIST_SYMBOL_POST_TEXT:
            return std::string(". ");
        case Property::LIST_START:
            return 1;
        case Property::LIST_SYMBOL_INDENT:
            return 0.f;
        default:
            return BlockElement<List>::getDefaultPropertyAny(property);
    }
}

IRendererPtr List::makeNewRenderer() { return std::make_shared<ListRenderer>(alias(this)); }
IRendererPtr ListItem::makeNewRenderer() { return std::make_shared<ListItemRenderer>(alias(this)); }

}  // namespace layout
}  // namespace pdfcpp
