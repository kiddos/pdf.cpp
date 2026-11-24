#include "pdfcpp/layout/renderers.h"

#include <algorithm>
#include <cmath>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/text_util.h"
#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace layout {

using kernel::PdfCanvas;
using kernel::PdfFont;

static constexpr float kItalicAngle = 0.21256f;
static constexpr float kBoldSimulationStrokeCoeff = 1 / 30.f;
static constexpr float kTypoAscenderScaleCoeff = 1.2f;
static constexpr float kEps = 1e-4f;

// ============================================================ AbstractRenderer
const std::any* AbstractRenderer::GetPropertyAny(int property) const {
  auto it = properties_.find(property);
  if (it != properties_.end()) return &it->second;
  if (model_element_) {
    if (const std::any* v = model_element_->GetOwnPropertyAny(property))
      return v;
  }
  if (parent_ && property::IsPropertyInherited(property)) {
    if (const std::any* v = parent_->GetPropertyAny(property)) return v;
  }
  // defaults are cached so that a stable pointer can be returned
  std::any d = GetDefaultPropertyAny(property);
  if (d.has_value()) {
    auto& slot = default_cache_[property];
    slot = std::move(d);
    return &slot;
  }
  return nullptr;
}

bool AbstractRenderer::HasProperty(int property) const {
  return GetPropertyAny(property) != nullptr;
}

std::any AbstractRenderer::GetDefaultPropertyAny(int property) const {
  return model_element_ ? model_element_->GetDefaultPropertyAny(property)
                        : std::any();
}

void AbstractRenderer::AddChild(IRendererPtr renderer) {
  if (!renderer) return;
  renderer->SetParent(this);
  child_renderers_.push_back(std::move(renderer));
}

void AbstractRenderer::Move(float dx, float dy) {
  occupied_area_.GetBBox().MoveRight(dx);
  occupied_area_.GetBBox().MoveUp(dy);
  for (auto& child : child_renderers_) child->Move(dx, dy);
}

std::shared_ptr<PdfFont> AbstractRenderer::GetPropertyAsFont(
    int property) const {
  auto f = GetProperty<std::shared_ptr<PdfFont>>(property);
  return f ? *f : nullptr;
}

ColorPtr AbstractRenderer::GetPropertyAsColor(int property) const {
  auto c = GetProperty<ColorPtr>(property);
  if (c) return *c;
  auto rgb = GetProperty<std::shared_ptr<kernel::DeviceRgb>>(property);
  if (rgb) return *rgb;
  return nullptr;
}

std::optional<float> AbstractRenderer::GetPropertyAsFloat(int property) const {
  if (auto f = GetProperty<float>(property)) return f;
  if (auto d = GetProperty<double>(property)) return static_cast<float>(*d);
  if (auto i = GetProperty<int>(property)) return static_cast<float>(*i);
  return std::nullopt;
}

std::optional<UnitValue> AbstractRenderer::GetPropertyAsUnitValue(
    int property) const {
  if (auto u = GetProperty<UnitValue>(property)) return u;
  if (auto f = GetPropertyAsFloat(property))
    return UnitValue::CreatePointValue(*f);
  return std::nullopt;
}

bool AbstractRenderer::GetPropertyAsBoolean(int property) const {
  auto b = GetProperty<bool>(property);
  return b && *b;
}

std::optional<int> AbstractRenderer::GetPropertyAsInteger(int property) const {
  return GetProperty<int>(property);
}

float AbstractRenderer::ResolveFontSize() const {
  auto fs = GetPropertyAsUnitValue(property::kFontSize);
  return fs ? fs->GetValue() : 12.f;
}

std::array<BorderPtr, 4> AbstractRenderer::GetBorders() const {
  std::array<BorderPtr, 4> borders;
  const int props[4] = {property::kBorderTop, property::kBorderRight,
                        property::kBorderBottom, property::kBorderLeft};
  for (int i = 0; i < 4; ++i) {
    auto b = GetProperty<BorderPtr>(props[i]);
    if (!b) {
      auto sb = GetProperty<std::shared_ptr<SolidBorder>>(props[i]);
      if (sb) b = *sb;
    }
    borders[i] = (b && *b && (*b)->GetType() >= 0) ? *b : nullptr;
  }
  return borders;
}

std::array<float, 4> AbstractRenderer::GetMargins(float base) const {
  const int props[4] = {property::kMarginTop, property::kMarginRight,
                        property::kMarginBottom, property::kMarginLeft};
  std::array<float, 4> r{};
  for (int i = 0; i < 4; ++i) {
    auto u = GetPropertyAsUnitValue(props[i]);
    r[i] = u ? u->Resolve(base) : 0.f;
  }
  return r;
}

std::array<float, 4> AbstractRenderer::GetPaddings(float base) const {
  const int props[4] = {property::kPaddingTop, property::kPaddingRight,
                        property::kPaddingBottom, property::kPaddingLeft};
  std::array<float, 4> r{};
  for (int i = 0; i < 4; ++i) {
    auto u = GetPropertyAsUnitValue(props[i]);
    r[i] = u ? u->Resolve(base) : 0.f;
  }
  return r;
}

Rectangle& AbstractRenderer::ApplyMargins(Rectangle& rect, bool reverse) const {
  auto m = GetMargins(rect.GetWidth());
  return rect.ApplyMargins(m[0], m[1], m[2], m[3], reverse);
}

Rectangle& AbstractRenderer::ApplyBorderBox(Rectangle& rect,
                                            bool reverse) const {
  auto b = GetBorders();
  return rect.ApplyMargins(
      b[0] ? b[0]->GetWidth() : 0, b[1] ? b[1]->GetWidth() : 0,
      b[2] ? b[2]->GetWidth() : 0, b[3] ? b[3]->GetWidth() : 0, reverse);
}

Rectangle& AbstractRenderer::ApplyPaddings(Rectangle& rect,
                                           bool reverse) const {
  auto p = GetPaddings(rect.GetWidth());
  return rect.ApplyMargins(p[0], p[1], p[2], p[3], reverse);
}

Rectangle AbstractRenderer::GetInnerAreaBBox() const {
  Rectangle r = occupied_area_.GetBBox();
  ApplyMargins(r, false);
  ApplyBorderBox(r, false);
  ApplyPaddings(r, false);
  return r;
}

std::optional<float> AbstractRenderer::RetrieveWidth(
    float parent_box_width) const {
  auto w = GetPropertyAsUnitValue(property::kWidth);
  if (!w) return std::nullopt;
  float v = w->Resolve(parent_box_width);
  if (auto mx = GetPropertyAsUnitValue(property::kMaxWidth))
    v = std::min(v, mx->Resolve(parent_box_width));
  if (auto mn = GetPropertyAsUnitValue(property::kMinWidth))
    v = std::max(v, mn->Resolve(parent_box_width));
  return v;
}

std::optional<float> AbstractRenderer::RetrieveHeight() const {
  auto h = GetPropertyAsUnitValue(property::kHeight);
  if (!h || h->IsPercentValue()) return std::nullopt;
  return h->GetValue();
}

std::optional<float> AbstractRenderer::RetrieveMinHeight() const {
  auto h = GetPropertyAsUnitValue(property::kMinHeight);
  if (!h || h->IsPercentValue()) return std::nullopt;
  return h->GetValue();
}

std::optional<float> AbstractRenderer::RetrieveMaxHeight() const {
  auto h = GetPropertyAsUnitValue(property::kMaxHeight);
  if (!h || h->IsPercentValue()) return std::nullopt;
  return h->GetValue();
}

bool AbstractRenderer::IsFixedLayout() const {
  auto pos = GetProperty<int>(property::kPosition);
  return pos && *pos == 4;
}

void AbstractRenderer::DrawBackground(DrawContext& draw_context) {
  auto bg = GetProperty<Background>(property::kBackground);
  if (!bg || !bg->GetColor()) return;
  Rectangle bbox = occupied_area_.GetBBox();
  ApplyMargins(bbox, false);
  if (bbox.GetWidth() <= 0 || bbox.GetHeight() <= 0) return;
  PdfCanvas& canvas = draw_context.GetCanvas();
  canvas.SaveState();
  if (bg->GetOpacity() < 1) {
    kernel::PdfExtGState gs;
    gs.SetFillOpacity(bg->GetOpacity());
    canvas.SetExtGState(gs);
  }
  canvas.SetFillColor(bg->GetColor())
      .Rectangle(bbox.GetX() - bg->GetExtraLeft(),
                 bbox.GetY() - bg->GetExtraBottom(),
                 bbox.GetWidth() + bg->GetExtraLeft() + bg->GetExtraRight(),
                 bbox.GetHeight() + bg->GetExtraTop() + bg->GetExtraBottom())
      .Fill();
  canvas.RestoreState();
}

void AbstractRenderer::DrawBorder(DrawContext& draw_context) {
  auto borders = GetBorders();
  if (!borders[0] && !borders[1] && !borders[2] && !borders[3]) return;
  Rectangle bbox = occupied_area_.GetBBox();
  ApplyMargins(bbox, false);
  float top_w = borders[0] ? borders[0]->GetWidth() : 0,
        right_w = borders[1] ? borders[1]->GetWidth() : 0;
  float bottom_w = borders[2] ? borders[2]->GetWidth() : 0,
        left_w = borders[3] ? borders[3]->GetWidth() : 0;
  // inner rectangle of the border box
  float x1 = bbox.GetX() + left_w, y1 = bbox.GetY() + bottom_w;
  float x2 = bbox.GetRight() - right_w, y2 = bbox.GetTop() - top_w;
  PdfCanvas& canvas = draw_context.GetCanvas();
  if (borders[0])
    borders[0]->Draw(canvas, x1, y2, x2, y2, Border::Side::kTop, left_w,
                     right_w);
  if (borders[1])
    borders[1]->Draw(canvas, x2, y2, x2, y1, Border::Side::kRight, top_w,
                     bottom_w);
  if (borders[2])
    borders[2]->Draw(canvas, x2, y1, x1, y1, Border::Side::kBottom, right_w,
                     left_w);
  if (borders[3])
    borders[3]->Draw(canvas, x1, y1, x1, y2, Border::Side::kLeft, bottom_w,
                     top_w);
}

