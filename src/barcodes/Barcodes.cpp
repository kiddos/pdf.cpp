#include "pdfcpp/barcodes/Barcodes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pdfcpp/io/util/TextUtil.h"

namespace pdfcpp {
namespace barcodes {

#include "BarcodeTables.inc"
#include "QrTables.inc"

using io::TextUtil;

// ============================================================ Barcode1D
float Barcode1D::getDescender() const {
    if (!font_) return 0;
    float sizeCoef = size_ / io::FontProgram::UNITS_NORMALIZATION;
    return font_->getFontProgram()->getFontMetrics().getTypoDescender() * sizeCoef;
}

std::shared_ptr<kernel::PdfFormXObject> Barcode1D::createFormXObject(const ColorPtr& barColor, const ColorPtr& textColor) {
    auto xObject = std::make_shared<kernel::PdfFormXObject>(Rectangle(0, 0, 0, 0));
    PdfCanvas canvas(*xObject, document_);
    Rectangle rect = placeBarcode(canvas, barColor, textColor);
    xObject->setBBox(rect);
    return xObject;
}

void Barcode1D::fitWidth(float width) {
    setX(x_ * width / getBarcodeSize().getWidth());
}

static void drawBars(PdfCanvas& canvas, const std::vector<uint8_t>& bars, float startX, float startY, float x, float n,
                     float barHeight, float inkSpreading, bool wideIsTwo) {
    bool print = true;
    for (uint8_t b : bars) {
        float w = wideIsTwo ? b * x : (b == 0 ? x : x * n);
        if (print) canvas.rectangle(startX, startY, w - inkSpreading, barHeight);
        print = !print;
        startX += w;
    }
    canvas.fill();
}

// ============================================================ Barcode128
Barcode128::Barcode128(PdfDocument* document) : Barcode1D(document) {
    x_ = 0.8f;
    font_ = kernel::PdfFontFactory::createFont();
    size_ = 8;
    baseline_ = size_;
    barHeight_ = size_ * 3;
    textAlignment_ = ALIGN_CENTER;
    codeType_ = CODE128;
}

std::string Barcode128::removeFNC1(const std::string& code) {
    std::string buf;
    for (uint32_t c : TextUtil::utf8ToCodePoints(code))
        if (c >= 32 && c <= 126) buf += static_cast<char>(c);
    return buf;
}

static bool isNextDigits(const std::u32string& text, size_t textIndex, int numDigits) {
    size_t len = text.size();
    while (textIndex < len && numDigits > 0) {
        if (text[textIndex] == Barcode128::FNC1) {
            ++textIndex;
            continue;
        }
        int n = std::min(2, numDigits);
        if (textIndex + n > len) return false;
        while (n-- > 0) {
            char32_t c = text[textIndex++];
            if (c < '0' || c > '9') return false;
            --numDigits;
        }
    }
    return numDigits == 0;
}

static size_t packRawDigits(const std::u32string& text, size_t textIndex, int numDigits, std::vector<int>& out) {
    size_t start = textIndex;
    while (numDigits > 0) {
        if (text[textIndex] == Barcode128::FNC1) {
            out.push_back(Barcode128::FNC1_INDEX);
            ++textIndex;
            continue;
        }
        numDigits -= 2;
        int c1 = static_cast<int>(text[textIndex++] - '0');
        int c2 = static_cast<int>(text[textIndex++] - '0');
        out.push_back(c1 * 10 + c2);
    }
    return textIndex - start;
}

static int startSymbol(Barcode128::Barcode128CodeSet cs) {
    switch (cs) {
        case Barcode128::Barcode128CodeSet::A: return Barcode128::START_A;
        case Barcode128::Barcode128CodeSet::C: return Barcode128::START_C;
        default: return Barcode128::START_B;
    }
}

std::vector<int> Barcode128::getRawText(const std::string& textUtf8, bool ucc, Barcode128CodeSet codeSet) {
    std::u32string text;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(textUtf8)) text += static_cast<char32_t>(cp);
    std::vector<int> out;
    if (text.empty()) {
        out.push_back(startSymbol(codeSet));
        if (ucc) out.push_back(FNC1_INDEX);
        return out;
    }
    for (char32_t c : text)
        if (c > 127 && c != FNC1) throw kernel::PdfException("There are illegal characters for barcode 128 in " + textUtf8);
    char32_t c = text[0];
    int currentCode = startSymbol(codeSet);
    size_t index = 0;
    auto illegal = [&]() { throw kernel::PdfException("There are illegal characters for barcode 128 in " + textUtf8); };
    if ((codeSet == Barcode128CodeSet::AUTO || codeSet == Barcode128CodeSet::C) && isNextDigits(text, index, 2)) {
        currentCode = START_C;
        out.push_back(currentCode);
        if (ucc) out.push_back(FNC1_INDEX);
        index += packRawDigits(text, index, 2, out);
    } else if (c < ' ') {
        currentCode = START_A;
        out.push_back(currentCode);
        if (ucc) out.push_back(FNC1_INDEX);
        out.push_back(static_cast<int>(c + 64));
        ++index;
    } else {
        out.push_back(currentCode);
        if (ucc) out.push_back(FNC1_INDEX);
        if (c == FNC1) out.push_back(FNC1_INDEX);
        else out.push_back(static_cast<int>(c - ' '));
        ++index;
    }
    if (codeSet != Barcode128CodeSet::AUTO && currentCode != startSymbol(codeSet)) illegal();
    while (index < text.size()) {
        switch (currentCode) {
            case START_A: {
                if (codeSet == Barcode128CodeSet::AUTO && isNextDigits(text, index, 4)) {
                    currentCode = START_C;
                    out.push_back(CODE_AB_TO_C);
                    index += packRawDigits(text, index, 4, out);
                } else {
                    c = text[index++];
                    if (c == FNC1) out.push_back(FNC1_INDEX);
                    else if (c > '_') {
                        currentCode = START_B;
                        out.push_back(CODE_AC_TO_B);
                        out.push_back(static_cast<int>(c - ' '));
                    } else if (c < ' ') out.push_back(static_cast<int>(c + 64));
                    else out.push_back(static_cast<int>(c - ' '));
                }
                break;
            }
            case START_B: {
                if (codeSet == Barcode128CodeSet::AUTO && isNextDigits(text, index, 4)) {
                    currentCode = START_C;
                    out.push_back(CODE_AB_TO_C);
                    index += packRawDigits(text, index, 4, out);
                } else {
                    c = text[index++];
                    if (c == FNC1) out.push_back(FNC1_INDEX);
                    else if (c < ' ') {
                        currentCode = START_A;
                        out.push_back(CODE_BC_TO_A);
                        out.push_back(static_cast<int>(c + 64));
                    } else out.push_back(static_cast<int>(c - ' '));
                }
                break;
            }
            case START_C: {
                if (isNextDigits(text, index, 2)) {
                    index += packRawDigits(text, index, 2, out);
                } else {
                    c = text[index++];
                    if (c == FNC1) out.push_back(FNC1_INDEX);
                    else if (c < ' ') {
                        currentCode = START_A;
                        out.push_back(CODE_BC_TO_A);
                        out.push_back(static_cast<int>(c + 64));
                    } else {
                        currentCode = START_B;
                        out.push_back(CODE_AC_TO_B);
                        out.push_back(static_cast<int>(c - ' '));
                    }
                }
                break;
            }
        }
        if (codeSet != Barcode128CodeSet::AUTO && currentCode != startSymbol(codeSet)) illegal();
    }
    return out;
}

std::vector<uint8_t> Barcode128::getBarsCode128Raw(std::vector<int> codes) {
    int chk = codes[0];
    for (size_t k = 1; k < codes.size(); ++k) chk += static_cast<int>(k) * codes[k];
    codes.push_back(chk % 103);
    std::vector<uint8_t> bars;
    for (int code : codes) bars.insert(bars.end(), BARS128[code], BARS128[code] + 6);
    bars.insert(bars.end(), BARS128_STOP, BARS128_STOP + 7);
    return bars;
}

std::string Barcode128::humanReadable() const { return textOrAlt(removeFNC1(code_)); }

Rectangle Barcode128::getBarcodeSize() const {
    float fontX = 0, fontY = 0;
    if (font_) {
        fontY = baseline_ > 0 ? baseline_ - getDescender() : -baseline_ + size_;
        fontX = font_->getWidth(humanReadable(), size_);
    }
    auto raw = getRawText(code_, codeType_ == CODE128_UCC, codeSet_);
    float fullWidth = (raw.size() + 2) * 11 * x_ + 2 * x_;
    return Rectangle(std::max(fullWidth, fontX), barHeight_ + fontY);
}

Rectangle Barcode128::placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) {
    std::string fullCode = humanReadable();
    float fontX = font_ ? font_->getWidth(fullCode, size_) : 0;
    auto raw = getRawText(code_, codeType_ == CODE128_UCC, codeSet_);
    float fullWidth = (raw.size() + 2) * 11 * x_ + 2 * x_;
    float barStartX = 0, textStartX = 0;
    switch (textAlignment_) {
        case ALIGN_LEFT: break;
        case ALIGN_RIGHT:
            if (fontX > fullWidth) barStartX = fontX - fullWidth;
            else textStartX = fullWidth - fontX;
            break;
        default:
            if (fontX > fullWidth) barStartX = (fontX - fullWidth) / 2;
            else textStartX = (fullWidth - fontX) / 2;
            break;
    }
    float barStartY = 0, textStartY = 0;
    if (font_) {
        if (baseline_ <= 0) {
            textStartY = barHeight_ - baseline_;
        } else {
            textStartY = -getDescender();
            barStartY = textStartY + baseline_;
        }
    }
    if (barColor) canvas.setFillColor(barColor);
    drawBars(canvas, getBarsCode128Raw(raw), barStartX, barStartY, x_, n_, barHeight_, inkSpreading_, true);
    if (font_) {
        if (textColor) canvas.setFillColor(textColor);
        canvas.beginText().setFontAndSize(font_, size_).setTextMatrix(textStartX, textStartY).showText(fullCode).endText();
    }
    return getBarcodeSize();
}

