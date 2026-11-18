#include "pdfcpp/io/image/ImageData.h"

#include <zlib.h>

#include <cstring>

#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/util/StreamUtil.h"

namespace pdfcpp {
namespace io {

ImageType ImageDataFactory::detectImageType(const Bytes& b) {
    if (b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return ImageType::JPEG;
    if (b.size() >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return ImageType::PNG;
    if (b.size() >= 3 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return ImageType::GIF;
    if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M') return ImageType::BMP;
    if (b.size() >= 4 && ((b[0] == 'I' && b[1] == 'I' && b[2] == 42) || (b[0] == 'M' && b[1] == 'M' && b[3] == 42)))
        return ImageType::TIFF;
    if (b.size() >= 12 && b[4] == 'j' && b[5] == 'P') return ImageType::JPEG2000;
    return ImageType::NONE;
}

std::shared_ptr<ImageData> ImageDataFactory::create(const std::string& filename) { return create(StreamUtil::readFile(filename)); }

std::shared_ptr<ImageData> ImageDataFactory::create(const Bytes& bytes) {
    switch (detectImageType(bytes)) {
        case ImageType::JPEG:
            return createJpeg(bytes);
        case ImageType::PNG:
            return createPng(bytes);
        default:
            throw IoException(IoExceptionMessageConstant::IMAGE_FORMAT_CANNOT_BE_RECOGNIZED);
    }
}

std::shared_ptr<ImageData> ImageDataFactory::create(int width, int height, int components, int bpc, const Bytes& data) {
    auto img = std::make_shared<ImageData>();
    img->setOriginalType(ImageType::RAW);
    img->setWidth(static_cast<float>(width));
    img->setHeight(static_cast<float>(height));
    img->setColorEncodingComponentsNumber(components);
    img->setBpc(bpc);
    img->setData(data);
    return img;
}

// ------------------------------------------------------------ JPEG (port of JpegImageHelper)
std::shared_ptr<ImageData> ImageDataFactory::createJpeg(const Bytes& b) {
    auto img = std::make_shared<ImageData>();
    img->setOriginalType(ImageType::JPEG);
    img->setFilter("DCTDecode");
    img->setData(b);
    size_t pos = 2;
    bool adobeInverted = false;
    bool found = false;
    Bytes icc;
    while (pos + 4 <= b.size()) {
        if (b[pos] != 0xFF) {
            ++pos;
            continue;
        }
        uint8_t marker = b[pos + 1];
        if (marker == 0xFF) {
            ++pos;
            continue;
        }
        pos += 2;
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xD9 || marker == 0xDA) break;
        if (pos + 2 > b.size()) break;
        int len = (b[pos] << 8) | b[pos + 1];
        size_t seg = pos + 2;
        if (marker == 0xE0 && len >= 14 && std::memcmp(&b[seg], "JFIF", 4) == 0) {
            int units = b[seg + 7];
            int dx = (b[seg + 8] << 8) | b[seg + 9];
            int dy = (b[seg + 10] << 8) | b[seg + 11];
            if (units == 1) img->setDpi(static_cast<float>(dx), static_cast<float>(dy));
            else if (units == 2) img->setDpi(dx * 2.54f, dy * 2.54f);
        } else if (marker == 0xEE && len >= 12 && std::memcmp(&b[seg], "Adobe", 5) == 0) {
            adobeInverted = true;
        } else if (marker == 0xE2 && len >= 16 && std::memcmp(&b[seg], "ICC_PROFILE", 11) == 0) {
            icc.insert(icc.end(), b.begin() + seg + 14, b.begin() + pos + len);
        } else if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                   (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
            img->setBpc(b[seg]);
            img->setHeight(static_cast<float>((b[seg + 1] << 8) | b[seg + 2]));
            img->setWidth(static_cast<float>((b[seg + 3] << 8) | b[seg + 4]));
            img->setColorEncodingComponentsNumber(b[seg + 5]);
            found = true;
        }
        pos += len;
    }
    if (!found) throw IoException("Jpeg image: no SOF marker found.");
    if (img->getColorEncodingComponentsNumber() == 4 && adobeInverted) {
        img->setInverted(true);
        img->setDecode({1, 0, 1, 0, 1, 0, 1, 0});
    }
    if (!icc.empty()) img->setIccProfile(icc);
    return img;
}

// ------------------------------------------------------------ PNG (port of PngImageHelper)
namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

Bytes inflateAll(const Bytes& in) {
    Bytes out;
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return out;
    zs.next_in = const_cast<Bytef*>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    uint8_t buf[65536];
    int ret;
    do {
        zs.next_out = buf;
        zs.avail_out = sizeof buf;
        ret = inflate(&zs, Z_NO_FLUSH);
        out.insert(out.end(), buf, buf + (sizeof buf - zs.avail_out));
    } while (ret == Z_OK);
    inflateEnd(&zs);
    return out;
}

Bytes deflateAll(const Bytes& in) {
    uLongf len = compressBound(static_cast<uLong>(in.size()));
    Bytes out(len);
    compress2(out.data(), &len, in.data(), static_cast<uLong>(in.size()), Z_DEFAULT_COMPRESSION);
    out.resize(len);
    return out;
}

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/// Unfilters one pass of PNG scanlines.
bool unfilter(const uint8_t* in, size_t inLen, Bytes& out, int width, int height, int bitsPerPixel) {
    size_t bpp = std::max(1, bitsPerPixel / 8);
    size_t rowBytes = (static_cast<size_t>(width) * bitsPerPixel + 7) / 8;
    if (inLen < (rowBytes + 1) * height) return false;
    out.assign(rowBytes * height, 0);
    Bytes prior(rowBytes, 0);
    for (int y = 0; y < height; ++y) {
        int filter = in[y * (rowBytes + 1)];
        const uint8_t* src = in + y * (rowBytes + 1) + 1;
        uint8_t* cur = out.data() + y * rowBytes;
        for (size_t i = 0; i < rowBytes; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = prior[i];
            int c = i >= bpp ? prior[i - bpp] : 0;
            int x = src[i];
            switch (filter) {
                case 0: cur[i] = static_cast<uint8_t>(x); break;
                case 1: cur[i] = static_cast<uint8_t>(x + a); break;
                case 2: cur[i] = static_cast<uint8_t>(x + b); break;
                case 3: cur[i] = static_cast<uint8_t>(x + ((a + b) >> 1)); break;
                case 4: cur[i] = static_cast<uint8_t>(x + paeth(a, b, c)); break;
                default: return false;
            }
        }
        std::memcpy(prior.data(), cur, rowBytes);
    }
    return true;
}

int getSample(const uint8_t* row, size_t index, int bitDepth) {
    if (bitDepth == 8) return row[index];
    if (bitDepth == 16) return (row[2 * index] << 8) | row[2 * index + 1];
    size_t bitPos = index * bitDepth;
    int shift = 8 - bitDepth - static_cast<int>(bitPos % 8);
    return (row[bitPos / 8] >> shift) & ((1 << bitDepth) - 1);
}
}  // namespace

std::shared_ptr<ImageData> ImageDataFactory::createPng(const Bytes& b) {
    if (b.size() < 33) throw IoException("Invalid PNG.");
    size_t pos = 8;
    int width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
    Bytes idat, palette, trns, icc;
    float dpiX = 0, dpiY = 0;
    while (pos + 8 <= b.size()) {
        uint32_t len = be32(&b[pos]);
        std::string type(reinterpret_cast<const char*>(&b[pos + 4]), 4);
        size_t data = pos + 8;
        if (data + len > b.size()) break;
        if (type == "IHDR") {
            width = static_cast<int>(be32(&b[data]));
            height = static_cast<int>(be32(&b[data + 4]));
            bitDepth = b[data + 8];
            colorType = b[data + 9];
            interlace = b[data + 12];
        } else if (type == "IDAT") {
            idat.insert(idat.end(), b.begin() + data, b.begin() + data + len);
        } else if (type == "PLTE") {
            palette.assign(b.begin() + data, b.begin() + data + len);
        } else if (type == "tRNS") {
            trns.assign(b.begin() + data, b.begin() + data + len);
        } else if (type == "pHYs" && len >= 9) {
            if (b[data + 8] == 1) {
                dpiX = be32(&b[data]) * 0.0254f;
                dpiY = be32(&b[data + 4]) * 0.0254f;
            }
        } else if (type == "iCCP") {
            size_t nameEnd = data;
            while (nameEnd < data + len && b[nameEnd] != 0) ++nameEnd;
            if (nameEnd + 2 <= data + len) icc = inflateAll(Bytes(b.begin() + nameEnd + 2, b.begin() + data + len));
        } else if (type == "IEND") {
            break;
        }
        pos = data + len + 4;
    }
    if (width <= 0 || height <= 0) throw IoException("Invalid PNG header.");
    int channels = colorType == 0 ? 1 : colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 4 ? 2 : 4;
    int colorComponents = (colorType == 0 || colorType == 4 || colorType == 3) ? 1 : 3;
    bool hasAlpha = colorType == 4 || colorType == 6;

    auto img = std::make_shared<ImageData>();
    img->setOriginalType(ImageType::PNG);
    img->setWidth(static_cast<float>(width));
    img->setHeight(static_cast<float>(height));
    img->setDpi(dpiX, dpiY);
    if (!icc.empty() && colorType != 3) img->setIccProfile(icc);
    if (colorType == 3) img->setPalette(palette);

    // Fast path: embed IDAT as-is with PNG predictors (no alpha, not interlaced).
    if (!hasAlpha && interlace == 0 && (trns.empty() || colorType != 3)) {
        img->setData(idat);
        img->setFilter("FlateDecode");
        img->setDecodeParms({{"Predictor", 15}, {"Colors", channels}, {"BitsPerComponent", bitDepth}, {"Columns", width}});
        img->setBpc(bitDepth);
        img->setColorEncodingComponentsNumber(colorComponents);
        if (!trns.empty()) {
            if (colorType == 0 && trns.size() >= 2) {
                int g = (trns[0] << 8) | trns[1];
                img->setColorKeyMask({g, g});
            } else if (colorType == 2 && trns.size() >= 6) {
                int r = (trns[0] << 8) | trns[1], g = (trns[2] << 8) | trns[3], bl = (trns[4] << 8) | trns[5];
                img->setColorKeyMask({r, r, g, g, bl, bl});
            }
        }
        return img;
    }
    if (interlace != 0) throw IoException("Interlaced PNG images with transparency are not supported.");
    // Full decode: split color and alpha.
    Bytes raw = inflateAll(idat);
    Bytes pixels;
    int bitsPerPixel = channels * bitDepth;
    if (!unfilter(raw.data(), raw.size(), pixels, width, height, bitsPerPixel)) throw IoException("Corrupted PNG data.");
    size_t rowBytes = (static_cast<size_t>(width) * bitsPerPixel + 7) / 8;
    int outBpc = bitDepth == 16 ? 8 : bitDepth;
    Bytes color, alpha;
    bool anyAlpha = false;
    if (colorType == 3) {
        // paletted with tRNS: keep indices, alpha from tRNS
        color = pixels;
        alpha.resize(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                int idx = getSample(pixels.data() + y * rowBytes, x, bitDepth);
                uint8_t a = idx < static_cast<int>(trns.size()) ? trns[idx] : 255;
                alpha[static_cast<size_t>(y) * width + x] = a;
                if (a != 255) anyAlpha = true;
            }
        outBpc = bitDepth;
    } else {
        color.reserve(static_cast<size_t>(width) * height * colorComponents);
        alpha.reserve(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = pixels.data() + y * rowBytes;
            for (int x = 0; x < width; ++x) {
                for (int c = 0; c < channels; ++c) {
                    int v = getSample(row, static_cast<size_t>(x) * channels + c, bitDepth);
                    if (bitDepth == 16) v >>= 8;
                    else if (bitDepth < 8) v = v * 255 / ((1 << bitDepth) - 1);
                    if (c < colorComponents) color.push_back(static_cast<uint8_t>(v));
                    else {
                        alpha.push_back(static_cast<uint8_t>(v));
                        if (v != 255) anyAlpha = true;
                    }
                }
            }
        }
        outBpc = 8;
    }
    img->setBpc(outBpc);
    img->setColorEncodingComponentsNumber(colorComponents);
    img->setData(deflateAll(color));
    img->setFilter("FlateDecode");
    if (anyAlpha) {
        auto mask = std::make_shared<ImageData>();
        mask->setWidth(static_cast<float>(width));
        mask->setHeight(static_cast<float>(height));
        mask->setBpc(8);
        mask->setColorEncodingComponentsNumber(1);
        mask->setData(deflateAll(alpha));
        mask->setFilter("FlateDecode");
        mask->makeMask();
        img->setImageMask(mask);
    }
    return img;
}

}  // namespace io
}  // namespace pdfcpp
