#pragma once

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/Color.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/layout/Property.h"

namespace pdfcpp {
namespace layout {

using kernel::ColorPtr;

/// Port of UnitValue.
class UnitValue {
public:
    static constexpr int POINT = 1;
    static constexpr int PERCENT = 2;
    UnitValue() = default;
    UnitValue(int unitType, float value) : unitType_(unitType), value_(value) {}
    static UnitValue createPointValue(float value) { return UnitValue(POINT, value); }
    static UnitValue createPercentValue(float value) { return UnitValue(PERCENT, value); }
    static std::vector<UnitValue> createPercentArray(const std::vector<float>& values);
    static std::vector<UnitValue> createPointArray(const std::vector<float>& values);
    int getUnitType() const { return unitType_; }
    float getValue() const { return value_; }
    void setValue(float v) { value_ = v; }
    bool isPointValue() const { return unitType_ == POINT; }
    bool isPercentValue() const { return unitType_ == PERCENT; }
    /// Resolves to points against a base dimension.
    float resolve(float base) const { return isPercentValue() ? base * value_ / 100.f : value_; }
    bool operator==(const UnitValue& o) const { return unitType_ == o.unitType_ && value_ == o.value_; }

private:
    int unitType_ = POINT;
    float value_ = 0;
};

/// Port of Leading.
class Leading {
public:
    static constexpr int FIXED = 1;
    static constexpr int MULTIPLIED = 2;
    Leading(int type, float value) : type_(type), value_(value) {}
    int getType() const { return type_; }
    float getValue() const { return value_; }

private:
    int type_;
    float value_;
};

enum class TextAlignment { LEFT, CENTER, RIGHT, JUSTIFIED, JUSTIFIED_ALL };
enum class HorizontalAlignment { LEFT, CENTER, RIGHT };
enum class VerticalAlignment { TOP, MIDDLE, BOTTOM };
enum class AreaBreakType { NEXT_AREA, NEXT_PAGE, LAST_PAGE };
enum class ListNumberingType {
    DECIMAL, DECIMAL_LEADING_ZERO, ROMAN_LOWER, ROMAN_UPPER, ENGLISH_LOWER, ENGLISH_UPPER, GREEK_LOWER, GREEK_UPPER, ZAPF_DINGBATS_1
};
enum class ListSymbolPosition { DEFAULT, INSIDE, OUTSIDE };
enum class BorderCollapsePropertyValue { COLLAPSE, SEPARATE };
enum class FontStyle { NORMAL, ITALIC };

/// Port of Background.
class Background {
public:
    explicit Background(ColorPtr color, float opacity = 1.f) : color_(std::move(color)), opacity_(opacity) {}
    Background(ColorPtr color, float extraLeft, float extraTop, float extraRight, float extraBottom)
        : color_(std::move(color)), extraLeft_(extraLeft), extraRight_(extraRight), extraTop_(extraTop), extraBottom_(extraBottom) {}
    const ColorPtr& getColor() const { return color_; }
    float getOpacity() const { return opacity_; }
    float getExtraLeft() const { return extraLeft_; }
    float getExtraRight() const { return extraRight_; }
    float getExtraTop() const { return extraTop_; }
    float getExtraBottom() const { return extraBottom_; }

private:
    ColorPtr color_;
    float opacity_ = 1.f;
    float extraLeft_ = 0, extraRight_ = 0, extraTop_ = 0, extraBottom_ = 0;
};

/// Port of Underline.
class Underline {
public:
    Underline(ColorPtr color, float thickness, float thicknessMul, float yPosition, float yPositionMul, int lineCapStyle = 0)
        : color_(std::move(color)), thickness_(thickness), thicknessMul_(thicknessMul), yPosition_(yPosition),
          yPositionMul_(yPositionMul), lineCapStyle_(lineCapStyle) {}
    const ColorPtr& getColor() const { return color_; }
    float getThickness(float fontSize) const { return thickness_ + thicknessMul_ * fontSize; }
    float getYPosition(float fontSize) const { return yPosition_ + yPositionMul_ * fontSize; }
    int getLineCapStyle() const { return lineCapStyle_; }

private:
    ColorPtr color_;
    float thickness_, thicknessMul_, yPosition_, yPositionMul_;
    int lineCapStyle_;
};

// ------------------------------------------------------------------ borders
/// Port of com.itextpdf.layout.borders.Border.
class Border {
public:
    enum class Side { NONE, TOP, RIGHT, BOTTOM, LEFT };
    static constexpr int SOLID = 0, DASHED = 1, DOTTED = 2, DOUBLE = 3, ROUND_DOTS = 4, GROOVE = 5, INSET = 6, OUTSET = 7, RIDGE = 8, DASHED_FIXED = 9;
    static const std::shared_ptr<Border> NO_BORDER;

