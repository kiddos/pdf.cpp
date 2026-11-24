#ifndef PDFCPP_KERNEL_COLORS_COLOR_H_
#define PDFCPP_KERNEL_COLORS_COLOR_H_

#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/pdf/pdf_object.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfColorSpace (simplified): the PDF color space object plus its
/// component count.
class PdfColorSpace {
 public:
  PdfColorSpace(PdfObjectPtr pdf_object, int number_of_components)
      : pdf_object_(std::move(pdf_object)),
        number_of_components_(number_of_components) {}
  static std::shared_ptr<PdfColorSpace> MakeColorSpace(
      const PdfObjectPtr& pdf_object);
  const PdfObjectPtr& GetPdfObject() const { return pdf_object_; }
  int GetNumberOfComponents() const { return number_of_components_; }
  /// True for /DeviceGray, /DeviceRGB, /DeviceCMYK.
  bool IsDevice() const;

 private:
  PdfObjectPtr pdf_object_;
  int number_of_components_;
};

/// Port of com.itextpdf.kernel.colors.Color.
class Color {
 public:
  Color(std::shared_ptr<PdfColorSpace> color_space,
        std::vector<float> color_value)
      : color_space_(std::move(color_space)),
        color_value_(std::move(color_value)) {}
  virtual ~Color() = default;
  int GetNumberOfComponents() const {
    return static_cast<int>(color_value_.size());
  }
  const std::shared_ptr<PdfColorSpace>& GetColorSpace() const {
    return color_space_;
  }
  const std::vector<float>& GetColorValue() const { return color_value_; }
  void SetColorValue(std::vector<float> value) {
    color_value_ = std::move(value);
  }
  bool operator==(const Color& o) const;
  bool operator!=(const Color& o) const { return !(*this == o); }
  /// Creates a color from a color space and values (port of Color.makeColor).
  static std::shared_ptr<Color> MakeColor(
      const std::shared_ptr<PdfColorSpace>& color_space,
      const std::vector<float>& color_value = {});
  /// Converts an RGB color to CMYK.
  static std::shared_ptr<Color> ConvertRgbToCmyk(const Color& rgb);
  static std::shared_ptr<Color> ConvertCmykToRgb(const Color& cmyk);

 protected:
  std::shared_ptr<PdfColorSpace> color_space_;
  std::vector<float> color_value_;
};

using ColorPtr = std::shared_ptr<Color>;

class DeviceGray : public Color {
 public:
  explicit DeviceGray(float value = 0.f);
  static const std::shared_ptr<DeviceGray> kWhite, kGray, kBlack;
  static DeviceGray MakeLighter(const DeviceGray& gray_color);
  static DeviceGray MakeDarker(const DeviceGray& gray_color);
};

class DeviceRgb : public Color {
 public:
  DeviceRgb(int r, int g, int b) : DeviceRgb(r / 255.f, g / 255.f, b / 255.f) {}
  DeviceRgb(float r, float g, float b);
  DeviceRgb(double r, double g, double b)
      : DeviceRgb(static_cast<float>(r), static_cast<float>(g),
                  static_cast<float>(b)) {}
  DeviceRgb() : DeviceRgb(0.f, 0.f, 0.f) {}
  static const std::shared_ptr<DeviceRgb> kBlack, kWhite, kRed, kGreen, kBlue;
  static DeviceRgb MakeLighter(const DeviceRgb& rgb_color);
  static DeviceRgb MakeDarker(const DeviceRgb& rgb_color);
};

class DeviceCmyk : public Color {
 public:
  DeviceCmyk(int c, int m, int y, int k)
      : DeviceCmyk(c / 100.f, m / 100.f, y / 100.f, k / 100.f) {}
  DeviceCmyk(float c, float m, float y, float k);
  DeviceCmyk() : DeviceCmyk(0.f, 0.f, 0.f, 1.f) {}
  static const std::shared_ptr<DeviceCmyk> kCyan, kMagenta, kYellow, kBlack;
};

/// Port of ColorConstants.
namespace color_constants {
extern const ColorPtr kBlack, kBlue, kCyan, kDarkGray, kGray, kGreen,
    kLightGray, kMagenta, kOrange, kPink, kRed, kWhite, kYellow;
}

/// Port of WebColors: parses CSS colors (#rrggbb, rgb(), names).
class WebColors {
 public:
  static std::shared_ptr<DeviceRgb> GetRgbColor(const std::string& name);
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_COLORS_COLOR_H_