void AbstractRenderer::DrawChildren(DrawContext& draw_context) {
  for (auto& child : child_renderers_) child->Draw(draw_context);
}

void AbstractRenderer::Draw(DrawContext& draw_context) {
  DrawBackground(draw_context);
  DrawBorder(draw_context);
  DrawChildren(draw_context);
}

// ============================================================ BlockRenderer
std::shared_ptr<BlockRenderer> BlockRenderer::CreateSplitRenderer() const {
  auto r = std::make_shared<BlockRenderer>(model_element_owner_);
  r->model_element_ = model_element_;
  r->properties_ = properties_;
  r->parent_ = parent_;
  return r;
}

std::shared_ptr<BlockRenderer> BlockRenderer::CreateOverflowRenderer() const {
  return CreateSplitRenderer();
}

IRendererPtr BlockRenderer::GetNextRenderer() { return CreateSplitRenderer(); }

static bool IsForced(const AbstractRenderer& r) {
  return r.GetPropertyAsBoolean(property::kForcedPlacement);
}

LayoutResult BlockRenderer::Layout(LayoutContext& layout_context) {
  LayoutArea& area = layout_context.GetArea();
  Rectangle parent_b_box = area.GetBBox();
  float parent_width = parent_b_box.GetWidth();
  Rectangle box = parent_b_box;
  ApplyMargins(box, false);
  auto borders_arr = GetBorders();
  auto paddings_arr = GetPaddings(parent_width);
  float horizontal_extras = (borders_arr[1] ? borders_arr[1]->GetWidth() : 0) +
                            (borders_arr[3] ? borders_arr[3]->GetWidth() : 0) +
                            paddings_arr[1] + paddings_arr[3];
  if (auto w = RetrieveWidth(parent_width)) {
    float outer = *w + horizontal_extras;
    if (outer < box.GetWidth()) {
      auto ha =
          GetProperty<HorizontalAlignment>(property::kHorizontalAlignment);
      float extra = box.GetWidth() - outer;
      if (ha && *ha == HorizontalAlignment::kCenter)
        box.MoveRight(extra / 2);
      else if (ha && *ha == HorizontalAlignment::kRight)
        box.MoveRight(extra);
    }
    box.SetWidth(outer);
  }
  std::optional<float> fixed_height = RetrieveHeight();
  ApplyBorderBox(box, false);
  ApplyPaddings(box, false);
  Rectangle content = box;
  if (fixed_height && *fixed_height < content.GetHeight()) {
    content.SetY(content.GetTop() - *fixed_height);
    content.SetHeight(*fixed_height);
  }
  float cur_top = content.GetTop();
  float lowest = cur_top;
  std::vector<IRendererPtr> placed;
  bool forced = IsForced(*this);
  for (size_t i = 0; i < child_renderers_.size(); ++i) {
    IRendererPtr child = child_renderers_[i];
    Rectangle child_box(content.GetX(), content.GetY(), content.GetWidth(),
                        cur_top - content.GetY());
    LayoutArea child_area(area.GetPageNumber(), child_box);
    child_area.SetEmptyArea(area.IsEmptyArea() && placed.empty());
    LayoutContext child_context(child_area);
    LayoutResult result = child->Layout(child_context);
    int status = result.GetStatus();
    if (status == LayoutResult::kFull) {
      placed.push_back(child);
      cur_top = result.GetOccupiedArea().GetBBox().GetY();
      lowest = std::min(lowest, cur_top);
      continue;
    }
    if (status == LayoutResult::kNothing && forced && placed.empty()) {
      // forced placement: keep the child even though it does not fit
      child->SetPropertyAny(property::kForcedPlacement, true);
      LayoutResult retry = child->Layout(child_context);
      if (retry.GetStatus() != LayoutResult::kNothing) {
        result = retry;
        status = retry.GetStatus();
        if (status == LayoutResult::kFull) {
          placed.push_back(child);
          cur_top = result.GetOccupiedArea().GetBBox().GetY();
          lowest = std::min(lowest, cur_top);
          continue;
        }
      }
    }
    // split
    bool keep_together = GetPropertyAsBoolean(property::kKeepTogether);
    if ((status == LayoutResult::kNothing && placed.empty()) ||
        (keep_together && !area.IsEmptyArea() && !forced)) {
      return LayoutResult(
          LayoutResult::kNothing, area, nullptr, shared_from_this(),
          result.GetCauseOfNothing() ? result.GetCauseOfNothing() : child);
    }
    auto split = CreateSplitRenderer();
    auto overflow = CreateOverflowRenderer();
    for (auto& p : placed) split->AddChild(p);
    if (status == LayoutResult::kPartial) {
      if (result.GetSplitRenderer()) {
        split->AddChild(result.GetSplitRenderer());
        lowest = std::min(lowest, result.GetOccupiedArea().GetBBox().GetY());
      }
      if (result.GetOverflowRenderer())
        overflow->AddChild(result.GetOverflowRenderer());
    } else {
      overflow->AddChild(child);
    }
    for (size_t k = i + 1; k < child_renderers_.size(); ++k)
      overflow->AddChild(child_renderers_[k]);
    split->SetPropertyAny(property::kMarginBottom,
                          UnitValue::CreatePointValue(0));
    overflow->SetPropertyAny(property::kMarginTop,
                             UnitValue::CreatePointValue(0));
    if (fixed_height) {
      // The overflow part only gets what is left of the fixed height. Shadow
      // the property on the renderer: a kHeight set on the model element would
      // otherwise still apply in full on the next area.
      float remaining = *fixed_height - (content.GetTop() - lowest);
      overflow->SetPropertyAny(
          property::kHeight,
          remaining > kEps ? std::any(UnitValue::CreatePointValue(remaining))
                           : std::any());
    }
    // occupied area of split part
    Rectangle used(content.GetX(), lowest, content.GetWidth(),
                   content.GetTop() - lowest);
    if (GetPropertyAsBoolean(property::kFillAvailableAreaOnSplit) ||
        GetPropertyAsBoolean(property::kFillAvailableArea))
      used = Rectangle(content.GetX(), content.GetY(), content.GetWidth(),
                       content.GetHeight());
    ApplyPaddings(used, true);
    ApplyBorderBox(used, true);
    auto m = GetMargins(parent_width);
    used.ApplyMargins(m[0], m[1], 0, m[3], true);
    split->occupied_area_ = LayoutArea(area.GetPageNumber(), used);
    return LayoutResult(LayoutResult::kPartial, split->occupied_area_, split,
                        overflow);
  }
  float content_height = content.GetTop() - lowest;
  if (fixed_height) content_height = *fixed_height;
  if (auto mh = RetrieveMinHeight())
    content_height = std::max(content_height, *mh);
  if (GetPropertyAsBoolean(property::kFillAvailableArea))
    content_height = content.GetTop() - parent_b_box.GetY();
  Rectangle used(content.GetX(), content.GetTop() - content_height,
                 content.GetWidth(), content_height);
  ApplyPaddings(used, true);
  ApplyBorderBox(used, true);
  ApplyMargins(used, true);
  if (used.GetY() < parent_b_box.GetY() - kEps && !forced) {
    // min/fixed height made the block overflow the area
    if (area.IsEmptyArea()) {
      // place anyway
    } else {
      return LayoutResult(LayoutResult::kNothing, area, nullptr,
                          shared_from_this(), shared_from_this());
    }
  }
  // vertical alignment of children inside a fixed-height block
  auto va = GetProperty<VerticalAlignment>(property::kVerticalAlignment);
  if (va && *va != VerticalAlignment::kTop &&
      content_height > content.GetTop() - lowest) {
    float free = content_height - (content.GetTop() - lowest);
    float dy = *va == VerticalAlignment::kMiddle ? free / 2 : free;
    for (auto& c : child_renderers_) c->Move(0, -dy);
  }
  occupied_area_ = LayoutArea(area.GetPageNumber(), used);
  return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
}

void BlockRenderer::Draw(DrawContext& draw_context) {
  bool positioned = IsFixedLayout();
  auto rotation = GetPropertyAsFloat(property::kRotationAngle);
  PdfCanvas& canvas = draw_context.GetCanvas();
  if (rotation && *rotation != 0) {
    Rectangle b = occupied_area_.GetBBox();
    double cx = b.GetX() + b.GetWidth() / 2, cy = b.GetY() + b.GetHeight() / 2;
    kernel::AffineTransform t =
        kernel::AffineTransform::GetRotateInstance(*rotation, cx, cy);
    canvas.SaveState().ConcatMatrix(t);
  }
  auto opacity = GetPropertyAsFloat(property::kOpacity);
  bool opacity_set =
      opacity && *opacity < 1 && !dynamic_cast<TextRenderer*>(this);
  if (opacity_set) {
    canvas.SaveState();
    kernel::PdfExtGState gs;
    gs.SetFillOpacity(*opacity).SetStrokeOpacity(*opacity);
    canvas.SetExtGState(gs);
  }
  DrawBackground(draw_context);
  DrawBorder(draw_context);
  DrawChildren(draw_context);
  if (opacity_set) canvas.RestoreState();
  if (rotation && *rotation != 0) canvas.RestoreState();
  (void)positioned;
}

std::optional<float> BlockRenderer::GetFirstYLineRecursively() const {
  for (const auto& child : child_renderers_) {
    if (auto* b = dynamic_cast<BlockRenderer*>(child.get())) {
      auto y = b->GetFirstYLineRecursively();
      if (y) return y;
    } else if (auto* t = dynamic_cast<TableRenderer*>(child.get())) {
      auto y = t->GetFirstYLineRecursively();
      if (y) return y;
    }
  }
  return std::nullopt;
}

// ============================================================ TextRenderer
TextRenderer::TextRenderer(std::shared_ptr<IElement> text_element)
    : AbstractRenderer(std::move(text_element)) {
  if (auto* t = dynamic_cast<Text*>(model_element_)) text_ = t->GetText();
}

TextRenderer::TextRenderer(std::shared_ptr<IElement> text_element,
                           std::string text)
    : AbstractRenderer(std::move(text_element)), text_(std::move(text)) {}

LayoutResult TextRenderer::Layout(LayoutContext& layout_context) {
  // Text is laid out by its parent ParagraphRenderer.
  return LayoutResult(LayoutResult::kFull, layout_context.GetArea(), nullptr,
                      nullptr);
}

