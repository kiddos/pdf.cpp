#pragma once

#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/pdf/PdfObject.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfColorSpace (simplified): the PDF color space object plus its component count.
class PdfColorSpace {
public:
    PdfColorSpace(PdfObjectPtr pdfObject, int numberOfComponents)
        : pdfObject_(std::move(pdfObject)), numberOfComponents_(numberOfComponents) {}
    static std::shared_ptr<PdfColorSpace> makeColorSpace(const PdfObjectPtr& pdfObject);
    const PdfObjectPtr& getPdfObject() const { return pdfObject_; }
    int getNumberOfComponents() const { return numberOfComponents_; }
    /// True for /DeviceGray, /DeviceRGB, /DeviceCMYK.
    bool isDevice() const;

private:
    PdfObjectPtr pdfObject_;
    int numberOfComponents_;
};

/// Port of com.itextpdf.kernel.colors.Color.
class Color {
public:
    Color(std::shared_ptr<PdfColorSpace> colorSpace, std::vector<float> colorValue)
        : colorSpace_(std::move(colorSpace)), colorValue_(std::move(colorValue)) {}
    virtual ~Color() = default;
    int getNumberOfComponents() const { return static_cast<int>(colorValue_.size()); }
    const std::shared_ptr<PdfColorSpace>& getColorSpace() const { return colorSpace_; }
    const std::vector<float>& getColorValue() const { return colorValue_; }
    void setColorValue(std::vector<float> value) { colorValue_ = std::move(value); }
    bool operator==(const Color& o) const;
    bool operator!=(const Color& o) const { return !(*this == o); }
    /// Creates a color from a color space and values (port of Color.makeColor).
    static std::shared_ptr<Color> makeColor(const std::shared_ptr<PdfColorSpace>& colorSpace, const std::vector<float>& colorValue = {});
    /// Converts an RGB color to CMYK.
    static std::shared_ptr<Color> convertRgbToCmyk(const Color& rgb);
    static std::shared_ptr<Color> convertCmykToRgb(const Color& cmyk);

protected:
    std::shared_ptr<PdfColorSpace> colorSpace_;
    std::vector<float> colorValue_;
};

using ColorPtr = std::shared_ptr<Color>;

class DeviceGray : public Color {
public:
    explicit DeviceGray(float value = 0.f);
    static const std::shared_ptr<DeviceGray> WHITE, GRAY, BLACK;
    static DeviceGray makeLighter(const DeviceGray& grayColor);
    static DeviceGray makeDarker(const DeviceGray& grayColor);
};

class DeviceRgb : public Color {
public:
    DeviceRgb(int r, int g, int b) : DeviceRgb(r / 255.f, g / 255.f, b / 255.f) {}
    DeviceRgb(float r, float g, float b);
    DeviceRgb(double r, double g, double b) : DeviceRgb(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)) {}
    DeviceRgb() : DeviceRgb(0.f, 0.f, 0.f) {}
    static const std::shared_ptr<DeviceRgb> BLACK, WHITE, RED, GREEN, BLUE;
    static DeviceRgb makeLighter(const DeviceRgb& rgbColor);
    static DeviceRgb makeDarker(const DeviceRgb& rgbColor);
};

class DeviceCmyk : public Color {
public:
    DeviceCmyk(int c, int m, int y, int k) : DeviceCmyk(c / 100.f, m / 100.f, y / 100.f, k / 100.f) {}
    DeviceCmyk(float c, float m, float y, float k);
    DeviceCmyk() : DeviceCmyk(0.f, 0.f, 0.f, 1.f) {}
    static const std::shared_ptr<DeviceCmyk> CYAN, MAGENTA, YELLOW, BLACK;
};

/// Port of ColorConstants.
namespace ColorConstants {
extern const ColorPtr BLACK, BLUE, CYAN, DARK_GRAY, GRAY, GREEN, LIGHT_GRAY, MAGENTA, ORANGE, PINK, RED, WHITE, YELLOW;
}

/// Port of WebColors: parses CSS colors (#rrggbb, rgb(), names).
class WebColors {
public:
    static std::shared_ptr<DeviceRgb> getRGBColor(const std::string& name);
};

}  // namespace kernel
}  // namespace pdfcpp
