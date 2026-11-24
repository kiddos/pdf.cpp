#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"

#include <cmath>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace kernel {

// ============================================================
// CanvasGraphicsState
void CanvasGraphicsState::UpdateFromExtGState(const PdfDictionary& ext_g_state,
                                              PdfDocument* document) {
  if (auto lw = ext_g_state.GetAsNumber(PdfName::kLW))
    line_width_ = lw->FloatValue();
  if (auto lc = ext_g_state.GetAsNumber(PdfName::kLC))
    line_cap_style_ = lc->IntValue();
  if (auto lj = ext_g_state.GetAsNumber(PdfName::kLJ))
    line_join_style_ = lj->IntValue();
  if (auto ml = ext_g_state.GetAsNumber(PdfName::kML))
    miter_limit_ = ml->FloatValue();
  if (auto d = ext_g_state.GetAsArray(PdfName::kD)) dash_pattern_ = d;
  if (auto ri = ext_g_state.GetAsName(PdfName::kRI)) rendering_intent_ = ri;
  if (auto fl = ext_g_state.GetAsNumber(PdfName::kFL))
    flatness_tolerance_ = fl->FloatValue();
  if (auto ca = ext_g_state.GetAsNumber(PdfName::kCa))
    fill_opacity_ = ca->FloatValue();
  if (auto ca = ext_g_state.GetAsNumber(PdfName::kCA))
    stroke_opacity_ = ca->FloatValue();
  if (auto tk = ext_g_state.GetAsBoolean(PdfName::kTK))
    text_knockout_ = tk->GetValue();
  if (auto font = ext_g_state.GetAsArray(PdfName::kFont)) {
    auto font_dict = font->GetAsDictionary(0);
    auto size = font->GetAsNumber(1);
    if (font_dict && document) font_ = document->GetFont(font_dict);
    if (size) font_size_ = size->FloatValue();
  }
}

// ============================================================ PdfCanvas
static PdfStreamPtr GetPageStream(PdfPage* page) {
  PdfStreamPtr stream = page->GetLastContentStream();
  if (!stream || stream->GetOutputStream() == nullptr ||
      stream->ContainsKey(PdfName::kFilter))
    return page->NewContentStreamAfter();
  return stream;
}

PdfCanvas::PdfCanvas(PdfStreamPtr content_stream, PdfResources* resources,
                     PdfDocument* document)
    : content_stream_(std::move(content_stream)),
      resources_(resources),
      document_(document),
      current_gs_(std::make_shared<CanvasGraphicsState>()) {
  if (content_stream_->GetOutputStream() == nullptr)
    content_stream_->InitOutputStream();
}

PdfCanvas::PdfCanvas(PdfPage* page, bool wrap_old_content)
    : PdfCanvas(GetPageStream(page), page->GetResources(),
                page->GetDocument()) {
  if (wrap_old_content) {
    // Wrap old content in q/Q to protect the new content from state changes (as
    // iText does).
    page->NewContentStreamBefore()->GetOutputStream()->WriteBytes(
        ToBytes("q\n"));
    content_stream_->GetOutputStream()->WriteBytes(ToBytes("Q\n"));
  }
}

PdfCanvas::PdfCanvas(PdfFormXObject& x_obj, PdfDocument* document)
    : PdfCanvas(x_obj.GetPdfObject(), x_obj.GetResources(), document) {}

PdfOutputStream& PdfCanvas::Out() {
  return *content_stream_->GetOutputStream();
}

PdfCanvas& PdfCanvas::SaveState() {
  gs_stack_.push_back(current_gs_);
  current_gs_ = std::make_shared<CanvasGraphicsState>(*current_gs_);
  Out().WriteString("q\n");
  return *this;
}

PdfCanvas& PdfCanvas::RestoreState() {
  if (gs_stack_.empty())
    throw PdfException(kernel_exception_message_constant::
                           kUnbalancedSaveRestoreStateOperators);
  current_gs_ = gs_stack_.back();
  gs_stack_.pop_back();
  Out().WriteString("Q\n");
  return *this;
}

