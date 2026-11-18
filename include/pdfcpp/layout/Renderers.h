#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "pdfcpp/layout/Elements.h"

namespace pdfcpp {
namespace layout {

using kernel::Rectangle;

/// Port of LayoutArea.
class LayoutArea {
public:
    LayoutArea() = default;
    LayoutArea(int pageNumber, const Rectangle& bBox) : pageNumber_(pageNumber), bBox_(bBox) {}
    int getPageNumber() const { return pageNumber_; }
    Rectangle& getBBox() { return bBox_; }
    const Rectangle& getBBox() const { return bBox_; }
    void setBBox(const Rectangle& r) { bBox_ = r; }
    bool isEmptyArea() const { return emptyArea_; }
    void setEmptyArea(bool v) { emptyArea_ = v; }

private:
    int pageNumber_ = 1;
    Rectangle bBox_;
    bool emptyArea_ = true;
};

/// Port of LayoutContext.
class LayoutContext {
public:
    explicit LayoutContext(const LayoutArea& area) : area_(area) {}
    LayoutArea& getArea() { return area_; }
    const LayoutArea& getArea() const { return area_; }

private:
    LayoutArea area_;
};

/// Port of LayoutResult.
class LayoutResult {
public:
    static constexpr int FULL = 1;
    static constexpr int PARTIAL = 2;
    static constexpr int NOTHING = 3;
    LayoutResult(int status, const LayoutArea& occupiedArea, IRendererPtr splitRenderer, IRendererPtr overflowRenderer,
                 IRendererPtr causeOfNothing = nullptr)
        : status_(status), occupiedArea_(occupiedArea), splitRenderer_(std::move(splitRenderer)),
          overflowRenderer_(std::move(overflowRenderer)), causeOfNothing_(std::move(causeOfNothing)) {}
    int getStatus() const { return status_; }
    const LayoutArea& getOccupiedArea() const { return occupiedArea_; }
    const IRendererPtr& getSplitRenderer() const { return splitRenderer_; }
    const IRendererPtr& getOverflowRenderer() const { return overflowRenderer_; }
    const IRendererPtr& getCauseOfNothing() const { return causeOfNothing_; }

private:
    int status_;
    LayoutArea occupiedArea_;
    IRendererPtr splitRenderer_, overflowRenderer_, causeOfNothing_;
};

/// Port of DrawContext.
class DrawContext {
public:
    DrawContext(kernel::PdfDocument* document, kernel::PdfCanvas& canvas) : document_(document), canvas_(canvas) {}
    kernel::PdfDocument* getDocument() const { return document_; }
    kernel::PdfCanvas& getCanvas() const { return canvas_; }

private:
    kernel::PdfDocument* document_;
    kernel::PdfCanvas& canvas_;
};

/// Port of IRenderer.
class IRenderer : public virtual IPropertyContainer, public std::enable_shared_from_this<IRenderer> {
public:
    virtual void addChild(IRendererPtr renderer) = 0;
    virtual LayoutResult layout(LayoutContext& layoutContext) = 0;
    virtual void draw(DrawContext& drawContext) = 0;
    virtual LayoutArea& getOccupiedArea() = 0;
    virtual IRenderer* getParent() const = 0;
    virtual IRenderer* setParent(IRenderer* parent) = 0;
    virtual const std::vector<IRendererPtr>& getChildRenderers() const = 0;
    virtual IPropertyContainer* getModelElement() const = 0;
    virtual void move(float dx, float dy) = 0;
    /// Creates a fresh renderer of the same kind for the same element (used when splitting).
    virtual IRendererPtr getNextRenderer() = 0;
    virtual bool isFlushed() const { return false; }
};

/// Port of AbstractRenderer: property resolution, margins/borders/paddings/background.
class AbstractRenderer : public IRenderer {
public:
    AbstractRenderer() = default;
    explicit AbstractRenderer(std::shared_ptr<IElement> modelElement) : modelElementOwner_(std::move(modelElement)) {
        modelElement_ = modelElementOwner_.get();
    }
    explicit AbstractRenderer(IPropertyContainer* modelElement) : modelElement_(modelElement) {}

    // IPropertyContainer
    bool hasProperty(int property) const override;
    bool hasOwnProperty(int property) const override { return properties_.count(property) > 0; }
    const std::any* getPropertyAny(int property) const override;
    const std::any* getOwnPropertyAny(int property) const override {
        auto it = properties_.find(property);
        return it == properties_.end() ? nullptr : &it->second;
    }
    std::any getDefaultPropertyAny(int property) const override;
    void setPropertyAny(int property, std::any value) override { properties_[property] = std::move(value); }
    void deleteOwnProperty(int property) override { properties_.erase(property); }

