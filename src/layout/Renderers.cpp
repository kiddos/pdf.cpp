#include "pdfcpp/layout/Renderers.h"

#include <algorithm>
#include <cmath>

#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/TextUtil.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace layout {

using kernel::PdfCanvas;
using kernel::PdfFont;

static constexpr float ITALIC_ANGLE = 0.21256f;
static constexpr float BOLD_SIMULATION_STROKE_COEFF = 1 / 30.f;
static constexpr float TYPO_ASCENDER_SCALE_COEFF = 1.2f;
static constexpr float EPS = 1e-4f;

// ============================================================ AbstractRenderer
const std::any* AbstractRenderer::getPropertyAny(int property) const {
    auto it = properties_.find(property);
    if (it != properties_.end()) return &it->second;
    if (modelElement_) {
        if (const std::any* v = modelElement_->getOwnPropertyAny(property)) return v;
    }
    if (parent_ && Property::isPropertyInherited(property)) {
        if (const std::any* v = parent_->getPropertyAny(property)) return v;
    }
    // defaults are cached so that a stable pointer can be returned
    std::any d = getDefaultPropertyAny(property);
    if (d.has_value()) {
        auto& slot = defaultCache_[property];
        slot = std::move(d);
        return &slot;
    }
    return nullptr;
}

bool AbstractRenderer::hasProperty(int property) const { return getPropertyAny(property) != nullptr; }

std::any AbstractRenderer::getDefaultPropertyAny(int property) const {
    return modelElement_ ? modelElement_->getDefaultPropertyAny(property) : std::any();
}

void AbstractRenderer::addChild(IRendererPtr renderer) {
    if (!renderer) return;
    renderer->setParent(this);
    childRenderers_.push_back(std::move(renderer));
}

void AbstractRenderer::move(float dx, float dy) {
    occupiedArea_.getBBox().moveRight(dx);
    occupiedArea_.getBBox().moveUp(dy);
    for (auto& child : childRenderers_) child->move(dx, dy);
}

std::shared_ptr<PdfFont> AbstractRenderer::getPropertyAsFont(int property) const {
    auto f = getProperty<std::shared_ptr<PdfFont>>(property);
    return f ? *f : nullptr;
}

ColorPtr AbstractRenderer::getPropertyAsColor(int property) const {
    auto c = getProperty<ColorPtr>(property);
    if (c) return *c;
    auto rgb = getProperty<std::shared_ptr<kernel::DeviceRgb>>(property);
    if (rgb) return *rgb;
    return nullptr;
}

std::optional<float> AbstractRenderer::getPropertyAsFloat(int property) const {
    if (auto f = getProperty<float>(property)) return f;
    if (auto d = getProperty<double>(property)) return static_cast<float>(*d);
    if (auto i = getProperty<int>(property)) return static_cast<float>(*i);
    return std::nullopt;
}

std::optional<UnitValue> AbstractRenderer::getPropertyAsUnitValue(int property) const {
    if (auto u = getProperty<UnitValue>(property)) return u;
    if (auto f = getPropertyAsFloat(property)) return UnitValue::createPointValue(*f);
    return std::nullopt;
}

bool AbstractRenderer::getPropertyAsBoolean(int property) const {
    auto b = getProperty<bool>(property);
    return b && *b;
}

std::optional<int> AbstractRenderer::getPropertyAsInteger(int property) const { return getProperty<int>(property); }

float AbstractRenderer::resolveFontSize() const {
    auto fs = getPropertyAsUnitValue(Property::FONT_SIZE);
    return fs ? fs->getValue() : 12.f;
}

std::array<BorderPtr, 4> AbstractRenderer::getBorders() const {
    std::array<BorderPtr, 4> borders;
    const int props[4] = {Property::BORDER_TOP, Property::BORDER_RIGHT, Property::BORDER_BOTTOM, Property::BORDER_LEFT};
    for (int i = 0; i < 4; ++i) {
        auto b = getProperty<BorderPtr>(props[i]);
        if (!b) {
            auto sb = getProperty<std::shared_ptr<SolidBorder>>(props[i]);
            if (sb) b = *sb;
        }
        borders[i] = (b && *b && (*b)->getType() >= 0) ? *b : nullptr;
    }
    return borders;
}

std::array<float, 4> AbstractRenderer::getMargins(float base) const {
    const int props[4] = {Property::MARGIN_TOP, Property::MARGIN_RIGHT, Property::MARGIN_BOTTOM, Property::MARGIN_LEFT};
    std::array<float, 4> r{};
    for (int i = 0; i < 4; ++i) {
        auto u = getPropertyAsUnitValue(props[i]);
        r[i] = u ? u->resolve(base) : 0.f;
    }
    return r;
}

std::array<float, 4> AbstractRenderer::getPaddings(float base) const {
    const int props[4] = {Property::PADDING_TOP, Property::PADDING_RIGHT, Property::PADDING_BOTTOM, Property::PADDING_LEFT};
    std::array<float, 4> r{};
    for (int i = 0; i < 4; ++i) {
        auto u = getPropertyAsUnitValue(props[i]);
        r[i] = u ? u->resolve(base) : 0.f;
    }
    return r;
}

Rectangle& AbstractRenderer::applyMargins(Rectangle& rect, bool reverse) const {
    auto m = getMargins(rect.getWidth());
    return rect.applyMargins(m[0], m[1], m[2], m[3], reverse);
}

Rectangle& AbstractRenderer::applyBorderBox(Rectangle& rect, bool reverse) const {
    auto b = getBorders();
    return rect.applyMargins(b[0] ? b[0]->getWidth() : 0, b[1] ? b[1]->getWidth() : 0, b[2] ? b[2]->getWidth() : 0,
                             b[3] ? b[3]->getWidth() : 0, reverse);
}

Rectangle& AbstractRenderer::applyPaddings(Rectangle& rect, bool reverse) const {
    auto p = getPaddings(rect.getWidth());
    return rect.applyMargins(p[0], p[1], p[2], p[3], reverse);
}

Rectangle AbstractRenderer::getInnerAreaBBox() const {
    Rectangle r = occupiedArea_.getBBox();
    applyMargins(r, false);
    applyBorderBox(r, false);
    applyPaddings(r, false);
    return r;
}

std::optional<float> AbstractRenderer::retrieveWidth(float parentBoxWidth) const {
    auto w = getPropertyAsUnitValue(Property::WIDTH);
    if (!w) return std::nullopt;
    float v = w->resolve(parentBoxWidth);
    if (auto mx = getPropertyAsUnitValue(Property::MAX_WIDTH)) v = std::min(v, mx->resolve(parentBoxWidth));
    if (auto mn = getPropertyAsUnitValue(Property::MIN_WIDTH)) v = std::max(v, mn->resolve(parentBoxWidth));
    return v;
}

std::optional<float> AbstractRenderer::retrieveHeight() const {
    auto h = getPropertyAsUnitValue(Property::HEIGHT);
    if (!h || h->isPercentValue()) return std::nullopt;
    return h->getValue();
}

std::optional<float> AbstractRenderer::retrieveMinHeight() const {
    auto h = getPropertyAsUnitValue(Property::MIN_HEIGHT);
    if (!h || h->isPercentValue()) return std::nullopt;
    return h->getValue();
}

std::optional<float> AbstractRenderer::retrieveMaxHeight() const {
    auto h = getPropertyAsUnitValue(Property::MAX_HEIGHT);
    if (!h || h->isPercentValue()) return std::nullopt;
    return h->getValue();
}

bool AbstractRenderer::isFixedLayout() const {
    auto pos = getProperty<int>(Property::POSITION);
    return pos && *pos == 4;
}

void AbstractRenderer::drawBackground(DrawContext& drawContext) {
    auto bg = getProperty<Background>(Property::BACKGROUND);
    if (!bg || !bg->getColor()) return;
    Rectangle bbox = occupiedArea_.getBBox();
    applyMargins(bbox, false);
    if (bbox.getWidth() <= 0 || bbox.getHeight() <= 0) return;
    PdfCanvas& canvas = drawContext.getCanvas();
    canvas.saveState();
    if (bg->getOpacity() < 1) {
        kernel::PdfExtGState gs;
        gs.setFillOpacity(bg->getOpacity());
        canvas.setExtGState(gs);
    }
    canvas.setFillColor(bg->getColor())
        .rectangle(bbox.getX() - bg->getExtraLeft(), bbox.getY() - bg->getExtraBottom(),
                   bbox.getWidth() + bg->getExtraLeft() + bg->getExtraRight(),
                   bbox.getHeight() + bg->getExtraTop() + bg->getExtraBottom())
        .fill();
    canvas.restoreState();
}

