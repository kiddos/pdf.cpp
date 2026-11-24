#ifndef PDFCPP_LAYOUT_PROPERTIES_H_
#define PDFCPP_LAYOUT_PROPERTIES_H_

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/layout/property.h"

namespace pdfcpp {
namespace layout {

using kernel::ColorPtr;

/// Port of UnitValue.
class UnitValue {
 public:
  static constexpr int kPoint = 1;
  static constexpr int kPercent = 2;
  UnitValue() = default;
  UnitValue(int unit_type, float value)
      : unit_type_(unit_type), value_(value) {}
  static UnitValue CreatePointValue(float value) {
    return UnitValue(kPoint, value);
  }
  static UnitValue CreatePercentValue(float value) {
    return UnitValue(kPercent, value);
  }
  static std::vector<UnitValue> CreatePercentArray(
      const std::vector<float>& values);
  static std::vector<UnitValue> CreatePointArray(
      const std::vector<float>& values);
  int GetUnitType() const { return unit_type_; }
  float GetValue() const { return value_; }
  void SetValue(float v) { value_ = v; }
  bool IsPointValue() const { return unit_type_ == kPoint; }
  bool IsPercentValue() const { return unit_type_ == kPercent; }
  /// Resolves to points against a base dimension.
  float Resolve(float base) const {
    return IsPercentValue() ? base * value_ / 100.f : value_;
  }
  bool operator==(const UnitValue& o) const {
    return unit_type_ == o.unit_type_ && value_ == o.value_;
  }

 private:
  int unit_type_ = kPoint;
  float value_ = 0;
};

/// Port of Leading.
class Leading {
 public:
  static constexpr int kFixed = 1;
  static constexpr int kMultiplied = 2;
  Leading(int type, float value) : type_(type), value_(value) {}
  int GetType() const { return type_; }
  float GetValue() const { return value_; }

 private:
  int type_;
  float value_;
};

enum class TextAlignment { kLeft, kCenter, kRight, kJustified, kJustifiedAll };
enum class HorizontalAlignment { kLeft, kCenter, kRight };
enum class VerticalAlignment { kTop, kMiddle, kBottom };
enum class AreaBreakType { kNextArea, kNextPage, kLastPage };
enum class ListNumberingType {
  kDecimal,
  kDecimalLeadingZero,
  kRomanLower,
  kRomanUpper,
  kEnglishLower,
  kEnglishUpper,
  kGreekLower,
  kGreekUpper,
  kZapfDingbats1
};
enum class ListSymbolPosition { kDefault, kInside, kOutside };
enum class BorderCollapsePropertyValue { kCollapse, kSeparate };
enum class FontStyle { kNormal, kItalic };

/// Port of Background.
class Background {
 public:
  explicit Background(ColorPtr color, float opacity = 1.f)
      : color_(std::move(color)), opacity_(opacity) {}
  Background(ColorPtr color, float extra_left, float extra_top,
             float extra_right, float extra_bottom)
      : color_(std::move(color)),
        extra_left_(extra_left),
        extra_right_(extra_right),
        extra_top_(extra_top),
        extra_bottom_(extra_bottom) {}
  const ColorPtr& GetColor() const { return color_; }
  float GetOpacity() const { return opacity_; }
  float GetExtraLeft() const { return extra_left_; }
  float GetExtraRight() const { return extra_right_; }
  float GetExtraTop() const { return extra_top_; }
  float GetExtraBottom() const { return extra_bottom_; }

 private:
  ColorPtr color_;
  float opacity_ = 1.f;
  float extra_left_ = 0, extra_right_ = 0, extra_top_ = 0, extra_bottom_ = 0;
};

/// Port of Underline.
class Underline {
 public:
  Underline(ColorPtr color, float thickness, float thickness_mul,
            float y_position, float y_position_mul, int line_cap_style = 0)
      : color_(std::move(color)),
        thickness_(thickness),
        thickness_mul_(thickness_mul),
        y_position_(y_position),
        y_position_mul_(y_position_mul),
        line_cap_style_(line_cap_style) {}
  const ColorPtr& GetColor() const { return color_; }
  float GetThickness(float font_size) const {
    return thickness_ + thickness_mul_ * font_size;
  }
  float GetYPosition(float font_size) const {
    return y_position_ + y_position_mul_ * font_size;
  }
  int GetLineCapStyle() const { return line_cap_style_; }

 private:
  ColorPtr color_;
  float thickness_, thickness_mul_, y_position_, y_position_mul_;
  int line_cap_style_;
};

// ------------------------------------------------------------------ borders
/// Port of com.itextpdf.layout.borders.Border.
class Border {
 public:
  enum class Side { kNone, kTop, kRight, kBottom, kLeft };
  static constexpr int kSolid = 0, kDashed = 1, kDotted = 2, kDouble = 3,
                       kRoundDots = 4, kGroove = 5, kInset = 6, kOutset = 7,
                       kRidge = 8, kDashedFixed = 9;
  static const std::shared_ptr<Border> kNoBorder;

