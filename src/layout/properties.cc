#include "pdfcpp/layout/properties.h"

#include <cmath>

#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace layout {

std::vector<UnitValue> UnitValue::CreatePercentArray(
    const std::vector<float>& values) {
  float sum = 0;
  for (float v : values) sum += v;
  std::vector<UnitValue> out;
  for (float v : values)
    out.push_back(CreatePercentValue(sum == 0 ? 0 : v * 100 / sum));
  return out;
}

std::vector<UnitValue> UnitValue::CreatePointArray(
    const std::vector<float>& values) {
  std::vector<UnitValue> out;
  for (float v : values) out.push_back(CreatePointValue(v));
  return out;
}

namespace {
class NoBorder : public Border {
 public:
  NoBorder() : Border(kernel::color_constants::kBlack, 0) {}
  int GetType() const override { return -1; }
  void Draw(kernel::PdfCanvas&, float, float, float, float, Side, float,
            float) override {}
  void DrawCellBorder(kernel::PdfCanvas&, float, float, float, float,
                      Side) override {}
};

void ApplyOpacity(kernel::PdfCanvas& canvas, float opacity, bool fill) {
  if (opacity >= 1) return;
  kernel::PdfExtGState gs;
  if (fill)
    gs.SetFillOpacity(opacity);
  else
    gs.SetStrokeOpacity(opacity);
  canvas.SetExtGState(gs);
}
}  // namespace

const std::shared_ptr<Border> Border::kNoBorder = std::make_shared<NoBorder>();

// The trapezoid filling approach of iText: the border band between the outer
// edge and the inner edge.
void Border::Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2,
                  float y2, Side side, float before, float after) {
  float x3 = 0, y3 = 0, x4 = 0, y4 = 0;
  switch (side) {
    case Side::kTop:
      x3 = x2 + after;
      y3 = y2 + width_;
      x4 = x1 - before;
      y4 = y1 + width_;
      break;
    case Side::kRight:
      x3 = x2 + width_;
      y3 = y2 - after;
      x4 = x1 + width_;
      y4 = y1 + before;
      break;
    case Side::kBottom:
      x3 = x2 - after;
      y3 = y2 - width_;
      x4 = x1 + before;
      y4 = y1 - width_;
      break;
    case Side::kLeft:
      x3 = x2 - width_;
      y3 = y2 + after;
      x4 = x1 - width_;
      y4 = y1 - before;
      break;
    default:
      return;
  }
  canvas.SaveState();
  ApplyOpacity(canvas, opacity_, true);
  canvas.SetFillColor(color_);
  canvas.MoveTo(x1, y1)
      .LineTo(x2, y2)
      .LineTo(x3, y3)
      .LineTo(x4, y4)
      .LineTo(x1, y1)
      .Fill();
  canvas.RestoreState();
}

void Border::DrawCellBorder(kernel::PdfCanvas& canvas, float x1, float y1,
                            float x2, float y2, Side) {
  canvas.SaveState();
  ApplyOpacity(canvas, opacity_, false);
  canvas.SetStrokeColor(color_);
  canvas.SetLineWidth(width_);
  ApplyStyle(canvas, std::hypot(x2 - x1, y2 - y1));
  canvas.MoveTo(x1, y1).LineTo(x2, y2).Stroke();
  canvas.RestoreState();
}

static void StrokeCenterLine(kernel::PdfCanvas& canvas, float x1, float y1,
                             float x2, float y2, Border::Side side,
                             float width) {
  float dx = 0, dy = 0;
  switch (side) {
    case Border::Side::kTop:
      dy = width / 2;
      break;
    case Border::Side::kBottom:
      dy = -width / 2;
      break;
    case Border::Side::kLeft:
      dx = -width / 2;
      break;
    case Border::Side::kRight:
      dx = width / 2;
      break;
    default:
      break;
  }
  canvas.MoveTo(x1 + dx, y1 + dy).LineTo(x2 + dx, y2 + dy).Stroke();
}