void AbstractRenderer::drawBorder(DrawContext& drawContext) {
    auto borders = getBorders();
    if (!borders[0] && !borders[1] && !borders[2] && !borders[3]) return;
    Rectangle bbox = occupiedArea_.getBBox();
    applyMargins(bbox, false);
    float topW = borders[0] ? borders[0]->getWidth() : 0, rightW = borders[1] ? borders[1]->getWidth() : 0;
    float bottomW = borders[2] ? borders[2]->getWidth() : 0, leftW = borders[3] ? borders[3]->getWidth() : 0;
    // inner rectangle of the border box
    float x1 = bbox.getX() + leftW, y1 = bbox.getY() + bottomW;
    float x2 = bbox.getRight() - rightW, y2 = bbox.getTop() - topW;
    PdfCanvas& canvas = drawContext.getCanvas();
    if (borders[0]) borders[0]->draw(canvas, x1, y2, x2, y2, Border::Side::TOP, leftW, rightW);
    if (borders[1]) borders[1]->draw(canvas, x2, y2, x2, y1, Border::Side::RIGHT, topW, bottomW);
    if (borders[2]) borders[2]->draw(canvas, x2, y1, x1, y1, Border::Side::BOTTOM, rightW, leftW);
    if (borders[3]) borders[3]->draw(canvas, x1, y1, x1, y2, Border::Side::LEFT, bottomW, topW);
}

void AbstractRenderer::drawChildren(DrawContext& drawContext) {
    for (auto& child : childRenderers_) child->draw(drawContext);
}

void AbstractRenderer::draw(DrawContext& drawContext) {
    drawBackground(drawContext);
    drawBorder(drawContext);
    drawChildren(drawContext);
}

// ============================================================ BlockRenderer
std::shared_ptr<BlockRenderer> BlockRenderer::createSplitRenderer() const {
    auto r = std::make_shared<BlockRenderer>(modelElementOwner_);
    r->modelElement_ = modelElement_;
    r->properties_ = properties_;
    r->parent_ = parent_;
    return r;
}

std::shared_ptr<BlockRenderer> BlockRenderer::createOverflowRenderer() const { return createSplitRenderer(); }

IRendererPtr BlockRenderer::getNextRenderer() { return createSplitRenderer(); }

static bool isForced(const AbstractRenderer& r) { return r.getPropertyAsBoolean(Property::FORCED_PLACEMENT); }

LayoutResult BlockRenderer::layout(LayoutContext& layoutContext) {
    LayoutArea& area = layoutContext.getArea();
    Rectangle parentBBox = area.getBBox();
    float parentWidth = parentBBox.getWidth();
    Rectangle box = parentBBox;
    applyMargins(box, false);
    auto bordersArr = getBorders();
    auto paddingsArr = getPaddings(parentWidth);
    float horizontalExtras = (bordersArr[1] ? bordersArr[1]->getWidth() : 0) + (bordersArr[3] ? bordersArr[3]->getWidth() : 0) +
                             paddingsArr[1] + paddingsArr[3];
    if (auto w = retrieveWidth(parentWidth)) {
        float outer = *w + horizontalExtras;
        if (outer < box.getWidth()) {
            auto ha = getProperty<HorizontalAlignment>(Property::HORIZONTAL_ALIGNMENT);
            float extra = box.getWidth() - outer;
            if (ha && *ha == HorizontalAlignment::CENTER) box.moveRight(extra / 2);
            else if (ha && *ha == HorizontalAlignment::RIGHT) box.moveRight(extra);
        }
        box.setWidth(outer);
    }
    std::optional<float> fixedHeight = retrieveHeight();
    applyBorderBox(box, false);
    applyPaddings(box, false);
    Rectangle content = box;
    if (fixedHeight && *fixedHeight < content.getHeight()) {
        content.setY(content.getTop() - *fixedHeight);
        content.setHeight(*fixedHeight);
    }
    float curTop = content.getTop();
    float lowest = curTop;
    std::vector<IRendererPtr> placed;
    bool forced = isForced(*this);
    for (size_t i = 0; i < childRenderers_.size(); ++i) {
        IRendererPtr child = childRenderers_[i];
        Rectangle childBox(content.getX(), content.getY(), content.getWidth(), curTop - content.getY());
        LayoutArea childArea(area.getPageNumber(), childBox);
        childArea.setEmptyArea(area.isEmptyArea() && placed.empty());
        LayoutContext childContext(childArea);
        LayoutResult result = child->layout(childContext);
        int status = result.getStatus();
        if (status == LayoutResult::FULL) {
            placed.push_back(child);
            curTop = result.getOccupiedArea().getBBox().getY();
            lowest = std::min(lowest, curTop);
            continue;
        }
        if (status == LayoutResult::NOTHING && forced && placed.empty()) {
            // forced placement: keep the child even though it does not fit
            child->setPropertyAny(Property::FORCED_PLACEMENT, true);
            LayoutResult retry = child->layout(childContext);
            if (retry.getStatus() != LayoutResult::NOTHING) {
                result = retry;
                status = retry.getStatus();
                if (status == LayoutResult::FULL) {
                    placed.push_back(child);
                    curTop = result.getOccupiedArea().getBBox().getY();
                    lowest = std::min(lowest, curTop);
                    continue;
                }
            }
        }
        // split
        bool keepTogether = getPropertyAsBoolean(Property::KEEP_TOGETHER);
        if ((status == LayoutResult::NOTHING && placed.empty()) || (keepTogether && !area.isEmptyArea() && !forced)) {
            return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), result.getCauseOfNothing() ? result.getCauseOfNothing() : child);
        }
        auto split = createSplitRenderer();
        auto overflow = createOverflowRenderer();
        for (auto& p : placed) split->addChild(p);
        if (status == LayoutResult::PARTIAL) {
            if (result.getSplitRenderer()) {
                split->addChild(result.getSplitRenderer());
                lowest = std::min(lowest, result.getOccupiedArea().getBBox().getY());
            }
            if (result.getOverflowRenderer()) overflow->addChild(result.getOverflowRenderer());
        } else {
            overflow->addChild(child);
        }
        for (size_t k = i + 1; k < childRenderers_.size(); ++k) overflow->addChild(childRenderers_[k]);
        split->setPropertyAny(Property::MARGIN_BOTTOM, UnitValue::createPointValue(0));
        overflow->setPropertyAny(Property::MARGIN_TOP, UnitValue::createPointValue(0));
        if (fixedHeight) overflow->deleteOwnProperty(Property::HEIGHT);
        // occupied area of split part
        Rectangle used(content.getX(), lowest, content.getWidth(), content.getTop() - lowest);
        if (getPropertyAsBoolean(Property::FILL_AVAILABLE_AREA_ON_SPLIT) || getPropertyAsBoolean(Property::FILL_AVAILABLE_AREA))
            used = Rectangle(content.getX(), content.getY(), content.getWidth(), content.getHeight());
        applyPaddings(used, true);
        applyBorderBox(used, true);
        auto m = getMargins(parentWidth);
        used.applyMargins(m[0], m[1], 0, m[3], true);
        split->occupiedArea_ = LayoutArea(area.getPageNumber(), used);
        return LayoutResult(LayoutResult::PARTIAL, split->occupiedArea_, split, overflow);
    }
    float contentHeight = content.getTop() - lowest;
    if (fixedHeight) contentHeight = *fixedHeight;
    if (auto mh = retrieveMinHeight()) contentHeight = std::max(contentHeight, *mh);
    if (getPropertyAsBoolean(Property::FILL_AVAILABLE_AREA)) contentHeight = content.getTop() - parentBBox.getY();
    Rectangle used(content.getX(), content.getTop() - contentHeight, content.getWidth(), contentHeight);
    applyPaddings(used, true);
    applyBorderBox(used, true);
    applyMargins(used, true);
    if (used.getY() < parentBBox.getY() - EPS && !forced) {
        // min/fixed height made the block overflow the area
        if (area.isEmptyArea()) {
            // place anyway
        } else {
            return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
        }
    }
    // vertical alignment of children inside a fixed-height block
    auto va = getProperty<VerticalAlignment>(Property::VERTICAL_ALIGNMENT);
    if (va && *va != VerticalAlignment::TOP && contentHeight > content.getTop() - lowest) {
        float free = contentHeight - (content.getTop() - lowest);
        float dy = *va == VerticalAlignment::MIDDLE ? free / 2 : free;
        for (auto& c : childRenderers_) c->move(0, -dy);
    }
    occupiedArea_ = LayoutArea(area.getPageNumber(), used);
    return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
}