  explicit Border(float width)
      : Border(kernel::color_constants::kBlack, width) {}
  Border(ColorPtr color, float width, float opacity = 1.f)
      : color_(std::move(color)), width_(width), opacity_(opacity) {}
  virtual ~Border() = default;
  virtual int GetType() const = 0;
  const ColorPtr& GetColor() const { return color_; }
  float GetWidth() const { return width_; }
  void SetWidth(float w) { width_ = w; }
  float GetOpacity() const { return opacity_; }
  /// Draws the border segment (x1,y1)->(x2,y2) on the given side of a box; the
  /// inner offsets (borderWidthBefore/After) give the trapezoid corners.
  virtual void Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2,
                    float y2, Side side, float border_width_before,
                    float border_width_after);
  /// Draws the border as a single line centred on (x1,y1)->(x2,y2) (used for
  /// table cells).
  virtual void DrawCellBorder(kernel::PdfCanvas& canvas, float x1, float y1,
                              float x2, float y2, Side side);

 protected:
  virtual void ApplyStyle(kernel::PdfCanvas& canvas, float length) const {}
  ColorPtr color_;
  float width_;
  float opacity_;
};

class SolidBorder : public Border {
 public:
  explicit SolidBorder(float width) : Border(width) {}
  SolidBorder(ColorPtr color, float width, float opacity = 1.f)
      : Border(std::move(color), width, opacity) {}
  int GetType() const override { return kSolid; }
};

class DashedBorder : public Border {
 public:
  explicit DashedBorder(float width) : Border(width) {}
  DashedBorder(ColorPtr color, float width, float opacity = 1.f)
      : Border(std::move(color), width, opacity) {}
  int GetType() const override { return kDashed; }
  void Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2,
            Side side, float before, float after) override;

 protected:
  void ApplyStyle(kernel::PdfCanvas& canvas, float length) const override;
};

class DottedBorder : public Border {
 public:
  explicit DottedBorder(float width) : Border(width) {}
  DottedBorder(ColorPtr color, float width, float opacity = 1.f)
      : Border(std::move(color), width, opacity) {}
  int GetType() const override { return kDotted; }
  void Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2,
            Side side, float before, float after) override;

 protected:
  void ApplyStyle(kernel::PdfCanvas& canvas, float length) const override;
};

class DoubleBorder : public Border {
 public:
  explicit DoubleBorder(float width) : Border(width) {}
  DoubleBorder(ColorPtr color, float width, float opacity = 1.f)
      : Border(std::move(color), width, opacity) {}
  int GetType() const override { return kDouble; }
  void Draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2,
            Side side, float before, float after) override;
};

using BorderPtr = std::shared_ptr<Border>;

// ------------------------------------------------------------------ line
// drawers
/// Port of ILineDrawer (used by LineSeparator).
class ILineDrawer {
 public:
  virtual ~ILineDrawer() = default;
  virtual void Draw(kernel::PdfCanvas& canvas,
                    const kernel::Rectangle& draw_area) = 0;
  virtual float GetLineWidth() const = 0;
  virtual void SetLineWidth(float line_width) = 0;
  virtual ColorPtr GetColor() const = 0;
  virtual void SetColor(ColorPtr color) = 0;
};

class SolidLine : public ILineDrawer {
 public:
  explicit SolidLine(float line_width = 1) : line_width_(line_width) {}
  void Draw(kernel::PdfCanvas& canvas,
            const kernel::Rectangle& draw_area) override;
  float GetLineWidth() const override { return line_width_; }
  void SetLineWidth(float w) override { line_width_ = w; }
  ColorPtr GetColor() const override { return color_; }
  void SetColor(ColorPtr c) override { color_ = std::move(c); }

 protected:
  float line_width_;
  ColorPtr color_ = kernel::color_constants::kBlack;
};

class DashedLine : public SolidLine {
 public:
  explicit DashedLine(float line_width = 1) : SolidLine(line_width) {}
  void Draw(kernel::PdfCanvas& canvas,
            const kernel::Rectangle& draw_area) override;
};

class DottedLine : public SolidLine {
 public:
  explicit DottedLine(float line_width = 1, float gap = 4)
      : SolidLine(line_width), gap_(gap) {}
  void Draw(kernel::PdfCanvas& canvas,
            const kernel::Rectangle& draw_area) override;

 private:
  float gap_;
};

}  // namespace layout
}  // namespace pdfcpp

#endif  // PDFCPP_LAYOUT_PROPERTIES_H_
