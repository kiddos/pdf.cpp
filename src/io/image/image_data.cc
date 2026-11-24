#include "pdfcpp/io/image/image_data.h"

#include <zlib.h>

#include <cstring>

#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/util/stream_util.h"

namespace pdfcpp {
namespace io {

ImageType ImageDataFactory::DetectImageType(const Bytes& b) {
  if (b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
    return ImageType::kJpeg;
  if (b.size() >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' &&
      b[3] == 'G')
    return ImageType::kPng;
  if (b.size() >= 3 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F')
    return ImageType::kGif;
  if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M') return ImageType::kBmp;
  if (b.size() >= 4 && ((b[0] == 'I' && b[1] == 'I' && b[2] == 42) ||
                        (b[0] == 'M' && b[1] == 'M' && b[3] == 42)))
    return ImageType::kTiff;
  if (b.size() >= 12 && b[4] == 'j' && b[5] == 'P') return ImageType::kJpeg2000;
  return ImageType::kNone;
}

std::shared_ptr<ImageData> ImageDataFactory::Create(
    const std::string& filename) {
  return Create(StreamUtil::ReadFile(filename));
}

std::shared_ptr<ImageData> ImageDataFactory::Create(const Bytes& bytes) {
  switch (DetectImageType(bytes)) {
    case ImageType::kJpeg:
      return CreateJpeg(bytes);
    case ImageType::kPng:
      return CreatePng(bytes);
    default:
      throw IoException(
          io_exception_message_constant::kImageFormatCannotBeRecognized);
  }
}

std::shared_ptr<ImageData> ImageDataFactory::Create(int width, int height,
                                                    int components, int bpc,
                                                    const Bytes& data) {
  auto img = std::make_shared<ImageData>();
  img->SetOriginalType(ImageType::kRaw);
  img->SetWidth(static_cast<float>(width));
  img->SetHeight(static_cast<float>(height));
  img->SetColorEncodingComponentsNumber(components);
  img->SetBpc(bpc);
  img->SetData(data);
  return img;
}

// ------------------------------------------------------------ JPEG (port of
// JpegImageHelper)
std::shared_ptr<ImageData> ImageDataFactory::CreateJpeg(const Bytes& b) {
  auto img = std::make_shared<ImageData>();
  img->SetOriginalType(ImageType::kJpeg);
  img->SetFilter("DCTDecode");
  img->SetData(b);
  size_t pos = 2;
  bool adobe_inverted = false;
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
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
      continue;
    if (marker == 0xD9 || marker == 0xDA) break;
    if (pos + 2 > b.size()) break;
    int len = (b[pos] << 8) | b[pos + 1];
    // A segment length counts its own two bytes; reject lengths that cannot
    // advance or that run past the data.
    if (len < 2 || pos + static_cast<size_t>(len) > b.size()) break;
    size_t seg = pos + 2;
    if (marker == 0xE0 && len >= 14 && std::memcmp(&b[seg], "JFIF", 4) == 0) {
      int units = b[seg + 7];
      int dx = (b[seg + 8] << 8) | b[seg + 9];
      int dy = (b[seg + 10] << 8) | b[seg + 11];
      if (units == 1)
        img->SetDpi(static_cast<float>(dx), static_cast<float>(dy));
      else if (units == 2)
        img->SetDpi(dx * 2.54f, dy * 2.54f);
    } else if (marker == 0xEE && len >= 12 &&
               std::memcmp(&b[seg], "Adobe", 5) == 0) {
      adobe_inverted = true;
    } else if (marker == 0xE2 && len >= 16 &&
               std::memcmp(&b[seg], "ICC_PROFILE", 11) == 0) {
      icc.insert(icc.end(), b.begin() + seg + 14, b.begin() + pos + len);
    } else if ((marker >= 0xC0 && marker <= 0xC3) ||
               (marker >= 0xC5 && marker <= 0xC7) ||
               (marker >= 0xC9 && marker <= 0xCB) ||
               (marker >= 0xCD && marker <= 0xCF)) {
      if (len < 8) throw IoException("Jpeg image: corrupted SOF marker.");
      img->SetBpc(b[seg]);
      img->SetHeight(static_cast<float>((b[seg + 1] << 8) | b[seg + 2]));
      img->SetWidth(static_cast<float>((b[seg + 3] << 8) | b[seg + 4]));
      img->SetColorEncodingComponentsNumber(b[seg + 5]);
      found = true;
    }
    pos += len;
  }
  if (!found) throw IoException("Jpeg image: no SOF marker found.");
  if (img->GetColorEncodingComponentsNumber() == 4 && adobe_inverted) {
    img->SetInverted(true);
    img->SetDecode({1, 0, 1, 0, 1, 0, 1, 0});
  }
  if (!icc.empty()) img->SetIccProfile(icc);
  return img;
}

// ------------------------------------------------------------ PNG (port of
// PngImageHelper)
namespace {
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}

Bytes InflateAll(const Bytes& in) {
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

Bytes DeflateAll(const Bytes& in) {
  uLongf len = compressBound(static_cast<uLong>(in.size()));
  Bytes out(len);
  compress2(out.data(), &len, in.data(), static_cast<uLong>(in.size()),
            Z_DEFAULT_COMPRESSION);
  out.resize(len);
  return out;
}

int Paeth(int a, int b, int c) {
  int p = a + b - c;
  int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

/// Unfilters one pass of PNG scanlines.
bool Unfilter(const uint8_t* in, size_t in_len, Bytes& out, int width,
              int height, int bits_per_pixel) {
  size_t bpp = std::max(1, bits_per_pixel / 8);
  size_t row_bytes = (static_cast<size_t>(width) * bits_per_pixel + 7) / 8;
  if (in_len < (row_bytes + 1) * height) return false;
  out.assign(row_bytes * height, 0);
  Bytes prior(row_bytes, 0);
  for (int y = 0; y < height; ++y) {
    int filter = in[y * (row_bytes + 1)];
    const uint8_t* src = in + y * (row_bytes + 1) + 1;
    uint8_t* cur = out.data() + y * row_bytes;
    for (size_t i = 0; i < row_bytes; ++i) {
      int a = i >= bpp ? cur[i - bpp] : 0;
      int b = prior[i];
      int c = i >= bpp ? prior[i - bpp] : 0;
      int x = src[i];
      switch (filter) {
        case 0:
          cur[i] = static_cast<uint8_t>(x);
          break;
        case 1:
          cur[i] = static_cast<uint8_t>(x + a);
          break;
        case 2:
          cur[i] = static_cast<uint8_t>(x + b);
          break;
        case 3:
          cur[i] = static_cast<uint8_t>(x + ((a + b) >> 1));
          break;
        case 4:
          cur[i] = static_cast<uint8_t>(x + Paeth(a, b, c));
          break;
        default:
          return false;
      }
    }
    std::memcpy(prior.data(), cur, row_bytes);
  }
  return true;
}

int GetSample(const uint8_t* row, size_t index, int bit_depth) {
  if (bit_depth == 8) return row[index];
  if (bit_depth == 16) return (row[2 * index] << 8) | row[2 * index + 1];
  size_t bit_pos = index * bit_depth;
  int shift = 8 - bit_depth - static_cast<int>(bit_pos % 8);
  return (row[bit_pos / 8] >> shift) & ((1 << bit_depth) - 1);
}
}  // namespace

std::shared_ptr<ImageData> ImageDataFactory::CreatePng(const Bytes& b) {
  if (b.size() < 33) throw IoException("Invalid PNG.");
  size_t pos = 8;
  int width = 0, height = 0, bit_depth = 0, color_type = 0, interlace = 0;
  Bytes idat, palette, trns, icc;
  float dpi_x = 0, dpi_y = 0;
  while (pos + 8 <= b.size()) {
    uint32_t len = Be32(&b[pos]);
    std::string type(reinterpret_cast<const char*>(&b[pos + 4]), 4);
    size_t data = pos + 8;
    if (data + len > b.size()) break;
    if (type == "IHDR") {
      if (len < 13) throw IoException("Invalid PNG header.");
      width = static_cast<int>(Be32(&b[data]));
      height = static_cast<int>(Be32(&b[data + 4]));
      bit_depth = b[data + 8];
      color_type = b[data + 9];
      interlace = b[data + 12];
    } else if (type == "IDAT") {
      idat.insert(idat.end(), b.begin() + data, b.begin() + data + len);
    } else if (type == "PLTE") {
      palette.assign(b.begin() + data, b.begin() + data + len);
    } else if (type == "tRNS") {
      trns.assign(b.begin() + data, b.begin() + data + len);
    } else if (type == "pHYs" && len >= 9) {
      if (b[data + 8] == 1) {
        dpi_x = Be32(&b[data]) * 0.0254f;
        dpi_y = Be32(&b[data + 4]) * 0.0254f;
      }
    } else if (type == "iCCP") {
      size_t name_end = data;
      while (name_end < data + len && b[name_end] != 0) ++name_end;
      if (name_end + 2 <= data + len)
        icc =
            InflateAll(Bytes(b.begin() + name_end + 2, b.begin() + data + len));
    } else if (type == "IEND") {
      break;
    }
    pos = data + len + 4;
  }
  if (width <= 0 || height <= 0) throw IoException("Invalid PNG header.");
  // Allowed bit depths per colour type (PNG spec, table 11.1); anything else is
  // corrupt and would break the sample arithmetic below (e.g. a zero bit depth
  // divides by zero).
  bool valid_depth = false;
  switch (color_type) {
    case 0:
      valid_depth = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
                    bit_depth == 8 || bit_depth == 16;
      break;
    case 3:
      valid_depth =
          bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
      break;
    case 2:
    case 4:
    case 6:
      valid_depth = bit_depth == 8 || bit_depth == 16;
      break;
    default:
      break;
  }
  if (!valid_depth)
    throw IoException(
        "Invalid PNG header: unsupported color type / bit depth combination.");
  int channels = color_type == 0   ? 1
                 : color_type == 2 ? 3
                 : color_type == 3 ? 1
                 : color_type == 4 ? 2
                                   : 4;
  int color_components =
      (color_type == 0 || color_type == 4 || color_type == 3) ? 1 : 3;
  bool has_alpha = color_type == 4 || color_type == 6;

  auto img = std::make_shared<ImageData>();
  img->SetOriginalType(ImageType::kPng);
  img->SetWidth(static_cast<float>(width));
  img->SetHeight(static_cast<float>(height));
  img->SetDpi(dpi_x, dpi_y);
  if (!icc.empty() && color_type != 3) img->SetIccProfile(icc);
  if (color_type == 3) img->SetPalette(palette);

  // Fast path: embed IDAT as-is with PNG predictors (no alpha, not interlaced).
  if (!has_alpha && interlace == 0 && (trns.empty() || color_type != 3)) {
    img->SetData(idat);
    img->SetFilter("FlateDecode");
    img->SetDecodeParms({{"Predictor", 15},
                         {"Colors", channels},
                         {"BitsPerComponent", bit_depth},
                         {"Columns", width}});
    img->SetBpc(bit_depth);
    img->SetColorEncodingComponentsNumber(color_components);
    if (!trns.empty()) {
      if (color_type == 0 && trns.size() >= 2) {
        int g = (trns[0] << 8) | trns[1];
        img->SetColorKeyMask({g, g});
      } else if (color_type == 2 && trns.size() >= 6) {
        int r = (trns[0] << 8) | trns[1], g = (trns[2] << 8) | trns[3],
            bl = (trns[4] << 8) | trns[5];
        img->SetColorKeyMask({r, r, g, g, bl, bl});
      }
    }
    return img;
  }
  if (interlace != 0)
    throw IoException(
        "Interlaced PNG images with transparency are not supported.");
  // Full decode: split color and alpha.
  Bytes raw = InflateAll(idat);
  Bytes pixels;
  int bits_per_pixel = channels * bit_depth;
  if (!Unfilter(raw.data(), raw.size(), pixels, width, height, bits_per_pixel))
    throw IoException("Corrupted PNG data.");
  size_t row_bytes = (static_cast<size_t>(width) * bits_per_pixel + 7) / 8;
  int out_bpc = bit_depth == 16 ? 8 : bit_depth;
  Bytes color, alpha;
  bool any_alpha = false;
  if (color_type == 3) {
    // paletted with tRNS: keep indices, alpha from tRNS
    color = pixels;
    alpha.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        int idx = GetSample(pixels.data() + y * row_bytes, x, bit_depth);
        uint8_t a = idx < static_cast<int>(trns.size()) ? trns[idx] : 255;
        alpha[static_cast<size_t>(y) * width + x] = a;
        if (a != 255) any_alpha = true;
      }
    out_bpc = bit_depth;
  } else {
    color.reserve(static_cast<size_t>(width) * height * color_components);
    alpha.reserve(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = pixels.data() + y * row_bytes;
      for (int x = 0; x < width; ++x) {
        for (int c = 0; c < channels; ++c) {
          int v =
              GetSample(row, static_cast<size_t>(x) * channels + c, bit_depth);
          if (bit_depth == 16)
            v >>= 8;
          else if (bit_depth < 8)
            v = v * 255 / ((1 << bit_depth) - 1);
          if (c < color_components)
            color.push_back(static_cast<uint8_t>(v));
          else {
            alpha.push_back(static_cast<uint8_t>(v));
            if (v != 255) any_alpha = true;
          }
        }
      }
    }
    out_bpc = 8;
  }
  img->SetBpc(out_bpc);
  img->SetColorEncodingComponentsNumber(color_components);
  img->SetData(DeflateAll(color));
  img->SetFilter("FlateDecode");
  if (any_alpha) {
    auto mask = std::make_shared<ImageData>();
    mask->SetWidth(static_cast<float>(width));
    mask->SetHeight(static_cast<float>(height));
    mask->SetBpc(8);
    mask->SetColorEncodingComponentsNumber(1);
    mask->SetData(DeflateAll(alpha));
    mask->SetFilter("FlateDecode");
    mask->MakeMask();
    img->SetImageMask(mask);
  }
  return img;
}

}  // namespace io
}  // namespace pdfcpp