// ============================================================ BarcodeEAN
BarcodeEAN::BarcodeEAN(PdfDocument* document) : Barcode1D(document) {
    x_ = 0.8f;
    font_ = kernel::PdfFontFactory::createFont();
    size_ = 8;
    baseline_ = size_;
    barHeight_ = size_ * 3;
    guardBars_ = true;
    codeType_ = EAN13;
}

int BarcodeEAN::calculateEANParity(const std::string& code) {
    int mul = 3, total = 0;
    for (int k = static_cast<int>(code.size()) - 1; k >= 0; --k) {
        int n = code[k] - '0';
        total += mul * n;
        mul ^= 2;
    }
    return (10 - (total % 10)) % 10;
}

namespace {
/// The bar tables are indexed by digit value: reject codes of the wrong length or with non-digit characters
/// (Java fails with an ArrayIndexOutOfBoundsException there).
void checkEanDigits(const std::string& code, size_t expectedLength) {
    bool ok = code.size() == expectedLength;
    for (size_t k = 0; ok && k < code.size(); ++k) ok = code[k] >= '0' && code[k] <= '9';
    if (!ok)
        throw kernel::PdfException("Invalid EAN/UPC code '" + code + "': expected " + std::to_string(expectedLength) +
                                   " digits.");
}
}  // namespace