static std::pair<float, float> AscenderDescender(PdfFont* font) {
  const auto& m = font->GetFontProgram()->GetFontMetrics();
  float ascender, descender;
  if (m.GetWinAscender() == 0 || m.GetWinDescender() == 0 ||
      (m.GetTypoAscender() == m.GetWinAscender() &&
       m.GetTypoDescender() == m.GetWinDescender())) {
    ascender = m.GetTypoAscender() * kTypoAscenderScaleCoeff;
    descender = m.GetTypoDescender() * kTypoAscenderScaleCoeff;
  } else {
    ascender = static_cast<float>(m.GetWinAscender());
    descender = static_cast<float>(m.GetWinDescender());
  }
  if (descender > 0) descender = -descender;
  return {ascender, descender};
}

static std::shared_ptr<PdfFont> ResolveFont(const AbstractRenderer& r) {
  auto font = r.GetPropertyAsFont(property::kFont);
  if (!font) {
    static std::shared_ptr<PdfFont> fallback;
    if (!fallback) fallback = kernel::PdfFontFactory::CreateFont();
    font = fallback;
  }
  return font;
}

float TextRenderer::GetAscent() const {
  auto font = ResolveFont(*this);
  float rise = GetPropertyAsFloat(property::kTextRise).value_or(0);
  return AscenderDescender(font.get()).first * ResolveFontSize() / 1000.f +
         rise;
}

float TextRenderer::GetDescent() const {
  auto font = ResolveFont(*this);
  float rise = GetPropertyAsFloat(property::kTextRise).value_or(0);
  return AscenderDescender(font.get()).second * ResolveFontSize() / 1000.f +
         rise;
}

void TextRenderer::DrawGlyphs(DrawContext& draw_context,
                              const io::GlyphLine& glyphs, float x, float y,
                              float width) {
  if (glyphs.size() == 0) return;
  auto font = ResolveFont(*this);
  float font_size = ResolveFontSize();
  PdfCanvas& canvas = draw_context.GetCanvas();
  canvas.SaveState();
  auto opacity = GetPropertyAsFloat(property::kOpacity);
  if (opacity && *opacity < 1) {
    kernel::PdfExtGState gs;
    gs.SetFillOpacity(*opacity).SetStrokeOpacity(*opacity);
    canvas.SetExtGState(gs);
  }
  ColorPtr font_color = GetPropertyAsColor(property::kFontColor);
  if (font_color) canvas.SetFillColor(font_color);
  int text_rendering_mode =
      GetPropertyAsInteger(property::kTextRenderingMode).value_or(0);
  bool bold = GetPropertyAsBoolean(property::kBoldSimulation);
  if (bold && text_rendering_mode ==
                  kernel::pdf_canvas_constants::text_rendering_mode::kFill)
    text_rendering_mode =
        kernel::pdf_canvas_constants::text_rendering_mode::kFillStroke;
  if (text_rendering_mode != 0) {
    ColorPtr stroke_color = GetPropertyAsColor(property::kStrokeColor);
    if (!stroke_color) stroke_color = font_color;
    if (stroke_color) canvas.SetStrokeColor(stroke_color);
    float stroke_width =
        GetPropertyAsFloat(property::kStrokeWidth)
            .value_or(bold ? font_size * kBoldSimulationStrokeCoeff : 1.f);
    canvas.SetLineWidth(stroke_width);
  }
  canvas.BeginText().SetFontAndSize(font, font_size);
  if (text_rendering_mode != 0)
    canvas.SetTextRenderingMode(text_rendering_mode);
  if (auto cs = GetPropertyAsFloat(property::kCharacterSpacing))
    canvas.SetCharacterSpacing(*cs);
  if (auto ws = GetPropertyAsFloat(property::kWordSpacing))
    canvas.SetWordSpacing(*ws);
  float hs = GetPropertyAsFloat(property::kHorizontalScaling).value_or(1);
  if (hs != 1) canvas.SetHorizontalScaling(hs * 100);
  float rise = GetPropertyAsFloat(property::kTextRise).value_or(0);
  if (rise != 0) canvas.SetTextRise(rise);
  bool italic = GetPropertyAsBoolean(property::kItalicSimulation);
  auto skew = GetProperty<std::vector<float>>(property::kSkew);
  if (italic)
    canvas.SetTextMatrix(1, 0, kItalicAngle, 1, x, y);
  else if (skew && skew->size() == 2)
    canvas.SetTextMatrix(
        1, static_cast<float>(std::tan((*skew)[0] * M_PI / 180)),
        static_cast<float>(std::tan((*skew)[1] * M_PI / 180)), 1, x, y);
  else
    canvas.SetTextMatrix(x, y);
  canvas.ShowText(glyphs);
  canvas.EndText();
  auto underlines = GetProperty<std::vector<Underline>>(property::kUnderline);
  if (underlines) {
    for (const auto& u : *underlines) {
      ColorPtr c =
          u.GetColor()
              ? u.GetColor()
              : (font_color ? font_color : kernel::color_constants::kBlack);
      float thickness = u.GetThickness(font_size);
      float uy = y + u.GetYPosition(font_size);
      canvas.SaveState()
          .SetStrokeColor(c)
          .SetLineWidth(thickness)
          .MoveTo(x, uy)
          .LineTo(x + width, uy)
          .Stroke()
          .RestoreState();
    }
  }
  canvas.RestoreState();
}

// ============================================================ ImageRenderer
ImageRenderer::ImageRenderer(std::shared_ptr<IElement> image)
    : AbstractRenderer(std::move(image)) {}

void ImageRenderer::CalculateImageSize(float available_width,
                                       float available_height) {
  auto* image = dynamic_cast<Image*>(model_element_);
  float w = image->GetImageWidth(), h = image->GetImageHeight();
  float sx = GetPropertyAsFloat(property::kHorizontalScaling).value_or(1);
  float sy = GetPropertyAsFloat(property::kVerticalScaling).value_or(1);
  float width = w * sx, height = h * sy;
  auto prop_width = RetrieveWidth(available_width);
  auto prop_height = RetrieveHeight();
  if (prop_width && prop_height) {
    width = *prop_width;
    height = *prop_height;
  } else if (prop_width) {
    // keep the aspect ratio; a zero-sized image (e.g. an empty form XObject)
    // has none to keep
    if (w != 0) height = *prop_width * h / w;
    width = *prop_width;
  } else if (prop_height) {
    if (h != 0) width = *prop_height * w / h;
    height = *prop_height;
  }
  bool auto_scale = GetPropertyAsBoolean(property::kAutoScale);
  bool auto_scale_width = GetPropertyAsBoolean(property::kAutoScaleWidth);
  bool auto_scale_height = GetPropertyAsBoolean(property::kAutoScaleHeight);
  if (auto_scale) {
    if (width != 0 || height != 0) {
      float scale = width == 0    ? available_height / height
                    : height == 0 ? available_width / width
                                  : std::min(available_width / width,
                                             available_height / height);
      width *= scale;
      height *= scale;
    }
  } else {
    if (auto_scale_width && !auto_scale_height) {
      if (width != 0) height = height * available_width / width;
      width = available_width;
    } else if (auto_scale_height && !auto_scale_width) {
      if (height != 0) width = width * available_height / height;
      height = available_height;
    }
  }
  image_width_ = width;
  image_height_ = height;
}

float ImageRenderer::RotatedWidth() const {
  float angle = GetPropertyAsFloat(property::kRotationAngle).value_or(0);
  return std::fabs(image_width_ * std::cos(angle)) +
         std::fabs(image_height_ * std::sin(angle));
}

float ImageRenderer::RotatedHeight() const {
  float angle = GetPropertyAsFloat(property::kRotationAngle).value_or(0);
  return std::fabs(image_width_ * std::sin(angle)) +
         std::fabs(image_height_ * std::cos(angle));
}

LayoutResult ImageRenderer::Layout(LayoutContext& layout_context) {
  LayoutArea& area = layout_context.GetArea();
  Rectangle box = area.GetBBox();
  ApplyMargins(box, false);
  ApplyBorderBox(box, false);
  ApplyPaddings(box, false);
  CalculateImageSize(box.GetWidth(), box.GetHeight());
  bool forced = IsForced(*this);
  // A rotated image occupies the bounding box of the rotated rectangle.
  float w = RotatedWidth(), h = RotatedHeight();
  if ((w > box.GetWidth() + kEps || h > box.GetHeight() + kEps) &&
      !IsFixedLayout()) {
    if (forced || area.IsEmptyArea()) {
      // scale down to fit as iText does for forced placement
      float scale = std::min(box.GetWidth() / w, box.GetHeight() / h);
      if (scale < 1 && scale > 0) {
        image_width_ *= scale;
        image_height_ *= scale;
      }
    } else {
      return LayoutResult(LayoutResult::kNothing, area, nullptr,
                          shared_from_this(), shared_from_this());
    }
  }
  w = RotatedWidth();
  h = RotatedHeight();
  float x = box.GetX();
  auto ha = GetProperty<HorizontalAlignment>(property::kHorizontalAlignment);
  if (ha && *ha == HorizontalAlignment::kCenter)
    x += (box.GetWidth() - w) / 2;
  else if (ha && *ha == HorizontalAlignment::kRight)
    x += box.GetWidth() - w;
  Rectangle used(x, box.GetTop() - h, w, h);
  ApplyPaddings(used, true);
  ApplyBorderBox(used, true);
  ApplyMargins(used, true);
  occupied_area_ = LayoutArea(area.GetPageNumber(), used);
  return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
}

