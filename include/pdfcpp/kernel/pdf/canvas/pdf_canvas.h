#ifndef PDFCPP_KERNEL_PDF_CANVAS_PDF_CANVAS_H_
#define PDFCPP_KERNEL_PDF_CANVAS_PDF_CANVAS_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include "pdfcpp/io/font/font_program.h"
#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/geom/geom.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace kernel {

class PdfFont;
class PdfXObject;
class PdfImageXObject;
class PdfFormXObject;
class PdfExtGState;

/// Port of PdfCanvasConstants.
namespace pdf_canvas_constants {
namespace text_rendering_mode {
inline constexpr int kFill = 0, kStroke = 1, kFillStroke = 2, kInvisible = 3,
                     kFillClip = 4, kStrokeClip = 5, kFillStrokeClip = 6,
                     kClip = 7;
}
namespace line_cap_style {
inline constexpr int kButt = 0, kRound = 1, kProjectingSquare = 2;
}
namespace line_join_style {
inline constexpr int kMiter = 0, kRound = 1, kBevel = 2;
}
namespace filling_rule {
inline constexpr int kNonzeroWinding = 1, kEvenOdd = 2;
}
}  // namespace pdf_canvas_constants

/// Port of CanvasGraphicsState (writer-side subset).
class CanvasGraphicsState {
 public:
  Matrix GetCtm() const { return ctm_; }
  void UpdateCtm(const Matrix& new_ctm) { ctm_ = new_ctm.Multiply(ctm_); }
  void SetCtm(const Matrix& m) { ctm_ = m; }
  ColorPtr GetFillColor() const { return fill_color_; }
  void SetFillColor(ColorPtr c) { fill_color_ = std::move(c); }
  ColorPtr GetStrokeColor() const { return stroke_color_; }
  void SetStrokeColor(ColorPtr c) { stroke_color_ = std::move(c); }
  float GetLineWidth() const { return line_width_; }
  void SetLineWidth(float v) { line_width_ = v; }
  int GetLineCapStyle() const { return line_cap_style_; }
  void SetLineCapStyle(int v) { line_cap_style_ = v; }
  int GetLineJoinStyle() const { return line_join_style_; }
  void SetLineJoinStyle(int v) { line_join_style_ = v; }
  float GetMiterLimit() const { return miter_limit_; }
  void SetMiterLimit(float v) { miter_limit_ = v; }
  float GetCharSpacing() const { return char_spacing_; }
  void SetCharSpacing(float v) { char_spacing_ = v; }
  float GetWordSpacing() const { return word_spacing_; }
  void SetWordSpacing(float v) { word_spacing_ = v; }
  float GetHorizontalScaling() const { return horizontal_scaling_; }
  void SetHorizontalScaling(float v) { horizontal_scaling_ = v; }
  float GetLeading() const { return leading_; }
  void SetLeading(float v) { leading_ = v; }
  PdfFont* GetFont() const { return font_; }
  void SetFont(PdfFont* f) { font_ = f; }
  float GetFontSize() const { return font_size_; }
  void SetFontSize(float v) { font_size_ = v; }
  int GetTextRenderingMode() const { return text_rendering_mode_; }
  void SetTextRenderingMode(int v) { text_rendering_mode_ = v; }
  float GetTextRise() const { return text_rise_; }
  void SetTextRise(float v) { text_rise_ = v; }
  bool GetTextKnockout() const { return text_knockout_; }
  void SetTextKnockout(bool v) { text_knockout_ = v; }
  float GetFlatnessTolerance() const { return flatness_tolerance_; }
  void SetFlatnessTolerance(float v) { flatness_tolerance_ = v; }
  const PdfArrayPtr& GetDashPattern() const { return dash_pattern_; }
  void SetDashPattern(PdfArrayPtr d) { dash_pattern_ = std::move(d); }
  const PdfNamePtr& GetRenderingIntent() const { return rendering_intent_; }
  void SetRenderingIntent(PdfNamePtr r) { rendering_intent_ = std::move(r); }
  std::optional<float> GetFillOpacity() const { return fill_opacity_; }
  void SetFillOpacity(float v) { fill_opacity_ = v; }
  std::optional<float> GetStrokeOpacity() const { return stroke_opacity_; }
  void SetStrokeOpacity(float v) { stroke_opacity_ = v; }
  /// Applies the entries of an ExtGState dictionary (LW, LC, LJ, ML, D, Font,
  /// ca, CA ...).
  void UpdateFromExtGState(const PdfDictionary& ext_g_state,
                           PdfDocument* document);