std::vector<uint8_t> BarcodeEAN::getBarsEAN13(const std::string& codeStr) {
    checkEanDigits(codeStr, 13);
    std::vector<int> code;
    for (char c : codeStr) code.push_back(c - '0');
    std::vector<uint8_t> bars{1, 1, 1};
    const int* sequence = PARITY13[code[0]];
    for (int k = 0; k < 6; ++k) {
        const uint8_t* stripes = BARS_EAN[code[k + 1]];
        if (sequence[k] == 0) bars.insert(bars.end(), {stripes[0], stripes[1], stripes[2], stripes[3]});
        else bars.insert(bars.end(), {stripes[3], stripes[2], stripes[1], stripes[0]});
    }
    bars.insert(bars.end(), {1, 1, 1, 1, 1});
    for (int k = 7; k < 13; ++k) {
        const uint8_t* stripes = BARS_EAN[code[k]];
        bars.insert(bars.end(), stripes, stripes + 4);
    }
    bars.insert(bars.end(), {1, 1, 1});
    return bars;
}

std::vector<uint8_t> BarcodeEAN::getBarsEAN8(const std::string& codeStr) {
    checkEanDigits(codeStr, 8);
    std::vector<uint8_t> bars{1, 1, 1};
    for (int k = 0; k < 4; ++k) {
        const uint8_t* stripes = BARS_EAN[codeStr[k] - '0'];
        bars.insert(bars.end(), stripes, stripes + 4);
    }
    bars.insert(bars.end(), {1, 1, 1, 1, 1});
    for (int k = 4; k < 8; ++k) {
        const uint8_t* stripes = BARS_EAN[codeStr[k] - '0'];
        bars.insert(bars.end(), stripes, stripes + 4);
    }
    bars.insert(bars.end(), {1, 1, 1});
    return bars;
}

Rectangle BarcodeEAN::getBarcodeSize() const {
    float width = 0, height = barHeight_;
    if (font_) height += baseline_ > 0 ? baseline_ - getDescender() : -baseline_ + size_;
    switch (codeType_) {
        case EAN13:
            checkEanDigits(code_, 13);
            width = x_ * (11 + 12 * 7);
            if (font_) width += font_->getWidth(std::string(1, code_[0]), size_);
            break;
        case EAN8:
            checkEanDigits(code_, 8);
            width = x_ * (11 + 8 * 7);
            break;
        case UPCA:
            checkEanDigits(code_, 12);
            width = x_ * (11 + 12 * 7);
            if (font_)
                width += font_->getWidth(std::string(1, code_[0]), size_) + font_->getWidth(std::string(1, code_.back()), size_);
            break;
        default:
            throw kernel::PdfException("Invalid code type");
    }
    return Rectangle(width, height);
}