void BlockRenderer::draw(DrawContext& drawContext) {
    bool positioned = isFixedLayout();
    auto rotation = getPropertyAsFloat(Property::ROTATION_ANGLE);
    PdfCanvas& canvas = drawContext.getCanvas();
    if (rotation && *rotation != 0) {
        Rectangle b = occupiedArea_.getBBox();
        double cx = b.getX() + b.getWidth() / 2, cy = b.getY() + b.getHeight() / 2;
        kernel::AffineTransform t = kernel::AffineTransform::getRotateInstance(*rotation, cx, cy);
        canvas.saveState().concatMatrix(t);
    }
    auto opacity = getPropertyAsFloat(Property::OPACITY);
    bool opacitySet = opacity && *opacity < 1 && !dynamic_cast<TextRenderer*>(this);
    if (opacitySet) {
        canvas.saveState();
        kernel::PdfExtGState gs;
        gs.setFillOpacity(*opacity).setStrokeOpacity(*opacity);
        canvas.setExtGState(gs);
    }
    drawBackground(drawContext);
    drawBorder(drawContext);
    drawChildren(drawContext);
    if (opacitySet) canvas.restoreState();
    if (rotation && *rotation != 0) canvas.restoreState();
    (void)positioned;
}

std::optional<float> BlockRenderer::getFirstYLineRecursively() const {
    for (const auto& child : childRenderers_) {
        if (auto* b = dynamic_cast<BlockRenderer*>(child.get())) {
            auto y = b->getFirstYLineRecursively();
            if (y) return y;
        } else if (auto* t = dynamic_cast<TableRenderer*>(child.get())) {
            auto y = t->getFirstYLineRecursively();
            if (y) return y;
        }
    }
    return std::nullopt;
}

// ============================================================ TextRenderer
TextRenderer::TextRenderer(std::shared_ptr<IElement> textElement) : AbstractRenderer(std::move(textElement)) {
    if (auto* t = dynamic_cast<Text*>(modelElement_)) text_ = t->getText();
}

TextRenderer::TextRenderer(std::shared_ptr<IElement> textElement, std::string text)
    : AbstractRenderer(std::move(textElement)), text_(std::move(text)) {}

LayoutResult TextRenderer::layout(LayoutContext& layoutContext) {
    // Text is laid out by its parent ParagraphRenderer.
    return LayoutResult(LayoutResult::FULL, layoutContext.getArea(), nullptr, nullptr);
}

static std::pair<float, float> ascenderDescender(PdfFont* font) {
    const auto& m = font->getFontProgram()->getFontMetrics();
    float ascender, descender;
    if (m.getWinAscender() == 0 || m.getWinDescender() == 0 ||
        (m.getTypoAscender() == m.getWinAscender() && m.getTypoDescender() == m.getWinDescender())) {
        ascender = m.getTypoAscender() * TYPO_ASCENDER_SCALE_COEFF;
        descender = m.getTypoDescender() * TYPO_ASCENDER_SCALE_COEFF;
    } else {
        ascender = static_cast<float>(m.getWinAscender());
        descender = static_cast<float>(m.getWinDescender());
    }
    if (descender > 0) descender = -descender;
    return {ascender, descender};
}

static std::shared_ptr<PdfFont> resolveFont(const AbstractRenderer& r) {
    auto font = r.getPropertyAsFont(Property::FONT);
    if (!font) {
        static std::shared_ptr<PdfFont> fallback;
        if (!fallback) fallback = kernel::PdfFontFactory::createFont();
        font = fallback;
    }
    return font;
}

float TextRenderer::getAscent() const {
    auto font = resolveFont(*this);
    float rise = getPropertyAsFloat(Property::TEXT_RISE).value_or(0);
    return ascenderDescender(font.get()).first * resolveFontSize() / 1000.f + rise;
}

float TextRenderer::getDescent() const {
    auto font = resolveFont(*this);
    float rise = getPropertyAsFloat(Property::TEXT_RISE).value_or(0);
    return ascenderDescender(font.get()).second * resolveFontSize() / 1000.f + rise;
}

void TextRenderer::drawGlyphs(DrawContext& drawContext, const io::GlyphLine& glyphs, float x, float y, float width) {
    if (glyphs.size() == 0) return;
    auto font = resolveFont(*this);
    float fontSize = resolveFontSize();
    PdfCanvas& canvas = drawContext.getCanvas();
    canvas.saveState();
    auto opacity = getPropertyAsFloat(Property::OPACITY);
    if (opacity && *opacity < 1) {
        kernel::PdfExtGState gs;
        gs.setFillOpacity(*opacity).setStrokeOpacity(*opacity);
        canvas.setExtGState(gs);
    }
    ColorPtr fontColor = getPropertyAsColor(Property::FONT_COLOR);
    if (fontColor) canvas.setFillColor(fontColor);
    int textRenderingMode = getPropertyAsInteger(Property::TEXT_RENDERING_MODE).value_or(0);
    bool bold = getPropertyAsBoolean(Property::BOLD_SIMULATION);
    if (bold && textRenderingMode == kernel::PdfCanvasConstants::TextRenderingMode::FILL)
        textRenderingMode = kernel::PdfCanvasConstants::TextRenderingMode::FILL_STROKE;
    if (textRenderingMode != 0) {
        ColorPtr strokeColor = getPropertyAsColor(Property::STROKE_COLOR);
        if (!strokeColor) strokeColor = fontColor;
        if (strokeColor) canvas.setStrokeColor(strokeColor);
        float strokeWidth = getPropertyAsFloat(Property::STROKE_WIDTH).value_or(bold ? fontSize * BOLD_SIMULATION_STROKE_COEFF : 1.f);
        canvas.setLineWidth(strokeWidth);
    }
    canvas.beginText().setFontAndSize(font, fontSize);
    if (textRenderingMode != 0) canvas.setTextRenderingMode(textRenderingMode);
    if (auto cs = getPropertyAsFloat(Property::CHARACTER_SPACING)) canvas.setCharacterSpacing(*cs);
    if (auto ws = getPropertyAsFloat(Property::WORD_SPACING)) canvas.setWordSpacing(*ws);
    float hs = getPropertyAsFloat(Property::HORIZONTAL_SCALING).value_or(1);
    if (hs != 1) canvas.setHorizontalScaling(hs * 100);
    float rise = getPropertyAsFloat(Property::TEXT_RISE).value_or(0);
    if (rise != 0) canvas.setTextRise(rise);
    bool italic = getPropertyAsBoolean(Property::ITALIC_SIMULATION);
    auto skew = getProperty<std::vector<float>>(Property::SKEW);
    if (italic) canvas.setTextMatrix(1, 0, ITALIC_ANGLE, 1, x, y);
    else if (skew && skew->size() == 2)
        canvas.setTextMatrix(1, static_cast<float>(std::tan((*skew)[0] * M_PI / 180)), static_cast<float>(std::tan((*skew)[1] * M_PI / 180)), 1, x, y);
    else canvas.setTextMatrix(x, y);
    canvas.showText(glyphs);
    canvas.endText();
    auto underlines = getProperty<std::vector<Underline>>(Property::UNDERLINE);
    if (underlines) {
        for (const auto& u : *underlines) {
            ColorPtr c = u.getColor() ? u.getColor() : (fontColor ? fontColor : kernel::ColorConstants::BLACK);
            float thickness = u.getThickness(fontSize);
            float uy = y + u.getYPosition(fontSize);
            canvas.saveState().setStrokeColor(c).setLineWidth(thickness).moveTo(x, uy).lineTo(x + width, uy).stroke().restoreState();
        }
    }
    canvas.restoreState();
}

// ============================================================ ImageRenderer
ImageRenderer::ImageRenderer(std::shared_ptr<IElement> image) : AbstractRenderer(std::move(image)) {}

void ImageRenderer::calculateImageSize(float availableWidth, float availableHeight) {
    auto* image = dynamic_cast<Image*>(modelElement_);
    float w = image->getImageWidth(), h = image->getImageHeight();
    float sx = getPropertyAsFloat(Property::HORIZONTAL_SCALING).value_or(1);
    float sy = getPropertyAsFloat(Property::VERTICAL_SCALING).value_or(1);
    float width = w * sx, height = h * sy;
    auto propWidth = retrieveWidth(availableWidth);
    auto propHeight = retrieveHeight();
    if (propWidth && propHeight) {
        width = *propWidth;
        height = *propHeight;
    } else if (propWidth) {
        height = *propWidth * h / w;
        width = *propWidth;
    } else if (propHeight) {
        width = *propHeight * w / h;
        height = *propHeight;
    }
    bool autoScale = getPropertyAsBoolean(Property::AUTO_SCALE);
    bool autoScaleWidth = getPropertyAsBoolean(Property::AUTO_SCALE_WIDTH);
    bool autoScaleHeight = getPropertyAsBoolean(Property::AUTO_SCALE_HEIGHT);
    if (autoScale) {
        float scale = std::min(availableWidth / width, availableHeight / height);
        width *= scale;
        height *= scale;
    } else {
        if (autoScaleWidth && !autoScaleHeight) {
            height = height * availableWidth / width;
            width = availableWidth;
        } else if (autoScaleHeight && !autoScaleWidth) {
            width = width * availableHeight / height;
            height = availableHeight;
        }
    }
    imageWidth_ = width;
    imageHeight_ = height;
}

