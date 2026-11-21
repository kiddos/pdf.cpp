#include "pdfcpp/layout/Document.h"

#include <cmath>

#include "pdfcpp/io/font/PdfEncodings.h"

namespace pdfcpp {
namespace layout {

// ============================================================ RootElementBase
std::any RootElementBase::getDefaultPropertyAny(int property) const {
    switch (property) {
        case Property::FONT:
            return getDefaultFont();
        case Property::FONT_SIZE:
            return UnitValue::createPointValue(12);
        case Property::TEXT_RENDERING_MODE:
            return 0;
        case Property::TEXT_RISE:
            return 0.f;
        case Property::SPACING_RATIO:
            return 0.75f;
        case Property::FONT_KERNING:
            return false;
        case Property::HORIZONTAL_SCALING:
            return 1.f;
        default:
            return std::any();
    }
}

std::shared_ptr<kernel::PdfFont> RootElementBase::getDefaultFont() const {
    if (!defaultFont_) defaultFont_ = kernel::PdfFontFactory::createFont(io::StandardFonts::HELVETICA, io::PdfEncodings::WINANSI);
    return defaultFont_;
}

RootElementBase& RootElementBase::add(IElementPtr element) {
    IRendererPtr renderer = element->createRendererSubTree();
    getRenderer().addChild(renderer);
    return *this;
}

RootElementBase& RootElementBase::showTextAligned(const std::string& text, float x, float y, TextAlignment textAlign,
                                                  VerticalAlignment vertAlign, float radAngle) {
    auto p = std::make_shared<Paragraph>(text);
    return showTextAligned(p, x, y, getRenderer().getCurrentArea().getPageNumber(), textAlign, vertAlign, radAngle);
}

RootElementBase& RootElementBase::showTextAligned(std::shared_ptr<Paragraph> p, float x, float y, int pageNumber,
                                                  TextAlignment textAlign, VerticalAlignment vertAlign, float radAngle) {
    p->setMargin(0).setMultipliedLeading(1);
    auto renderer = std::dynamic_pointer_cast<ParagraphRenderer>(p->createRendererSubTree());
    renderer->setParent(&getRenderer());
    LayoutArea area(pageNumber, Rectangle(0, 0, 1e5f, 1e5f));
    LayoutContext ctx(area);
    LayoutResult result = renderer->layout(ctx);
    if (result.getStatus() != LayoutResult::FULL || renderer->getLines().empty()) return *this;
    const LineInfo& line = renderer->getLines().front();
    float width = 0;
    for (const auto& l : renderer->getLines()) width = std::max(width, l.width);
    float dx = textAlign == TextAlignment::CENTER ? -width / 2 : (textAlign == TextAlignment::RIGHT ? -width : 0);
    float totalHeight = renderer->getOccupiedArea().getBBox().getHeight();
    float baselineFromTop = line.baselineOffset;
    float dy;
    switch (vertAlign) {
        case VerticalAlignment::TOP: dy = -baselineFromTop; break;
        case VerticalAlignment::MIDDLE: dy = totalHeight / 2 - baselineFromTop; break;
        default: dy = 0; break;
    }
    // move so that the first baseline is at (x + dx, y + dy) before rotation
    auto first = renderer->getFirstYLineRecursively();
    float curBaseline = first ? *first : renderer->getOccupiedArea().getBBox().getTop() - baselineFromTop;
    renderer->move(x + dx - renderer->getOccupiedArea().getBBox().getX(), y + dy - curBaseline);
    kernel::PdfCanvas* canvas = &getRenderer().getPageCanvas(pageNumber);
    canvas->saveState();
    if (radAngle != 0) canvas->concatMatrix(kernel::AffineTransform::getRotateInstance(radAngle, x, y));
    DrawContext dc(pdfDocument_, *canvas);
    renderer->draw(dc);
    canvas->restoreState();
    return *this;
}

// ============================================================ RootRenderer
RootRenderer::RootRenderer(RootElementBase* root) : AbstractRenderer(static_cast<IPropertyContainer*>(root)), root_(root) {}

void RootRenderer::drawRenderer(const IRendererPtr& renderer) {
    int page = renderer->getOccupiedArea().getPageNumber();
    kernel::PdfCanvas& canvas = getCanvas(page);
    DrawContext dc(root_->getPdfDocument(), canvas);
    renderer->draw(dc);
}

void RootRenderer::addFixedChild(IRendererPtr renderer) {
    auto* ar = dynamic_cast<AbstractRenderer*>(renderer.get());
    float left = ar->getPropertyAsFloat(Property::LEFT).value_or(0);
    float bottom = ar->getPropertyAsFloat(Property::BOTTOM).value_or(0);
    float width = 1e5f;
    auto declaredWidth = ar->getPropertyAsUnitValue(Property::WIDTH);
    if (declaredWidth) width = declaredWidth->resolve(currentArea_.getBBox().getWidth());
    int page = ar->getPropertyAsInteger(Property::PAGE_NUMBER).value_or(currentArea_.getPageNumber());
    // the element's own width property already constrains it; give generous room
    LayoutArea area(page, Rectangle(left, bottom, std::max(width, 1.f) + 1e4f, 1e5f));
    area.getBBox().setWidth(width < 1e5f ? width : 1e5f);
    // The WIDTH is already applied through the area. Deleting the renderer's own property is not enough (the model
    // element - where setFixedPosition stores it - would still be consulted and e.g. 50% applied a second time), so
    // shadow it with "the whole area".
    if (declaredWidth) ar->setPropertyAny(Property::WIDTH, UnitValue::createPercentValue(100));
    LayoutContext ctx(area);
    renderer->setPropertyAny(Property::FORCED_PLACEMENT, true);
    LayoutResult result = renderer->layout(ctx);
    if (result.getStatus() == LayoutResult::NOTHING) return;
    IRendererPtr toDraw = result.getStatus() == LayoutResult::PARTIAL ? result.getSplitRenderer() : renderer;
    float occupiedBottom = toDraw->getOccupiedArea().getBBox().getY();
    toDraw->move(0, bottom - occupiedBottom);
    toDraw->getOccupiedArea() = LayoutArea(page, toDraw->getOccupiedArea().getBBox());
    drawRenderer(toDraw);
}

void RootRenderer::addChild(IRendererPtr renderer) {
    if (!renderer) return;
    renderer->setParent(this);
    auto* ar = dynamic_cast<AbstractRenderer*>(renderer.get());
    if (ar && ar->isFixedLayout()) {
        if (!hasArea_) newArea();
        addFixedChild(renderer);
        return;
    }
    if (auto* areaBreak = dynamic_cast<AreaBreakRenderer*>(renderer.get())) {
        auto* element = dynamic_cast<AreaBreak*>(areaBreak->getModelElement());
        if (!hasArea_) newArea();
        newArea(element ? element->getPageSize() : std::nullopt);
        return;
    }
    if (!hasArea_) newArea();
    IRendererPtr current = renderer;
    int guard = 0;
    while (current && guard++ < 100000) {
        LayoutContext ctx(currentArea_);
        LayoutResult result = current->layout(ctx);
        switch (result.getStatus()) {
            case LayoutResult::FULL: {
                current->getOccupiedArea() = LayoutArea(currentArea_.getPageNumber(), current->getOccupiedArea().getBBox());
                drawRenderer(current);
                float newTop = result.getOccupiedArea().getBBox().getY();
                Rectangle& bbox = currentArea_.getBBox();
                bbox.setHeight(std::max(0.f, newTop - bbox.getY()));
                currentArea_.setEmptyArea(false);
                current = nullptr;
                break;
            }
            case LayoutResult::PARTIAL: {
                IRendererPtr split = result.getSplitRenderer();
                if (split) {
                    split->getOccupiedArea() = LayoutArea(currentArea_.getPageNumber(), split->getOccupiedArea().getBBox());
                    drawRenderer(split);
                }
                newArea();
                current = result.getOverflowRenderer();
                if (current) current->setParent(this);
                break;
            }
            default: {
                if (currentArea_.isEmptyArea()) {
                    if (current->hasOwnProperty(Property::FORCED_PLACEMENT)) {
                        // cannot be placed at all: drop it (iText logs the same situation)
                        current = nullptr;
                    } else {
                        current->setPropertyAny(Property::FORCED_PLACEMENT, true);
                    }
                } else {
                    newArea();
                }
                break;
            }
        }
    }
}

// ============================================================ DocumentRenderer
DocumentRenderer::DocumentRenderer(Document* document) : RootRenderer(document), document_(document) {}

void DocumentRenderer::newArea(const std::optional<kernel::PageSize>& pageSize) {
    kernel::PdfDocument* pdf = document_->getPdfDocument();
    ++currentPageNumber_;
    kernel::PageSize size = pageSize ? *pageSize : document_->getPageSize();
    if (currentPageNumber_ > pdf->getNumberOfPages()) {
        pdf->addNewPage(size);
    } else {
        size = kernel::PageSize(pdf->getPage(currentPageNumber_)->getPageSize());
    }
    currentArea_ = LayoutArea(currentPageNumber_, document_->getPageEffectiveArea(size));
    currentArea_.setEmptyArea(true);
    hasArea_ = true;
}

kernel::PdfCanvas& DocumentRenderer::getCanvas(int pageNumber) {
    auto it = canvases_.find(pageNumber);
    if (it != canvases_.end()) return *it->second;
    kernel::PdfDocument* pdf = document_->getPdfDocument();
    while (pdf->getNumberOfPages() < pageNumber) pdf->addNewPage(document_->getPageSize());
    auto canvas = std::make_unique<kernel::PdfCanvas>(pdf->getPage(pageNumber));
    auto& ref = *canvas;
    canvases_[pageNumber] = std::move(canvas);
    return ref;
}

void DocumentRenderer::addFixedChild(IRendererPtr renderer) { RootRenderer::addFixedChild(std::move(renderer)); }

void DocumentRenderer::flush() {}

// ============================================================ Document
Document::Document(kernel::PdfDocument& pdfDoc, bool immediateFlush)
    : Document(pdfDoc, pdfDoc.getDefaultPageSize(), immediateFlush) {}

Document::Document(kernel::PdfDocument& pdfDoc, const kernel::PageSize& pageSize, bool immediateFlush)
    : pageSize_(pageSize), immediateFlush_(immediateFlush) {
    pdfDocument_ = &pdfDoc;
    pdfDoc.setDefaultPageSize(pageSize);
}

Document::~Document() {
    try {
        if (!closed_) close();
    } catch (...) {
    }
}

RootRenderer& Document::getRenderer() {
    if (!renderer_) renderer_ = std::make_unique<DocumentRenderer>(this);
    return *renderer_;
}

Rectangle Document::getPageEffectiveArea(const kernel::PageSize& pageSize) const {
    return Rectangle(pageSize.getLeft() + leftMargin_, pageSize.getBottom() + bottomMargin_,
                     pageSize.getWidth() - leftMargin_ - rightMargin_, pageSize.getHeight() - bottomMargin_ - topMargin_);
}

void Document::close() {
    if (closed_) return;
    closed_ = true;
    if (renderer_) renderer_->close();
    renderer_.reset();
    pdfDocument_->close();
}

// ============================================================ Canvas
namespace {
class CanvasRenderer : public RootRenderer {
public:
    CanvasRenderer(Canvas* canvas) : RootRenderer(canvas), canvas_(canvas) {}
    void newArea(const std::optional<kernel::PageSize>&) override {
        if (hasArea_) {
            // a Canvas has a single area: further content overflows and is dropped
            currentArea_ = LayoutArea(1, Rectangle(canvas_->getRootArea().getX(), canvas_->getRootArea().getY(), canvas_->getRootArea().getWidth(), 0));
            currentArea_.setEmptyArea(false);
            overflowed_ = true;
            return;
        }
        currentArea_ = LayoutArea(1, canvas_->getRootArea());
        hasArea_ = true;
    }

protected:
    kernel::PdfCanvas& getCanvas(int) override { return canvas_->getPdfCanvas(); }

private:
    Canvas* canvas_;
    bool overflowed_ = false;
};
}  // namespace

Canvas::Canvas(kernel::PdfCanvas& pdfCanvas, const Rectangle& rootArea) : pdfCanvas_(&pdfCanvas), rootArea_(rootArea) {
    pdfDocument_ = pdfCanvas.getDocument();
}

Canvas::Canvas(kernel::PdfPage* page, const Rectangle& rootArea)
    : ownedCanvas_(std::make_unique<kernel::PdfCanvas>(page)), pdfCanvas_(ownedCanvas_.get()), rootArea_(rootArea) {
    pdfDocument_ = page->getDocument();
}

Canvas::~Canvas() = default;

RootRenderer& Canvas::getRenderer() {
    if (!renderer_) renderer_ = std::make_unique<CanvasRenderer>(this);
    return *renderer_;
}

}  // namespace layout
}  // namespace pdfcpp