Rectangle BarcodeEAN::placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) {
    Rectangle rect = getBarcodeSize();
    float barStartX = 0, barStartY = 0, textStartY = 0;
    if (font_) {
        if (baseline_ <= 0) {
            textStartY = barHeight_ - baseline_;
        } else {
            textStartY = -getDescender();
            barStartY = textStartY + baseline_;
        }
    }
    if ((codeType_ == EAN13 || codeType_ == UPCA) && font_)
        barStartX += font_->getWidth(std::string(1, code_[0]), size_);
    std::vector<uint8_t> bars;
    const int* guard = nullptr;
    int guardLen = 0;
    switch (codeType_) {
        case EAN13:
            bars = getBarsEAN13(code_);
            guard = GUARD_EAN13;
            guardLen = 6;
            break;
        case EAN8:
            bars = getBarsEAN8(code_);
            guard = GUARD_EAN8;
            guardLen = 6;
            break;
        case UPCA:
            bars = getBarsEAN13("0" + code_);
            guard = GUARD_UPCA;
            guardLen = static_cast<int>(sizeof(GUARD_UPCA) / sizeof(int));
            break;
    }
    float keepBarX = barStartX;
    bool print = true;
    float gd = (font_ && baseline_ > 0 && guardBars_) ? baseline_ / 2 : 0;
    if (barColor) canvas.setFillColor(barColor);
    for (size_t k = 0; k < bars.size(); ++k) {
        float w = bars[k] * x_;
        if (print) {
            if (std::binary_search(guard, guard + guardLen, static_cast<int>(k)))
                canvas.rectangle(barStartX, barStartY - gd, w - inkSpreading_, barHeight_ + gd);
            else
                canvas.rectangle(barStartX, barStartY, w - inkSpreading_, barHeight_);
        }
        print = !print;
        barStartX += w;
    }
    canvas.fill();
    if (font_) {
        if (textColor) canvas.setFillColor(textColor);
        canvas.beginText().setFontAndSize(font_, size_);
        auto put = [&](char ch, float px) {
            canvas.setTextMatrix(px, textStartY);
            canvas.showText(std::string(1, ch));
        };
        switch (codeType_) {
            case EAN13:
                put(code_[0], 0);
                for (int k = 1; k < 13; ++k) {
                    float len = font_->getWidth(std::string(1, code_[k]), size_);
                    put(code_[k], keepBarX + TEXTPOS_EAN13[k - 1] * x_ - len / 2);
                }
                break;
            case EAN8:
                for (int k = 0; k < 8; ++k) {
                    float len = font_->getWidth(std::string(1, code_[k]), size_);
                    put(code_[k], TEXTPOS_EAN8[k] * x_ - len / 2);
                }
                break;
            case UPCA:
                put(code_[0], 0);
                for (int k = 1; k < 11; ++k) {
                    float len = font_->getWidth(std::string(1, code_[k]), size_);
                    put(code_[k], keepBarX + TEXTPOS_EAN13[k] * x_ - len / 2);
                }
                put(code_[11], keepBarX + x_ * (11 + 12 * 7));
                break;
        }
        canvas.endText();
    }
    return rect;
}

// ============================================================ Barcode39
Barcode39::Barcode39(PdfDocument* document) : Barcode1D(document) {
    x_ = 0.8f;
    n_ = 2;
    font_ = kernel::PdfFontFactory::createFont();
    size_ = 8;
    baseline_ = size_;
    barHeight_ = size_ * 3;
    textAlignment_ = ALIGN_CENTER;
    startStopText_ = true;
}

std::vector<uint8_t> Barcode39::getBarsCode39(const std::string& text) {
    std::string t = "*" + text + "*";
    std::vector<uint8_t> bars;
    for (size_t k = 0; k < t.size(); ++k) {
        const char* p = std::strchr(CHARS39, t[k]);
        if (!p || t[k] == '\0') throw kernel::PdfException("Illegal character in Barcode39: " + std::string(1, t[k]));
        int idx = static_cast<int>(p - CHARS39);
        bars.insert(bars.end(), BARS39[idx], BARS39[idx] + 9);
        if (k + 1 < t.size()) bars.push_back(0);
    }
    return bars;
}

std::string Barcode39::getCode39Ex(const std::string& text) {
    std::string out;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c > 127) throw kernel::PdfException("Illegal character in Barcode39 extended");
        char c1 = EXTENDED39[c * 2], c2 = EXTENDED39[c * 2 + 1];
        if (c1 != ' ') out += c1;
        out += c2;
    }
    return out;
}

char Barcode39::getChecksum(const std::string& text) {
    int chk = 0;
    for (char c : text) {
        const char* p = std::strchr(CHARS39, c);
        if (!p) throw kernel::PdfException("Illegal character in Barcode39");
        chk += static_cast<int>(p - CHARS39);
    }
    return CHARS39[chk % 43];
}

std::string Barcode39::barcodeText() const {
    std::string bCode = extended_ ? getCode39Ex(code_) : code_;
    if (generateChecksum_) bCode += getChecksum(bCode);
    return bCode;
}