    // IRenderer
    void addChild(IRendererPtr renderer) override;
    void draw(DrawContext& drawContext) override;
    LayoutArea& getOccupiedArea() override { return occupiedArea_; }
    IRenderer* getParent() const override { return parent_; }
    IRenderer* setParent(IRenderer* parent) override {
        parent_ = parent;
        return this;
    }
    const std::vector<IRendererPtr>& getChildRenderers() const override { return childRenderers_; }
    std::vector<IRendererPtr>& childRenderers() { return childRenderers_; }
    IPropertyContainer* getModelElement() const override { return modelElement_; }
    const std::shared_ptr<IElement>& getModelElementOwner() const { return modelElementOwner_; }
    void move(float dx, float dy) override;

    // property helpers
    std::shared_ptr<kernel::PdfFont> getPropertyAsFont(int property) const;
    ColorPtr getPropertyAsColor(int property) const;
    std::optional<float> getPropertyAsFloat(int property) const;
    std::optional<UnitValue> getPropertyAsUnitValue(int property) const;
    bool getPropertyAsBoolean(int property) const;
    std::optional<int> getPropertyAsInteger(int property) const;
    float resolveFontSize() const;

    std::array<BorderPtr, 4> getBorders() const;  // top, right, bottom, left
    std::array<float, 4> getMargins(float base = 0) const;
    std::array<float, 4> getPaddings(float base = 0) const;
    Rectangle& applyMargins(Rectangle& rect, bool reverse) const;
    Rectangle& applyBorderBox(Rectangle& rect, bool reverse) const;
    Rectangle& applyPaddings(Rectangle& rect, bool reverse) const;
    Rectangle getInnerAreaBBox() const;
    std::optional<float> retrieveWidth(float parentBoxWidth) const;
    std::optional<float> retrieveHeight() const;
    std::optional<float> retrieveMinHeight() const;
    std::optional<float> retrieveMaxHeight() const;
    void drawBackground(DrawContext& drawContext);
    void drawBorder(DrawContext& drawContext);
    virtual void drawChildren(DrawContext& drawContext);
    bool isFixedLayout() const;
    bool isPositioned() const { return isFixedLayout(); }

protected:
    template <typename R>
    std::shared_ptr<R> createSplitLike() const {
        auto r = std::make_shared<R>(modelElementOwner_);
        r->modelElement_ = modelElement_;
        r->properties_ = properties_;
        r->parent_ = parent_;
        return r;
    }