void DashedBorder::ApplyStyle(kernel::PdfCanvas& canvas, float) const {
  canvas.SetLineDash(width_ * 5, width_ * 5 / 2 + width_, 0);
}

void DashedBorder::Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2,
                        float y2, Side side, float, float) {
  canvas.SaveState();
  ApplyOpacity(canvas, opacity_, false);
  canvas.SetStrokeColor(color_).SetLineWidth(width_);
  ApplyStyle(canvas, 0);
  StrokeCenterLine(canvas, x1, y1, x2, y2, side, width_);
  canvas.RestoreState();
}

void DottedBorder::ApplyStyle(kernel::PdfCanvas& canvas, float) const {
  canvas.SetLineDash(width_, width_ * 2, width_ * 2.5f);
}

void DottedBorder::Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2,
                        float y2, Side side, float, float) {
  canvas.SaveState();
  ApplyOpacity(canvas, opacity_, false);
  canvas.SetStrokeColor(color_).SetLineWidth(width_);
  ApplyStyle(canvas, 0);
  StrokeCenterLine(canvas, x1, y1, x2, y2, side, width_);
  canvas.RestoreState();
}

void DoubleBorder::Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2,
                        float y2, Side side, float before, float after) {
  float third = width_ / 3;
  canvas.SaveState();
  ApplyOpacity(canvas, opacity_, false);
  canvas.SetStrokeColor(color_).SetLineWidth(third);
  float dx = 0, dy = 0;
  switch (side) {
    case Side::kTop:
      dy = 1;
      break;
    case Side::kBottom:
      dy = -1;
      break;
    case Side::kLeft:
      dx = -1;
      break;
    case Side::kRight:
      dx = 1;
      break;
    default:
      break;
  }
  float inner = third / 2, outer = width_ - third / 2;
  canvas.MoveTo(x1 + dx * inner, y1 + dy * inner)
      .LineTo(x2 + dx * inner, y2 + dy * inner)
      .Stroke();
  canvas.MoveTo(x1 + dx * outer, y1 + dy * outer)
      .LineTo(x2 + dx * outer, y2 + dy * outer)
      .Stroke();
  canvas.RestoreState();
  (void)before;
  (void)after;
}

void SolidLine::Draw(kernel::PdfCanvas& canvas,
                     const kernel::Rectangle& draw_area) {
  canvas.SaveState()
      .SetStrokeColor(color_)
      .SetLineWidth(line_width_)
      .MoveTo(draw_area.GetX(), draw_area.GetY() + line_width_ / 2)
      .LineTo(draw_area.GetX() + draw_area.GetWidth(),
              draw_area.GetY() + line_width_ / 2)
      .Stroke()
      .RestoreState();
}

void DashedLine::Draw(kernel::PdfCanvas& canvas,
                      const kernel::Rectangle& draw_area) {
  canvas.SaveState()
      .SetLineWidth(line_width_)
      .SetStrokeColor(color_)
      .SetLineDash(2, 2, 0)
      .MoveTo(draw_area.GetX(), draw_area.GetY() + line_width_ / 2)
      .LineTo(draw_area.GetX() + draw_area.GetWidth(),
              draw_area.GetY() + line_width_ / 2)
      .Stroke()
      .RestoreState();
}

void DottedLine::Draw(kernel::PdfCanvas& canvas,
                      const kernel::Rectangle& draw_area) {
  canvas.SaveState()
      .SetLineWidth(line_width_)
      .SetStrokeColor(color_)
      .SetLineDash(0, gap_, gap_ / 2)
      .SetLineCapStyle(kernel::pdf_canvas_constants::line_cap_style::kRound)
      .MoveTo(draw_area.GetX(), draw_area.GetY() + line_width_ / 2)
      .LineTo(draw_area.GetX() + draw_area.GetWidth(),
              draw_area.GetY() + line_width_ / 2)
      .Stroke()
      .RestoreState();
}

}  // namespace layout
}  // namespace pdfcpp
