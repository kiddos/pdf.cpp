#include "pdfcpp/kernel/colors/Color.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace pdfcpp {
namespace kernel {

static std::shared_ptr<PdfColorSpace> deviceSpace(int n) {
    // Created locally (not from PdfName statics) to be safe during static initialization.
    static auto gray = std::make_shared<PdfColorSpace>(PdfName::createDirectName("DeviceGray"), 1);
    static auto rgb = std::make_shared<PdfColorSpace>(PdfName::createDirectName("DeviceRGB"), 3);
    static auto cmyk = std::make_shared<PdfColorSpace>(PdfName::createDirectName("DeviceCMYK"), 4);
    return n == 1 ? gray : (n == 3 ? rgb : cmyk);
}

bool PdfColorSpace::isDevice() const {
    return nameEquals(pdfObject_, PdfName::DeviceGray) || nameEquals(pdfObject_, PdfName::DeviceRGB) ||
           nameEquals(pdfObject_, PdfName::DeviceCMYK);
}

std::shared_ptr<PdfColorSpace> PdfColorSpace::makeColorSpace(const PdfObjectPtr& pdfObject) {
    if (!pdfObject) return nullptr;
    if (pdfObject->isName()) {
        const std::string& n = static_cast<PdfName&>(*pdfObject).getValue();
        if (n == "DeviceGray" || n == "G" || n == "CalGray") return deviceSpace(1);
        if (n == "DeviceRGB" || n == "RGB" || n == "CalRGB") return deviceSpace(3);
        if (n == "DeviceCMYK" || n == "CMYK") return deviceSpace(4);
        if (n == "Pattern") return std::make_shared<PdfColorSpace>(pdfObject, 0);
        return std::make_shared<PdfColorSpace>(pdfObject, 1);
    }
    if (pdfObject->isArray()) {
        auto arr = std::static_pointer_cast<PdfArray>(pdfObject);
        auto family = arr->getAsName(0);
        if (!family) return nullptr;
        const std::string& f = family->getValue();
        if (f == "CalGray") return std::make_shared<PdfColorSpace>(pdfObject, 1);
        if (f == "CalRGB" || f == "Lab") return std::make_shared<PdfColorSpace>(pdfObject, 3);
        if (f == "ICCBased") {
            auto s = arr->getAsStream(1);
            auto n = s ? s->getAsNumber(PdfName::N) : nullptr;
            return std::make_shared<PdfColorSpace>(pdfObject, n ? n->intValue() : 3);
        }
        if (f == "Indexed" || f == "I" || f == "Separation") return std::make_shared<PdfColorSpace>(pdfObject, 1);
        if (f == "DeviceN") {
            auto names = arr->getAsArray(1);
            return std::make_shared<PdfColorSpace>(pdfObject, names ? names->size() : 1);
        }
        if (f == "Pattern") return std::make_shared<PdfColorSpace>(pdfObject, 0);
    }
    return nullptr;
}

bool Color::operator==(const Color& o) const {
    if (colorValue_ != o.colorValue_) return false;
    if (colorSpace_ == o.colorSpace_) return true;
    if (!colorSpace_ || !o.colorSpace_) return false;
    return PdfObject::equalContent(colorSpace_->getPdfObject().get(), o.colorSpace_->getPdfObject().get());
}

std::shared_ptr<Color> Color::makeColor(const std::shared_ptr<PdfColorSpace>& colorSpace, const std::vector<float>& colorValue) {
    if (colorSpace && colorSpace->isDevice()) {
        int n = colorSpace->getNumberOfComponents();
        std::vector<float> v = colorValue;
        if (v.empty()) v.assign(n, 0.f);
        if (n == 1) return std::make_shared<DeviceGray>(v[0]);
        if (n == 3 && v.size() >= 3) return std::make_shared<DeviceRgb>(v[0], v[1], v[2]);
        if (n == 4 && v.size() >= 4) return std::make_shared<DeviceCmyk>(v[0], v[1], v[2], v[3]);
    }
    std::vector<float> v = colorValue;
    if (v.empty() && colorSpace) v.assign(colorSpace->getNumberOfComponents(), 0.f);
    return std::make_shared<Color>(colorSpace, v);
}

std::shared_ptr<Color> Color::convertRgbToCmyk(const Color& rgb) {
    float r = rgb.getColorValue()[0], g = rgb.getColorValue()[1], b = rgb.getColorValue()[2];
    float k = 1 - std::max(r, std::max(g, b));
    float c = 0, m = 0, y = 0;
    if (k < 1) {
        c = (1 - r - k) / (1 - k);
        m = (1 - g - k) / (1 - k);
        y = (1 - b - k) / (1 - k);
    }
    return std::make_shared<DeviceCmyk>(c, m, y, k);
}

std::shared_ptr<Color> Color::convertCmykToRgb(const Color& cmyk) {
    const auto& v = cmyk.getColorValue();
    float r = 1 - std::min(1.f, v[0] + v[3]);
    float g = 1 - std::min(1.f, v[1] + v[3]);
    float b = 1 - std::min(1.f, v[2] + v[3]);
    return std::make_shared<DeviceRgb>(r, g, b);
}

static float clamp01(float v) { return v > 1 ? 1 : (v < 0 ? 0 : v); }

DeviceGray::DeviceGray(float value) : Color(deviceSpace(1), {clamp01(value)}) {}
const std::shared_ptr<DeviceGray> DeviceGray::WHITE = std::make_shared<DeviceGray>(1.f);
const std::shared_ptr<DeviceGray> DeviceGray::GRAY = std::make_shared<DeviceGray>(0.5f);
const std::shared_ptr<DeviceGray> DeviceGray::BLACK = std::make_shared<DeviceGray>(0.f);
DeviceGray DeviceGray::makeLighter(const DeviceGray& c) {
    float v = c.getColorValue()[0];
    return DeviceGray(v == 0 ? 0.3f : std::min(1.f, v * 1.3f));
}
DeviceGray DeviceGray::makeDarker(const DeviceGray& c) { return DeviceGray(std::max(0.f, c.getColorValue()[0] * 0.7f)); }

DeviceRgb::DeviceRgb(float r, float g, float b) : Color(deviceSpace(3), {clamp01(r), clamp01(g), clamp01(b)}) {}
const std::shared_ptr<DeviceRgb> DeviceRgb::BLACK = std::make_shared<DeviceRgb>(0, 0, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::WHITE = std::make_shared<DeviceRgb>(255, 255, 255);
const std::shared_ptr<DeviceRgb> DeviceRgb::RED = std::make_shared<DeviceRgb>(255, 0, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::GREEN = std::make_shared<DeviceRgb>(0, 255, 0);
const std::shared_ptr<DeviceRgb> DeviceRgb::BLUE = std::make_shared<DeviceRgb>(0, 0, 255);
DeviceRgb DeviceRgb::makeLighter(const DeviceRgb& c) {
    const auto& v = c.getColorValue();
    float m = std::max(v[0], std::max(v[1], v[2]));
    if (m == 0) return DeviceRgb(0.3f, 0.3f, 0.3f);
    float mul = std::min(1.f, m + 0.33f) / m;
    return DeviceRgb(v[0] * mul, v[1] * mul, v[2] * mul);
}
DeviceRgb DeviceRgb::makeDarker(const DeviceRgb& c) {
    const auto& v = c.getColorValue();
    float m = std::max(v[0], std::max(v[1], v[2]));
    float mul = m == 0 ? 0 : std::max(0.f, (m - 0.33f) / m);
    return DeviceRgb(v[0] * mul, v[1] * mul, v[2] * mul);
}

DeviceCmyk::DeviceCmyk(float c, float m, float y, float k) : Color(deviceSpace(4), {clamp01(c), clamp01(m), clamp01(y), clamp01(k)}) {}
const std::shared_ptr<DeviceCmyk> DeviceCmyk::CYAN = std::make_shared<DeviceCmyk>(100, 0, 0, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::MAGENTA = std::make_shared<DeviceCmyk>(0, 100, 0, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::YELLOW = std::make_shared<DeviceCmyk>(0, 0, 100, 0);
const std::shared_ptr<DeviceCmyk> DeviceCmyk::BLACK = std::make_shared<DeviceCmyk>(0, 0, 0, 100);

namespace ColorConstants {
const ColorPtr BLACK = std::make_shared<DeviceRgb>(0, 0, 0);
const ColorPtr BLUE = std::make_shared<DeviceRgb>(0, 0, 255);
const ColorPtr CYAN = std::make_shared<DeviceRgb>(0, 255, 255);
const ColorPtr DARK_GRAY = std::make_shared<DeviceRgb>(64, 64, 64);
const ColorPtr GRAY = std::make_shared<DeviceRgb>(128, 128, 128);
const ColorPtr GREEN = std::make_shared<DeviceRgb>(0, 255, 0);
const ColorPtr LIGHT_GRAY = std::make_shared<DeviceRgb>(192, 192, 192);
const ColorPtr MAGENTA = std::make_shared<DeviceRgb>(255, 0, 255);
const ColorPtr ORANGE = std::make_shared<DeviceRgb>(255, 200, 0);
const ColorPtr PINK = std::make_shared<DeviceRgb>(255, 175, 175);
const ColorPtr RED = std::make_shared<DeviceRgb>(255, 0, 0);
const ColorPtr WHITE = std::make_shared<DeviceRgb>(255, 255, 255);
const ColorPtr YELLOW = std::make_shared<DeviceRgb>(255, 255, 0);
}  // namespace ColorConstants

std::shared_ptr<DeviceRgb> WebColors::getRGBColor(const std::string& nameIn) {
    static const std::map<std::string, int> names = {
        {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
        {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
        {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
        {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
        {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
        {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
        {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
        {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc},
        {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b},
        {"darkslategray", 0x2f4f4f}, {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493},
        {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969}, {"dodgerblue", 0x1e90ff}, {"firebrick", 0xb22222},
        {"floralwhite", 0xfffaf0}, {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc},
        {"ghostwhite", 0xf8f8ff}, {"gold", 0xffd700}, {"goldenrod", 0xdaa520}, {"gray", 0x808080}, {"grey", 0x808080},
        {"green", 0x008000}, {"greenyellow", 0xadff2f}, {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4},
        {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c}, {"lavender", 0xe6e6fa},
        {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd}, {"lightblue", 0xadd8e6},
        {"lightcoral", 0xf08080}, {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3},
        {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1}, {"lightsalmon", 0xffa07a},
        {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899},
        {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32},
        {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa},
        {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db}, {"mediumseagreen", 0x3cb371},
        {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a}, {"mediumturquoise", 0x48d1cc},
        {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970}, {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1},
        {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead}, {"navy", 0x000080}, {"oldlace", 0xfdf5e6},
        {"olive", 0x808000}, {"olivedrab", 0x6b8e23}, {"orange", 0xffa500}, {"orangered", 0xff4500},
        {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee},
        {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5}, {"peachpuff", 0xffdab9}, {"peru", 0xcd853f},
        {"pink", 0xffc0cb}, {"plum", 0xdda0dd}, {"powderblue", 0xb0e0e6}, {"purple", 0x800080}, {"red", 0xff0000},
        {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1}, {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072},
        {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d},
        {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd}, {"slategray", 0x708090},
        {"snow", 0xfffafa}, {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4}, {"tan", 0xd2b48c}, {"teal", 0x008080},
        {"thistle", 0xd8bfd8}, {"tomato", 0xff6347}, {"turquoise", 0x40e0d0}, {"violet", 0xee82ee},
        {"wheat", 0xf5deb3}, {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5}, {"yellow", 0xffff00},
        {"yellowgreen", 0x9acd32}};
    std::string name;
    for (char c : nameIn)
        if (!std::isspace(static_cast<unsigned char>(c))) name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!name.empty() && name[0] == '#') {
        std::string hex = name.substr(1);
        if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        if (hex.size() != 6) return nullptr;
        int v = static_cast<int>(std::strtol(hex.c_str(), nullptr, 16));
        return std::make_shared<DeviceRgb>((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
    if (name.rfind("rgb(", 0) == 0 || name.rfind("rgba(", 0) == 0) {
        auto open = name.find('(');
        std::string inner = name.substr(open + 1, name.find(')') - open - 1);
        float c[3] = {0, 0, 0};
        int idx = 0;
        size_t pos = 0;
        while (idx < 3 && pos <= inner.size()) {
            size_t comma = inner.find(',', pos);
            std::string part = inner.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            float v = std::strtof(part.c_str(), nullptr);
            if (part.find('%') != std::string::npos) v = v * 255 / 100;
            c[idx++] = v / 255.f;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return std::make_shared<DeviceRgb>(c[0], c[1], c[2]);
    }
    auto it = names.find(name);
    if (it == names.end()) return nullptr;
    int v = it->second;
    return std::make_shared<DeviceRgb>((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

}  // namespace kernel
}  // namespace pdfcpp
