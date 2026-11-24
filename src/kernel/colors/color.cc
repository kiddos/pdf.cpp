#include "pdfcpp/kernel/colors/color.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace pdfcpp {
namespace kernel {

static std::shared_ptr<PdfColorSpace> DeviceSpace(int n) {
  // Created locally (not from PdfName statics) to be safe during static
  // initialization.
  static auto gray = std::make_shared<PdfColorSpace>(
      PdfName::CreateDirectName("DeviceGray"), 1);
  static auto rgb = std::make_shared<PdfColorSpace>(
      PdfName::CreateDirectName("DeviceRGB"), 3);
  static auto cmyk = std::make_shared<PdfColorSpace>(
      PdfName::CreateDirectName("DeviceCMYK"), 4);
  return n == 1 ? gray : (n == 3 ? rgb : cmyk);
}

bool PdfColorSpace::IsDevice() const {
  return NameEquals(pdf_object_, PdfName::kDeviceGray) ||
         NameEquals(pdf_object_, PdfName::kDeviceRGB) ||
         NameEquals(pdf_object_, PdfName::kDeviceCMYK);
}

std::shared_ptr<PdfColorSpace> PdfColorSpace::MakeColorSpace(
    const PdfObjectPtr& pdf_object) {
  if (!pdf_object) return nullptr;
  if (pdf_object->IsName()) {
    const std::string& n = static_cast<PdfName&>(*pdf_object).GetValue();
    if (n == "DeviceGray" || n == "G" || n == "CalGray") return DeviceSpace(1);
    if (n == "DeviceRGB" || n == "RGB" || n == "CalRGB") return DeviceSpace(3);
    if (n == "DeviceCMYK" || n == "CMYK") return DeviceSpace(4);
    if (n == "Pattern") return std::make_shared<PdfColorSpace>(pdf_object, 0);
    return std::make_shared<PdfColorSpace>(pdf_object, 1);
  }
  if (pdf_object->IsArray()) {
    auto arr = std::static_pointer_cast<PdfArray>(pdf_object);
    auto family = arr->GetAsName(0);
    if (!family) return nullptr;
    const std::string& f = family->GetValue();
    if (f == "CalGray") return std::make_shared<PdfColorSpace>(pdf_object, 1);
    if (f == "CalRGB" || f == "Lab")
      return std::make_shared<PdfColorSpace>(pdf_object, 3);
    if (f == "ICCBased") {
      auto s = arr->GetAsStream(1);
      auto n = s ? s->GetAsNumber(PdfName::kN) : nullptr;
      return std::make_shared<PdfColorSpace>(pdf_object, n ? n->IntValue() : 3);
    }
    if (f == "Indexed" || f == "I" || f == "Separation")
      return std::make_shared<PdfColorSpace>(pdf_object, 1);
    if (f == "DeviceN") {
      auto names = arr->GetAsArray(1);
      return std::make_shared<PdfColorSpace>(pdf_object,
                                             names ? names->size() : 1);
    }
    if (f == "Pattern") return std::make_shared<PdfColorSpace>(pdf_object, 0);
  }
  return nullptr;
}

bool Color::operator==(const Color& o) const {
  if (color_value_ != o.color_value_) return false;
  if (color_space_ == o.color_space_) return true;
  if (!color_space_ || !o.color_space_) return false;
  return PdfObject::EqualContent(color_space_->GetPdfObject().get(),
                                 o.color_space_->GetPdfObject().get());
}

std::shared_ptr<Color> Color::MakeColor(
    const std::shared_ptr<PdfColorSpace>& color_space,
    const std::vector<float>& color_value) {
  if (color_space && color_space->IsDevice()) {
    int n = color_space->GetNumberOfComponents();
    std::vector<float> v = color_value;
    if (v.empty()) v.assign(n, 0.f);
    if (n == 1) return std::make_shared<DeviceGray>(v[0]);
    if (n == 3 && v.size() >= 3)
      return std::make_shared<DeviceRgb>(v[0], v[1], v[2]);
    if (n == 4 && v.size() >= 4)
      return std::make_shared<DeviceCmyk>(v[0], v[1], v[2], v[3]);
  }
  std::vector<float> v = color_value;
  if (v.empty() && color_space)
    v.assign(color_space->GetNumberOfComponents(), 0.f);
  return std::make_shared<Color>(color_space, v);
}

std::shared_ptr<Color> Color::ConvertRgbToCmyk(const Color& rgb) {
  float r = rgb.GetColorValue()[0], g = rgb.GetColorValue()[1],
        b = rgb.GetColorValue()[2];
  float k = 1 - std::max(r, std::max(g, b));
  float c = 0, m = 0, y = 0;
  if (k < 1) {
    c = (1 - r - k) / (1 - k);
    m = (1 - g - k) / (1 - k);
    y = (1 - b - k) / (1 - k);
  }
  return std::make_shared<DeviceCmyk>(c, m, y, k);
}

std::shared_ptr<Color> Color::ConvertCmykToRgb(const Color& cmyk) {
  const auto& v = cmyk.GetColorValue();
  float r = 1 - std::min(1.f, v[0] + v[3]);
  float g = 1 - std::min(1.f, v[1] + v[3]);
  float b = 1 - std::min(1.f, v[2] + v[3]);
  return std::make_shared<DeviceRgb>(r, g, b);
}

static float Clamp01(float v) { return v > 1 ? 1 : (v < 0 ? 0 : v); }

DeviceGray::DeviceGray(float value) : Color(DeviceSpace(1), {Clamp01(value)}) {}
const std::shared_ptr<DeviceGray> DeviceGray::kWhite =
    std::make_shared<DeviceGray>(1.f);
const std::shared_ptr<DeviceGray> DeviceGray::kGray =
    std::make_shared<DeviceGray>(0.5f);
const std::shared_ptr<DeviceGray> DeviceGray::kBlack =
    std::make_shared<DeviceGray>(0.f);
DeviceGray DeviceGray::MakeLighter(const DeviceGray& c) {
  float v = c.GetColorValue()[0];
  return DeviceGray(v == 0 ? 0.3f : std::min(1.f, v * 1.3f));
}
DeviceGray DeviceGray::MakeDarker(const DeviceGray& c) {
  return DeviceGray(std::max(0.f, c.GetColorValue()[0] * 0.7f));
}

DeviceRgb::DeviceRgb(float r, float g, float b)
    : Color(DeviceSpace(3), {Clamp01(r), Clamp01(g), Clamp01(b)}) {}
const std::shared_ptr<DeviceRgb> DeviceRgb::kBlack =
    std::make_shared<DeviceRgb>(0, 0, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::kWhite =
    std::make_shared<DeviceRgb>(255, 255, 255);
const std::shared_ptr<DeviceRgb> DeviceRgb::kRed =
    std::make_shared<DeviceRgb>(255, 0, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::kGreen =
    std::make_shared<DeviceRgb>(0, 255, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::kBlue =
    std::make_shared<DeviceRgb>(0, 0, 255);
DeviceRgb DeviceRgb::MakeLighter(const DeviceRgb& c) {
  const auto& v = c.GetColorValue();
  float m = std::max(v[0], std::max(v[1], v[2]));
  if (m == 0) return DeviceRgb(0.3f, 0.3f, 0.3f);
  float mul = std::min(1.f, m + 0.33f) / m;
  return DeviceRgb(v[0] * mul, v[1] * mul, v[2] * mul);
}
DeviceRgb DeviceRgb::MakeDarker(const DeviceRgb& c) {
  const auto& v = c.GetColorValue();
  float m = std::max(v[0], std::max(v[1], v[2]));
  float mul = m == 0 ? 0 : std::max(0.f, (m - 0.33f) / m);
  return DeviceRgb(v[0] * mul, v[1] * mul, v[2] * mul);
}

DeviceCmyk::DeviceCmyk(float c, float m, float y, float k)
    : Color(DeviceSpace(4), {Clamp01(c), Clamp01(m), Clamp01(y), Clamp01(k)}) {}
const std::shared_ptr<DeviceCmyk> DeviceCmyk::kCyan =
    std::make_shared<DeviceCmyk>(100, 0, 0, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::kMagenta =
    std::make_shared<DeviceCmyk>(0, 100, 0, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::kYellow =
    std::make_shared<DeviceCmyk>(0, 0, 100, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::kBlack =
    std::make_shared<DeviceCmyk>(0, 0, 0, 100);

namespace color_constants {
const ColorPtr kBlack = std::make_shared<DeviceRgb>(0, 0, 0);
const ColorPtr kBlue = std::make_shared<DeviceRgb>(0, 0, 255);
const ColorPtr kCyan = std::make_shared<DeviceRgb>(0, 255, 255);
const ColorPtr kDarkGray = std::make_shared<DeviceRgb>(64, 64, 64);
const ColorPtr kGray = std::make_shared<DeviceRgb>(128, 128, 128);
const ColorPtr kGreen = std::make_shared<DeviceRgb>(0, 255, 0);
const ColorPtr kLightGray = std::make_shared<DeviceRgb>(192, 192, 192);
const ColorPtr kMagenta = std::make_shared<DeviceRgb>(255, 0, 255);
const ColorPtr kOrange = std::make_shared<DeviceRgb>(255, 200, 0);
const ColorPtr kPink = std::make_shared<DeviceRgb>(255, 175, 175);
const ColorPtr kRed = std::make_shared<DeviceRgb>(255, 0, 0);
const ColorPtr kWhite = std::make_shared<DeviceRgb>(255, 255, 255);
const ColorPtr kYellow = std::make_shared<DeviceRgb>(255, 255, 0);
}  // namespace color_constants

std::shared_ptr<DeviceRgb> WebColors::GetRgbColor(const std::string& name_in) {
  static const std::map<std::string, int> kNames = {
      {"aliceblue", 0xf0f8ff},
      {"antiquewhite", 0xfaebd7},
      {"aqua", 0x00ffff},
      {"aquamarine", 0x7fffd4},
      {"azure", 0xf0ffff},
      {"beige", 0xf5f5dc},
      {"bisque", 0xffe4c4},
      {"black", 0x000000},
      {"blanchedalmond", 0xffebcd},
      {"blue", 0x0000ff},
      {"blueviolet", 0x8a2be2},
      {"brown", 0xa52a2a},
      {"burlywood", 0xdeb887},
      {"cadetblue", 0x5f9ea0},
      {"chartreuse", 0x7fff00},
      {"chocolate", 0xd2691e},
      {"coral", 0xff7f50},
      {"cornflowerblue", 0x6495ed},
      {"cornsilk", 0xfff8dc},
      {"crimson", 0xdc143c},
      {"cyan", 0x00ffff},
      {"darkblue", 0x00008b},
      {"darkcyan", 0x008b8b},
      {"darkgoldenrod", 0xb8860b},
      {"darkgray", 0xa9a9a9},
      {"darkgreen", 0x006400},
      {"darkgrey", 0xa9a9a9},
      {"darkkhaki", 0xbdb76b},
      {"darkmagenta", 0x8b008b},
      {"darkolivegreen", 0x556b2f},
      {"darkorange", 0xff8c00},
      {"darkorchid", 0x9932cc},
      {"darkred", 0x8b0000},
      {"darksalmon", 0xe9967a},
      {"darkseagreen", 0x8fbc8f},
      {"darkslateblue", 0x483d8b},
      {"darkslategray", 0x2f4f4f},
      {"darkturquoise", 0x00ced1},
      {"darkviolet", 0x9400d3},
      {"deeppink", 0xff1493},
      {"deepskyblue", 0x00bfff},
      {"dimgray", 0x696969},
      {"dodgerblue", 0x1e90ff},
      {"firebrick", 0xb22222},
      {"floralwhite", 0xfffaf0},
      {"forestgreen", 0x228b22},
      {"fuchsia", 0xff00ff},
      {"gainsboro", 0xdcdcdc},
      {"ghostwhite", 0xf8f8ff},
      {"gold", 0xffd700},
      {"goldenrod", 0xdaa520},
      {"gray", 0x808080},
      {"grey", 0x808080},
      {"green", 0x008000},
      {"greenyellow", 0xadff2f},
      {"honeydew", 0xf0fff0},
      {"hotpink", 0xff69b4},
      {"indianred", 0xcd5c5c},
      {"indigo", 0x4b0082},
      {"ivory", 0xfffff0},
      {"khaki", 0xf0e68c},
      {"lavender", 0xe6e6fa},
      {"lavenderblush", 0xfff0f5},
      {"lawngreen", 0x7cfc00},
      {"lemonchiffon", 0xfffacd},
      {"lightblue", 0xadd8e6},
      {"lightcoral", 0xf08080},
      {"lightcyan", 0xe0ffff},
      {"lightgoldenrodyellow", 0xfafad2},
      {"lightgray", 0xd3d3d3},
      {"lightgreen", 0x90ee90},
      {"lightgrey", 0xd3d3d3},
      {"lightpink", 0xffb6c1},
      {"lightsalmon", 0xffa07a},
      {"lightseagreen", 0x20b2aa},
      {"lightskyblue", 0x87cefa},
      {"lightslategray", 0x778899},
      {"lightsteelblue", 0xb0c4de},
      {"lightyellow", 0xffffe0},
      {"lime", 0x00ff00},
      {"limegreen", 0x32cd32},
      {"linen", 0xfaf0e6},
      {"magenta", 0xff00ff},
      {"maroon", 0x800000},
      {"mediumaquamarine", 0x66cdaa},
      {"mediumblue", 0x0000cd},
      {"mediumorchid", 0xba55d3},
      {"mediumpurple", 0x9370db},
      {"mediumseagreen", 0x3cb371},
      {"mediumslateblue", 0x7b68ee},
      {"mediumspringgreen", 0x00fa9a},
      {"mediumturquoise", 0x48d1cc},
      {"mediumvioletred", 0xc71585},
      {"midnightblue", 0x191970},
      {"mintcream", 0xf5fffa},
      {"mistyrose", 0xffe4e1},
      {"moccasin", 0xffe4b5},
      {"navajowhite", 0xffdead},
      {"navy", 0x000080},
      {"oldlace", 0xfdf5e6},
      {"olive", 0x808000},
      {"olivedrab", 0x6b8e23},
      {"orange", 0xffa500},
      {"orangered", 0xff4500},
      {"orchid", 0xda70d6},
      {"palegoldenrod", 0xeee8aa},
      {"palegreen", 0x98fb98},
      {"paleturquoise", 0xafeeee},
      {"palevioletred", 0xdb7093},
      {"papayawhip", 0xffefd5},
      {"peachpuff", 0xffdab9},
      {"peru", 0xcd853f},
      {"pink", 0xffc0cb},
      {"plum", 0xdda0dd},
      {"powderblue", 0xb0e0e6},
      {"purple", 0x800080},
      {"red", 0xff0000},
      {"rosybrown", 0xbc8f8f},
      {"royalblue", 0x4169e1},
      {"saddlebrown", 0x8b4513},
      {"salmon", 0xfa8072},
      {"sandybrown", 0xf4a460},
      {"seagreen", 0x2e8b57},
      {"seashell", 0xfff5ee},
      {"sienna", 0xa0522d},
      {"silver", 0xc0c0c0},
      {"skyblue", 0x87ceeb},
      {"slateblue", 0x6a5acd},
      {"slategray", 0x708090},
      {"snow", 0xfffafa},
      {"springgreen", 0x00ff7f},
      {"steelblue", 0x4682b4},
      {"tan", 0xd2b48c},
      {"teal", 0x008080},
      {"thistle", 0xd8bfd8},
      {"tomato", 0xff6347},
      {"turquoise", 0x40e0d0},
      {"violet", 0xee82ee},
      {"wheat", 0xf5deb3},
      {"white", 0xffffff},
      {"whitesmoke", 0xf5f5f5},
      {"yellow", 0xffff00},
      {"yellowgreen", 0x9acd32}};
  std::string name;
  for (char c : name_in)
    if (!std::isspace(static_cast<unsigned char>(c)))
      name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!name.empty() && name[0] == '#') {
    std::string hex = name.substr(1);
    if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6) return nullptr;
    int v = static_cast<int>(std::strtol(hex.c_str(), nullptr, 16));
    return std::make_shared<DeviceRgb>((v >> 16) & 0xff, (v >> 8) & 0xff,
                                       v & 0xff);
  }
  if (name.rfind("rgb(", 0) == 0 || name.rfind("rgba(", 0) == 0) {
    auto open = name.find('(');
    std::string inner = name.substr(open + 1, name.find(')') - open - 1);
    float c[3] = {0, 0, 0};
    int idx = 0;
    size_t pos = 0;
    while (idx < 3 && pos <= inner.size()) {
      size_t comma = inner.find(',', pos);
      std::string part = inner.substr(
          pos, comma == std::string::npos ? std::string::npos : comma - pos);
      float v = std::strtof(part.c_str(), nullptr);
      if (part.find('%') != std::string::npos) v = v * 255 / 100;
      c[idx++] = v / 255.f;
      if (comma == std::string::npos) break;
      pos = comma + 1;
    }
    return std::make_shared<DeviceRgb>(c[0], c[1], c[2]);
  }
  auto it = kNames.find(name);
  if (it == kNames.end()) return nullptr;
  int v = it->second;
  return std::make_shared<DeviceRgb>((v >> 16) & 0xff, (v >> 8) & 0xff,
                                     v & 0xff);
}

}  // namespace kernel
}  // namespace pdfcpp
