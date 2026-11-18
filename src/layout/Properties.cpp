#include "pdfcpp/layout/Properties.h"

#include <cmath>

#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace layout {

std::vector<UnitValue> UnitValue::createPercentArray(const std::vector<float>& values) {
    float sum = 0;
    for (float v : values) sum += v;
    std::vector<UnitValue> out;
    for (float v : values) out.push_back(createPercentValue(sum == 0 ? 0 : v * 100 / sum));
    return out;
}

std::vector<UnitValue> UnitValue::createPointArray(const std::vector<float>& values) {
    std::vector<UnitValue> out;
    for (float v : values) out.push_back(createPointValue(v));
    return out;
}

namespace {
class NoBorder : public Border {
public:
    NoBorder() : Border(kernel::ColorConstants::BLACK, 0) {}
    int getType() const override { return -1; }
    void draw(kernel::PdfCanvas&, float, float, float, float, Side, float, float) override {}
    void drawCellBorder(kernel::PdfCanvas&, float, float, float, float, Side) override {}
};

void applyOpacity(kernel::PdfCanvas& canvas, float opacity, bool fill) {
    if (opacity >= 1) return;
    kernel::PdfExtGState gs;
    if (fill) gs.setFillOpacity(opacity);
    else gs.setStrokeOpacity(opacity);
    canvas.setExtGState(gs);
}
}  // namespace

const std::shared_ptr<Border> Border::NO_BORDER = std::make_shared<NoBorder>();

// The trapezoid filling approach of iText: the border band between the outer edge and the inner edge.
void Border::draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float before, float after) {
    float x3 = 0, y3 = 0, x4 = 0, y4 = 0;
    switch (side) {
        case Side::TOP:
            x3 = x2 + after;
            y3 = y2 + width_;
            x4 = x1 - before;
            y4 = y1 + width_;
            break;
        case Side::RIGHT:
            x3 = x2 + width_;
            y3 = y2 - after;
            x4 = x1 + width_;
            y4 = y1 + before;
            break;
        case Side::BOTTOM:
            x3 = x2 - after;
            y3 = y2 - width_;
            x4 = x1 + before;
            y4 = y1 - width_;
            break;
        case Side::LEFT:
            x3 = x2 - width_;
            y3 = y2 + after;
            x4 = x1 - width_;
            y4 = y1 - before;
            break;
        default:
            return;
    }
    canvas.saveState();
    applyOpacity(canvas, opacity_, true);
    canvas.setFillColor(color_);
    canvas.moveTo(x1, y1).lineTo(x2, y2).lineTo(x3, y3).lineTo(x4, y4).lineTo(x1, y1).fill();
    canvas.restoreState();
}

void Border::drawCellBorder(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side) {
    canvas.saveState();
    applyOpacity(canvas, opacity_, false);
    canvas.setStrokeColor(color_);
    canvas.setLineWidth(width_);
    applyStyle(canvas, std::hypot(x2 - x1, y2 - y1));
    canvas.moveTo(x1, y1).lineTo(x2, y2).stroke();
    canvas.restoreState();
}

static void strokeCenterLine(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Border::Side side, float width) {
    float dx = 0, dy = 0;
    switch (side) {
        case Border::Side::TOP: dy = width / 2; break;
        case Border::Side::BOTTOM: dy = -width / 2; break;
        case Border::Side::LEFT: dx = -width / 2; break;
        case Border::Side::RIGHT: dx = width / 2; break;
        default: break;
    }
    canvas.moveTo(x1 + dx, y1 + dy).lineTo(x2 + dx, y2 + dy).stroke();
}

void DashedBorder::applyStyle(kernel::PdfCanvas& canvas, float) const {
    canvas.setLineDash(width_ * 5, width_ * 5 / 2 + width_, 0);
}

void DashedBorder::draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float, float) {
    canvas.saveState();
    applyOpacity(canvas, opacity_, false);
    canvas.setStrokeColor(color_).setLineWidth(width_);
    applyStyle(canvas, 0);
    strokeCenterLine(canvas, x1, y1, x2, y2, side, width_);
    canvas.restoreState();
}

void DottedBorder::applyStyle(kernel::PdfCanvas& canvas, float) const { canvas.setLineDash(width_, width_ * 2, width_ * 2.5f); }

void DottedBorder::draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float, float) {
    canvas.saveState();
    applyOpacity(canvas, opacity_, false);
    canvas.setStrokeColor(color_).setLineWidth(width_);
    applyStyle(canvas, 0);
    strokeCenterLine(canvas, x1, y1, x2, y2, side, width_);
    canvas.restoreState();
}

void DoubleBorder::draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float before, float after) {
    float third = width_ / 3;
    canvas.saveState();
    applyOpacity(canvas, opacity_, false);
    canvas.setStrokeColor(color_).setLineWidth(third);
    float dx = 0, dy = 0;
    switch (side) {
        case Side::TOP: dy = 1; break;
        case Side::BOTTOM: dy = -1; break;
        case Side::LEFT: dx = -1; break;
        case Side::RIGHT: dx = 1; break;
        default: break;
    }
    float inner = third / 2, outer = width_ - third / 2;
    canvas.moveTo(x1 + dx * inner, y1 + dy * inner).lineTo(x2 + dx * inner, y2 + dy * inner).stroke();
    canvas.moveTo(x1 + dx * outer, y1 + dy * outer).lineTo(x2 + dx * outer, y2 + dy * outer).stroke();
    canvas.restoreState();
    (void)before;
    (void)after;
}

void SolidLine::draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) {
    canvas.saveState()
        .setStrokeColor(color_)
        .setLineWidth(lineWidth_)
        .moveTo(drawArea.getX(), drawArea.getY() + lineWidth_ / 2)
        .lineTo(drawArea.getX() + drawArea.getWidth(), drawArea.getY() + lineWidth_ / 2)
        .stroke()
        .restoreState();
}

void DashedLine::draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) {
    canvas.saveState()
        .setLineWidth(lineWidth_)
        .setStrokeColor(color_)
        .setLineDash(2, 2, 0)
        .moveTo(drawArea.getX(), drawArea.getY() + lineWidth_ / 2)
        .lineTo(drawArea.getX() + drawArea.getWidth(), drawArea.getY() + lineWidth_ / 2)
        .stroke()
        .restoreState();
}

void DottedLine::draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) {
    canvas.saveState()
        .setLineWidth(lineWidth_)
        .setStrokeColor(color_)
        .setLineDash(0, gap_, gap_ / 2)
        .setLineCapStyle(kernel::PdfCanvasConstants::LineCapStyle::ROUND)
        .moveTo(drawArea.getX(), drawArea.getY() + lineWidth_ / 2)
        .lineTo(drawArea.getX() + drawArea.getWidth(), drawArea.getY() + lineWidth_ / 2)
        .stroke()
        .restoreState();
}

}  // namespace layout
}  // namespace pdfcpp