void ImageRenderer::Draw(DrawContext& draw_context) {
  DrawBackground(draw_context);
  DrawBorder(draw_context);
  auto* image = dynamic_cast<Image*>(model_element_);
  Rectangle inner = GetInnerAreaBBox();
  PdfCanvas& canvas = draw_context.GetCanvas();
  auto& x_object = *image->GetXObject();
  auto rotation = GetPropertyAsFloat(property::kRotationAngle);
  auto opacity = GetPropertyAsFloat(property::kOpacity);
  canvas.SaveState();
  if (opacity && *opacity < 1) {
    kernel::PdfExtGState gs;
    gs.SetFillOpacity(*opacity);
    canvas.SetExtGState(gs);
  }
  // The image is centred in its (rotated) bounding box and rotated about
  // its centre.
  double cx = inner.GetX() + RotatedWidth() / 2,
         cy = inner.GetY() + RotatedHeight() / 2;
  float left = static_cast<float>(cx - image_width_ / 2),
        bottom = static_cast<float>(cy - image_height_ / 2);
  if (rotation && *rotation != 0) {
    canvas.ConcatMatrix(
        kernel::AffineTransform::GetRotateInstance(*rotation, cx, cy));
  }
  if (x_object.IsImage()) {
    canvas.AddXObjectWithTransformationMatrix(x_object, image_width_, 0, 0,
                                              image_height_, left, bottom);
  } else {
    float sx =
        x_object.GetWidth() != 0 ? image_width_ / x_object.GetWidth() : 1;
    float sy =
        x_object.GetHeight() != 0 ? image_height_ / x_object.GetHeight() : 1;
    canvas.AddXObjectWithTransformationMatrix(x_object, sx, 0, 0, sy, left,
                                              bottom);
  }
  canvas.RestoreState();
}

// ============================================================
// ParagraphRenderer
ParagraphRenderer::ParagraphRenderer(std::shared_ptr<IElement> paragraph)
    : BlockRenderer(std::move(paragraph)) {}

static bool IsBreakableSpace(uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == 0x2002 || cp == 0x2003 ||
         cp == 0x2009 || cp == 0x3000;
}