Rectangle Barcode39::getBarcodeSize() const {
    float fontX = 0, fontY = 0;
    std::string fCode = code_;
    if (generateChecksum_ && checksumText_) fCode += getChecksum(extended_ ? getCode39Ex(code_) : code_);
    if (startStopText_) fCode = "*" + fCode + "*";
    if (font_) {
        fontY = baseline_ > 0 ? baseline_ - getDescender() : -baseline_ + size_;
        fontX = font_->getWidth(textOrAlt(fCode), size_);
    }
    size_t len = barcodeText().size() + 2;
    float fullWidth = len * (6 * x_ + 3 * x_ * n_) + (len - 1) * x_;
    return Rectangle(std::max(fullWidth, fontX), barHeight_ + fontY);
}

Rectangle Barcode39::placeBarcode(PdfCanvas& canvas, const ColorPtr& barColor, const ColorPtr& textColor) {
    std::string fullCode = code_;
    std::string bCode = barcodeText();
    if (generateChecksum_ && checksumText_) fullCode += bCode.back();
    if (startStopText_) fullCode = "*" + fullCode + "*";
    fullCode = textOrAlt(fullCode);
    float fontX = font_ ? font_->getWidth(fullCode, size_) : 0;
    size_t len = bCode.size() + 2;
    float fullWidth = len * (6 * x_ + 3 * x_ * n_) + (len - 1) * x_;
    float barStartX = 0, textStartX = 0;
    switch (textAlignment_) {
        case ALIGN_LEFT: break;
        case ALIGN_RIGHT:
            if (fontX > fullWidth) barStartX = fontX - fullWidth;
            else textStartX = fullWidth - fontX;
            break;
        default:
            if (fontX > fullWidth) barStartX = (fontX - fullWidth) / 2;
            else textStartX = (fullWidth - fontX) / 2;
            break;
    }
    float barStartY = 0, textStartY = 0;
    if (font_) {
        if (baseline_ <= 0) textStartY = barHeight_ - baseline_;
        else {
            textStartY = -getDescender();
            barStartY = textStartY + baseline_;
        }
    }
    if (barColor) canvas.setFillColor(barColor);
    drawBars(canvas, getBarsCode39(bCode), barStartX, barStartY, x_, n_, barHeight_, inkSpreading_, false);
    if (font_) {
        if (textColor) canvas.setFillColor(textColor);
        canvas.beginText().setFontAndSize(font_, size_).setTextMatrix(textStartX, textStartY).showText(fullCode).endText();
    }
    return getBarcodeSize();
}

// ============================================================ QR encoder
namespace {
struct Gf256 {
    int exp[512];
    int log[256];
    Gf256() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = x;
            log[x] = i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    int mul(int a, int b) const { return (a == 0 || b == 0) ? 0 : exp[log[a] + log[b]]; }
};
const Gf256& gf() {
    static Gf256 g;
    return g;
}

class BitBuffer {
public:
    void append(uint32_t value, int numBits) {
        for (int i = numBits - 1; i >= 0; --i) bits.push_back(((value >> i) & 1) != 0);
    }
    size_t size() const { return bits.size(); }
    std::vector<bool> bits;
};

int findMSBSet(uint32_t v) {
    int n = 0;
    while (v) {
        ++n;
        v >>= 1;
    }
    return n;
}

uint32_t bchCode(uint32_t value, uint32_t poly) {
    int msbSet = findMSBSet(poly);
    value <<= msbSet - 1;
    while (findMSBSet(value) >= msbSet) value ^= poly << (findMSBSet(value) - msbSet);
    return value;
}

const char* ALPHANUMERIC = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

int charCountBits(int mode, int version) {
    int idx = version <= 9 ? 0 : (version <= 26 ? 1 : 2);
    switch (mode) {
        case 1: { static const int b[] = {10, 12, 14}; return b[idx]; }
        case 2: { static const int b[] = {9, 11, 13}; return b[idx]; }
        default: { static const int b[] = {8, 16, 16}; return b[idx]; }
    }
}

using Matrix = std::vector<std::vector<int>>;  // -1 empty, 0 light, 1 dark

void embedFinder(Matrix& m, int x, int y) {
    for (int dy = -1; dy <= 7; ++dy)
        for (int dx = -1; dx <= 7; ++dx) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || yy >= static_cast<int>(m.size()) || xx >= static_cast<int>(m.size())) continue;
            bool dark = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                        (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
            m[yy][xx] = dark ? 1 : 0;
        }
}