    explicit Border(float width) : Border(kernel::ColorConstants::BLACK, width) {}
    Border(ColorPtr color, float width, float opacity = 1.f) : color_(std::move(color)), width_(width), opacity_(opacity) {}
    virtual ~Border() = default;
    virtual int getType() const = 0;
    const ColorPtr& getColor() const { return color_; }
    float getWidth() const { return width_; }
    void setWidth(float w) { width_ = w; }
    float getOpacity() const { return opacity_; }
    /// Draws the border segment (x1,y1)->(x2,y2) on the given side of a box; the
    /// inner offsets (borderWidthBefore/After) give the trapezoid corners.
    virtual void draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float borderWidthBefore,
                      float borderWidthAfter);
    /// Draws the border as a single line centred on (x1,y1)->(x2,y2) (used for table cells).
    virtual void drawCellBorder(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side);

protected:
    virtual void applyStyle(kernel::PdfCanvas& canvas, float length) const {}
    ColorPtr color_;
    float width_;
    float opacity_;
};

class SolidBorder : public Border {
public:
    explicit SolidBorder(float width) : Border(width) {}
    SolidBorder(ColorPtr color, float width, float opacity = 1.f) : Border(std::move(color), width, opacity) {}
    int getType() const override { return SOLID; }
};

class DashedBorder : public Border {
public:
    explicit DashedBorder(float width) : Border(width) {}
    DashedBorder(ColorPtr color, float width, float opacity = 1.f) : Border(std::move(color), width, opacity) {}
    int getType() const override { return DASHED; }
    void draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float before, float after) override;

protected:
    void applyStyle(kernel::PdfCanvas& canvas, float length) const override;
};

class DottedBorder : public Border {
public:
    explicit DottedBorder(float width) : Border(width) {}
    DottedBorder(ColorPtr color, float width, float opacity = 1.f) : Border(std::move(color), width, opacity) {}
    int getType() const override { return DOTTED; }
    void draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float before, float after) override;

protected:
    void applyStyle(kernel::PdfCanvas& canvas, float length) const override;
};

class DoubleBorder : public Border {
public:
    explicit DoubleBorder(float width) : Border(width) {}
    DoubleBorder(ColorPtr color, float width, float opacity = 1.f) : Border(std::move(color), width, opacity) {}
    int getType() const override { return DOUBLE; }
    void draw(kernel::PdfCanvas& canvas, float x1, float y1, float x2, float y2, Side side, float before, float after) override;
};

using BorderPtr = std::shared_ptr<Border>;

// ------------------------------------------------------------------ line drawers
/// Port of ILineDrawer (used by LineSeparator).
class ILineDrawer {
public:
    virtual ~ILineDrawer() = default;
    virtual void draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) = 0;
    virtual float getLineWidth() const = 0;
    virtual void setLineWidth(float lineWidth) = 0;
    virtual ColorPtr getColor() const = 0;
    virtual void setColor(ColorPtr color) = 0;
};

class SolidLine : public ILineDrawer {
public:
    explicit SolidLine(float lineWidth = 1) : lineWidth_(lineWidth) {}
    void draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) override;
    float getLineWidth() const override { return lineWidth_; }
    void setLineWidth(float w) override { lineWidth_ = w; }
    ColorPtr getColor() const override { return color_; }
    void setColor(ColorPtr c) override { color_ = std::move(c); }

protected:
    float lineWidth_;
    ColorPtr color_ = kernel::ColorConstants::BLACK;
};

class DashedLine : public SolidLine {
public:
    explicit DashedLine(float lineWidth = 1) : SolidLine(lineWidth) {}
    void draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) override;
};

class DottedLine : public SolidLine {
public:
    explicit DottedLine(float lineWidth = 1, float gap = 4) : SolidLine(lineWidth), gap_(gap) {}
    void draw(kernel::PdfCanvas& canvas, const kernel::Rectangle& drawArea) override;

private:
    float gap_;
};

}  // namespace layout
}  // namespace pdfcpp
