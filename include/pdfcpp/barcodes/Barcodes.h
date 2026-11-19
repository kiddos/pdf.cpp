#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace barcodes {

using kernel::ColorPtr;
using kernel::PdfCanvas;
using kernel::PdfDocument;
using kernel::Rectangle;

/// Port of com.itextpdf.barcodes.Barcode1D.
class Barcode1D {
public:
    static constexpr int ALIGN_LEFT = 1;
    static constexpr int ALIGN_RIGHT = 2;
    static constexpr int ALIGN_CENTER = 3;

    explicit Barcode1D(PdfDocument* document) : document_(document) {}
    virtual ~Barcode1D() = default;

    float getX() const { return x_; }
    void setX(float x) { x_ = x; }
    float getN() const { return n_; }
    void setN(float n) { n_ = n; }
    std::shared_ptr<kernel::PdfFont> getFont() const { return font_; }
    void setFont(std::shared_ptr<kernel::PdfFont> font) { font_ = std::move(font); }
    float getSize() const { return size_; }
    void setSize(float size) { size_ = size; }
    float getBaseline() const { return baseline_; }
    void setBaseline(float baseline) { baseline_ = baseline; }
    float getBarHeight() const { return barHeight_; }
    void setBarHeight(float barHeight) { barHeight_ = barHeight; }
    int getTextAlignment() const { return textAlignment_; }
    void setTextAlignment(int a) { textAlignment_ = a; }
    bool isGenerateChecksum() const { return generateChecksum_; }
    void setGenerateChecksum(bool v) { generateChecksum_ = v; }
    bool isChecksumText() const { return checksumText_; }
    void setChecksumText(bool v) { checksumText_ = v; }
    bool isStartStopText() const { return startStopText_; }
    void setStartStopText(bool v) { startStopText_ = v; }
    bool isExtended() const { return extended_; }
    void setExtended(bool v) { extended_ = v; }
    const std::string& getCode() const { return code_; }
    virtual void setCode(const std::string& code) { code_ = code; }
    bool isGuardBars() const { return guardBars_; }
    void setGuardBars(bool v) { guardBars_ = v; }
    int getCodeType() const { return codeType_; }
    void setCodeType(int t) { codeType_ = t; }
    float getInkSpreading() const { return inkSpreading_; }
    void setInkSpreading(float v) { inkSpreading_ = v; }
    const std::string& getAltText() const { return altText_; }
    void setAltText(const std::string& t) {
        altText_ = t;
        hasAltText_ = true;
    }

    /// Size of the barcode (including text) in points.
    virtual Rectangle getBarcodeSize() const = 0;
    /// Draws the barcode at the origin of the canvas; returns its size.
    virtual Rectangle placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) = 0;
    std::shared_ptr<kernel::PdfFormXObject> createFormXObject(const ColorPtr& barColor = nullptr, const ColorPtr& textColor = nullptr);
    /// Scales the module width so that the barcode is `width` points wide.
    void fitWidth(float width);

protected:
    float getDescender() const;
    std::string textOrAlt(const std::string& text) const { return hasAltText_ ? altText_ : text; }

    PdfDocument* document_;
    std::shared_ptr<kernel::PdfFont> font_;
    float x_ = 0.8f;
    float n_ = 2;
    float size_ = 8;
    float baseline_ = 8;
    float barHeight_ = 24;
    int textAlignment_ = ALIGN_CENTER;
    bool generateChecksum_ = false;
    bool checksumText_ = false;
    bool startStopText_ = false;
    bool extended_ = false;
    std::string code_;
    bool guardBars_ = false;
    int codeType_ = 0;
    float inkSpreading_ = 0;
    std::string altText_;
    bool hasAltText_ = false;
};

/// Port of Barcode128 (CODE128 and CODE128_RAW; UCC/EAN-128 via FNC1).
class Barcode128 : public Barcode1D {
public:
    static constexpr int CODE128 = 1;
    static constexpr int CODE128_UCC = 2;
    static constexpr int CODE128_RAW = 3;
    static constexpr int CODE_AB_TO_C = 99, CODE_AC_TO_B = 100, CODE_BC_TO_A = 101, FNC1_INDEX = 102;
    static constexpr int START_A = 103, START_B = 104, START_C = 105;
    /// FNC1 marker inside codes (U+00CA in Java).
    static constexpr char32_t FNC1 = 0xCA;
    enum class Barcode128CodeSet { A, B, C, AUTO };