PdfCanvas& PdfCanvas::ConcatMatrix(double a, double b, double c, double d,
                                   double e, double f) {
  current_gs_->UpdateCtm(Matrix(static_cast<float>(a), static_cast<float>(b),
                                static_cast<float>(c), static_cast<float>(d),
                                static_cast<float>(e), static_cast<float>(f)));
  Out()
      .WriteDouble(a)
      .WriteSpace()
      .WriteDouble(b)
      .WriteSpace()
      .WriteDouble(c)
      .WriteSpace()
      .WriteDouble(d)
      .WriteSpace()
      .WriteDouble(e)
      .WriteSpace()
      .WriteDouble(f)
      .WriteString(" cm\n");
  return *this;
}

PdfCanvas& PdfCanvas::ConcatMatrix(const AffineTransform& t) {
  double m[6];
  t.GetMatrix(m);
  return ConcatMatrix(m[0], m[1], m[2], m[3], m[4], m[5]);
}

PdfCanvas& PdfCanvas::BeginText() {
  Out().WriteString("BT\n");
  return *this;
}

PdfCanvas& PdfCanvas::EndText() {
  Out().WriteString("ET\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetFontAndSize(PdfFont* font, float size) {
  current_gs_->SetFontSize(size);
  PdfNamePtr font_name = resources_->AddFont(document_, font);
  current_gs_->SetFont(font);
  Out().Write(*font_name).WriteSpace().WriteFloat(size).WriteString(" Tf\n");
  return *this;
}

PdfCanvas& PdfCanvas::MoveText(double x, double y) {
  Out().WriteDouble(x).WriteSpace().WriteDouble(y).WriteString(" Td\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLeading(float leading) {
  current_gs_->SetLeading(leading);
  Out().WriteFloat(leading).WriteString(" TL\n");
  return *this;
}

PdfCanvas& PdfCanvas::MoveTextWithLeading(float x, float y) {
  current_gs_->SetLeading(-y);
  Out().WriteFloat(x).WriteSpace().WriteFloat(y).WriteString(" TD\n");
  return *this;
}

PdfCanvas& PdfCanvas::NewlineText() {
  Out().WriteString("T*\n");
  return *this;
}

PdfCanvas& PdfCanvas::NewlineShowText(const std::string& text) {
  if (!current_gs_->GetFont())
    throw PdfException("Font and size must be set before writing any text.");
  current_gs_->GetFont()->WriteText(text, Out());
  Out().WriteString("'\n");
  return *this;
}

PdfCanvas& PdfCanvas::NewlineShowText(float word_spacing, float char_spacing,
                                      const std::string& text) {
  if (!current_gs_->GetFont())
    throw PdfException("Font and size must be set before writing any text.");
  Out()
      .WriteFloat(word_spacing)
      .WriteSpace()
      .WriteFloat(char_spacing)
      .WriteSpace();
  current_gs_->GetFont()->WriteText(text, Out());
  Out().WriteString("\"\n");
  current_gs_->SetCharSpacing(char_spacing);
  current_gs_->SetWordSpacing(word_spacing);
  return *this;
}

PdfCanvas& PdfCanvas::SetTextRenderingMode(int text_rendering_mode) {
  current_gs_->SetTextRenderingMode(text_rendering_mode);
  Out().WriteInteger(text_rendering_mode).WriteString(" Tr\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetTextRise(float text_rise) {
  current_gs_->SetTextRise(text_rise);
  Out().WriteFloat(text_rise).WriteString(" Ts\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetWordSpacing(float word_spacing) {
  current_gs_->SetWordSpacing(word_spacing);
  Out().WriteFloat(word_spacing).WriteString(" Tw\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetCharacterSpacing(float char_spacing) {
  current_gs_->SetCharSpacing(char_spacing);
  Out().WriteFloat(char_spacing).WriteString(" Tc\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetHorizontalScaling(float scale) {
  current_gs_->SetHorizontalScaling(scale);
  Out().WriteFloat(scale).WriteString(" Tz\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetTextMatrix(float a, float b, float c, float d, float x,
                                    float y) {
  Out()
      .WriteFloat(a)
      .WriteSpace()
      .WriteFloat(b)
      .WriteSpace()
      .WriteFloat(c)
      .WriteSpace()
      .WriteFloat(d)
      .WriteSpace()
      .WriteFloat(x)
      .WriteSpace()
      .WriteFloat(y)
      .WriteString(" Tm\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetTextMatrix(const AffineTransform& t) {
  float m[6];
  t.GetMatrix(m);
  return SetTextMatrix(m[0], m[1], m[2], m[3], m[4], m[5]);
}

PdfCanvas& PdfCanvas::ShowText(const std::string& text) {
  return ShowText(current_gs_->GetFont()
                      ? current_gs_->GetFont()->CreateGlyphLine(text)
                      : io::GlyphLine());
}

float PdfCanvas::GetSubrangeWidth(const io::GlyphLine& text, int from, int to) {
  float font_size = current_gs_->GetFontSize() / 1000.f;
  float char_spacing = current_gs_->GetCharSpacing();
  float scaling = current_gs_->GetHorizontalScaling() / 100.f;
  float width = 0;
  for (int i = from; i <= to; i++) {
    const io::Glyph& glyph = text.Get(i);
    if (!glyph.HasPlacement()) {
      width += glyph.GetWidth() * font_size + glyph.GetXAdvance() * font_size +
               char_spacing;
      if (glyph.HasValidUnicode() && glyph.GetUnicode() == ' ')
        width += current_gs_->GetWordSpacing();
    }
  }
  return width * scaling;
}

PdfCanvas& PdfCanvas::ShowText(const io::GlyphLine& text) {
  PdfFont* font = current_gs_->GetFont();
  if (!font)
    throw PdfException("Font and size must be set before writing any text.");
  bool has_advances = false;
  for (int i = text.GetStart(); i < text.GetEnd(); ++i)
    if (text.Get(i).HasAdvance()) has_advances = true;
  if (!has_advances) {
    if (text.GetEnd() > text.GetStart()) {
      font->WriteText(text, text.GetStart(), text.GetEnd() - 1, Out());
      Out().WriteString("Tj\n");
    }
    return *this;
  }
  // Use TJ with adjustments for advances (e.g. kerning)
  Out().WriteByte('[');
  int sub = text.GetStart();
  for (int i = text.GetStart(); i < text.GetEnd(); ++i) {
    const io::Glyph& g = text.Get(i);
    if (g.GetXAdvance() != 0) {
      font->WriteText(text, sub, i, Out());
      Out().WriteFloat(static_cast<float>(-g.GetXAdvance()), true);
      sub = i + 1;
    }
  }
  if (sub < text.GetEnd()) font->WriteText(text, sub, text.GetEnd() - 1, Out());
  Out().WriteString("]TJ\n");
  return *this;
}

PdfCanvas& PdfCanvas::ShowText(const PdfArray& text_array) {
  if (!current_gs_->GetFont())
    throw PdfException("Font and size must be set before writing any text.");
  Out().WriteByte('[');
  for (const auto& obj : text_array) {
    if (obj->IsString()) {
      const auto& s = static_cast<PdfString&>(*obj);
      current_gs_->GetFont()->WriteText(s.GetValue(), Out());
    } else if (obj->IsNumber()) {
      Out().WriteFloat(static_cast<PdfNumber&>(*obj).FloatValue());
    }
  }
  Out().WriteString("]TJ\n");
  return *this;
}

PdfCanvas& PdfCanvas::MoveTo(double x, double y) {
  Out().WriteDouble(x).WriteSpace().WriteDouble(y).WriteString(" m\n");
  return *this;
}

PdfCanvas& PdfCanvas::LineTo(double x, double y) {
  Out().WriteDouble(x).WriteSpace().WriteDouble(y).WriteString(" l\n");
  return *this;
}

PdfCanvas& PdfCanvas::CurveTo(double x1, double y1, double x2, double y2,
                              double x3, double y3) {
  Out()
      .WriteDouble(x1)
      .WriteSpace()
      .WriteDouble(y1)
      .WriteSpace()
      .WriteDouble(x2)
      .WriteSpace()
      .WriteDouble(y2)
      .WriteSpace()
      .WriteDouble(x3)
      .WriteSpace()
      .WriteDouble(y3)
      .WriteString(" c\n");
  return *this;
}

PdfCanvas& PdfCanvas::CurveTo(double x2, double y2, double x3, double y3) {
  Out()
      .WriteDouble(x2)
      .WriteSpace()
      .WriteDouble(y2)
      .WriteSpace()
      .WriteDouble(x3)
      .WriteSpace()
      .WriteDouble(y3)
      .WriteString(" v\n");
  return *this;
}

PdfCanvas& PdfCanvas::CurveFromTo(double x1, double y1, double x3, double y3) {
  Out()
      .WriteDouble(x1)
      .WriteSpace()
      .WriteDouble(y1)
      .WriteSpace()
      .WriteDouble(x3)
      .WriteSpace()
      .WriteDouble(y3)
      .WriteString(" y\n");
  return *this;
}

std::vector<std::array<double, 8>> PdfCanvas::BezierArc(double x1, double y1,
                                                        double x2, double y2,
                                                        double start_ang,
                                                        double extent) {
  double tmp;
  if (x1 > x2) {
    tmp = x1;
    x1 = x2;
    x2 = tmp;
  }
  if (y2 > y1) {
    tmp = y1;
    y1 = y2;
    y2 = tmp;
  }
  double frag_angle;
  int n_frag;
  if (std::fabs(extent) <= 90.0) {
    frag_angle = extent;
    n_frag = 1;
  } else {
    n_frag = static_cast<int>(std::ceil(std::fabs(extent) / 90.0));
    frag_angle = extent / n_frag;
  }
  double x_cen = (x1 + x2) / 2., y_cen = (y1 + y2) / 2.;
  double rx = (x2 - x1) / 2., ry = (y2 - y1) / 2.;
  double half_ang = frag_angle * M_PI / 360.;
  // A zero extent is a degenerate arc: all control points coincide (Java
  // divides 0 by 0 here and emits NaN).
  double kappa =
      half_ang == 0
          ? 0.
          : std::fabs(4. / 3. * (1. - std::cos(half_ang)) / std::sin(half_ang));
  std::vector<std::array<double, 8>> point_list;
  for (int i = 0; i < n_frag; ++i) {
    double theta0 = (start_ang + i * frag_angle) * M_PI / 180.;
    double theta1 = (start_ang + (i + 1) * frag_angle) * M_PI / 180.;
    double cos0 = std::cos(theta0), cos1 = std::cos(theta1),
           sin0 = std::sin(theta0), sin1 = std::sin(theta1);
    if (frag_angle > 0) {
      point_list.push_back({x_cen + rx * cos0, y_cen - ry * sin0,
                            x_cen + rx * (cos0 - kappa * sin0),
                            y_cen - ry * (sin0 + kappa * cos0),
                            x_cen + rx * (cos1 + kappa * sin1),
                            y_cen - ry * (sin1 - kappa * cos1),
                            x_cen + rx * cos1, y_cen - ry * sin1});
    } else {
      point_list.push_back({x_cen + rx * cos0, y_cen - ry * sin0,
                            x_cen + rx * (cos0 + kappa * sin0),
                            y_cen - ry * (sin0 - kappa * cos0),
                            x_cen + rx * (cos1 - kappa * sin1),
                            y_cen - ry * (sin1 + kappa * cos1),
                            x_cen + rx * cos1, y_cen - ry * sin1});
    }
  }
  return point_list;
}

PdfCanvas& PdfCanvas::Arc(double x1, double y1, double x2, double y2,
                          double start_ang, double extent) {
  auto ar = BezierArc(x1, y1, x2, y2, start_ang, extent);
  if (ar.empty()) return *this;
  MoveTo(ar[0][0], ar[0][1]);
  for (const auto& pt : ar) CurveTo(pt[2], pt[3], pt[4], pt[5], pt[6], pt[7]);
  return *this;
}

PdfCanvas& PdfCanvas::ArcContinuous(double x1, double y1, double x2, double y2,
                                    double start_ang, double extent) {
  auto ar = BezierArc(x1, y1, x2, y2, start_ang, extent);
  if (ar.empty()) return *this;
  LineTo(ar[0][0], ar[0][1]);
  for (const auto& pt : ar) CurveTo(pt[2], pt[3], pt[4], pt[5], pt[6], pt[7]);
  return *this;
}

PdfCanvas& PdfCanvas::Rectangle(double x, double y, double width,
                                double height) {
  Out()
      .WriteDouble(x)
      .WriteSpace()
      .WriteDouble(y)
      .WriteSpace()
      .WriteDouble(width)
      .WriteSpace()
      .WriteDouble(height)
      .WriteString(" re\n");
  return *this;
}

PdfCanvas& PdfCanvas::RoundRectangle(double x, double y, double width,
                                     double height, double radius) {
  if (width < 0) {
    x += width;
    width = -width;
  }
  if (height < 0) {
    y += height;
    height = -height;
  }
  if (radius < 0) radius = -radius;
  const double curv = 0.4477f;
  MoveTo(x + radius, y);
  LineTo(x + width - radius, y);
  CurveTo(x + width - radius * curv, y, x + width, y + radius * curv, x + width,
          y + radius);
  LineTo(x + width, y + height - radius);
  CurveTo(x + width, y + height - radius * curv, x + width - radius * curv,
          y + height, x + width - radius, y + height);
  LineTo(x + radius, y + height);
  CurveTo(x + radius * curv, y + height, x, y + height - radius * curv, x,
          y + height - radius);
  LineTo(x, y + radius);
  CurveTo(x, y + radius * curv, x + radius * curv, y, x + radius, y);
  return *this;
}

PdfCanvas& PdfCanvas::Circle(double x, double y, double r) {
  const double curve = 0.5523f;
  MoveTo(x + r, y);
  CurveTo(x + r, y + r * curve, x + r * curve, y + r, x, y + r);
  CurveTo(x - r * curve, y + r, x - r, y + r * curve, x - r, y);
  CurveTo(x - r, y - r * curve, x - r * curve, y - r, x, y - r);
  CurveTo(x + r * curve, y - r, x + r, y - r * curve, x + r, y);
  return *this;
}

#define PDFCPP_OP(name, op)      \
  PdfCanvas& PdfCanvas::name() { \
    Out().WriteString(op "\n");  \
    return *this;                \
  }
PDFCPP_OP(ClosePath, "h")
PDFCPP_OP(ClosePathEoFillStroke, "b*")
PDFCPP_OP(ClosePathFillStroke, "b")
PDFCPP_OP(EndPath, "n")
PDFCPP_OP(Stroke, "S")
PDFCPP_OP(Clip, "W")
PDFCPP_OP(EoClip, "W*")
PDFCPP_OP(ClosePathStroke, "s")
PDFCPP_OP(Fill, "f")
PDFCPP_OP(FillStroke, "B")
PDFCPP_OP(EoFill, "f*")
PDFCPP_OP(EoFillStroke, "B*")
#undef PDFCPP_OP

PdfCanvas& PdfCanvas::SetLineWidth(float line_width) {
  if (current_gs_->GetLineWidth() == line_width) return *this;
  current_gs_->SetLineWidth(line_width);
  Out().WriteFloat(line_width).WriteString(" w\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineCapStyle(int line_cap_style) {
  if (current_gs_->GetLineCapStyle() == line_cap_style) return *this;
  current_gs_->SetLineCapStyle(line_cap_style);
  Out().WriteInteger(line_cap_style).WriteString(" J\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineJoinStyle(int line_join_style) {
  if (current_gs_->GetLineJoinStyle() == line_join_style) return *this;
  current_gs_->SetLineJoinStyle(line_join_style);
  Out().WriteInteger(line_join_style).WriteString(" j\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetMiterLimit(float miter_limit) {
  if (current_gs_->GetMiterLimit() == miter_limit) return *this;
  current_gs_->SetMiterLimit(miter_limit);
  Out().WriteFloat(miter_limit).WriteString(" M\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineDash(float phase) {
  Out().WriteString("[] ").WriteFloat(phase).WriteString(" d\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineDash(float units_on, float phase) {
  Out()
      .WriteByte('[')
      .WriteFloat(units_on)
      .WriteString("] ")
      .WriteFloat(phase)
      .WriteString(" d\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineDash(float units_on, float units_off,
                                  float phase) {
  Out()
      .WriteByte('[')
      .WriteFloat(units_on)
      .WriteSpace()
      .WriteFloat(units_off)
      .WriteString("] ")
      .WriteFloat(phase)
      .WriteString(" d\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetLineDash(const std::vector<float>& array,
                                  float phase) {
  Out().WriteByte('[');
  for (size_t i = 0; i < array.size(); ++i) {
    Out().WriteFloat(array[i]);
    if (i + 1 < array.size()) Out().WriteSpace();
  }
  Out().WriteString("] ").WriteFloat(phase).WriteString(" d\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetRenderingIntent(const PdfNamePtr& rendering_intent) {
  current_gs_->SetRenderingIntent(rendering_intent);
  Out().Write(*rendering_intent).WriteString(" ri\n");
  return *this;
}

PdfCanvas& PdfCanvas::SetFlatnessTolerance(float flatness_tolerance) {
  current_gs_->SetFlatnessTolerance(flatness_tolerance);
  Out().WriteFloat(flatness_tolerance).WriteString(" i\n");
  return *this;
}

void PdfCanvas::WriteColorValues(const std::vector<float>& values) {
  for (float v : values) Out().WriteFloat(v).WriteSpace();
}

PdfCanvas& PdfCanvas::SetColor(const ColorPtr& color, bool fill) {
  if (!color) return *this;
  ColorPtr current =
      fill ? current_gs_->GetFillColor() : current_gs_->GetStrokeColor();
  if (current && *current == *color) return *this;
  if (fill)
    current_gs_->SetFillColor(color);
  else
    current_gs_->SetStrokeColor(color);
  auto cs = color->GetColorSpace();
  if (cs && cs->IsDevice()) {
    WriteColorValues(color->GetColorValue());
    int n = cs->GetNumberOfComponents();
    const char* op = n == 1   ? (fill ? "g" : "G")
                     : n == 3 ? (fill ? "rg" : "RG")
                              : (fill ? "k" : "K");
    Out().WriteString(op).WriteNewLine();
  } else if (cs) {
    PdfNamePtr cs_name = resources_->AddColorSpace(cs->GetPdfObject());
    Out().Write(*cs_name).WriteString(fill ? " cs\n" : " CS\n");
    WriteColorValues(color->GetColorValue());
    Out().WriteString(fill ? "scn\n" : "SCN\n");
  }
  return *this;
}

PdfCanvas& PdfCanvas::SetExtGState(PdfExtGState& ext_g_state) {
  if (!ext_g_state.IsFlushed())
    current_gs_->UpdateFromExtGState(*ext_g_state.GetPdfObject(), document_);
  PdfNamePtr name = resources_->AddExtGState(ext_g_state);
  Out().Write(*name).WriteString(" gs\n");
  return *this;
}

PdfCanvas& PdfCanvas::BeginMarkedContent(const PdfNamePtr& tag,
                                         const PdfDictionaryPtr& properties) {
  ++mc_depth_;
  Out().Write(*tag).WriteSpace();
  if (!properties) {
    Out().WriteString("BMC\n");
  } else if (properties->GetIndirectReference() == nullptr) {
    Out().Write(*properties).WriteString(" BDC\n");
  } else {
    Out().Write(*resources_->AddProperties(properties)).WriteString(" BDC\n");
  }
  return *this;
}

PdfCanvas& PdfCanvas::EndMarkedContent() {
  if (--mc_depth_ < 0)
    throw PdfException(kernel_exception_message_constant::
                           kUnbalancedBeginEndMarkedContentOperators);
  Out().WriteString("EMC\n");
  return *this;
}

PdfCanvas& PdfCanvas::AddXObject(PdfXObject& x_object) {
  PdfNamePtr name;
  if (auto* image = dynamic_cast<PdfImageXObject*>(&x_object))
    name = resources_->AddImage(*image);
  else if (auto* form = dynamic_cast<PdfFormXObject*>(&x_object))
    name = resources_->AddForm(*form);
  else
    return *this;
  Out().Write(*name).WriteString(" Do\n");
  return *this;
}

PdfCanvas& PdfCanvas::AddXObjectWithTransformationMatrix(PdfXObject& x_object,
                                                         float a, float b,
                                                         float c, float d,
                                                         float e, float f) {
  SaveState();
  ConcatMatrix(a, b, c, d, e, f);
  AddXObject(x_object);
  return RestoreState();
}

PdfCanvas& PdfCanvas::AddXObjectAt(PdfXObject& x_object, float x, float y) {
  if (x_object.IsImage())
    return AddXObjectWithTransformationMatrix(x_object, x_object.GetWidth(), 0,
                                              0, x_object.GetHeight(), x, y);
  auto* form = dynamic_cast<PdfFormXObject*>(&x_object);
  float bx = 0, by = 0;
  if (form && form->GetBBox()) {
    bx = form->GetBBox()->GetX();
    by = form->GetBBox()->GetY();
  }
  return AddXObjectWithTransformationMatrix(x_object, 1, 0, 0, 1, x - bx,
                                            y - by);
}

PdfCanvas& PdfCanvas::AddXObjectFittedIntoRectangle(
    PdfXObject& x_object, const kernel::Rectangle& rect) {
  if (x_object.IsImage())
    return AddXObjectWithTransformationMatrix(x_object, rect.GetWidth(), 0, 0,
                                              rect.GetHeight(), rect.GetX(),
                                              rect.GetY());
  float w = x_object.GetWidth(), h = x_object.GetHeight();
  float sx = w != 0 ? rect.GetWidth() / w : 1,
        sy = h != 0 ? rect.GetHeight() / h : 1;
  auto* form = dynamic_cast<PdfFormXObject*>(&x_object);
  float bx = 0, by = 0;
  if (form && form->GetBBox()) {
    bx = form->GetBBox()->GetX();
    by = form->GetBBox()->GetY();
  }
  return AddXObjectWithTransformationMatrix(
      x_object, sx, 0, 0, sy, rect.GetX() - bx * sx, rect.GetY() - by * sy);
}

std::shared_ptr<PdfImageXObject> PdfCanvas::AddImageWithTransformationMatrix(
    const std::shared_ptr<io::ImageData>& image, float a, float b, float c,
    float d, float e, float f) {
  auto x_object = std::make_shared<PdfImageXObject>(image);
  AddXObjectWithTransformationMatrix(*x_object, a, b, c, d, e, f);
  return x_object;
}

std::shared_ptr<PdfImageXObject> PdfCanvas::AddImageAt(
    const std::shared_ptr<io::ImageData>& image, float x, float y) {
  return AddImageWithTransformationMatrix(image, image->GetWidth(), 0, 0,
                                          image->GetHeight(), x, y);
}

std::shared_ptr<PdfImageXObject> PdfCanvas::AddImageFittedIntoRectangle(
    const std::shared_ptr<io::ImageData>& image,
    const kernel::Rectangle& rect) {
  return AddImageWithTransformationMatrix(
      image, rect.GetWidth(), 0, 0, rect.GetHeight(), rect.GetX(), rect.GetY());
}

PdfCanvas& PdfCanvas::WriteLiteral(const std::string& s) {
  Out().WriteString(s);
  return *this;
}

PdfCanvas& PdfCanvas::WriteLiteral(char c) {
  Out().WriteByte(c);
  return *this;
}

PdfCanvas& PdfCanvas::WriteLiteral(float n) {
  Out().WriteFloat(n);
  return *this;
}

}  // namespace kernel
}  // namespace pdfcpp