void buildFunctionPatterns(Matrix& m, int version) {
    int size = static_cast<int>(m.size());
    embedFinder(m, 0, 0);
    embedFinder(m, size - 7, 0);
    embedFinder(m, 0, size - 7);
    const QrVersion& v = QR_VERSIONS[version - 1];
    for (int a = 0; a < v.numAlign; ++a)
        for (int b = 0; b < v.numAlign; ++b) {
            int cy = v.alignment[a], cx = v.alignment[b];
            if (m[cy][cx] != -1) continue;  // overlaps a finder pattern
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    m[cy + dy][cx + dx] = (std::max(std::abs(dx), std::abs(dy)) != 1) ? 1 : 0;
        }
    for (int i = 8; i < size - 8; ++i) {
        if (m[6][i] == -1) m[6][i] = (i % 2 == 0) ? 1 : 0;
        if (m[i][6] == -1) m[i][6] = (i % 2 == 0) ? 1 : 0;
    }
    m[size - 8][8] = 1;  // dark module
    // reserve format info
    for (int i = 0; i <= 8; ++i) {
        if (m[8][i] == -1) m[8][i] = 0;
        if (m[i][8] == -1) m[i][8] = 0;
    }
    for (int i = 0; i < 8; ++i) {
        if (m[8][size - 1 - i] == -1) m[8][size - 1 - i] = 0;
        if (m[size - 1 - i][8] == -1) m[size - 1 - i][8] = 0;
    }
    if (version >= 7) {
        uint32_t versionBits = (static_cast<uint32_t>(version) << 12) | bchCode(version, 0x1F25);
        int bitIndex = 17;
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 3; ++j) {
                // zxing: versionInfoBits.at(bitIndex) where index 0 is the MSB
                int bit = (versionBits >> (17 - bitIndex)) & 1;
                --bitIndex;
                m[size - 11 + j][i] = bit;
                m[i][size - 11 + j] = bit;
            }
    }
}

bool maskBit(int mask, int x, int y) {
    int temp;
    switch (mask) {
        case 0: return ((y + x) & 1) == 0;
        case 1: return (y & 1) == 0;
        case 2: return x % 3 == 0;
        case 3: return (y + x) % 3 == 0;
        case 4: return (((y >> 1) + (x / 3)) & 1) == 0;
        case 5: temp = y * x; return ((temp & 1) + (temp % 3)) == 0;
        case 6: temp = y * x; return (((temp & 1) + (temp % 3)) & 1) == 0;
        default: temp = y * x; return (((temp % 3) + ((y + x) & 1)) & 1) == 0;
    }
}

void embedFormat(Matrix& m, int ecBits, int mask) {
    static const int COORDS[15][2] = {{8, 0}, {8, 1}, {8, 2}, {8, 3}, {8, 4}, {8, 5}, {8, 7}, {8, 8},
                                      {7, 8}, {5, 8}, {4, 8}, {3, 8}, {2, 8}, {1, 8}, {0, 8}};
    int size = static_cast<int>(m.size());
    uint32_t data = (static_cast<uint32_t>(ecBits) << 3) | static_cast<uint32_t>(mask);
    uint32_t bits = ((data << 10) | bchCode(data, 0x537)) ^ 0x5412;
    for (int i = 0; i < 15; ++i) {
        int bit = (bits >> i) & 1;  // LSB first
        m[COORDS[i][1]][COORDS[i][0]] = bit;
        if (i < 8) m[8][size - i - 1] = bit;
        else m[size - 7 + (i - 8)][8] = bit;
    }
}

void embedData(Matrix& m, const std::vector<bool>& dataBits, int mask) {
    int size = static_cast<int>(m.size());
    size_t bitIndex = 0;
    int direction = -1;
    int x = size - 1, y = size - 1;
    while (x > 0) {
        if (x == 6) x -= 1;
        while (y >= 0 && y < size) {
            for (int i = 0; i < 2; ++i) {
                int xx = x - i;
                if (m[y][xx] != -1) continue;
                bool bit = bitIndex < dataBits.size() ? dataBits[bitIndex++] : false;
                if (maskBit(mask, xx, y)) bit = !bit;
                m[y][xx] = bit ? 1 : 0;
            }
            y += direction;
        }
        direction = -direction;
        y += direction;
        x -= 2;
    }
}

// Port of MaskUtil (same quirks as iText's zxing-derived implementation, so mask choice matches).
int penalty(const Matrix& m) {
    int size = static_cast<int>(m.size());
    int total = 0;
    // rule 1 (prevBit deliberately carries over between rows, as in iText)
    for (int pass = 0; pass < 2; ++pass) {
        int numSameBitCells = 0, prevBit = -1;
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                int bit = pass == 0 ? m[i][j] : m[j][i];
                if (bit == prevBit) {
                    numSameBitCells += 1;
                    if (numSameBitCells == 5) total += 3;
                    else if (numSameBitCells > 5) total += 1;
                } else {
                    numSameBitCells = 1;
                    prevBit = bit;
                }
            }
            numSameBitCells = 0;
        }
    }
    // rule 2
    for (int y = 0; y < size - 1; ++y)
        for (int x = 0; x < size - 1; ++x) {
            int v = m[y][x];
            if (v == m[y][x + 1] && v == m[y + 1][x] && v == m[y + 1][x + 1]) total += 3;
        }
    // rule 3
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            auto h = [&](int k) { return m[y][x + k]; };
            if (x + 6 < size && h(0) == 1 && h(1) == 0 && h(2) == 1 && h(3) == 1 && h(4) == 1 && h(5) == 0 && h(6) == 1 &&
                ((x + 10 < size && h(7) == 0 && h(8) == 0 && h(9) == 0 && h(10) == 0) ||
                 (x - 4 >= 0 && h(-1) == 0 && h(-2) == 0 && h(-3) == 0 && h(-4) == 0)))
                total += 40;
            auto v = [&](int k) { return m[y + k][x]; };
            if (y + 6 < size && v(0) == 1 && v(1) == 0 && v(2) == 1 && v(3) == 1 && v(4) == 1 && v(5) == 0 && v(6) == 1 &&
                ((y + 10 < size && v(7) == 0 && v(8) == 0 && v(9) == 0 && v(10) == 0) ||
                 (y - 4 >= 0 && v(-1) == 0 && v(-2) == 0 && v(-3) == 0 && v(-4) == 0)))
                total += 40;
        }
    // rule 4
    int dark = 0;
    for (const auto& row : m)
        for (int c : row) dark += c;
    double darkRatio = static_cast<double>(dark) / (size * size);
    total += std::abs(static_cast<int>(darkRatio * 100 - 50)) / 5 * 10;
    return total;
}
}  // namespace