 private:
  Matrix ctm_;
  ColorPtr fill_color_ = DeviceGray::kBlack;
  ColorPtr stroke_color_ = DeviceGray::kBlack;
  float line_width_ = 1;
  int line_cap_style_ = 0;
  int line_join_style_ = 0;
  float miter_limit_ = 10;
  float char_spacing_ = 0;
  float word_spacing_ = 0;
  float horizontal_scaling_ = 100;
  float leading_ = 0;
  PdfFont* font_ = nullptr;
  float font_size_ = 0;
  int text_rendering_mode_ = 0;
  float text_rise_ = 0;
  bool text_knockout_ = true;
  float flatness_tolerance_ = 1;
  PdfArrayPtr dash_pattern_;
  PdfNamePtr rendering_intent_;
  std::optional<float> fill_opacity_, stroke_opacity_;
};

/// Port of com.itextpdf.kernel.pdf.canvas.PdfCanvas: writes PDF content stream
/// operators.
class PdfCanvas {
 public:
  PdfCanvas(PdfStreamPtr content_stream, PdfResources* resources,
            PdfDocument* document);
  explicit PdfCanvas(PdfPage* page, bool wrap_old_content = false);
  PdfCanvas(PdfDocument* doc, int page_num)
      : PdfCanvas(doc->GetPage(page_num)) {}
  PdfCanvas(PdfFormXObject& x_obj, PdfDocument* document);
  virtual ~PdfCanvas() = default;

  PdfResources* GetResources() const { return resources_; }
  PdfDocument* GetDocument() const { return document_; }
  const PdfStreamPtr& GetContentStream() const { return content_stream_; }
  CanvasGraphicsState& GetGraphicsState() { return *current_gs_; }
  void AttachContentStream(PdfStreamPtr content_stream) {
    content_stream_ = std::move(content_stream);
  }

  PdfCanvas& SaveState();
  PdfCanvas& RestoreState();
  PdfCanvas& ConcatMatrix(double a, double b, double c, double d, double e,
                          double f);
  PdfCanvas& ConcatMatrix(const AffineTransform& transform);
  PdfCanvas& BeginText();
  PdfCanvas& EndText();
  PdfCanvas& BeginVariableText() { return BeginMarkedContent(PdfName::kTx); }
  PdfCanvas& EndVariableText() { return EndMarkedContent(); }
  PdfCanvas& SetFontAndSize(PdfFont* font, float size);
  PdfCanvas& SetFontAndSize(const std::shared_ptr<PdfFont>& font, float size) {
    return SetFontAndSize(font.get(), size);
  }
  PdfCanvas& MoveText(double x, double y);
  PdfCanvas& SetLeading(float leading);
  PdfCanvas& MoveTextWithLeading(float x, float y);
  PdfCanvas& NewlineText();
  PdfCanvas& NewlineShowText(const std::string& text);
  PdfCanvas& NewlineShowText(float word_spacing, float char_spacing,
                             const std::string& text);
  PdfCanvas& SetTextRenderingMode(int text_rendering_mode);
  PdfCanvas& SetTextRise(float text_rise);
  PdfCanvas& SetWordSpacing(float word_spacing);
  PdfCanvas& SetCharacterSpacing(float char_spacing);
  PdfCanvas& SetHorizontalScaling(float scale);
  PdfCanvas& SetTextMatrix(float a, float b, float c, float d, float x,
                           float y);
  PdfCanvas& SetTextMatrix(const AffineTransform& transform);
  PdfCanvas& SetTextMatrix(float x, float y) {
    return SetTextMatrix(1, 0, 0, 1, x, y);
  }
  /// Shows text (UTF-8) with the current font.
  PdfCanvas& ShowText(const std::string& text);
  /// Shows a glyph line, applying glyph advances (kerning) via TJ.
  PdfCanvas& ShowText(const io::GlyphLine& text);
  PdfCanvas& ShowText(const PdfArray& text_array);

  PdfCanvas& MoveTo(double x, double y);
  PdfCanvas& LineTo(double x, double y);
  PdfCanvas& CurveTo(double x1, double y1, double x2, double y2, double x3,
                     double y3);
  PdfCanvas& CurveTo(double x2, double y2, double x3, double y3);
  PdfCanvas& CurveFromTo(double x1, double y1, double x3, double y3);
  PdfCanvas& Arc(double x1, double y1, double x2, double y2, double start_ang,
                 double extent);
  PdfCanvas& ArcContinuous(double x1, double y1, double x2, double y2,
                           double start_ang, double extent);
  PdfCanvas& Ellipse(double x1, double y1, double x2, double y2) {
    return Arc(x1, y1, x2, y2, 0, 360);
  }
  static std::vector<std::array<double, 8>> BezierArc(double x1, double y1,
                                                      double x2, double y2,
                                                      double start_ang,
                                                      double extent);
  PdfCanvas& Rectangle(double x, double y, double width, double height);
  PdfCanvas& Rectangle(const kernel::Rectangle& rectangle) {
    return this->Rectangle(rectangle.GetX(), rectangle.GetY(),
                           rectangle.GetWidth(), rectangle.GetHeight());
  }
  PdfCanvas& RoundRectangle(double x, double y, double width, double height,
                            double radius);
  PdfCanvas& Circle(double x, double y, double r);
  PdfCanvas& ClosePath();
  PdfCanvas& ClosePathEoFillStroke();
  PdfCanvas& ClosePathFillStroke();
  PdfCanvas& EndPath();
  PdfCanvas& Stroke();
  PdfCanvas& Clip();
  PdfCanvas& EoClip();
  PdfCanvas& ClosePathStroke();
  PdfCanvas& Fill();
  PdfCanvas& FillStroke();
  PdfCanvas& EoFill();
  PdfCanvas& EoFillStroke();

