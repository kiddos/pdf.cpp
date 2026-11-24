#ifndef PDFCPP_BARCODES_BARCODES_H_
#define PDFCPP_BARCODES_BARCODES_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace barcodes {

using kernel::ColorPtr;
using kernel::PdfCanvas;
using kernel::PdfDocument;
using kernel::Rectangle;

/// Port of com.itextpdf.barcodes.Barcode1D.
class Barcode1D {
 public:
  static constexpr int kAlignLeft = 1;
  static constexpr int kAlignRight = 2;
  static constexpr int kAlignCenter = 3;

  explicit Barcode1D(PdfDocument* document) : document_(document) {}
  virtual ~Barcode1D() = default;

  float GetX() const { return x_; }
  void SetX(float x) { x_ = x; }
  float GetN() const { return n_; }
  void SetN(float n) { n_ = n; }
  std::shared_ptr<kernel::PdfFont> GetFont() const { return font_; }
  void SetFont(std::shared_ptr<kernel::PdfFont> font) {
    font_ = std::move(font);
  }
  float GetSize() const { return size_; }
  void SetSize(float size) { size_ = size; }
  float GetBaseline() const { return baseline_; }
  void SetBaseline(float baseline) { baseline_ = baseline; }
  float GetBarHeight() const { return bar_height_; }
  void SetBarHeight(float bar_height) { bar_height_ = bar_height; }
  int GetTextAlignment() const { return text_alignment_; }
  void SetTextAlignment(int a) { text_alignment_ = a; }
  bool IsGenerateChecksum() const { return generate_checksum_; }
  void SetGenerateChecksum(bool v) { generate_checksum_ = v; }
  bool IsChecksumText() const { return checksum_text_; }
  void SetChecksumText(bool v) { checksum_text_ = v; }
  bool IsStartStopText() const { return start_stop_text_; }
  void SetStartStopText(bool v) { start_stop_text_ = v; }
  bool IsExtended() const { return extended_; }
  void SetExtended(bool v) { extended_ = v; }
  const std::string& GetCode() const { return code_; }
  virtual void SetCode(const std::string& code) { code_ = code; }
  bool IsGuardBars() const { return guard_bars_; }
  void SetGuardBars(bool v) { guard_bars_ = v; }
  int GetCodeType() const { return code_type_; }
  void SetCodeType(int t) { code_type_ = t; }
  float GetInkSpreading() const { return ink_spreading_; }
  void SetInkSpreading(float v) { ink_spreading_ = v; }
  const std::string& GetAltText() const { return alt_text_; }
  void SetAltText(const std::string& t) {
    alt_text_ = t;
    has_alt_text_ = true;
  }

  /// Size of the barcode (including text) in points.
  virtual Rectangle GetBarcodeSize() const = 0;
  /// Draws the barcode at the origin of the canvas; returns its size.
  virtual Rectangle PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                                 const ColorPtr& text_color) = 0;
  std::shared_ptr<kernel::PdfFormXObject> CreateFormXObject(
      const ColorPtr& bar_color = nullptr,
      const ColorPtr& text_color = nullptr);
  /// Scales the module width so that the barcode is `width` points wide.
  void FitWidth(float width);

 protected:
  float GetDescender() const;
  std::string TextOrAlt(const std::string& text) const {
    return has_alt_text_ ? alt_text_ : text;
  }

  PdfDocument* document_;
  std::shared_ptr<kernel::PdfFont> font_;
  float x_ = 0.8f;
  float n_ = 2;
  float size_ = 8;
  float baseline_ = 8;
  float bar_height_ = 24;
  int text_alignment_ = kAlignCenter;
  bool generate_checksum_ = false;
  bool checksum_text_ = false;
  bool start_stop_text_ = false;
  bool extended_ = false;
  std::string code_;
  bool guard_bars_ = false;
  int code_type_ = 0;
  float ink_spreading_ = 0;
  std::string alt_text_;
  bool has_alt_text_ = false;
};

/// Port of Barcode128 (CODE128 and CODE128_RAW; UCC/EAN-128 via FNC1).
class Barcode128 : public Barcode1D {
 public:
  static constexpr int kCode128 = 1;
  static constexpr int kCode128Ucc = 2;
  static constexpr int kCode128Raw = 3;
  static constexpr int kCodeAbToC = 99, kCodeAcToB = 100, kCodeBcToA = 101,
                       kFnc1Index = 102;
  static constexpr int kStartA = 103, kStartB = 104, kStartC = 105;
  /// FNC1 marker inside codes (U+00CA in Java).
  static constexpr char32_t kFnc1 = 0xCA;
  enum class Barcode128CodeSet { kA, kB, kC, kAuto };