std::vector<uint8_t> QrEncoder::generateEcBytes(const std::vector<uint8_t>& data, int numEcBytes) {
    const Gf256& g = gf();
    // generator polynomial
    std::vector<int> gen{1};
    for (int i = 0; i < numEcBytes; ++i) {
        std::vector<int> next(gen.size() + 1, 0);
        for (size_t j = 0; j < gen.size(); ++j) {
            next[j] ^= gen[j];
            next[j + 1] ^= g.mul(gen[j], g.exp[i]);
        }
        gen = next;
    }
    std::vector<int> rem(numEcBytes, 0);
    for (uint8_t d : data) {
        int factor = d ^ rem[0];
        rem.erase(rem.begin());
        rem.push_back(0);
        for (int j = 0; j < numEcBytes; ++j) rem[j] ^= g.mul(gen[j + 1], factor);
    }
    return std::vector<uint8_t>(rem.begin(), rem.end());
}

std::vector<std::vector<bool>> QrEncoder::encode(const std::string& content, ErrorCorrectionLevel level, int minVersion,
                                                 int& versionOut) {
    // mode selection
    bool numeric = !content.empty(), alnum = !content.empty();
    for (char c : content) {
        if (c < '0' || c > '9') numeric = false;
        if (!std::strchr(ALPHANUMERIC, c) || c == '\0') alnum = false;
    }
    int mode = numeric ? 1 : (alnum ? 2 : 4);
    Bytes bytes;
    bool needsEci = false;
    if (mode == 4) {
        auto cps = TextUtil::utf8ToCodePoints(content);
        bool latin1 = std::all_of(cps.begin(), cps.end(), [](uint32_t c) { return c < 256; });
        if (latin1) for (uint32_t c : cps) bytes.push_back(static_cast<uint8_t>(c));
        else {
            bytes.assign(content.begin(), content.end());
            needsEci = true;  // UTF-8 (ECI 26)
        }
    }
    int levelIdx = static_cast<int>(level);
    int version = 0;
    BitBuffer bits;
    for (int v = std::max(1, minVersion); v <= 40; ++v) {
        const QrEcBlocks& ec = QR_VERSIONS[v - 1].ec[levelIdx];
        int dataCodewords = ec.blocks[0].count * ec.blocks[0].dataCodewords + ec.blocks[1].count * ec.blocks[1].dataCodewords;
        size_t count = mode == 4 ? bytes.size() : content.size();
        size_t needed = (needsEci ? 12 : 0) + 4 + charCountBits(mode, v);
        if (mode == 1) needed += 10 * (count / 3) + (count % 3 == 2 ? 7 : (count % 3 == 1 ? 4 : 0));
        else if (mode == 2) needed += 11 * (count / 2) + 6 * (count % 2);
        else needed += 8 * count;
        if ((needed + 7) / 8 <= static_cast<size_t>(dataCodewords)) {
            version = v;
            break;
        }
    }
    if (version == 0) throw kernel::PdfException("Data too big for a QR code");
    versionOut = version;
    const QrEcBlocks& ec = QR_VERSIONS[version - 1].ec[levelIdx];
    int dataCodewords = ec.blocks[0].count * ec.blocks[0].dataCodewords + ec.blocks[1].count * ec.blocks[1].dataCodewords;
    if (needsEci) {
        bits.append(0x7, 4);
        bits.append(26, 8);
    }
    bits.append(static_cast<uint32_t>(mode), 4);
    if (mode == 1) {
        bits.append(static_cast<uint32_t>(content.size()), charCountBits(1, version));
        for (size_t i = 0; i < content.size(); i += 3) {
            size_t n = std::min<size_t>(3, content.size() - i);
            uint32_t v = static_cast<uint32_t>(std::stoi(content.substr(i, n)));
            bits.append(v, n == 3 ? 10 : (n == 2 ? 7 : 4));
        }
    } else if (mode == 2) {
        bits.append(static_cast<uint32_t>(content.size()), charCountBits(2, version));
        auto code = [](char c) { return static_cast<uint32_t>(std::strchr(ALPHANUMERIC, c) - ALPHANUMERIC); };
        for (size_t i = 0; i < content.size(); i += 2) {
            if (i + 1 < content.size()) bits.append(code(content[i]) * 45 + code(content[i + 1]), 11);
            else bits.append(code(content[i]), 6);
        }
    } else {
        bits.append(static_cast<uint32_t>(bytes.size()), charCountBits(4, version));
        for (uint8_t b : bytes) bits.append(b, 8);
    }
    size_t capacity = static_cast<size_t>(dataCodewords) * 8;
    for (int i = 0; i < 4 && bits.size() < capacity; ++i) bits.bits.push_back(false);
    while (bits.size() % 8) bits.bits.push_back(false);
    std::vector<uint8_t> data;
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (int k = 0; k < 8; ++k) b = static_cast<uint8_t>((b << 1) | (bits.bits[i + k] ? 1 : 0));
        data.push_back(b);
    }
    for (bool pad = true; static_cast<int>(data.size()) < dataCodewords; pad = !pad) data.push_back(pad ? 0xEC : 0x11);
    // split into blocks
    std::vector<std::vector<uint8_t>> dataBlocks, ecBlocks;
    size_t offset = 0;
    for (const auto& group : ec.blocks) {
        for (int b = 0; b < group.count; ++b) {
            std::vector<uint8_t> block(data.begin() + offset, data.begin() + offset + group.dataCodewords);
            offset += group.dataCodewords;
            ecBlocks.push_back(generateEcBytes(block, ec.ecCodewordsPerBlock));
            dataBlocks.push_back(std::move(block));
        }
    }
    std::vector<uint8_t> interleaved;
    size_t maxData = 0;
    for (const auto& b : dataBlocks) maxData = std::max(maxData, b.size());
    for (size_t i = 0; i < maxData; ++i)
        for (const auto& b : dataBlocks)
            if (i < b.size()) interleaved.push_back(b[i]);
    for (int i = 0; i < ec.ecCodewordsPerBlock; ++i)
        for (const auto& b : ecBlocks) interleaved.push_back(b[i]);
    std::vector<bool> dataBits;
    for (uint8_t b : interleaved)
        for (int k = 7; k >= 0; --k) dataBits.push_back(((b >> k) & 1) != 0);
    // matrix with the best mask
    int size = 17 + 4 * version;
    static const int EC_BITS[4] = {1, 0, 3, 2};  // L, M, Q, H
    Matrix best;
    int bestPenalty = -1;
    for (int mask = 0; mask < 8; ++mask) {
        Matrix m(size, std::vector<int>(size, -1));
        buildFunctionPatterns(m, version);
        embedFormat(m, EC_BITS[levelIdx], mask);
        embedData(m, dataBits, mask);
        int p = penalty(m);
        if (bestPenalty < 0 || p < bestPenalty) {
            bestPenalty = p;
            best = std::move(m);
        }
    }
    std::vector<std::vector<bool>> result(size, std::vector<bool>(size));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) result[y][x] = best[y][x] == 1;
    return result;
}