LayoutResult ImageRenderer::layout(LayoutContext& layoutContext) {
    LayoutArea& area = layoutContext.getArea();
    Rectangle box = area.getBBox();
    applyMargins(box, false);
    applyBorderBox(box, false);
    applyPaddings(box, false);
    calculateImageSize(box.getWidth(), box.getHeight());
    bool forced = isForced(*this);
    float w = imageWidth_, h = imageHeight_;
    if ((w > box.getWidth() + EPS || h > box.getHeight() + EPS) && !isFixedLayout()) {
        if (forced || area.isEmptyArea()) {
            // scale down to fit as iText does for forced placement
            float scale = std::min(box.getWidth() / w, box.getHeight() / h);
            if (scale < 1 && scale > 0) {
                imageWidth_ *= scale;
                imageHeight_ *= scale;
            }
        } else {
            return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
        }
    }
    float x = box.getX();
    auto ha = getProperty<HorizontalAlignment>(Property::HORIZONTAL_ALIGNMENT);
    if (ha && *ha == HorizontalAlignment::CENTER) x += (box.getWidth() - imageWidth_) / 2;
    else if (ha && *ha == HorizontalAlignment::RIGHT) x += box.getWidth() - imageWidth_;
    Rectangle used(x, box.getTop() - imageHeight_, imageWidth_, imageHeight_);
    applyPaddings(used, true);
    applyBorderBox(used, true);
    applyMargins(used, true);
    occupiedArea_ = LayoutArea(area.getPageNumber(), used);
    return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
}

void ImageRenderer::draw(DrawContext& drawContext) {
    drawBackground(drawContext);
    drawBorder(drawContext);
    auto* image = dynamic_cast<Image*>(modelElement_);
    Rectangle inner = getInnerAreaBBox();
    PdfCanvas& canvas = drawContext.getCanvas();
    auto& xObject = *image->getXObject();
    auto rotation = getPropertyAsFloat(Property::ROTATION_ANGLE);
    auto opacity = getPropertyAsFloat(Property::OPACITY);
    canvas.saveState();
    if (opacity && *opacity < 1) {
        kernel::PdfExtGState gs;
        gs.setFillOpacity(*opacity);
        canvas.setExtGState(gs);
    }
    if (rotation && *rotation != 0) {
        double cx = inner.getX() + imageWidth_ / 2, cy = inner.getY() + imageHeight_ / 2;
        canvas.concatMatrix(kernel::AffineTransform::getRotateInstance(*rotation, cx, cy));
    }
    if (xObject.isImage()) {
        canvas.addXObjectWithTransformationMatrix(xObject, imageWidth_, 0, 0, imageHeight_, inner.getX(), inner.getY());
    } else {
        float sx = xObject.getWidth() != 0 ? imageWidth_ / xObject.getWidth() : 1;
        float sy = xObject.getHeight() != 0 ? imageHeight_ / xObject.getHeight() : 1;
        canvas.addXObjectWithTransformationMatrix(xObject, sx, 0, 0, sy, inner.getX(), inner.getY());
    }
    canvas.restoreState();
}

// ============================================================ ParagraphRenderer
ParagraphRenderer::ParagraphRenderer(std::shared_ptr<IElement> paragraph) : BlockRenderer(std::move(paragraph)) {}

static bool isBreakableSpace(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x3000; }

