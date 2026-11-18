#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Port of ImageType.
enum class ImageType { JPEG, JPEG2000, JBIG2, PNG, GIF, BMP, TIFF, WMF, PS, RAW, NONE };

/// Port of com.itextpdf.io.image.ImageData (simplified).
/// Holds image data in a PDF-ready form: either raw (unfiltered) samples or a stream encoded with a
/// PDF filter (DCTDecode for JPEG, FlateDecode with PNG predictors for PNG).
class ImageData {
public:
    ImageType getOriginalType() const { return originalType_; }
    float getWidth() const { return width_; }
    float getHeight() const { return height_; }
    int getBpc() const { return bpc_; }
    int getColorEncodingComponentsNumber() const { return colorSpace_; }
    float getDpiX() const { return dpiX_; }
    float getDpiY() const { return dpiY_; }
    bool isInverted() const { return inverted_; }
    const Bytes& getData() const { return data_; }
    /// PDF filter name ("" = raw samples), e.g. "DCTDecode", "FlateDecode".
    const std::string& getFilter() const { return filter_; }
    /// Decode parameters as (key -> int) pairs, e.g. Predictor/Colors/Columns/BitsPerComponent.
    const std::map<std::string, int>& getDecodeParms() const { return decodeParms_; }
    /// Indexed palette (RGB triples) for paletted images.
    const Bytes& getPalette() const { return palette_; }
    /// Optional soft mask (raw 8-bit gray samples).
    const std::shared_ptr<ImageData>& getImageMask() const { return mask_; }
    /// Optional color key mask ranges /Mask [min max ...].
    const std::vector<int>& getColorKeyMask() const { return colorKeyMask_; }
    const Bytes& getIccProfile() const { return iccProfile_; }
    bool isMask() const { return isMask_; }
    std::vector<float> getDecode() const { return decode_; }

    // setters used by factories
    void setWidth(float w) { width_ = w; }
    void setHeight(float h) { height_ = h; }
    void setBpc(int b) { bpc_ = b; }
    void setColorEncodingComponentsNumber(int n) { colorSpace_ = n; }
    void setData(Bytes d) { data_ = std::move(d); }
    void setFilter(const std::string& f) { filter_ = f; }
    void setDecodeParms(std::map<std::string, int> p) { decodeParms_ = std::move(p); }
    void setPalette(Bytes p) { palette_ = std::move(p); }
    void setImageMask(std::shared_ptr<ImageData> m) { mask_ = std::move(m); }
    void setColorKeyMask(std::vector<int> m) { colorKeyMask_ = std::move(m); }
    void setIccProfile(Bytes p) { iccProfile_ = std::move(p); }
    void setDpi(float x, float y) {
        dpiX_ = x;
        dpiY_ = y;
    }
    void setInverted(bool v) { inverted_ = v; }
    void setDecode(std::vector<float> d) { decode_ = std::move(d); }
    void makeMask() { isMask_ = true; }
    void setOriginalType(ImageType t) { originalType_ = t; }

private:
    ImageType originalType_ = ImageType::RAW;
    float width_ = 0, height_ = 0;
    int bpc_ = 8;
    int colorSpace_ = 1;
    float dpiX_ = 0, dpiY_ = 0;
    bool inverted_ = false;
    bool isMask_ = false;
    Bytes data_;
    std::string filter_;
    std::map<std::string, int> decodeParms_;
    Bytes palette_;
    std::shared_ptr<ImageData> mask_;
    std::vector<int> colorKeyMask_;
    Bytes iccProfile_;
    std::vector<float> decode_;
};

/// Port of ImageDataFactory: JPEG, PNG and raw images.
class ImageDataFactory {
public:
    static std::shared_ptr<ImageData> create(const Bytes& bytes);
    static std::shared_ptr<ImageData> create(const std::string& filename);
    /// Raw image: components 1 (gray), 3 (RGB) or 4 (CMYK), samples packed by rows.
    static std::shared_ptr<ImageData> create(int width, int height, int components, int bpc, const Bytes& data);
    static std::shared_ptr<ImageData> createJpeg(const Bytes& bytes);
    static std::shared_ptr<ImageData> createPng(const Bytes& bytes);
    static ImageType detectImageType(const Bytes& bytes);
};

}  // namespace io
}  // namespace pdfcpp