// CJK ideographs allow line breaks between any two characters.
static bool IsCjk(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

void ParagraphRenderer::BuildPieces() {
  if (pieces_built_) return;
  pieces_built_ = true;
  for (auto& child_ptr : child_renderers_) {
    auto child = std::dynamic_pointer_cast<AbstractRenderer>(child_ptr);
    if (!child) continue;
    if (auto* text_renderer = dynamic_cast<TextRenderer*>(child.get())) {
      auto font = ResolveFont(*text_renderer);
      float font_size = text_renderer->ResolveFontSize();
      float char_spacing =
          text_renderer->GetPropertyAsFloat(property::kCharacterSpacing)
              .value_or(0);
      float word_spacing =
          text_renderer->GetPropertyAsFloat(property::kWordSpacing).value_or(0);
      float h_scale =
          text_renderer->GetPropertyAsFloat(property::kHorizontalScaling)
              .value_or(1);
      bool kerning =
          text_renderer->GetPropertyAsBoolean(property::kFontKerning);
      float asc = text_renderer->GetAscent(),
            desc = text_renderer->GetDescent();
      auto cps = io::TextUtil::Utf8ToCodePoints(text_renderer->GetText());
      size_t i = 0;
      bool first_piece_of_child = true;
      while (i < cps.size()) {
        Piece p{child, io::GlyphLine(), std::string(), 0,     asc,
                desc,  false,           false,         false, true};
        size_t j = i;
        if (cps[i] == '\n' || cps[i] == '\r') {
          if (cps[i] == '\r' && i + 1 < cps.size() && cps[i + 1] == '\n') ++j;
          p.is_newline = true;
          ++j;
        } else if (IsBreakableSpace(cps[i])) {
          p.is_space = true;
          while (j < cps.size() && IsBreakableSpace(cps[j])) ++j;
        } else if (IsCjk(cps[i])) {
          ++j;
        } else {
          while (j < cps.size() && !IsBreakableSpace(cps[j]) &&
                 cps[j] != '\n' && cps[j] != '\r' && !IsCjk(cps[j])) {
            ++j;
            // allow breaking after a hyphen
            if (cps[j - 1] == '-' && j < cps.size() &&
                !IsBreakableSpace(cps[j]))
              break;
          }
        }
        std::vector<uint32_t> sub(cps.begin() + i, cps.begin() + j);
        p.text = io::TextUtil::CodePointsToUtf8(sub);
        if (!p.is_newline) {
          p.glyphs = font->CreateGlyphLine(p.text);
          if (kerning && font->GetFontProgram()->HasKernPairs()) {
            for (int g = 0; g + 1 < p.glyphs.size(); ++g) {
              int k = font->GetFontProgram()->GetKerning(p.glyphs.Get(g),
                                                         p.glyphs.Get(g + 1));
              if (k != 0) p.glyphs.Get(g).SetXAdvance(static_cast<int16_t>(k));
            }
          }
          float w = 0;
          for (int g = 0; g < p.glyphs.size(); ++g) {
            const io::Glyph& gl = p.glyphs.Get(g);
            w += (gl.GetWidth() + gl.GetXAdvance()) * font_size / 1000.f +
                 char_spacing;
            if (gl.HasValidUnicode() && gl.GetUnicode() == ' ')
              w += word_spacing;
          }
          p.width = w * h_scale;
        }
        p.break_allowed_before = !first_piece_of_child || true;
        pieces_.push_back(std::move(p));
        first_piece_of_child = false;
        i = j;
      }
    } else if (auto* image_renderer =
                   dynamic_cast<ImageRenderer*>(child.get())) {
      Piece p{child, io::GlyphLine(), std::string(), 0,    0,
              0,     false,           false,         true, true};
      pieces_.push_back(std::move(p));
      (void)image_renderer;
    } else {
      // nested block elements inside a paragraph are not supported in this
      // port; ignore
    }
  }
  // join adjacent non-space pieces that must not be separated (word made of
  // several Text chunks)
  for (size_t k = 1; k < pieces_.size(); ++k) {
    const Piece& prev = pieces_[k - 1];
    Piece& cur = pieces_[k];
    if (!prev.is_space && !prev.is_newline && !cur.is_space &&
        !cur.is_newline && !prev.is_image && !cur.is_image &&
        prev.source != cur.source) {
      auto last_cp = io::TextUtil::Utf8ToCodePoints(prev.text);
      auto first_cp = io::TextUtil::Utf8ToCodePoints(cur.text);
      bool cjk_boundary = (!last_cp.empty() && IsCjk(last_cp.back())) ||
                          (!first_cp.empty() && IsCjk(first_cp.front()));
      bool after_hyphen = !last_cp.empty() && last_cp.back() == '-';
      if (!cjk_boundary && !after_hyphen) cur.break_allowed_before = false;
    }
  }
}

static Leading ResolveLeading(const AbstractRenderer& r) {
  auto l = r.GetProperty<Leading>(property::kLeading);
  return l ? *l : Leading(Leading::kMultiplied, 1.35f);
}

/// Box an inline image occupies in a line: the image plus its paddings, borders
/// and margins (ImageRenderer::Draw places the image inside GetInnerAreaBBox(),
/// i.e. inside all three).
static Rectangle InlineImageOuterBox(const ImageRenderer& ir, float x,
                                     float y) {
  Rectangle r(x, y, ir.GetImageWidth(), ir.GetImageHeight());
  ir.ApplyPaddings(r, true);
  ir.ApplyBorderBox(r, true);
  ir.ApplyMargins(r, true);
  return Rectangle(x, y, r.GetWidth(), r.GetHeight());
}

void ParagraphRenderer::AdoptChildren() {
  for (auto& child : child_renderers_)
    if (child) child->SetParent(this);
  for (auto& piece : pieces_)
    if (piece.source) piece.source->SetParent(this);
}

LayoutResult ParagraphRenderer::Layout(LayoutContext& layout_context) {
  BuildPieces();
  AdoptChildren();
  LayoutArea& area = layout_context.GetArea();
  Rectangle parent_b_box = area.GetBBox();
  float parent_width = parent_b_box.GetWidth();
  Rectangle box = parent_b_box;
  ApplyMargins(box, false);
  auto borders_arr = GetBorders();
  auto paddings_arr = GetPaddings(parent_width);
  float horizontal_extras = (borders_arr[1] ? borders_arr[1]->GetWidth() : 0) +
                            (borders_arr[3] ? borders_arr[3]->GetWidth() : 0) +
                            paddings_arr[1] + paddings_arr[3];
  if (auto w = RetrieveWidth(parent_width)) {
    float outer = *w + horizontal_extras;
    if (outer < box.GetWidth()) {
      auto ha =
          GetProperty<HorizontalAlignment>(property::kHorizontalAlignment);
      float extra = box.GetWidth() - outer;
      if (ha && *ha == HorizontalAlignment::kCenter)
        box.MoveRight(extra / 2);
      else if (ha && *ha == HorizontalAlignment::kRight)
        box.MoveRight(extra);
    }
    box.SetWidth(outer);
  }
  ApplyBorderBox(box, false);
  ApplyPaddings(box, false);
  content_box_ = box;
  auto fixed_height = RetrieveHeight();
  Leading leading = ResolveLeading(*this);
  float first_line_indent =
      continuation_
          ? 0
          : GetPropertyAsFloat(property::kFirstLineIndent).value_or(0);
  bool forced = IsForced(*this);

  lines_.clear();
  float used_height = 0;
  size_t idx = start_piece_;
  float font_size_for_empty = ResolveFontSize();
  // Empty paragraph still occupies one line height (as in iText when it has no
  // children: zero height).
  while (idx < pieces_.size()) {
    LineInfo line;
    line.left_indent = lines_.empty() ? first_line_indent : 0;
    line.available_width = box.GetWidth() - line.left_indent;
    // skip leading spaces on wrapped lines
    while (idx < pieces_.size() && pieces_[idx].is_space && !lines_.empty())
      ++idx;
    if (idx >= pieces_.size()) break;
    float x = 0;
    size_t line_start = idx;
    size_t last_break = idx;  // index where a break may happen
    float width_at_break = 0;
    bool ended = false;
    while (idx < pieces_.size()) {
      Piece& p = pieces_[idx];
      if (p.is_newline) {
        ++idx;
        ended = true;
        break;
      }
      float piece_width = p.width;
      float piece_ascent = p.ascent, piece_descent = p.descent;
      if (p.is_image) {
        auto* ir = static_cast<ImageRenderer*>(p.source.get());
        ir->CalculateImageSize(line.available_width, box.GetHeight());
        Rectangle outer = InlineImageOuterBox(*ir, 0, 0);
        piece_width = outer.GetWidth();
        piece_ascent = outer.GetHeight();
        piece_descent = 0;
        p.width = piece_width;
        p.ascent = piece_ascent;
        p.descent = 0;
      }
      if (p.break_allowed_before || p.is_space) {
        last_break = idx;
        width_at_break = x;
      }
      if (x + piece_width > line.available_width + kEps && !p.is_space) {
        if (last_break > line_start) {
          // wrap at the last allowed break: items map 1:1 to pieces
          // lineStart..idx-1, so drop those from lastBreak on (by index -
          // positions are ambiguous when leading pieces have zero width)
          idx = last_break;
          x = width_at_break;
          line.items.resize(last_break - line_start);
          break;
        }
        if (idx == line_start && !p.is_image) {
          // a single word wider than the line: split it by glyphs
          float font_size =
              static_cast<AbstractRenderer*>(p.source.get())->ResolveFontSize();
          float char_spacing =
              p.source->GetPropertyAsFloat(property::kCharacterSpacing)
                  .value_or(0);
          float h_scale =
              p.source->GetPropertyAsFloat(property::kHorizontalScaling)
                  .value_or(1);
          float acc = 0;
          int g = 0;
          for (; g < p.glyphs.size(); ++g) {
            float gw =
                ((p.glyphs.Get(g).GetWidth() + p.glyphs.Get(g).GetXAdvance()) *
                     font_size / 1000.f +
                 char_spacing) *
                h_scale;
            if (acc + gw > line.available_width && g > 0) break;
            acc += gw;
          }
          if (g < p.glyphs.size()) {
            std::vector<io::Glyph> head, tail;
            for (int k = 0; k < p.glyphs.size(); ++k)
              (k < g ? head : tail).push_back(p.glyphs.Get(k));
            Piece rest = p;
            rest.glyphs = io::GlyphLine(tail);
            rest.width = p.width - acc;
            rest.text = rest.glyphs.ToString();
            rest.break_allowed_before = true;
            p.glyphs = io::GlyphLine(head);
            p.width = acc;
            p.text = p.glyphs.ToString();
            pieces_.insert(pieces_.begin() + idx + 1, rest);
            piece_width = acc;
          }
        } else if (idx > line_start) {
          break;
        }
      }
      LineItem item;
      item.source = p.source;
      item.glyphs = p.glyphs;
      item.text = p.text;
      item.x = x;
      item.width = piece_width;
      item.ascent = piece_ascent;
      item.descent = piece_descent;
      item.is_image = p.is_image;
      item.is_space = p.is_space;
      line.items.push_back(std::move(item));
      x += piece_width;
      ++idx;
    }
    // trailing spaces do not count for alignment
    float width = 0;
    for (const auto& it : line.items)
      if (!it.is_space) width = std::max(width, it.x + it.width);
    line.width = width;
    float asc = 0, desc = 0;
    bool has_text = false;
    for (const auto& it : line.items) {
      asc = std::max(asc, it.ascent);
      desc = std::min(desc, it.descent);
      if (!it.is_image) has_text = true;
    }
    if (line.items.empty()) {
      // empty line created by a newline: use the paragraph font metrics
      asc = font_size_for_empty * 0.8f;
      desc = -font_size_for_empty * 0.2f;
      has_text = true;
    }
    line.ascent = asc;
    line.descent = desc;
    float text_height = asc - desc;
    if (leading.GetType() == Leading::kFixed) {
      line.height = std::max(leading.GetValue(), text_height);
      line.baseline_offset = (line.height - text_height) / 2 + asc;
    } else {
      float l = has_text ? leading.GetValue() : 1.f;
      line.height = text_height * l;
      line.baseline_offset = asc + text_height * (l - 1) / 2;
    }
    line.last_line = ended || idx >= pieces_.size();
    if (used_height + line.height > box.GetHeight() + kEps &&
        !(forced && lines_.empty())) {
      // does not fit
      if (lines_.empty()) {
        return LayoutResult(LayoutResult::kNothing, area, nullptr,
                            shared_from_this(), shared_from_this());
      }
      bool keep_together = GetPropertyAsBoolean(property::kKeepTogether);
      if (keep_together && !area.IsEmptyArea())
        return LayoutResult(LayoutResult::kNothing, area, nullptr,
                            shared_from_this(), shared_from_this());
      // split
      auto split = std::make_shared<ParagraphRenderer>(model_element_owner_);
      split->model_element_ = model_element_;
      split->properties_ = properties_;
      split->parent_ = parent_;
      split->pieces_ = pieces_;
      split->pieces_built_ = true;
      split->lines_ = lines_;
      split->content_box_ = content_box_;
      split->child_renderers_ = child_renderers_;
      split->SetPropertyAny(property::kMarginBottom,
                            UnitValue::CreatePointValue(0));
      auto overflow = std::make_shared<ParagraphRenderer>(model_element_owner_);
      overflow->model_element_ = model_element_;
      overflow->properties_ = properties_;
      overflow->parent_ = parent_;
      overflow->pieces_ = pieces_;
      overflow->pieces_built_ = true;
      overflow->start_piece_ = line_start;
      overflow->continuation_ = true;
      overflow->child_renderers_ = child_renderers_;
      overflow->SetPropertyAny(property::kMarginTop,
                               UnitValue::CreatePointValue(0));
      if (fixed_height) {
        // as in BlockRenderer: only the rest of a fixed height applies to the
        // next area
        float remaining = *fixed_height - used_height;
        overflow->SetPropertyAny(
            property::kHeight,
            remaining > kEps ? std::any(UnitValue::CreatePointValue(remaining))
                             : std::any());
      }
      Rectangle used(box.GetX(), box.GetTop() - used_height, box.GetWidth(),
                     used_height);
      ApplyPaddings(used, true);
      ApplyBorderBox(used, true);
      auto m = GetMargins(parent_width);
      used.ApplyMargins(m[0], m[1], 0, m[3], true);
      split->occupied_area_ = LayoutArea(area.GetPageNumber(), used);
      split->content_box_.SetY(box.GetTop() - used_height)
          .SetHeight(used_height);
      return LayoutResult(LayoutResult::kPartial, split->occupied_area_, split,
                          overflow);
    }
    lines_.push_back(std::move(line));
    used_height += lines_.back().height;
  }
  float content_height = used_height;
  if (fixed_height) content_height = *fixed_height;
  if (auto mh = RetrieveMinHeight())
    content_height = std::max(content_height, *mh);
  if (content_height > box.GetHeight() + kEps && !forced &&
      !area.IsEmptyArea() && used_height <= box.GetHeight())
    return LayoutResult(LayoutResult::kNothing, area, nullptr,
                        shared_from_this(), shared_from_this());
  content_box_ = Rectangle(box.GetX(), box.GetTop() - content_height,
                           box.GetWidth(), content_height);
  // vertical alignment inside fixed height
  auto va = GetProperty<VerticalAlignment>(property::kVerticalAlignment);
  if (va && *va != VerticalAlignment::kTop && content_height > used_height) {
    float free = content_height - used_height;
    float shift = *va == VerticalAlignment::kMiddle ? free / 2 : free;
    for (auto& l : lines_) l.baseline_offset += shift;
  }
  Rectangle used = content_box_;
  ApplyPaddings(used, true);
  ApplyBorderBox(used, true);
  ApplyMargins(used, true);
  occupied_area_ = LayoutArea(area.GetPageNumber(), used);
  return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
}

std::any ParagraphRenderer::GetDefaultPropertyAny(int property) const {
  if ((property == property::kMarginTop ||
       property == property::kMarginBottom) &&
      parent_ && parent_->GetModelElement() &&
      dynamic_cast<Cell*>(parent_->GetModelElement()))
    return UnitValue::CreatePointValue(0);
  return BlockRenderer::GetDefaultPropertyAny(property);
}

std::optional<float> ParagraphRenderer::GetFirstYLineRecursively() const {
  if (lines_.empty()) return std::nullopt;
  return content_box_.GetTop() - lines_.front().baseline_offset;
}

void ParagraphRenderer::DrawChildren(DrawContext& draw_context) {
  AdoptChildren();
  TextAlignment alignment = GetProperty<TextAlignment>(property::kTextAlignment)
                                .value_or(TextAlignment::kLeft);
  float line_top = content_box_.GetTop();
  for (const auto& line : lines_) {
    float baseline = line_top - line.baseline_offset;
    float offset = 0;
    float extra_per_space = 0;
    float free_space = line.available_width - line.width;
    if (alignment == TextAlignment::kRight)
      offset = free_space;
    else if (alignment == TextAlignment::kCenter)
      offset = free_space / 2;
    else if ((alignment == TextAlignment::kJustified && !line.last_line) ||
             alignment == TextAlignment::kJustifiedAll) {
      int spaces = 0;
      float last_end = 0;
      for (const auto& it : line.items)
        if (!it.is_space) last_end = it.x + it.width;
      for (const auto& it : line.items)
        if (it.is_space && it.x < last_end) ++spaces;
      if (spaces > 0) extra_per_space = free_space / spaces;
    }
    float start_x = content_box_.GetX() + line.left_indent + offset;
    float shift = 0;
    // draw consecutive text items from the same source in one operation
    size_t i = 0;
    while (i < line.items.size()) {
      const LineItem& it = line.items[i];
      if (it.is_space) {
        if (extra_per_space > 0) shift += extra_per_space;
        ++i;
        continue;
      }
      if (it.is_image) {
        auto* ir = static_cast<ImageRenderer*>(it.source.get());
        Rectangle r =
            InlineImageOuterBox(*ir, start_x + it.x + shift, baseline);
        ir->GetOccupiedArea() = LayoutArea(occupied_area_.GetPageNumber(), r);
        ir->Draw(draw_context);
        ++i;
        continue;
      }
      auto* tr = static_cast<TextRenderer*>(it.source.get());
      std::vector<io::Glyph> glyphs;
      float run_x = start_x + it.x + shift;
      float run_width = 0;
      size_t j = i;
      while (j < line.items.size() && line.items[j].source == it.source &&
             !line.items[j].is_image &&
             (extra_per_space == 0 || !line.items[j].is_space)) {
        // trailing spaces at line end are not drawn
        bool trailing = true;
        for (size_t k = j; k < line.items.size(); ++k)
          if (!line.items[k].is_space) trailing = false;
        if (trailing) break;
        for (const auto& g : line.items[j].glyphs.GetGlyphs())
          glyphs.push_back(g);
        run_width = line.items[j].x + line.items[j].width - it.x;
        ++j;
      }
      tr->DrawGlyphs(draw_context, io::GlyphLine(glyphs), run_x, baseline,
                     run_width);
      if (auto* link = dynamic_cast<Link*>(tr->GetModelElement())) {
        kernel::PdfDocument* doc = draw_context.GetDocument();
        int page_number = occupied_area_.GetPageNumber();
        if (doc && doc->GetWriter() && page_number >= 1 &&
            page_number <= doc->GetNumberOfPages()) {
          Rectangle rect(run_x, baseline + tr->GetDescent(), run_width,
                         tr->GetAscent() - tr->GetDescent());
          auto annot = kernel::PdfAnnotation::CreateLink(rect);
          annot->SetAction(kernel::PdfAction::CreateUri(link->GetUri()));
          doc->GetPage(page_number)->AddAnnotation(annot->GetPdfObject());
        }
      }
      i = std::max(j, i + 1);
    }
    line_top -= line.height;
  }
}

// ============================================================
// LineSeparatorRenderer
LayoutResult LineSeparatorRenderer::Layout(LayoutContext& layout_context) {
  LayoutArea& area = layout_context.GetArea();
  Rectangle box = area.GetBBox();
  ApplyMargins(box, false);
  auto drawer =
      GetProperty<std::shared_ptr<ILineDrawer>>(property::kLineDrawer);
  float height = drawer && *drawer ? (*drawer)->GetLineWidth() : 0;
  if (auto w = RetrieveWidth(box.GetWidth())) box.SetWidth(*w);
  if (height > box.GetHeight() && !area.IsEmptyArea())
    return LayoutResult(LayoutResult::kNothing, area, nullptr,
                        shared_from_this(), shared_from_this());
  Rectangle used(box.GetX(), box.GetTop() - height, box.GetWidth(), height);
  ApplyMargins(used, true);
  occupied_area_ = LayoutArea(area.GetPageNumber(), used);
  return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
}

void LineSeparatorRenderer::DrawChildren(DrawContext& draw_context) {
  auto drawer =
      GetProperty<std::shared_ptr<ILineDrawer>>(property::kLineDrawer);
  if (!drawer || !*drawer) return;
  Rectangle area = occupied_area_.GetBBox();
  ApplyMargins(area, false);
  (*drawer)->Draw(draw_context.GetCanvas(), area);
}

// ============================================================ ListRenderer /
// ListItemRenderer
ListRenderer::ListRenderer(std::shared_ptr<IElement> list)
    : BlockRenderer(std::move(list)) {}

static std::string ToRoman(int n) {
  static const std::pair<int, const char*> kTable[] = {
      {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
      {90, "xc"},  {50, "l"},   {40, "xl"}, {10, "x"},   {9, "ix"},
      {5, "v"},    {4, "iv"},   {1, "i"}};
  std::string s;
  for (const auto& p : kTable)
    while (n >= p.first) {
      s += p.second;
      n -= p.first;
    }
  return s;
}

// Port of AlphabetNumbering.toAlphabetNumber: bijective numbering over
// `alphabet` (1 -> a, 26 -> z, 27 -> aa, ...), as UTF-8.
static std::string ToAlphabetNumber(int n,
                                    const std::vector<char32_t>& alphabet) {
  const int cardinality = static_cast<int>(alphabet.size());
  std::vector<char32_t> digits;
  while (n > 0) {
    --n;
    digits.push_back(alphabet[n % cardinality]);
    n /= cardinality;
  }
  std::string s;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it)
    io::TextUtil::AppendUtf8(s, *it);
  return s;
}

static std::string ToLatin(int n) {
  static const std::vector<char32_t> kLatin = [] {
    std::vector<char32_t> a;
    for (char32_t c = 'a'; c <= 'z'; ++c) a.push_back(c);
    return a;
  }();
  return ToAlphabetNumber(n, kLatin);
}

// Port of GreekAlphabetNumbering: the 24 letters, skipping final sigma.
static std::vector<char32_t> GreekAlphabet(char32_t first) {
  std::vector<char32_t> alphabet;
  for (int i = 0; i < 24; ++i) alphabet.push_back(first + i + (i > 16 ? 1 : 0));
  return alphabet;
}

static std::string ToGreek(int n, bool upper_case) {
  static const std::vector<char32_t> kLower = GreekAlphabet(0x3B1),
                                     kUpper = GreekAlphabet(0x391);
  return ToAlphabetNumber(n, upper_case ? kUpper : kLower);
}

std::string ListRenderer::MakeSymbol(
    int index, std::optional<ListNumberingType> type) const {
  if (!type) {
    const std::any* raw = GetPropertyAny(property::kListSymbol);
    if (raw)
      if (auto* t = std::any_cast<ListNumberingType>(raw)) type = *t;
  }
  std::string pre =
      GetProperty<std::string>(property::kListSymbolPreText).value_or("");
  std::string post =
      GetProperty<std::string>(property::kListSymbolPostText).value_or(". ");
  if (!type) return std::string();
  std::string num;
  // Cyclic alphabets: wrap non-positive indices (kListStart <= 0) into range
  // instead of producing code points before the alphabet.
  auto cyclic = [index](int n) { return ((index - 1) % n + n) % n; };
  switch (*type) {
    case ListNumberingType::kDecimal:
      num = std::to_string(index);
      break;
    case ListNumberingType::kDecimalLeadingZero:
      num = (index < 10 ? "0" : "") + std::to_string(index);
      break;
    case ListNumberingType::kRomanLower:
      num = ToRoman(index);
      break;
    case ListNumberingType::kRomanUpper: {
      num = ToRoman(index);
      for (auto& c : num)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      break;
    }
    case ListNumberingType::kEnglishLower:
      num = ToLatin(index);
      break;
    case ListNumberingType::kEnglishUpper: {
      num = ToLatin(index);
      for (auto& c : num)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      break;
    }
    // Greek letters and dingbats are drawn in the Symbol / ZapfDingbats
    // fonts (see SymbolFont()), like Java's ConstantFontTextRenderer.
    case ListNumberingType::kGreekLower:
      num = ToGreek(index > 0 ? index : cyclic(24) + 1, false);
      break;
    case ListNumberingType::kGreekUpper:
      num = ToGreek(index > 0 ? index : cyclic(24) + 1, true);
      break;
    case ListNumberingType::kZapfDingbats1:
      // Font-specific codes 172..181 are the circled digits 1..10; Java
      // continues into the following dingbats (index + 171), up to code 255.
      io::TextUtil::AppendUtf8(
          num, 172 + (index > 0 ? (index - 1) % 84 : cyclic(84)));
      break;
  }
  return pre + num + post;
}

std::shared_ptr<PdfFont> ListRenderer::SymbolFont(ListNumberingType type) {
  bool greek = type == ListNumberingType::kGreekLower ||
               type == ListNumberingType::kGreekUpper;
  if (!greek && type != ListNumberingType::kZapfDingbats1) return nullptr;
  auto& font = greek ? symbol_font_ : zapf_dingbats_font_;
  if (!font) {
    // Symbol uses its built-in encoding so that Unicode Greek (and the ASCII
    // pre/post text) maps to Symbol codes; ZapfDingbats is font-specific.
    font = greek ? kernel::PdfFontFactory::CreateFont(
                       io::standard_fonts::kSymbol, io::PdfEncodings::kSymbol)
                 : kernel::PdfFontFactory::CreateFont(
                       io::standard_fonts::kZapfdingbats);
  }
  return font;
}

LayoutResult ListRenderer::Layout(LayoutContext& layout_context) {
  if (!symbols_assigned_) {
    symbols_assigned_ = true;
    int start = GetPropertyAsInteger(property::kListStart).value_or(1);
    std::vector<std::pair<ListItemRenderer*, std::shared_ptr<TextRenderer>>>
        items;
    float max_width = 0;
    int index = start;
    for (auto& child : child_renderers_) {
      auto* item = dynamic_cast<ListItemRenderer*>(child.get());
      if (!item) continue;
      std::shared_ptr<TextRenderer> symbol;
      // a ListItem may override the symbol
      auto item_symbol = item->GetModelElement()
                             ? item->GetModelElement()->GetOwnPropertyAny(
                                   property::kListSymbol)
                             : nullptr;
      const std::any* list_symbol_any = GetPropertyAny(property::kListSymbol);
      std::optional<std::shared_ptr<Text>> list_text;
      if (list_symbol_any)
        if (auto* t = std::any_cast<std::shared_ptr<Text>>(list_symbol_any))
          list_text = *t;
      if (item_symbol) {
        if (auto* t = std::any_cast<std::shared_ptr<Text>>(item_symbol)) {
          symbol = std::make_shared<TextRenderer>(
              std::static_pointer_cast<IElement>(*t), (*t)->GetText());
        } else if (auto* nt = std::any_cast<ListNumberingType>(item_symbol)) {
          symbol =
              std::make_shared<TextRenderer>(nullptr, MakeSymbol(index, *nt));
          if (auto font = SymbolFont(*nt))
            symbol->SetPropertyAny(property::kFont, font);
        }
      }
      if (!symbol) {
        if (list_text && *list_text) {
          symbol = std::make_shared<TextRenderer>(
              std::static_pointer_cast<IElement>(*list_text),
              (*list_text)->GetText());
        } else {
          symbol = std::make_shared<TextRenderer>(nullptr, MakeSymbol(index));
          const std::any* raw = GetPropertyAny(property::kListSymbol);
          if (auto* t = raw ? std::any_cast<ListNumberingType>(raw) : nullptr)
            if (auto font = SymbolFont(*t))
              symbol->SetPropertyAny(property::kFont, font);
        }
      }
      symbol->SetParent(item);
      auto font = ResolveFont(*symbol);
      float w = font->GetWidth(symbol->GetText(), symbol->ResolveFontSize());
      max_width = std::max(max_width, w);
      items.emplace_back(item, symbol);
      ++index;
    }
    float indent = GetPropertyAsFloat(property::kListSymbolIndent).value_or(0);
    for (auto& p : items) p.first->SetSymbol(p.second, max_width + indent);
  }
  return BlockRenderer::Layout(layout_context);
}

ListItemRenderer::ListItemRenderer(std::shared_ptr<IElement> item)
    : BlockRenderer(std::move(item)) {}

LayoutResult ListItemRenderer::Layout(LayoutContext& layout_context) {
  // reserve the symbol area on the left
  LayoutContext ctx = layout_context;
  Rectangle& bbox = ctx.GetArea().GetBBox();
  bbox.SetX(bbox.GetX() + symbol_area_width_)
      .SetWidth(bbox.GetWidth() - symbol_area_width_);
  LayoutResult result = BlockRenderer::Layout(ctx);
  // Give back the symbol area: the occupied area (used to draw background,
  // borders and the symbol) starts at the left edge of the item. On a split
  // this applies to the split renderer, which is what gets drawn.
  auto widen = [this](LayoutArea& a) {
    a.GetBBox().SetX(a.GetBBox().GetX() - symbol_area_width_);
    a.GetBBox().SetWidth(a.GetBBox().GetWidth() + symbol_area_width_);
  };
  if (result.GetStatus() == LayoutResult::kFull) {
    widen(occupied_area_);
    return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
  }
  if (result.GetStatus() == LayoutResult::kPartial) {
    widen(occupied_area_);
    auto split = result.GetSplitRenderer();
    if (split && &split->GetOccupiedArea() != &occupied_area_)
      widen(split->GetOccupiedArea());
    return LayoutResult(LayoutResult::kPartial, occupied_area_, split,
                        result.GetOverflowRenderer(),
                        result.GetCauseOfNothing());
  }
  return result;
}

std::shared_ptr<BlockRenderer> ListItemRenderer::CreateSplitRenderer() const {
  auto r = std::make_shared<ListItemRenderer>(model_element_owner_);
  r->model_element_ = model_element_;
  r->properties_ = properties_;
  r->parent_ = parent_;
  r->symbol_ = symbol_;
  r->symbol_area_width_ = symbol_area_width_;
  return r;
}

std::shared_ptr<BlockRenderer> ListItemRenderer::CreateOverflowRenderer()
    const {
  auto r = std::static_pointer_cast<ListItemRenderer>(CreateSplitRenderer());
  r->symbol_ = nullptr;  // symbol drawn only with the first part
  return r;
}

void ListItemRenderer::Draw(DrawContext& draw_context) {
  BlockRenderer::Draw(draw_context);
  if (symbol_ && !symbol_drawn_) {
    auto y = GetFirstYLineRecursively();
    Rectangle inner = occupied_area_.GetBBox();
    ApplyMargins(inner, false);
    float baseline = y ? *y : inner.GetTop() - symbol_->GetAscent();
    auto font = ResolveFont(*symbol_);
    io::GlyphLine glyphs = font->CreateGlyphLine(symbol_->GetText());
    symbol_->DrawGlyphs(
        draw_context, glyphs, inner.GetX(), baseline,
        font->GetWidth(symbol_->GetText(), symbol_->ResolveFontSize()));
  }
}

// ============================================================ TableRenderer
TableRenderer::TableRenderer(std::shared_ptr<IElement> table)
    : AbstractRenderer(std::move(table)) {}

std::vector<float> TableRenderer::ComputeColumnWidths(
    float available_width) const {
  const auto& cols = GetTable().GetColumnWidths();
  float table_width = available_width;
  auto w = RetrieveWidth(available_width);
  bool all_points =
      std::all_of(cols.begin(), cols.end(),
                  [](const UnitValue& u) { return u.IsPointValue(); });
  float sum_points = 0, sum_percents = 0;
  for (const auto& c : cols)
    (c.IsPointValue() ? sum_points : sum_percents) += c.GetValue();
  if (w)
    table_width = *w;
  else if (all_points)
    table_width = std::min(sum_points, available_width);
  std::vector<float> widths;
  if (all_points) {
    float scale = sum_points > 0 ? table_width / sum_points : 1;
    for (const auto& c : cols) widths.push_back(c.GetValue() * scale);
  } else {
    float remaining = table_width - sum_points;
    for (const auto& c : cols) {
      if (c.IsPointValue())
        widths.push_back(c.GetValue());
      else
        widths.push_back(
            sum_percents > 0 ? remaining * c.GetValue() / sum_percents : 0);
    }
  }
  return widths;
}

std::optional<float> TableRenderer::GetFirstYLineRecursively() const {
  for (const auto& pc : placed_) {
    auto y = pc.renderer->GetFirstYLineRecursively();
    if (y) return y;
  }
  return std::nullopt;
}

LayoutResult TableRenderer::Layout(LayoutContext& layout_context) {
  LayoutArea& area = layout_context.GetArea();
  Rectangle parent_b_box = area.GetBBox();
  Rectangle box = parent_b_box;
  ApplyMargins(box, false);
  ApplyBorderBox(box, false);
  std::vector<float> col_widths = ComputeColumnWidths(box.GetWidth());
  float table_width = 0;
  for (float w : col_widths) table_width += w;
  auto ha = GetProperty<HorizontalAlignment>(property::kHorizontalAlignment);
  float x0 = box.GetX();
  if (ha && *ha == HorizontalAlignment::kCenter)
    x0 += (box.GetWidth() - table_width) / 2;
  else if (ha && *ha == HorizontalAlignment::kRight)
    x0 += box.GetWidth() - table_width;
  std::vector<float> col_x(col_widths.size() + 1, x0);
  for (size_t i = 0; i < col_widths.size(); ++i)
    col_x[i + 1] = col_x[i] + col_widths[i];
  placed_.clear();
  bool forced = IsForced(*this);

  float cur_top = box.GetTop();
  float bottom_limit = box.GetY();
  // Lays out a block of rows from `t` (header/body/footer) starting at row
  // `from`. Returns rows consumed.
  struct RowLayout {
    std::vector<PlacedCell> cells;
    float height;
  };
  auto layout_cell_at = [&](const CellPtr& cell, float left, float width,
                            float top, float available_height,
                            std::shared_ptr<BlockRenderer> existing)
      -> std::pair<LayoutResult, std::shared_ptr<BlockRenderer>> {
    std::shared_ptr<BlockRenderer> r = existing;
    if (!r) {
      r = std::static_pointer_cast<BlockRenderer>(
          cell->CreateRendererSubTree());
    }
    r->SetParent(this);
    LayoutArea cell_area(
        area.GetPageNumber(),
        Rectangle(left, top - available_height, width, available_height));
    cell_area.SetEmptyArea(true);
    LayoutContext ctx(cell_area);
    return {r->Layout(ctx), r};
  };
  // Measures/lays out a row group of table `t` starting at row r. A row group
  // extends while rowspans continue. Rows of the group are stacked: a cell
  // starting at row rr is placed below the rows r..rr-1 and its box spans the
  // heights of the rows it covers. A cell taller than the rows it spans grows
  // the last of those rows. `content_placed` reports whether any real cell
  // content was laid out (as opposed to nothing fitting at all).
  auto layout_row_group =
      [&](Table& t, int r, float top, float available_height, bool allow_split,
          std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>>&
              pending,
          std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>>&
              next_pending,
          int& rows_in_group, bool& split_happened,
          bool* content_placed = nullptr) -> std::optional<RowLayout> {
    int n_rows = t.GetNumberOfRows();
    int n_cols = t.GetNumberOfColumns();
    int group_end = r + 1;
    for (int rr = r; rr < group_end && rr < n_rows; ++rr)
      for (int c = 0; c < n_cols; ++c) {
        auto cell = t.GetCell(rr, c);
        if (cell)
          group_end =
              std::max(group_end, std::min(n_rows, rr + cell->GetRowspan()));
      }
    rows_in_group = group_end - r;
    struct CellResult {
      CellPtr cell;
      int row;
      int col;
      int span;
      float height;
      std::shared_ptr<BlockRenderer> renderer;
      bool full;
    };
    std::vector<float> row_heights(static_cast<size_t>(group_end - r), 0.f);
    std::vector<CellResult> results;
    split_happened = false;
    bool any_content = false;
    float offset =
        0;  // total height of the rows r..rr-1 (final once row rr starts)
    for (int rr = r; rr < group_end; ++rr) {
      float row_top = top - offset;
      float row_available = allow_split ? available_height - offset : 1e6f;
      for (int c = 0; c < n_cols; ++c) {
        auto cell = t.GetCell(rr, c);
        if (!cell) continue;
        int colspan = std::min(cell->GetColspan(), n_cols - c);
        int span = std::min(cell->GetRowspan(), group_end - rr);
        float left = col_x[c], width = col_x[c + colspan] - col_x[c];
        auto it = pending.find({rr, c});
        std::shared_ptr<BlockRenderer> existing =
            it != pending.end() ? it->second : nullptr;
        // An empty continuation placeholder (see below) carries no content of
        // its own.
        bool placeholder = existing && existing->GetChildRenderers().empty();
        std::pair<LayoutResult, std::shared_ptr<BlockRenderer>> res =
            row_available > 0
                ? layout_cell_at(cell, left, width, row_top, row_available,
                                 existing)
                : std::make_pair(LayoutResult(LayoutResult::kNothing, area,
                                              nullptr, nullptr),
                                 existing
                                     ? existing
                                     : std::static_pointer_cast<BlockRenderer>(
                                           cell->CreateRendererSubTree()));
        const LayoutResult& lr = res.first;
        float h = 0;
        if (lr.GetStatus() == LayoutResult::kFull) {
          h = lr.GetOccupiedArea().GetBBox().GetHeight();
          results.push_back({cell, rr, c, span, h, res.second, true});
          if (!placeholder) any_content = true;
        } else if (lr.GetStatus() == LayoutResult::kPartial) {
          split_happened = true;
          any_content = true;
          h = lr.GetOccupiedArea().GetBBox().GetHeight();
          results.push_back(
              {cell, rr, c, span, h,
               std::static_pointer_cast<BlockRenderer>(lr.GetSplitRenderer()),
               false});
          next_pending[{rr, c}] =
              std::static_pointer_cast<BlockRenderer>(lr.GetOverflowRenderer());
        } else {
          if (!allow_split) return std::nullopt;
          split_happened = true;
          // nothing fits: the whole cell moves to the next part; keep an empty
          // box here
          next_pending[{rr, c}] = res.second;
          results.push_back({cell, rr, c, span, 0.f, nullptr, false});
        }
        if (span == 1) row_heights[rr - r] = std::max(row_heights[rr - r], h);
      }
      // cells spanning rows (rr - span + 1)..rr must fit into those rows: grow
      // row rr if needed
      for (const auto& cr : results) {
        if (cr.span <= 1 || cr.row + cr.span - 1 != rr) continue;
        float spanned = 0;
        for (int k = cr.row; k <= rr; ++k) spanned += row_heights[k - r];
        if (cr.height > spanned) row_heights[rr - r] += cr.height - spanned;
      }
      offset += row_heights[rr - r];
    }
    float group_height = offset;
    if (split_happened && group_height < available_height) {
      row_heights.back() += available_height - group_height;
      group_height = available_height;
    }
    if (auto mh = t.GetProperty<UnitValue>(property::kMinHeight)) {
      if (group_height < mh->GetValue()) {
        row_heights.back() += mh->GetValue() - group_height;
        group_height = mh->GetValue();
      }
    }
    RowLayout row;
    row.height = group_height;
    for (auto& cr : results) {
      int colspan = std::min(cr.cell->GetColspan(), n_cols - cr.col);
      float cell_top = top, cell_height = 0;
      for (int k = r; k < cr.row; ++k) cell_top -= row_heights[k - r];
      for (int k = cr.row; k < cr.row + cr.span; ++k)
        cell_height += row_heights[k - r];
      Rectangle cell_box(col_x[cr.col], cell_top - cell_height,
                         col_x[cr.col + colspan] - col_x[cr.col], cell_height);
      auto& renderer = cr.renderer;
      if (!renderer) {
        // the cell's content moved to the next part entirely; still draw its
        // (empty) box
        renderer =
            std::static_pointer_cast<BlockRenderer>(cr.cell->GetRenderer());
        renderer->SetParent(this);
        renderer->GetOccupiedArea() =
            LayoutArea(area.GetPageNumber(), cell_box);
      }
      // stretch the cell to the row height and apply vertical alignment
      Rectangle occ = renderer->GetOccupiedArea().GetBBox();
      float free = cell_height - occ.GetHeight();
      auto va = renderer->GetProperty<VerticalAlignment>(
          property::kVerticalAlignment);
      if (va && *va != VerticalAlignment::kTop && free > 0) {
        float dy = *va == VerticalAlignment::kMiddle ? free / 2 : free;
        for (auto& ch : renderer->GetChildRenderers()) ch->Move(0, -dy);
      }
      renderer->GetOccupiedArea().SetBBox(cell_box);
      row.cells.push_back({renderer, cell_box});
      // When the group is split, cells that were completed here continue as
      // empty boxes in the next part (otherwise their content would be laid out
      // - and drawn - a second time).
      if (split_happened && cr.full) {
        auto continuation =
            std::static_pointer_cast<BlockRenderer>(cr.cell->GetRenderer());
        next_pending[{cr.row, cr.col}] = continuation;
      }
    }
    if (content_placed) *content_placed = any_content;
    return row;
  };

  Table& t = GetTable();
  auto header = t.GetHeader();
  auto footer = t.GetFooter();
  std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> no_pending,
      dummy;
  // header
  float header_height = 0;
  std::vector<PlacedCell> header_cells;
  bool show_header = header && !(t.IsSkipFirstHeader() && !is_overflow_part_);
  if (show_header) {
    float top = cur_top;
    for (int r = 0; r < header->GetNumberOfRows();) {
      int n = 1;
      bool split = false;
      auto row = layout_row_group(*header, r, top, 1e6f, false, no_pending,
                                  dummy, n, split);
      if (!row) break;
      for (auto& c : row->cells) header_cells.push_back(c);
      top -= row->height;
      r += n;
    }
    header_height = cur_top - top;
  }
  float footer_height = 0;
  std::vector<PlacedCell> footer_cells;
  if (footer) {
    float top = 0;
    for (int r = 0; r < footer->GetNumberOfRows();) {
      int n = 1;
      bool split = false;
      auto row = layout_row_group(*footer, r, top, 1e6f, false, no_pending,
                                  dummy, n, split);
      if (!row) break;
      for (auto& c : row->cells) footer_cells.push_back(c);
      top -= row->height;
      r += n;
    }
    footer_height = -top;
  }
  float body_top = cur_top - header_height;
  (void)body_top;
  int n_rows = t.GetNumberOfRows();
  int r = start_row_;
  std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> pending =
      pending_cell_overflow_;
  std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> next_pending;
  float y = body_top;
  bool any_row_placed = false;
  bool split_at_row = false;
  while (r < n_rows) {
    int n = 1;
    bool split = false;
    next_pending.clear();
    float avail = y - (bottom_limit + footer_height);
    // try without splitting first
    std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>> np;
    auto row = layout_row_group(t, r, y, 1e6f, false, pending, np, n, split);
    bool keep_together = GetPropertyAsBoolean(property::kKeepTogether);
    if (row && row->height <= avail + kEps) {
      placed_.insert(placed_.end(), row->cells.begin(), row->cells.end());
      y -= row->height;
      r += n;
      any_row_placed = true;
      pending.clear();
      continue;
    }
    // the row group does not fit
    if (any_row_placed || (keep_together && !area.IsEmptyArea() && !forced)) {
      if (!any_row_placed)
        return LayoutResult(LayoutResult::kNothing, area, nullptr,
                            shared_from_this(), shared_from_this());
      split_at_row = true;
      break;
    }
    // Split the cell contents of this (first) row group in place, as iText
    // does. If no content fits at all, the table moves to the next area; on an
    // empty area (e.g. header + footer taller than the page) it is laid out
    // again with forced placement, which places the row group unsplit
    // (overflowing) so that no rows are silently dropped.
    bool content_placed = false;
    auto part_row =
        avail > 0 ? layout_row_group(t, r, y, avail, true, pending,
                                     next_pending, n, split, &content_placed)
                  : std::nullopt;
    if (!part_row || part_row->height <= 0 || !content_placed) {
      if (!area.IsEmptyArea() || !forced || !row)
        return LayoutResult(LayoutResult::kNothing, area, nullptr,
                            shared_from_this(), shared_from_this());
      placed_.insert(placed_.end(), row->cells.begin(), row->cells.end());
      y -= row->height;
      r += n;
      any_row_placed = true;
      pending.clear();
      continue;
    }
    placed_.insert(placed_.end(), part_row->cells.begin(),
                   part_row->cells.end());
    y -= part_row->height;
    any_row_placed = true;
    if (split) {
      split_at_row = true;
      pending_cell_overflow_ = next_pending;
      break;
    }
    r += n;
    pending.clear();
  }
  // header & footer positions
  placed_.insert(placed_.begin(), header_cells.begin(),
                 header_cells.end());  // keep the header's column order
  if (!footer_cells.empty() && !(t.IsSkipLastFooter() && !split_at_row)) {
    for (auto& fc : footer_cells) {
      float dy = y;  // footer cells were laid out with top = 0
      fc.renderer->Move(0, dy);
      fc.box.MoveUp(dy);
      fc.renderer->GetOccupiedArea().SetBBox(fc.box);
      placed_.push_back(fc);
    }
    y -= footer_height;
  }
  Rectangle used(x0, y, table_width, cur_top - y);
  ApplyBorderBox(used, true);
  auto m = GetMargins(parent_b_box.GetWidth());
  if (split_at_row) {
    used.ApplyMargins(m[0], m[1], 0, m[3], true);
    occupied_area_ = LayoutArea(area.GetPageNumber(), used);
    auto split = std::make_shared<TableRenderer>(model_element_owner_);
    split->model_element_ = model_element_;
    split->properties_ = properties_;
    split->parent_ = parent_;
    split->placed_ = placed_;
    split->occupied_area_ = occupied_area_;
    split->SetPropertyAny(property::kMarginBottom,
                          UnitValue::CreatePointValue(0));
    auto overflow = std::make_shared<TableRenderer>(model_element_owner_);
    overflow->model_element_ = model_element_;
    overflow->properties_ = properties_;
    overflow->parent_ = parent_;
    overflow->start_row_ = pending_cell_overflow_.empty() ? r : r;
    overflow->pending_cell_overflow_ = pending_cell_overflow_;
    overflow->is_overflow_part_ = true;
    overflow->SetPropertyAny(property::kMarginTop,
                             UnitValue::CreatePointValue(0));
    pending_cell_overflow_.clear();
    return LayoutResult(LayoutResult::kPartial, occupied_area_, split,
                        overflow);
  }
  used.ApplyMargins(m[0], m[1], m[2], m[3], true);
  occupied_area_ = LayoutArea(area.GetPageNumber(), used);
  return LayoutResult(LayoutResult::kFull, occupied_area_, nullptr, nullptr);
}

void TableRenderer::Draw(DrawContext& draw_context) {
  DrawBackground(draw_context);
  // cells: background, content, then borders on top
  for (auto& pc : placed_) {
    pc.renderer->DrawBackground(draw_context);
    pc.renderer->DrawChildren(draw_context);
  }
  PdfCanvas& canvas = draw_context.GetCanvas();
  for (auto& pc : placed_) {
    auto borders = pc.renderer->GetBorders();
    const Rectangle& b = pc.box;
    if (borders[0])
      borders[0]->DrawCellBorder(canvas, b.GetLeft(), b.GetTop(), b.GetRight(),
                                 b.GetTop(), Border::Side::kTop);
    if (borders[1])
      borders[1]->DrawCellBorder(canvas, b.GetRight(), b.GetTop(), b.GetRight(),
                                 b.GetBottom(), Border::Side::kRight);
    if (borders[2])
      borders[2]->DrawCellBorder(canvas, b.GetRight(), b.GetBottom(),
                                 b.GetLeft(), b.GetBottom(),
                                 Border::Side::kBottom);
    if (borders[3])
      borders[3]->DrawCellBorder(canvas, b.GetLeft(), b.GetBottom(),
                                 b.GetLeft(), b.GetTop(), Border::Side::kLeft);
  }
  DrawBorder(draw_context);
}

}  // namespace layout
}  // namespace pdfcpp