  explicit Barcode128(PdfDocument* document);
  void SetCodeSet(Barcode128CodeSet code_set) { code_set_ = code_set; }
  Barcode128CodeSet GetCodeSet() const { return code_set_; }
  /// Converts text into the sequence of code values (start symbol included,
  /// checksum excluded).
  static std::vector<int> GetRawText(
      const std::string& text, bool ucc,
      Barcode128CodeSet code_set = Barcode128CodeSet::kAuto);
  /// Bar/space widths (in modules) for the code values, with checksum and stop
  /// pattern.
  static std::vector<uint8_t> GetBarsCode128Raw(std::vector<int> codes);
  static std::string RemoveFnc1(const std::string& code);
  Rectangle GetBarcodeSize() const override;
  Rectangle PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                         const ColorPtr& text_color) override;

 private:
  std::string HumanReadable() const;
  Barcode128CodeSet code_set_ = Barcode128CodeSet::kAuto;
};

/// Port of BarcodeEAN (EAN-13, EAN-8, UPC-A).
class BarcodeEAN : public Barcode1D {
 public:
  static constexpr int kEan13 = 1, kEan8 = 2, kUpca = 3, kUpce = 4, kSupp2 = 5,
                       kSupp5 = 6;
  explicit BarcodeEAN(PdfDocument* document);
  static int CalculateEanParity(const std::string& code);
  static std::vector<uint8_t> GetBarsEan13(const std::string& code);
  static std::vector<uint8_t> GetBarsEan8(const std::string& code);
  Rectangle GetBarcodeSize() const override;
  Rectangle PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                         const ColorPtr& text_color) override;
};

/// Port of Barcode39 (Code 39, optionally extended and with checksum).
class Barcode39 : public Barcode1D {
 public:
  explicit Barcode39(PdfDocument* document);
  static std::vector<uint8_t> GetBarsCode39(const std::string& text);
  static std::string GetCode39Ex(const std::string& text);
  static char GetChecksum(const std::string& text);
  Rectangle GetBarcodeSize() const override;
  Rectangle PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                         const ColorPtr& text_color) override;

 private:
  std::string BarcodeText() const;
};

/// QR error correction levels (port of ErrorCorrectionLevel).
enum class ErrorCorrectionLevel { kL = 0, kM = 1, kQ = 2, kH = 3 };

/// Port of BarcodeQRCode with a native QR encoder (ISO/IEC 18004).
class BarcodeQRCode {
 public:
  BarcodeQRCode() = default;
  explicit BarcodeQRCode(const std::string& code,
                         ErrorCorrectionLevel level = ErrorCorrectionLevel::kL,
                         int min_version = 0)
      : code_(code), level_(level), min_version_(min_version) {
    Regenerate();
  }
  void SetCode(const std::string& code) {
    code_ = code;
    Regenerate();
  }
  const std::string& GetCode() const { return code_; }
  void SetErrorCorrectionLevel(ErrorCorrectionLevel level) {
    level_ = level;
    Regenerate();
  }
  /// The symbol version chosen (1..40).
  int GetVersion() const { return version_; }
  /// Module matrix including the 4-module quiet zone; true = dark.
  const std::vector<std::vector<bool>>& GetMatrix() const { return matrix_; }
  Rectangle GetBarcodeSize(float module_size = 1) const;
  Rectangle PlaceBarcode(PdfCanvas& canvas, const ColorPtr& foreground,
                         float module_side = 1);
  std::shared_ptr<kernel::PdfFormXObject> CreateFormXObject(
      PdfDocument* document, const ColorPtr& foreground = nullptr,
      float module_side = 1);

 private:
  void Regenerate();
  std::string code_;
  ErrorCorrectionLevel level_ = ErrorCorrectionLevel::kL;
  int min_version_ = 0;
  int version_ = 0;
  std::vector<std::vector<bool>> matrix_;
};

/// Low-level QR encoder (port of qrcode.Encoder/MatrixUtil/MaskUtil).
class QrEncoder {
 public:
  /// Encodes data bytes into a module matrix (without quiet zone). Mode is
  /// selected automatically.
  static std::vector<std::vector<bool>> Encode(const std::string& content,
                                               ErrorCorrectionLevel level,
                                               int min_version, int& version);
  /// Reed-Solomon EC codewords for data (GF(256), polynomial 0x11D).
  static std::vector<uint8_t> GenerateEcBytes(const std::vector<uint8_t>& data,
                                              int num_ec_bytes);
};

}  // namespace barcodes
}  // namespace pdfcpp

#endif  // PDFCPP_BARCODES_BARCODES_H_
