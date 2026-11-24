#ifndef PDFCPP_IO_IMAGE_IMAGE_DATA_H_
#define PDFCPP_IO_IMAGE_IMAGE_DATA_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Port of ImageType.
enum class ImageType {
  kJpeg,
  kJpeg2000,
  kJbig2,
  kPng,
  kGif,
  kBmp,
  kTiff,
  kWmf,
  kPs,
  kRaw,
  kNone
};

/// Port of com.itextpdf.io.image.ImageData (simplified).
/// Holds image data in a PDF-ready form: either raw (unfiltered) samples or a
/// stream encoded with a PDF filter (DCTDecode for JPEG, FlateDecode with PNG
/// predictors for PNG).
class ImageData {
 public:
  ImageType GetOriginalType() const { return original_type_; }
  float GetWidth() const { return width_; }
  float GetHeight() const { return height_; }
  int GetBpc() const { return bpc_; }
  int GetColorEncodingComponentsNumber() const { return color_space_; }
  float GetDpiX() const { return dpi_x_; }
  float GetDpiY() const { return dpi_y_; }
  bool IsInverted() const { return inverted_; }
  const Bytes& GetData() const { return data_; }
  /// PDF filter name ("" = raw samples), e.g. "DCTDecode", "FlateDecode".
  const std::string& GetFilter() const { return filter_; }
  /// Decode parameters as (key -> int) pairs, e.g.
  /// Predictor/Colors/Columns/BitsPerComponent.
  const std::map<std::string, int>& GetDecodeParms() const {
    return decode_parms_;
  }
  /// Indexed palette (RGB triples) for paletted images.
  const Bytes& GetPalette() const { return palette_; }
  /// Optional soft mask (raw 8-bit gray samples).
  const std::shared_ptr<ImageData>& GetImageMask() const { return mask_; }
  /// Optional color key mask ranges /Mask [min max ...].
  const std::vector<int>& GetColorKeyMask() const { return color_key_mask_; }
  const Bytes& GetIccProfile() const { return icc_profile_; }
  bool IsMask() const { return is_mask_; }
  std::vector<float> GetDecode() const { return decode_; }

  // setters used by factories
  void SetWidth(float w) { width_ = w; }
  void SetHeight(float h) { height_ = h; }
  void SetBpc(int b) { bpc_ = b; }
  void SetColorEncodingComponentsNumber(int n) { color_space_ = n; }
  void SetData(Bytes d) { data_ = std::move(d); }
  void SetFilter(const std::string& f) { filter_ = f; }
  void SetDecodeParms(std::map<std::string, int> p) {
    decode_parms_ = std::move(p);
  }
  void SetPalette(Bytes p) { palette_ = std::move(p); }
  void SetImageMask(std::shared_ptr<ImageData> m) { mask_ = std::move(m); }
  void SetColorKeyMask(std::vector<int> m) { color_key_mask_ = std::move(m); }
  void SetIccProfile(Bytes p) { icc_profile_ = std::move(p); }
  void SetDpi(float x, float y) {
    dpi_x_ = x;
    dpi_y_ = y;
  }
  void SetInverted(bool v) { inverted_ = v; }
  void SetDecode(std::vector<float> d) { decode_ = std::move(d); }
  void MakeMask() { is_mask_ = true; }
  void SetOriginalType(ImageType t) { original_type_ = t; }

 private:
  ImageType original_type_ = ImageType::kRaw;
  float width_ = 0, height_ = 0;
  int bpc_ = 8;
  int color_space_ = 1;
  float dpi_x_ = 0, dpi_y_ = 0;
  bool inverted_ = false;
  bool is_mask_ = false;
  Bytes data_;
  std::string filter_;
  std::map<std::string, int> decode_parms_;
  Bytes palette_;
  std::shared_ptr<ImageData> mask_;
  std::vector<int> color_key_mask_;
  Bytes icc_profile_;
  std::vector<float> decode_;
};

/// Port of ImageDataFactory: JPEG, PNG and raw images.
class ImageDataFactory {
 public:
  static std::shared_ptr<ImageData> Create(const Bytes& bytes);
  static std::shared_ptr<ImageData> Create(const std::string& filename);
  /// Raw image: components 1 (gray), 3 (RGB) or 4 (CMYK), samples packed by
  /// rows.
  static std::shared_ptr<ImageData> Create(int width, int height,
                                           int components, int bpc,
                                           const Bytes& data);
  static std::shared_ptr<ImageData> CreateJpeg(const Bytes& bytes);
  static std::shared_ptr<ImageData> CreatePng(const Bytes& bytes);
  static ImageType DetectImageType(const Bytes& bytes);
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_IMAGE_IMAGE_DATA_H_