  PdfCanvas& SetLineWidth(float line_width);
  PdfCanvas& SetLineCapStyle(int line_cap_style);
  PdfCanvas& SetLineJoinStyle(int line_join_style);
  PdfCanvas& SetMiterLimit(float miter_limit);
  PdfCanvas& SetLineDash(float phase);
  PdfCanvas& SetLineDash(float units_on, float phase);
  PdfCanvas& SetLineDash(float units_on, float units_off, float phase);
  PdfCanvas& SetLineDash(const std::vector<float>& array, float phase);
  PdfCanvas& SetRenderingIntent(const PdfNamePtr& rendering_intent);
  PdfCanvas& SetFlatnessTolerance(float flatness_tolerance);

  PdfCanvas& SetFillColor(const ColorPtr& color) {
    return SetColor(color, true);
  }
  PdfCanvas& SetStrokeColor(const ColorPtr& color) {
    return SetColor(color, false);
  }
  PdfCanvas& SetColor(const ColorPtr& color, bool fill);
  PdfCanvas& SetFillColorGray(float g) {
    return SetColor(std::make_shared<DeviceGray>(g), true);
  }
  PdfCanvas& SetStrokeColorGray(float g) {
    return SetColor(std::make_shared<DeviceGray>(g), false);
  }
  PdfCanvas& ResetFillColorGray() { return SetFillColorGray(0); }
  PdfCanvas& ResetStrokeColorGray() { return SetStrokeColorGray(0); }
  PdfCanvas& SetFillColorRgb(float r, float g, float b) {
    return SetColor(std::make_shared<DeviceRgb>(r, g, b), true);
  }
  PdfCanvas& SetStrokeColorRgb(float r, float g, float b) {
    return SetColor(std::make_shared<DeviceRgb>(r, g, b), false);
  }
  PdfCanvas& ResetFillColorRgb() { return SetFillColorRgb(0, 0, 0); }
  PdfCanvas& ResetStrokeColorRgb() { return SetStrokeColorRgb(0, 0, 0); }
  PdfCanvas& SetFillColorCmyk(float c, float m, float y, float k) {
    return SetColor(std::make_shared<DeviceCmyk>(c, m, y, k), true);
  }
  PdfCanvas& SetStrokeColorCmyk(float c, float m, float y, float k) {
    return SetColor(std::make_shared<DeviceCmyk>(c, m, y, k), false);
  }

  PdfCanvas& SetExtGState(PdfExtGState& ext_g_state);
  PdfCanvas& BeginMarkedContent(const PdfNamePtr& tag,
                                const PdfDictionaryPtr& properties = nullptr);
  PdfCanvas& EndMarkedContent();

  PdfCanvas& AddXObject(PdfXObject& x_object);
  PdfCanvas& AddXObjectAt(PdfXObject& x_object, float x, float y);
  PdfCanvas& AddXObjectFittedIntoRectangle(PdfXObject& x_object,
                                           const kernel::Rectangle& rect);
  PdfCanvas& AddXObjectWithTransformationMatrix(PdfXObject& x_object, float a,
                                                float b, float c, float d,
                                                float e, float f);
  /// Adds an image; returns the created image XObject.
  std::shared_ptr<PdfImageXObject> AddImageAt(
      const std::shared_ptr<io::ImageData>& image, float x, float y);
  std::shared_ptr<PdfImageXObject> AddImageFittedIntoRectangle(
      const std::shared_ptr<io::ImageData>& image,
      const kernel::Rectangle& rect);
  std::shared_ptr<PdfImageXObject> AddImageWithTransformationMatrix(
      const std::shared_ptr<io::ImageData>& image, float a, float b, float c,
      float d, float e, float f);

  PdfCanvas& WriteLiteral(const std::string& s);
  PdfCanvas& WriteLiteral(char c);
  PdfCanvas& WriteLiteral(float n);
  void Release() {}

 protected:
  PdfOutputStream& Out();
  PdfStreamPtr content_stream_;
  PdfResources* resources_;
  PdfDocument* document_;
  std::shared_ptr<CanvasGraphicsState> current_gs_;
  std::vector<std::shared_ptr<CanvasGraphicsState>> gs_stack_;
  int mc_depth_ = 0;

 private:
  void WriteColorValues(const std::vector<float>& values);
  float GetSubrangeWidth(const io::GlyphLine& text, int from, int to);
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_CANVAS_PDF_CANVAS_H_