    explicit Barcode128(PdfDocument* document);
    void setCodeSet(Barcode128CodeSet codeSet) { codeSet_ = codeSet; }
    Barcode128CodeSet getCodeSet() const { return codeSet_; }
    /// Converts text into the sequence of code values (start symbol included, checksum excluded).
    static std::vector<int> getRawText(const std::string& text, bool ucc, Barcode128CodeSet codeSet = Barcode128CodeSet::AUTO);
    /// Bar/space widths (in modules) for the code values, with checksum and stop pattern.
    static std::vector<uint8_t> getBarsCode128Raw(std::vector<int> codes);
    static std::string removeFNC1(const std::string& code);
    Rectangle getBarcodeSize() const override;
    Rectangle placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) override;

private:
    std::string humanReadable() const;
    Barcode128CodeSet codeSet_ = Barcode128CodeSet::AUTO;
};

/// Port of BarcodeEAN (EAN-13, EAN-8, UPC-A).
class BarcodeEAN : public Barcode1D {
public:
    static constexpr int EAN13 = 1, EAN8 = 2, UPCA = 3, UPCE = 4, SUPP2 = 5, SUPP5 = 6;
    explicit BarcodeEAN(PdfDocument* document);
    static int calculateEANParity(const std::string& code);
    static std::vector<uint8_t> getBarsEAN13(const std::string& code);
    static std::vector<uint8_t> getBarsEAN8(const std::string& code);
    Rectangle getBarcodeSize() const override;
    Rectangle placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) override;
};

/// Port of Barcode39 (Code 39, optionally extended and with checksum).
class Barcode39 : public Barcode1D {
public:
    explicit Barcode39(PdfDocument* document);
    static std::vector<uint8_t> getBarsCode39(const std::string& text);
    static std::string getCode39Ex(const std::string& text);
    static char getChecksum(const std::string& text);
    Rectangle getBarcodeSize() const override;
    Rectangle placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) override;

private:
    std::string barcodeText() const;
};

/// QR error correction levels (port of ErrorCorrectionLevel).
enum class ErrorCorrectionLevel { L = 0, M = 1, Q = 2, H = 3 };

/// Port of BarcodeQRCode with a native QR encoder (ISO/IEC 18004).
class BarcodeQRCode {
public:
    BarcodeQRCode() = default;
    explicit BarcodeQRCode(const std::string& code, ErrorCorrectionLevel level = ErrorCorrectionLevel::L, int minVersion = 0)
        : code_(code), level_(level), minVersion_(minVersion) {
        regenerate();
    }
    void setCode(const std::string& code) {
        code_ = code;
        regenerate();
    }
    const std::string& getCode() const { return code_; }
    void setErrorCorrectionLevel(ErrorCorrectionLevel level) {
        level_ = level;
        regenerate();
    }
    /// The symbol version chosen (1..40).
    int getVersion() const { return version_; }
    /// Module matrix including the 4-module quiet zone; true = dark.
    const std::vector<std::vector<bool>>& getMatrix() const { return matrix_; }
    Rectangle getBarcodeSize(float moduleSize = 1) const;
    Rectangle placeBarcode(PdfCanvas& canvas, const ColorPtr& foreground, float moduleSide = 1);
    std::shared_ptr<kernel::PdfFormXObject> createFormXObject(PdfDocument* document, const ColorPtr& foreground = nullptr,
                                                             float moduleSide = 1);

private:
    void regenerate();
    std::string code_;
    ErrorCorrectionLevel level_ = ErrorCorrectionLevel::L;
    int minVersion_ = 0;
    int version_ = 0;
    std::vector<std::vector<bool>> matrix_;
};

/// Low-level QR encoder (port of qrcode.Encoder/MatrixUtil/MaskUtil).
class QrEncoder {
public:
    /// Encodes data bytes into a module matrix (without quiet zone). Mode is selected automatically.
    static std::vector<std::vector<bool>> encode(const std::string& content, ErrorCorrectionLevel level, int minVersion, int& version);
    /// Reed-Solomon EC codewords for data (GF(256), polynomial 0x11D).
    static std::vector<uint8_t> generateEcBytes(const std::vector<uint8_t>& data, int numEcBytes);
};

}  // namespace barcodes
}  // namespace pdfcpp