// ============================================================ BarcodeQRCode
void BarcodeQRCode::regenerate() {
    if (code_.empty()) {
        matrix_.clear();
        return;
    }
    auto m = QrEncoder::encode(code_, level_, minVersion_, version_);
    const int quiet = 4;
    int size = static_cast<int>(m.size()) + 2 * quiet;
    matrix_.assign(size, std::vector<bool>(size, false));
    for (size_t y = 0; y < m.size(); ++y)
        for (size_t x = 0; x < m.size(); ++x) matrix_[y + quiet][x + quiet] = m[y][x];
}

Rectangle BarcodeQRCode::getBarcodeSize(float moduleSize) const {
    float n = static_cast<float>(matrix_.size());
    return Rectangle(0, 0, n * moduleSize, n * moduleSize);
}

Rectangle BarcodeQRCode::placeBarcode(PdfCanvas& canvas, const ColorPtr& foreground, float moduleSide) {
    int height = static_cast<int>(matrix_.size());
    if (foreground) canvas.setFillColor(foreground);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < height; ++x)
            if (matrix_[y][x]) canvas.rectangle(x * moduleSide, (height - y - 1) * moduleSide, moduleSide, moduleSide);
    canvas.fill();
    return getBarcodeSize(moduleSide);
}

std::shared_ptr<kernel::PdfFormXObject> BarcodeQRCode::createFormXObject(PdfDocument* document, const ColorPtr& foreground,
                                                                        float moduleSide) {
    auto xObject = std::make_shared<kernel::PdfFormXObject>(Rectangle(0, 0, 0, 0));
    PdfCanvas canvas(*xObject, document);
    xObject->setBBox(placeBarcode(canvas, foreground, moduleSide));
    return xObject;
}

}  // namespace barcodes
}  // namespace pdfcpp