    std::map<int, std::any> properties_;
    mutable std::map<int, std::any> defaultCache_;
    std::shared_ptr<IElement> modelElementOwner_;
    IPropertyContainer* modelElement_ = nullptr;
    IRenderer* parent_ = nullptr;
    std::vector<IRendererPtr> childRenderers_;
    LayoutArea occupiedArea_;
    template <typename>
    friend class RendererFactory;
    friend class BlockRenderer;
    friend class TableRenderer;
};

/// Port of BlockRenderer (+ DivRenderer/CellRenderer/ListItemRenderer behaviour).
class BlockRenderer : public AbstractRenderer {
public:
    using AbstractRenderer::AbstractRenderer;
    LayoutResult layout(LayoutContext& layoutContext) override;
    IRendererPtr getNextRenderer() override;
    void draw(DrawContext& drawContext) override;
    /// Baseline of the first line of text inside this block (absolute y), if any.
    virtual std::optional<float> getFirstYLineRecursively() const;

protected:
    virtual std::shared_ptr<BlockRenderer> createSplitRenderer() const;
    virtual std::shared_ptr<BlockRenderer> createOverflowRenderer() const;
};

/// One placed text fragment inside a line.
struct LineItem {
    std::shared_ptr<AbstractRenderer> source;  // TextRenderer or ImageRenderer
    io::GlyphLine glyphs;
    std::string text;
    float x = 0;          // offset from line start
    float width = 0;
    float ascent = 0, descent = 0;
    bool isImage = false;
    bool isSpace = false;
};

/// A laid-out line of a paragraph (port of LineRenderer).
struct LineInfo {
    std::vector<LineItem> items;
    float width = 0;
    float ascent = 0, descent = 0;
    float height = 0;
    float baselineOffset = 0;  // from top of line box
    float leftIndent = 0;
    float availableWidth = 0;
    bool lastLine = false;
};

/// Port of TextRenderer.
class TextRenderer : public AbstractRenderer {
public:
    explicit TextRenderer(std::shared_ptr<IElement> textElement);
    TextRenderer(std::shared_ptr<IElement> textElement, std::string text);
    LayoutResult layout(LayoutContext& layoutContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<TextRenderer>(modelElementOwner_); }
    void draw(DrawContext&) override {}
    const std::string& getText() const { return text_; }
    float getAscent() const;
    float getDescent() const;
    /// Draws the given glyphs at (x, baseline y) using this renderer's text properties.
    void drawGlyphs(DrawContext& drawContext, const io::GlyphLine& glyphs, float x, float y, float width);

private:
    std::string text_;
};

/// Port of ImageRenderer.
class ImageRenderer : public AbstractRenderer {
public:
    explicit ImageRenderer(std::shared_ptr<IElement> image);
    LayoutResult layout(LayoutContext& layoutContext) override;
    void draw(DrawContext& drawContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<ImageRenderer>(modelElementOwner_); }
    float getImageWidth() const { return imageWidth_; }
    float getImageHeight() const { return imageHeight_; }
    /// Computes width/height for the given available width/height (applies autoscale/scale/width props).
    void calculateImageSize(float availableWidth, float availableHeight);

private:
    float imageWidth_ = 0, imageHeight_ = 0;
};

/// Port of ParagraphRenderer (with LineRenderer folded in).
class ParagraphRenderer : public BlockRenderer {
public:
    explicit ParagraphRenderer(std::shared_ptr<IElement> paragraph);
    LayoutResult layout(LayoutContext& layoutContext) override;
    void drawChildren(DrawContext& drawContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<ParagraphRenderer>(modelElementOwner_); }
    std::optional<float> getFirstYLineRecursively() const override;
    std::any getDefaultPropertyAny(int property) const override;
    void move(float dx, float dy) override {
        BlockRenderer::move(dx, dy);
        contentBox_.moveRight(dx);
        contentBox_.moveUp(dy);
    }
    const std::vector<LineInfo>& getLines() const { return lines_; }

private:
    struct Piece {
        std::shared_ptr<AbstractRenderer> source;
        io::GlyphLine glyphs;
        std::string text;
        float width;
        float ascent, descent;
        bool isSpace, isNewline, isImage;
        bool breakAllowedBefore;
    };
    void buildPieces();
    std::vector<Piece> pieces_;
    size_t startPiece_ = 0;
    bool piecesBuilt_ = false;
    bool continuation_ = false;  // overflow part: first line indent not applied
    std::vector<LineInfo> lines_;
    Rectangle contentBox_;
};

/// Port of TableRenderer (separated borders model).
class TableRenderer : public AbstractRenderer {
public:
    explicit TableRenderer(std::shared_ptr<IElement> table);
    LayoutResult layout(LayoutContext& layoutContext) override;
    void draw(DrawContext& drawContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<TableRenderer>(modelElementOwner_); }
    std::optional<float> getFirstYLineRecursively() const;
    void move(float dx, float dy) override {
        AbstractRenderer::move(dx, dy);
        for (auto& pc : placed_) {
            pc.renderer->move(dx, dy);
            pc.box.moveRight(dx);
            pc.box.moveUp(dy);
            pc.renderer->getOccupiedArea().setBBox(pc.box);
        }
    }

private:
    struct PlacedCell {
        std::shared_ptr<BlockRenderer> renderer;
        Rectangle box;
    };
    std::vector<float> computeColumnWidths(float availableWidth) const;
    Table& table() const { return *dynamic_cast<Table*>(modelElement_); }
    int startRow_ = 0;
    bool isOverflowPart_ = false;
    std::vector<PlacedCell> placed_;
    std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> pendingCellOverflow_;  // (row, col) -> remaining content
};

/// Port of ListRenderer.
class ListRenderer : public BlockRenderer {
public:
    explicit ListRenderer(std::shared_ptr<IElement> list);
    LayoutResult layout(LayoutContext& layoutContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<ListRenderer>(modelElementOwner_); }

private:
    std::string makeSymbol(int index) const;
    bool symbolsAssigned_ = false;
};

/// Port of ListItemRenderer.
class ListItemRenderer : public BlockRenderer {
public:
    explicit ListItemRenderer(std::shared_ptr<IElement> item);
    void setSymbol(std::shared_ptr<TextRenderer> symbol, float symbolAreaWidth) {
        symbol_ = std::move(symbol);
        symbolAreaWidth_ = symbolAreaWidth;
    }
    LayoutResult layout(LayoutContext& layoutContext) override;
    void draw(DrawContext& drawContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<ListItemRenderer>(modelElementOwner_); }

protected:
    std::shared_ptr<BlockRenderer> createSplitRenderer() const override;
    std::shared_ptr<BlockRenderer> createOverflowRenderer() const override;

private:
    std::shared_ptr<TextRenderer> symbol_;
    float symbolAreaWidth_ = 0;
    bool symbolDrawn_ = false;
};

/// Port of AreaBreakRenderer.
class AreaBreakRenderer : public AbstractRenderer {
public:
    explicit AreaBreakRenderer(std::shared_ptr<IElement> areaBreak) : AbstractRenderer(std::move(areaBreak)) {}
    LayoutResult layout(LayoutContext& layoutContext) override {
        return LayoutResult(LayoutResult::NOTHING, layoutContext.getArea(), nullptr, nullptr);
    }
    IRendererPtr getNextRenderer() override { return nullptr; }
};

/// Port of LineSeparatorRenderer.
class LineSeparatorRenderer : public BlockRenderer {
public:
    explicit LineSeparatorRenderer(std::shared_ptr<IElement> sep) : BlockRenderer(std::move(sep)) {}
    LayoutResult layout(LayoutContext& layoutContext) override;
    void drawChildren(DrawContext& drawContext) override;
    IRendererPtr getNextRenderer() override { return std::make_shared<LineSeparatorRenderer>(modelElementOwner_); }
};

}  // namespace layout
}  // namespace pdfcpp