// CJK ideographs allow line breaks between any two characters.
static bool isCjk(uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

void ParagraphRenderer::buildPieces() {
    if (piecesBuilt_) return;
    piecesBuilt_ = true;
    for (auto& childPtr : childRenderers_) {
        auto child = std::dynamic_pointer_cast<AbstractRenderer>(childPtr);
        if (!child) continue;
        if (auto* textRenderer = dynamic_cast<TextRenderer*>(child.get())) {
            auto font = resolveFont(*textRenderer);
            float fontSize = textRenderer->resolveFontSize();
            float charSpacing = textRenderer->getPropertyAsFloat(Property::CHARACTER_SPACING).value_or(0);
            float wordSpacing = textRenderer->getPropertyAsFloat(Property::WORD_SPACING).value_or(0);
            float hScale = textRenderer->getPropertyAsFloat(Property::HORIZONTAL_SCALING).value_or(1);
            bool kerning = textRenderer->getPropertyAsBoolean(Property::FONT_KERNING);
            float asc = textRenderer->getAscent(), desc = textRenderer->getDescent();
            auto cps = io::TextUtil::utf8ToCodePoints(textRenderer->getText());
            size_t i = 0;
            bool firstPieceOfChild = true;
            while (i < cps.size()) {
                Piece p{child, io::GlyphLine(), std::string(), 0, asc, desc, false, false, false, true};
                size_t j = i;
                if (cps[i] == '\n' || cps[i] == '\r') {
                    if (cps[i] == '\r' && i + 1 < cps.size() && cps[i + 1] == '\n') ++j;
                    p.isNewline = true;
                    ++j;
                } else if (isBreakableSpace(cps[i])) {
                    p.isSpace = true;
                    while (j < cps.size() && isBreakableSpace(cps[j])) ++j;
                } else if (isCjk(cps[i])) {
                    ++j;
                } else {
                    while (j < cps.size() && !isBreakableSpace(cps[j]) && cps[j] != '\n' && cps[j] != '\r' && !isCjk(cps[j])) {
                        ++j;
                        // allow breaking after a hyphen
                        if (cps[j - 1] == '-' && j < cps.size() && !isBreakableSpace(cps[j])) break;
                    }
                }
                std::vector<uint32_t> sub(cps.begin() + i, cps.begin() + j);
                p.text = io::TextUtil::codePointsToUtf8(sub);
                if (!p.isNewline) {
                    p.glyphs = font->createGlyphLine(p.text);
                    if (kerning && font->getFontProgram()->hasKernPairs()) {
                        for (int g = 0; g + 1 < p.glyphs.size(); ++g) {
                            int k = font->getFontProgram()->getKerning(p.glyphs.get(g), p.glyphs.get(g + 1));
                            if (k != 0) p.glyphs.get(g).setXAdvance(static_cast<int16_t>(k));
                        }
                    }
                    float w = 0;
                    for (int g = 0; g < p.glyphs.size(); ++g) {
                        const io::Glyph& gl = p.glyphs.get(g);
                        w += (gl.getWidth() + gl.getXAdvance()) * fontSize / 1000.f + charSpacing;
                        if (gl.hasValidUnicode() && gl.getUnicode() == ' ') w += wordSpacing;
                    }
                    p.width = w * hScale;
                }
                p.breakAllowedBefore = !firstPieceOfChild || true;
                pieces_.push_back(std::move(p));
                firstPieceOfChild = false;
                i = j;
            }
        } else if (auto* imageRenderer = dynamic_cast<ImageRenderer*>(child.get())) {
            Piece p{child, io::GlyphLine(), std::string(), 0, 0, 0, false, false, true, true};
            pieces_.push_back(std::move(p));
            (void)imageRenderer;
        } else {
            // nested block elements inside a paragraph are not supported in this port; ignore
        }
    }
    // join adjacent non-space pieces that must not be separated (word made of several Text chunks)
    for (size_t k = 1; k < pieces_.size(); ++k) {
        const Piece& prev = pieces_[k - 1];
        Piece& cur = pieces_[k];
        if (!prev.isSpace && !prev.isNewline && !cur.isSpace && !cur.isNewline && !prev.isImage && !cur.isImage &&
            prev.source != cur.source) {
            auto lastCp = io::TextUtil::utf8ToCodePoints(prev.text);
            auto firstCp = io::TextUtil::utf8ToCodePoints(cur.text);
            bool cjkBoundary = (!lastCp.empty() && isCjk(lastCp.back())) || (!firstCp.empty() && isCjk(firstCp.front()));
            bool afterHyphen = !lastCp.empty() && lastCp.back() == '-';
            if (!cjkBoundary && !afterHyphen) cur.breakAllowedBefore = false;
        }
    }
}

static Leading resolveLeading(const AbstractRenderer& r) {
    auto l = r.getProperty<Leading>(Property::LEADING);
    return l ? *l : Leading(Leading::MULTIPLIED, 1.35f);
}

LayoutResult ParagraphRenderer::layout(LayoutContext& layoutContext) {
    buildPieces();
    LayoutArea& area = layoutContext.getArea();
    Rectangle parentBBox = area.getBBox();
    float parentWidth = parentBBox.getWidth();
    Rectangle box = parentBBox;
    applyMargins(box, false);
    auto bordersArr = getBorders();
    auto paddingsArr = getPaddings(parentWidth);
    float horizontalExtras = (bordersArr[1] ? bordersArr[1]->getWidth() : 0) + (bordersArr[3] ? bordersArr[3]->getWidth() : 0) +
                             paddingsArr[1] + paddingsArr[3];
    if (auto w = retrieveWidth(parentWidth)) {
        float outer = *w + horizontalExtras;
        if (outer < box.getWidth()) {
            auto ha = getProperty<HorizontalAlignment>(Property::HORIZONTAL_ALIGNMENT);
            float extra = box.getWidth() - outer;
            if (ha && *ha == HorizontalAlignment::CENTER) box.moveRight(extra / 2);
            else if (ha && *ha == HorizontalAlignment::RIGHT) box.moveRight(extra);
        }
        box.setWidth(outer);
    }
    applyBorderBox(box, false);
    applyPaddings(box, false);
    contentBox_ = box;
    auto fixedHeight = retrieveHeight();
    Leading leading = resolveLeading(*this);
    float firstLineIndent = continuation_ ? 0 : getPropertyAsFloat(Property::FIRST_LINE_INDENT).value_or(0);
    bool forced = isForced(*this);

    lines_.clear();
    float usedHeight = 0;
    size_t idx = startPiece_;
    float fontSizeForEmpty = resolveFontSize();
    // Empty paragraph still occupies one line height (as in iText when it has no children: zero height).
    while (idx < pieces_.size()) {
        LineInfo line;
        line.leftIndent = lines_.empty() ? firstLineIndent : 0;
        line.availableWidth = box.getWidth() - line.leftIndent;
        // skip leading spaces on wrapped lines
        while (idx < pieces_.size() && pieces_[idx].isSpace && !lines_.empty()) ++idx;
        if (idx >= pieces_.size()) break;
        float x = 0;
        size_t lineStart = idx;
        size_t lastBreak = idx;       // index where a break may happen
        float widthAtBreak = 0;
        bool ended = false;
        while (idx < pieces_.size()) {
            Piece& p = pieces_[idx];
            if (p.isNewline) {
                ++idx;
                ended = true;
                break;
            }
            float pieceWidth = p.width;
            float pieceAscent = p.ascent, pieceDescent = p.descent;
            if (p.isImage) {
                auto* ir = static_cast<ImageRenderer*>(p.source.get());
                ir->calculateImageSize(line.availableWidth, box.getHeight());
                auto m = ir->getMargins();
                pieceWidth = ir->getImageWidth() + m[1] + m[3];
                pieceAscent = ir->getImageHeight() + m[0] + m[2];
                pieceDescent = 0;
                p.width = pieceWidth;
                p.ascent = pieceAscent;
                p.descent = 0;
            }
            if (p.breakAllowedBefore || p.isSpace) {
                lastBreak = idx;
                widthAtBreak = x;
            }
            if (x + pieceWidth > line.availableWidth + EPS && !p.isSpace) {
                if (lastBreak > lineStart) {
                    // wrap at the last allowed break
                    idx = lastBreak;
                    x = widthAtBreak;
                    line.items.erase(std::remove_if(line.items.begin(), line.items.end(),
                                                    [&](const LineItem& it) { return it.x >= widthAtBreak - EPS && it.x > 0; }),
                                     line.items.end());
                    break;
                }
                if (idx == lineStart && !p.isImage) {
                    // a single word wider than the line: split it by glyphs
                    float fontSize = static_cast<AbstractRenderer*>(p.source.get())->resolveFontSize();
                    float charSpacing = p.source->getPropertyAsFloat(Property::CHARACTER_SPACING).value_or(0);
                    float hScale = p.source->getPropertyAsFloat(Property::HORIZONTAL_SCALING).value_or(1);
                    float acc = 0;
                    int g = 0;
                    for (; g < p.glyphs.size(); ++g) {
                        float gw = ((p.glyphs.get(g).getWidth() + p.glyphs.get(g).getXAdvance()) * fontSize / 1000.f + charSpacing) * hScale;
                        if (acc + gw > line.availableWidth && g > 0) break;
                        acc += gw;
                    }
                    if (g < p.glyphs.size()) {
                        std::vector<io::Glyph> head, tail;
                        for (int k = 0; k < p.glyphs.size(); ++k) (k < g ? head : tail).push_back(p.glyphs.get(k));
                        Piece rest = p;
                        rest.glyphs = io::GlyphLine(tail);
                        rest.width = p.width - acc;
                        rest.text = rest.glyphs.toString();
                        rest.breakAllowedBefore = true;
                        p.glyphs = io::GlyphLine(head);
                        p.width = acc;
                        p.text = p.glyphs.toString();
                        pieces_.insert(pieces_.begin() + idx + 1, rest);
                        pieceWidth = acc;
                    }
                } else if (idx > lineStart) {
                    break;
                }
            }
            LineItem item;
            item.source = p.source;
            item.glyphs = p.glyphs;
            item.text = p.text;
            item.x = x;
            item.width = pieceWidth;
            item.ascent = pieceAscent;
            item.descent = pieceDescent;
            item.isImage = p.isImage;
            item.isSpace = p.isSpace;
            line.items.push_back(std::move(item));
            x += pieceWidth;
            ++idx;
        }
        // trailing spaces do not count for alignment
        float width = 0;
        for (const auto& it : line.items)
            if (!it.isSpace) width = std::max(width, it.x + it.width);
        line.width = width;
        float asc = 0, desc = 0;
        bool hasText = false;
        for (const auto& it : line.items) {
            asc = std::max(asc, it.ascent);
            desc = std::min(desc, it.descent);
            if (!it.isImage) hasText = true;
        }
        if (line.items.empty()) {
            // empty line created by a newline: use the paragraph font metrics
            asc = fontSizeForEmpty * 0.8f;
            desc = -fontSizeForEmpty * 0.2f;
            hasText = true;
        }
        line.ascent = asc;
        line.descent = desc;
        float textHeight = asc - desc;
        if (leading.getType() == Leading::FIXED) {
            line.height = std::max(leading.getValue(), textHeight);
            line.baselineOffset = (line.height - textHeight) / 2 + asc;
        } else {
            float l = hasText ? leading.getValue() : 1.f;
            line.height = textHeight * l;
            line.baselineOffset = asc + textHeight * (l - 1) / 2;
        }
        line.lastLine = ended || idx >= pieces_.size();
        if (usedHeight + line.height > box.getHeight() + EPS && !(forced && lines_.empty())) {
            // does not fit
            if (lines_.empty()) {
                return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
            }
            bool keepTogether = getPropertyAsBoolean(Property::KEEP_TOGETHER);
            if (keepTogether && !area.isEmptyArea())
                return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
            // split
            auto split = std::make_shared<ParagraphRenderer>(modelElementOwner_);
            split->modelElement_ = modelElement_;
            split->properties_ = properties_;
            split->parent_ = parent_;
            split->pieces_ = pieces_;
            split->piecesBuilt_ = true;
            split->lines_ = lines_;
            split->contentBox_ = contentBox_;
            split->childRenderers_ = childRenderers_;
            split->setPropertyAny(Property::MARGIN_BOTTOM, UnitValue::createPointValue(0));
            auto overflow = std::make_shared<ParagraphRenderer>(modelElementOwner_);
            overflow->modelElement_ = modelElement_;
            overflow->properties_ = properties_;
            overflow->parent_ = parent_;
            overflow->pieces_ = pieces_;
            overflow->piecesBuilt_ = true;
            overflow->startPiece_ = lineStart;
            overflow->continuation_ = true;
            overflow->childRenderers_ = childRenderers_;
            overflow->setPropertyAny(Property::MARGIN_TOP, UnitValue::createPointValue(0));
            Rectangle used(box.getX(), box.getTop() - usedHeight, box.getWidth(), usedHeight);
            applyPaddings(used, true);
            applyBorderBox(used, true);
            auto m = getMargins(parentWidth);
            used.applyMargins(m[0], m[1], 0, m[3], true);
            split->occupiedArea_ = LayoutArea(area.getPageNumber(), used);
            split->contentBox_.setY(box.getTop() - usedHeight).setHeight(usedHeight);
            return LayoutResult(LayoutResult::PARTIAL, split->occupiedArea_, split, overflow);
        }
        lines_.push_back(std::move(line));
        usedHeight += lines_.back().height;
    }
    float contentHeight = usedHeight;
    if (fixedHeight) contentHeight = *fixedHeight;
    if (auto mh = retrieveMinHeight()) contentHeight = std::max(contentHeight, *mh);
    if (contentHeight > box.getHeight() + EPS && !forced && !area.isEmptyArea() && usedHeight <= box.getHeight())
        return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
    contentBox_ = Rectangle(box.getX(), box.getTop() - contentHeight, box.getWidth(), contentHeight);
    // vertical alignment inside fixed height
    auto va = getProperty<VerticalAlignment>(Property::VERTICAL_ALIGNMENT);
    if (va && *va != VerticalAlignment::TOP && contentHeight > usedHeight) {
        float free = contentHeight - usedHeight;
        float shift = *va == VerticalAlignment::MIDDLE ? free / 2 : free;
        for (auto& l : lines_) l.baselineOffset += shift;
    }
    Rectangle used = contentBox_;
    applyPaddings(used, true);
    applyBorderBox(used, true);
    applyMargins(used, true);
    occupiedArea_ = LayoutArea(area.getPageNumber(), used);
    return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
}

std::any ParagraphRenderer::getDefaultPropertyAny(int property) const {
    if ((property == Property::MARGIN_TOP || property == Property::MARGIN_BOTTOM) && parent_ && parent_->getModelElement() &&
        dynamic_cast<Cell*>(parent_->getModelElement()))
        return UnitValue::createPointValue(0);
    return BlockRenderer::getDefaultPropertyAny(property);
}

std::optional<float> ParagraphRenderer::getFirstYLineRecursively() const {
    if (lines_.empty()) return std::nullopt;
    return contentBox_.getTop() - lines_.front().baselineOffset;
}

void ParagraphRenderer::drawChildren(DrawContext& drawContext) {
    TextAlignment alignment = getProperty<TextAlignment>(Property::TEXT_ALIGNMENT).value_or(TextAlignment::LEFT);
    float lineTop = contentBox_.getTop();
    for (const auto& line : lines_) {
        float baseline = lineTop - line.baselineOffset;
        float offset = 0;
        float extraPerSpace = 0;
        float freeSpace = line.availableWidth - line.width;
        if (alignment == TextAlignment::RIGHT) offset = freeSpace;
        else if (alignment == TextAlignment::CENTER) offset = freeSpace / 2;
        else if ((alignment == TextAlignment::JUSTIFIED && !line.lastLine) || alignment == TextAlignment::JUSTIFIED_ALL) {
            int spaces = 0;
            float lastEnd = 0;
            for (const auto& it : line.items)
                if (!it.isSpace) lastEnd = it.x + it.width;
            for (const auto& it : line.items)
                if (it.isSpace && it.x < lastEnd) ++spaces;
            if (spaces > 0) extraPerSpace = freeSpace / spaces;
        }
        float startX = contentBox_.getX() + line.leftIndent + offset;
        float shift = 0;
        // draw consecutive text items from the same source in one operation
        size_t i = 0;
        while (i < line.items.size()) {
            const LineItem& it = line.items[i];
            if (it.isSpace) {
                if (extraPerSpace > 0) shift += extraPerSpace;
                ++i;
                continue;
            }
            if (it.isImage) {
                auto* ir = static_cast<ImageRenderer*>(it.source.get());
                auto m = ir->getMargins();
                Rectangle r(startX + it.x + shift, baseline, ir->getImageWidth() + m[1] + m[3], ir->getImageHeight() + m[0] + m[2]);
                ir->getOccupiedArea() = LayoutArea(occupiedArea_.getPageNumber(), r);
                ir->draw(drawContext);
                ++i;
                continue;
            }
            auto* tr = static_cast<TextRenderer*>(it.source.get());
            std::vector<io::Glyph> glyphs;
            float runX = startX + it.x + shift;
            float runWidth = 0;
            size_t j = i;
            while (j < line.items.size() && line.items[j].source == it.source && !line.items[j].isImage &&
                   (extraPerSpace == 0 || !line.items[j].isSpace)) {
                // trailing spaces at line end are not drawn
                bool trailing = true;
                for (size_t k = j; k < line.items.size(); ++k)
                    if (!line.items[k].isSpace) trailing = false;
                if (trailing) break;
                for (const auto& g : line.items[j].glyphs.getGlyphs()) glyphs.push_back(g);
                runWidth = line.items[j].x + line.items[j].width - it.x;
                ++j;
            }
            tr->drawGlyphs(drawContext, io::GlyphLine(glyphs), runX, baseline, runWidth);
            i = std::max(j, i + 1);
        }
        lineTop -= line.height;
    }
}

// ============================================================ LineSeparatorRenderer
LayoutResult LineSeparatorRenderer::layout(LayoutContext& layoutContext) {
    LayoutArea& area = layoutContext.getArea();
    Rectangle box = area.getBBox();
    applyMargins(box, false);
    auto drawer = getProperty<std::shared_ptr<ILineDrawer>>(Property::LINE_DRAWER);
    float height = drawer && *drawer ? (*drawer)->getLineWidth() : 0;
    if (auto w = retrieveWidth(box.getWidth())) box.setWidth(*w);
    if (height > box.getHeight() && !area.isEmptyArea())
        return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
    Rectangle used(box.getX(), box.getTop() - height, box.getWidth(), height);
    applyMargins(used, true);
    occupiedArea_ = LayoutArea(area.getPageNumber(), used);
    return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
}

void LineSeparatorRenderer::drawChildren(DrawContext& drawContext) {
    auto drawer = getProperty<std::shared_ptr<ILineDrawer>>(Property::LINE_DRAWER);
    if (!drawer || !*drawer) return;
    Rectangle area = occupiedArea_.getBBox();
    applyMargins(area, false);
    (*drawer)->draw(drawContext.getCanvas(), area);
}

// ============================================================ ListRenderer / ListItemRenderer
ListRenderer::ListRenderer(std::shared_ptr<IElement> list) : BlockRenderer(std::move(list)) {}

static std::string toRoman(int n) {
    static const std::pair<int, const char*> table[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
                                                        {90, "xc"},  {50, "l"},   {40, "xl"}, {10, "x"},   {9, "ix"},
                                                        {5, "v"},    {4, "iv"},   {1, "i"}};
    std::string s;
    for (const auto& p : table)
        while (n >= p.first) {
            s += p.second;
            n -= p.first;
        }
    return s;
}

static std::string toLatin(int n) {
    std::string s;
    while (n > 0) {
        --n;
        s.insert(s.begin(), static_cast<char>('a' + n % 26));
        n /= 26;
    }
    return s;
}

std::string ListRenderer::makeSymbol(int index) const {
    const std::any* raw = getPropertyAny(Property::LIST_SYMBOL);
    std::optional<ListNumberingType> type;
    if (raw)
        if (auto* t = std::any_cast<ListNumberingType>(raw)) type = *t;
    std::string pre = getProperty<std::string>(Property::LIST_SYMBOL_PRE_TEXT).value_or("");
    std::string post = getProperty<std::string>(Property::LIST_SYMBOL_POST_TEXT).value_or(". ");
    if (!type) return std::string();
    std::string num;
    switch (*type) {
        case ListNumberingType::DECIMAL: num = std::to_string(index); break;
        case ListNumberingType::DECIMAL_LEADING_ZERO: num = (index < 10 ? "0" : "") + std::to_string(index); break;
        case ListNumberingType::ROMAN_LOWER: num = toRoman(index); break;
        case ListNumberingType::ROMAN_UPPER: {
            num = toRoman(index);
            for (auto& c : num) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            break;
        }
        case ListNumberingType::ENGLISH_LOWER: num = toLatin(index); break;
        case ListNumberingType::ENGLISH_UPPER: {
            num = toLatin(index);
            for (auto& c : num) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            break;
        }
        case ListNumberingType::GREEK_LOWER: io::TextUtil::appendUtf8(num, 0x3B1 + (index - 1) % 24); break;
        case ListNumberingType::GREEK_UPPER: io::TextUtil::appendUtf8(num, 0x391 + (index - 1) % 24); break;
        case ListNumberingType::ZAPF_DINGBATS_1: io::TextUtil::appendUtf8(num, 0x2460 + (index - 1) % 10); break;
    }
    return pre + num + post;
}

LayoutResult ListRenderer::layout(LayoutContext& layoutContext) {
    if (!symbolsAssigned_) {
        symbolsAssigned_ = true;
        int start = getPropertyAsInteger(Property::LIST_START).value_or(1);
        std::vector<std::pair<ListItemRenderer*, std::shared_ptr<TextRenderer>>> items;
        float maxWidth = 0;
        int index = start;
        for (auto& child : childRenderers_) {
            auto* item = dynamic_cast<ListItemRenderer*>(child.get());
            if (!item) continue;
            std::shared_ptr<TextRenderer> symbol;
            // a ListItem may override the symbol
            auto itemSymbol = item->getModelElement() ? item->getModelElement()->getOwnPropertyAny(Property::LIST_SYMBOL) : nullptr;
            const std::any* listSymbolAny = getPropertyAny(Property::LIST_SYMBOL);
            std::optional<std::shared_ptr<Text>> listText;
            if (listSymbolAny)
                if (auto* t = std::any_cast<std::shared_ptr<Text>>(listSymbolAny)) listText = *t;
            if (itemSymbol) {
                if (auto* t = std::any_cast<std::shared_ptr<Text>>(itemSymbol)) {
                    symbol = std::make_shared<TextRenderer>(std::static_pointer_cast<IElement>(*t), (*t)->getText());
                } else if (auto* nt = std::any_cast<ListNumberingType>(itemSymbol)) {
                    (void)nt;
                    symbol = std::make_shared<TextRenderer>(nullptr, makeSymbol(index));
                }
            }
            if (!symbol) {
                if (listText && *listText) {
                    symbol = std::make_shared<TextRenderer>(std::static_pointer_cast<IElement>(*listText), (*listText)->getText());
                } else {
                    symbol = std::make_shared<TextRenderer>(nullptr, makeSymbol(index));
                }
            }
            symbol->setParent(item);
            auto font = resolveFont(*symbol);
            float w = font->getWidth(symbol->getText(), symbol->resolveFontSize());
            maxWidth = std::max(maxWidth, w);
            items.emplace_back(item, symbol);
            ++index;
        }
        float indent = getPropertyAsFloat(Property::LIST_SYMBOL_INDENT).value_or(0);
        for (auto& p : items) p.first->setSymbol(p.second, maxWidth + indent);
    }
    return BlockRenderer::layout(layoutContext);
}

ListItemRenderer::ListItemRenderer(std::shared_ptr<IElement> item) : BlockRenderer(std::move(item)) {}

LayoutResult ListItemRenderer::layout(LayoutContext& layoutContext) {
    // reserve the symbol area on the left
    LayoutContext ctx = layoutContext;
    Rectangle& bbox = ctx.getArea().getBBox();
    bbox.setX(bbox.getX() + symbolAreaWidth_).setWidth(bbox.getWidth() - symbolAreaWidth_);
    LayoutResult result = BlockRenderer::layout(ctx);
    if (result.getStatus() == LayoutResult::FULL) {
        occupiedArea_.getBBox().setX(occupiedArea_.getBBox().getX() - symbolAreaWidth_);
        occupiedArea_.getBBox().setWidth(occupiedArea_.getBBox().getWidth() + symbolAreaWidth_);
        return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
    }
    return result;
}

std::shared_ptr<BlockRenderer> ListItemRenderer::createSplitRenderer() const {
    auto r = std::make_shared<ListItemRenderer>(modelElementOwner_);
    r->modelElement_ = modelElement_;
    r->properties_ = properties_;
    r->parent_ = parent_;
    r->symbol_ = symbol_;
    r->symbolAreaWidth_ = symbolAreaWidth_;
    return r;
}

std::shared_ptr<BlockRenderer> ListItemRenderer::createOverflowRenderer() const {
    auto r = std::static_pointer_cast<ListItemRenderer>(createSplitRenderer());
    r->symbol_ = nullptr;  // symbol drawn only with the first part
    return r;
}

void ListItemRenderer::draw(DrawContext& drawContext) {
    BlockRenderer::draw(drawContext);
    if (symbol_ && !symbolDrawn_) {
        auto y = getFirstYLineRecursively();
        Rectangle inner = occupiedArea_.getBBox();
        applyMargins(inner, false);
        float baseline = y ? *y : inner.getTop() - symbol_->getAscent();
        auto font = resolveFont(*symbol_);
        io::GlyphLine glyphs = font->createGlyphLine(symbol_->getText());
        symbol_->drawGlyphs(drawContext, glyphs, inner.getX(), baseline, font->getWidth(symbol_->getText(), symbol_->resolveFontSize()));
    }
}

// ============================================================ TableRenderer
TableRenderer::TableRenderer(std::shared_ptr<IElement> table) : AbstractRenderer(std::move(table)) {}

std::vector<float> TableRenderer::computeColumnWidths(float availableWidth) const {
    const auto& cols = table().getColumnWidths();
    float tableWidth = availableWidth;
    auto w = retrieveWidth(availableWidth);
    bool allPoints = std::all_of(cols.begin(), cols.end(), [](const UnitValue& u) { return u.isPointValue(); });
    float sumPoints = 0, sumPercents = 0;
    for (const auto& c : cols) (c.isPointValue() ? sumPoints : sumPercents) += c.getValue();
    if (w) tableWidth = *w;
    else if (allPoints) tableWidth = std::min(sumPoints, availableWidth);
    std::vector<float> widths;
    if (allPoints) {
        float scale = sumPoints > 0 ? tableWidth / sumPoints : 1;
        for (const auto& c : cols) widths.push_back(c.getValue() * scale);
    } else {
        float remaining = tableWidth - sumPoints;
        for (const auto& c : cols) {
            if (c.isPointValue()) widths.push_back(c.getValue());
            else widths.push_back(sumPercents > 0 ? remaining * c.getValue() / sumPercents : 0);
        }
    }
    return widths;
}

std::optional<float> TableRenderer::getFirstYLineRecursively() const {
    for (const auto& pc : placed_) {
        auto y = pc.renderer->getFirstYLineRecursively();
        if (y) return y;
    }
    return std::nullopt;
}

LayoutResult TableRenderer::layout(LayoutContext& layoutContext) {
    LayoutArea& area = layoutContext.getArea();
    Rectangle parentBBox = area.getBBox();
    Rectangle box = parentBBox;
    applyMargins(box, false);
    applyBorderBox(box, false);
    std::vector<float> colWidths = computeColumnWidths(box.getWidth());
    float tableWidth = 0;
    for (float w : colWidths) tableWidth += w;
    auto ha = getProperty<HorizontalAlignment>(Property::HORIZONTAL_ALIGNMENT);
    float x0 = box.getX();
    if (ha && *ha == HorizontalAlignment::CENTER) x0 += (box.getWidth() - tableWidth) / 2;
    else if (ha && *ha == HorizontalAlignment::RIGHT) x0 += box.getWidth() - tableWidth;
    std::vector<float> colX(colWidths.size() + 1, x0);
    for (size_t i = 0; i < colWidths.size(); ++i) colX[i + 1] = colX[i] + colWidths[i];
    placed_.clear();
    bool forced = isForced(*this);

    float curTop = box.getTop();
    float bottomLimit = box.getY();
    // Lays out a block of rows from `t` (header/body/footer) starting at row `from`. Returns rows consumed.
    struct RowLayout {
        std::vector<PlacedCell> cells;
        float height;
    };
    auto layoutCellAt = [&](const CellPtr& cell, float left, float width, float top, float availableHeight,
                            std::shared_ptr<BlockRenderer> existing) -> std::pair<LayoutResult, std::shared_ptr<BlockRenderer>> {
        std::shared_ptr<BlockRenderer> r = existing;
        if (!r) {
            r = std::static_pointer_cast<BlockRenderer>(cell->createRendererSubTree());
        }
        r->setParent(this);
        LayoutArea cellArea(area.getPageNumber(), Rectangle(left, top - availableHeight, width, availableHeight));
        cellArea.setEmptyArea(true);
        LayoutContext ctx(cellArea);
        return {r->layout(ctx), r};
    };
    // Measures/lays out a row group of table `t` starting at row r. A row group extends while rowspans continue.
    auto layoutRowGroup = [&](Table& t, int r, float top, float availableHeight, bool allowSplit,
                              std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>>& pending,
                              std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>>& nextPending, int& rowsInGroup,
                              bool& splitHappened) -> std::optional<RowLayout> {
        int nRows = t.getNumberOfRows();
        int nCols = t.getNumberOfColumns();
        int groupEnd = r + 1;
        for (int rr = r; rr < groupEnd && rr < nRows; ++rr)
            for (int c = 0; c < nCols; ++c) {
                auto cell = t.getCell(rr, c);
                if (cell) groupEnd = std::max(groupEnd, std::min(nRows, rr + cell->getRowspan()));
            }
        rowsInGroup = groupEnd - r;
        RowLayout row;
        float groupHeight = 0;
        std::vector<std::tuple<CellPtr, int, LayoutResult, std::shared_ptr<BlockRenderer>>> results;
        splitHappened = false;
        for (int rr = r; rr < groupEnd; ++rr) {
            for (int c = 0; c < nCols; ++c) {
                auto cell = t.getCell(rr, c);
                if (!cell) continue;
                int colspan = std::min(cell->getColspan(), nCols - c);
                float left = colX[c], width = colX[c + colspan] - colX[c];
                auto it = pending.find({rr, c});
                auto res = layoutCellAt(cell, left, width, top, allowSplit ? availableHeight : 1e6f,
                                        it != pending.end() ? it->second : nullptr);
                const LayoutResult& lr = res.first;
                if (lr.getStatus() == LayoutResult::FULL) {
                    groupHeight = std::max(groupHeight, lr.getOccupiedArea().getBBox().getHeight());
                    results.emplace_back(cell, c, lr, res.second);
                } else if (lr.getStatus() == LayoutResult::PARTIAL) {
                    splitHappened = true;
                    groupHeight = std::max(groupHeight, lr.getOccupiedArea().getBBox().getHeight());
                    results.emplace_back(cell, c, lr, std::static_pointer_cast<BlockRenderer>(lr.getSplitRenderer()));
                    nextPending[{rr, c}] = std::static_pointer_cast<BlockRenderer>(lr.getOverflowRenderer());
                } else {
                    if (!allowSplit) return std::nullopt;
                    splitHappened = true;
                    // nothing fits: the whole cell moves to the next part
                    nextPending[{rr, c}] = res.second;
                    results.emplace_back(cell, c, LayoutResult(LayoutResult::FULL, LayoutArea(area.getPageNumber(), Rectangle(left, top, width, 0)), nullptr, nullptr),
                                         nullptr);
                }
            }
        }
        if (splitHappened) groupHeight = std::max(groupHeight, availableHeight);
        if (auto mh = t.getProperty<UnitValue>(Property::MIN_HEIGHT)) groupHeight = std::max(groupHeight, mh->getValue());
        row.height = groupHeight;
        for (auto& tup : results) {
            CellPtr cell = std::get<0>(tup);
            int c = std::get<1>(tup);
            auto renderer = std::get<3>(tup);
            int colspan = std::min(cell->getColspan(), nCols - c);
            Rectangle cellBox(colX[c], top - groupHeight, colX[c + colspan] - colX[c], groupHeight);
            if (renderer) {
                // stretch the cell to the row height and apply vertical alignment
                Rectangle occ = renderer->getOccupiedArea().getBBox();
                float free = groupHeight - occ.getHeight();
                auto va = renderer->getProperty<VerticalAlignment>(Property::VERTICAL_ALIGNMENT);
                if (va && *va != VerticalAlignment::TOP && free > 0) {
                    float dy = *va == VerticalAlignment::MIDDLE ? free / 2 : free;
                    for (auto& ch : renderer->getChildRenderers()) ch->move(0, -dy);
                }
                renderer->getOccupiedArea().setBBox(cellBox);
                row.cells.push_back({renderer, cellBox});
            }
        }
        return row;
    };

    Table& t = table();
    auto header = t.getHeader();
    auto footer = t.getFooter();
    std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> noPending, dummy;
    // header
    float headerHeight = 0;
    std::vector<PlacedCell> headerCells;
    bool showHeader = header && !(t.isSkipFirstHeader() && !isOverflowPart_);
    if (showHeader) {
        float top = curTop;
        for (int r = 0; r < header->getNumberOfRows();) {
            int n = 1;
            bool split = false;
            auto row = layoutRowGroup(*header, r, top, 1e6f, false, noPending, dummy, n, split);
            if (!row) break;
            for (auto& c : row->cells) headerCells.push_back(c);
            top -= row->height;
            r += n;
        }
        headerHeight = curTop - top;
    }
    float footerHeight = 0;
    std::vector<PlacedCell> footerCells;
    if (footer) {
        float top = 0;
        for (int r = 0; r < footer->getNumberOfRows();) {
            int n = 1;
            bool split = false;
            auto row = layoutRowGroup(*footer, r, top, 1e6f, false, noPending, dummy, n, split);
            if (!row) break;
            for (auto& c : row->cells) footerCells.push_back(c);
            top -= row->height;
            r += n;
        }
        footerHeight = -top;
    }
    float bodyTop = curTop - headerHeight;
    (void)bodyTop;
    int nRows = t.getNumberOfRows();
    int r = startRow_;
    std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> pending = pendingCellOverflow_;
    std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> nextPending;
    float y = bodyTop;
    bool anyRowPlaced = false;
    bool splitAtRow = false;
    while (r < nRows) {
        int n = 1;
        bool split = false;
        nextPending.clear();
        float avail = y - (bottomLimit + footerHeight);
        // try without splitting first
        std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> np;
        auto row = layoutRowGroup(t, r, y, 1e6f, false, pending, np, n, split);
        bool keepTogether = getPropertyAsBoolean(Property::KEEP_TOGETHER);
        if (row && row->height <= avail + EPS) {
            placed_.insert(placed_.end(), row->cells.begin(), row->cells.end());
            y -= row->height;
            r += n;
            anyRowPlaced = true;
            pending.clear();
            continue;
        }
        // the row group does not fit
        if (anyRowPlaced || (keepTogether && !area.isEmptyArea() && !forced)) {
            if (!anyRowPlaced) return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
            splitAtRow = true;
            break;
        }
        if (!area.isEmptyArea() && !forced && avail < (row ? row->height : 1e6f) && avail < 0.5f * (bodyTop - bottomLimit + 1e6f)) {
            // move the whole table to the next area if it is not empty
            if (startRow_ == 0 && pending.empty()) return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
        }
        // split cell contents of this row group
        auto partRow = layoutRowGroup(t, r, y, avail, true, pending, nextPending, n, split);
        if (!partRow || partRow->height <= 0) {
            if (!area.isEmptyArea()) return LayoutResult(LayoutResult::NOTHING, area, nullptr, shared_from_this(), shared_from_this());
            break;
        }
        placed_.insert(placed_.end(), partRow->cells.begin(), partRow->cells.end());
        y -= partRow->height;
        anyRowPlaced = true;
        if (split) {
            splitAtRow = true;
            pendingCellOverflow_ = nextPending;
            break;
        }
        r += n;
        pending.clear();
    }
    // header & footer positions
    for (auto& hc : headerCells) placed_.insert(placed_.begin(), hc);
    if (!footerCells.empty() && !(t.isSkipLastFooter() && !splitAtRow)) {
        for (auto& fc : footerCells) {
            float dy = y;  // footer cells were laid out with top = 0
            fc.renderer->move(0, dy);
            fc.box.moveUp(dy);
            fc.renderer->getOccupiedArea().setBBox(fc.box);
            placed_.push_back(fc);
        }
        y -= footerHeight;
    }
    Rectangle used(x0, y, tableWidth, curTop - y);
    applyBorderBox(used, true);
    auto m = getMargins(parentBBox.getWidth());
    if (splitAtRow) {
        used.applyMargins(m[0], m[1], 0, m[3], true);
        occupiedArea_ = LayoutArea(area.getPageNumber(), used);
        auto split = std::make_shared<TableRenderer>(modelElementOwner_);
        split->modelElement_ = modelElement_;
        split->properties_ = properties_;
        split->parent_ = parent_;
        split->placed_ = placed_;
        split->occupiedArea_ = occupiedArea_;
        split->setPropertyAny(Property::MARGIN_BOTTOM, UnitValue::createPointValue(0));
        auto overflow = std::make_shared<TableRenderer>(modelElementOwner_);
        overflow->modelElement_ = modelElement_;
        overflow->properties_ = properties_;
        overflow->parent_ = parent_;
        overflow->startRow_ = pendingCellOverflow_.empty() ? r : r;
        overflow->pendingCellOverflow_ = pendingCellOverflow_;
        overflow->isOverflowPart_ = true;
        overflow->setPropertyAny(Property::MARGIN_TOP, UnitValue::createPointValue(0));
        pendingCellOverflow_.clear();
        return LayoutResult(LayoutResult::PARTIAL, occupiedArea_, split, overflow);
    }
    used.applyMargins(m[0], m[1], m[2], m[3], true);
    occupiedArea_ = LayoutArea(area.getPageNumber(), used);
    return LayoutResult(LayoutResult::FULL, occupiedArea_, nullptr, nullptr);
}

void TableRenderer::draw(DrawContext& drawContext) {
    drawBackground(drawContext);
    // cells: background, content, then borders on top
    for (auto& pc : placed_) {
        pc.renderer->drawBackground(drawContext);
        pc.renderer->drawChildren(drawContext);
    }
    PdfCanvas& canvas = drawContext.getCanvas();
    for (auto& pc : placed_) {
        auto borders = pc.renderer->getBorders();
        const Rectangle& b = pc.box;
        if (borders[0]) borders[0]->drawCellBorder(canvas, b.getLeft(), b.getTop(), b.getRight(), b.getTop(), Border::Side::TOP);
        if (borders[1]) borders[1]->drawCellBorder(canvas, b.getRight(), b.getTop(), b.getRight(), b.getBottom(), Border::Side::RIGHT);
        if (borders[2]) borders[2]->drawCellBorder(canvas, b.getRight(), b.getBottom(), b.getLeft(), b.getBottom(), Border::Side::BOTTOM);
        if (borders[3]) borders[3]->drawCellBorder(canvas, b.getLeft(), b.getBottom(), b.getLeft(), b.getTop(), Border::Side::LEFT);
    }
    drawBorder(drawContext);
}

}  // namespace layout
}  // namespace pdfcpp
