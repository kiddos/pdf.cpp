#include "pdfcpp/barcodes/barcodes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pdfcpp/io/util/text_util.h"

namespace pdfcpp {
namespace barcodes {

#include "barcode_tables.inc"
#include "qr_tables.inc"

using io::TextUtil;

// ============================================================ Barcode1D
float Barcode1D::GetDescender() const {
  if (!font_) return 0;
  float size_coef = size_ / io::FontProgram::kUnitsNormalization;
  return font_->GetFontProgram()->GetFontMetrics().GetTypoDescender() *
         size_coef;
}

std::shared_ptr<kernel::PdfFormXObject> Barcode1D::CreateFormXObject(
    const ColorPtr& bar_color, const ColorPtr& text_color) {
  auto x_object =
      std::make_shared<kernel::PdfFormXObject>(Rectangle(0, 0, 0, 0));
  PdfCanvas canvas(*x_object, document_);
  Rectangle rect = PlaceBarcode(canvas, bar_color, text_color);
  x_object->SetBBox(rect);
  return x_object;
}

void Barcode1D::FitWidth(float width) {
  SetX(x_ * width / GetBarcodeSize().GetWidth());
}

static void DrawBars(PdfCanvas& canvas, const std::vector<uint8_t>& bars,
                     float start_x, float start_y, float x, float n,
                     float bar_height, float ink_spreading, bool wide_is_two) {
  bool print = true;
  for (uint8_t b : bars) {
    float w = wide_is_two ? b * x : (b == 0 ? x : x * n);
    if (print)
      canvas.Rectangle(start_x, start_y, w - ink_spreading, bar_height);
    print = !print;
    start_x += w;
  }
  canvas.Fill();
}

// ============================================================ Barcode128
Barcode128::Barcode128(PdfDocument* document) : Barcode1D(document) {
  x_ = 0.8f;
  font_ = kernel::PdfFontFactory::CreateFont();
  size_ = 8;
  baseline_ = size_;
  bar_height_ = size_ * 3;
  text_alignment_ = kAlignCenter;
  code_type_ = kCode128;
}

std::string Barcode128::RemoveFnc1(const std::string& code) {
  std::string buf;
  for (uint32_t c : TextUtil::Utf8ToCodePoints(code))
    if (c >= 32 && c <= 126) buf += static_cast<char>(c);
  return buf;
}

static bool IsNextDigits(const std::u32string& text, size_t text_index,
                         int num_digits) {
  size_t len = text.size();
  while (text_index < len && num_digits > 0) {
    if (text[text_index] == Barcode128::kFnc1) {
      ++text_index;
      continue;
    }
    int n = std::min(2, num_digits);
    if (text_index + n > len) return false;
    while (n-- > 0) {
      char32_t c = text[text_index++];
      if (c < '0' || c > '9') return false;
      --num_digits;
    }
  }
  return num_digits == 0;
}

static size_t PackRawDigits(const std::u32string& text, size_t text_index,
                            int num_digits, std::vector<int>& out) {
  size_t start = text_index;
  while (num_digits > 0) {
    if (text[text_index] == Barcode128::kFnc1) {
      out.push_back(Barcode128::kFnc1Index);
      ++text_index;
      continue;
    }
    num_digits -= 2;
    int c1 = static_cast<int>(text[text_index++] - '0');
    int c2 = static_cast<int>(text[text_index++] - '0');
    out.push_back(c1 * 10 + c2);
  }
  return text_index - start;
}

static int StartSymbol(Barcode128::Barcode128CodeSet cs) {
  switch (cs) {
    case Barcode128::Barcode128CodeSet::kA:
      return Barcode128::kStartA;
    case Barcode128::Barcode128CodeSet::kC:
      return Barcode128::kStartC;
    default:
      return Barcode128::kStartB;
  }
}

std::vector<int> Barcode128::GetRawText(const std::string& text_utf8, bool ucc,
                                        Barcode128CodeSet code_set) {
  std::u32string text;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(text_utf8))
    text += static_cast<char32_t>(cp);
  std::vector<int> out;
  if (text.empty()) {
    out.push_back(StartSymbol(code_set));
    if (ucc) out.push_back(kFnc1Index);
    return out;
  }
  for (char32_t c : text)
    if (c > 127 && c != kFnc1)
      throw kernel::PdfException(
          "There are illegal characters for barcode 128 in " + text_utf8);
  char32_t c = text[0];
  int current_code = StartSymbol(code_set);
  size_t index = 0;
  auto illegal = [&]() {
    throw kernel::PdfException(
        "There are illegal characters for barcode 128 in " + text_utf8);
  };
  if ((code_set == Barcode128CodeSet::kAuto ||
       code_set == Barcode128CodeSet::kC) &&
      IsNextDigits(text, index, 2)) {
    current_code = kStartC;
    out.push_back(current_code);
    if (ucc) out.push_back(kFnc1Index);
    index += PackRawDigits(text, index, 2, out);
  } else if (c < ' ') {
    current_code = kStartA;
    out.push_back(current_code);
    if (ucc) out.push_back(kFnc1Index);
    out.push_back(static_cast<int>(c + 64));
    ++index;
  } else {
    out.push_back(current_code);
    if (ucc) out.push_back(kFnc1Index);
    if (c == kFnc1)
      out.push_back(kFnc1Index);
    else
      out.push_back(static_cast<int>(c - ' '));
    ++index;
  }
  if (code_set != Barcode128CodeSet::kAuto &&
      current_code != StartSymbol(code_set))
    illegal();
  while (index < text.size()) {
    switch (current_code) {
      case kStartA: {
        if (code_set == Barcode128CodeSet::kAuto &&
            IsNextDigits(text, index, 4)) {
          current_code = kStartC;
          out.push_back(kCodeAbToC);
          index += PackRawDigits(text, index, 4, out);
        } else {
          c = text[index++];
          if (c == kFnc1)
            out.push_back(kFnc1Index);
          else if (c > '_') {
            current_code = kStartB;
            out.push_back(kCodeAcToB);
            out.push_back(static_cast<int>(c - ' '));
          } else if (c < ' ')
            out.push_back(static_cast<int>(c + 64));
          else
            out.push_back(static_cast<int>(c - ' '));
        }
        break;
      }
      case kStartB: {
        if (code_set == Barcode128CodeSet::kAuto &&
            IsNextDigits(text, index, 4)) {
          current_code = kStartC;
          out.push_back(kCodeAbToC);
          index += PackRawDigits(text, index, 4, out);
        } else {
          c = text[index++];
          if (c == kFnc1)
            out.push_back(kFnc1Index);
          else if (c < ' ') {
            current_code = kStartA;
            out.push_back(kCodeBcToA);
            out.push_back(static_cast<int>(c + 64));
          } else
            out.push_back(static_cast<int>(c - ' '));
        }
        break;
      }
      case kStartC: {
        if (IsNextDigits(text, index, 2)) {
          index += PackRawDigits(text, index, 2, out);
        } else {
          c = text[index++];
          if (c == kFnc1)
            out.push_back(kFnc1Index);
          else if (c < ' ') {
            current_code = kStartA;
            out.push_back(kCodeBcToA);
            out.push_back(static_cast<int>(c + 64));
          } else {
            current_code = kStartB;
            out.push_back(kCodeAcToB);
            out.push_back(static_cast<int>(c - ' '));
          }
        }
        break;
      }
    }
    if (code_set != Barcode128CodeSet::kAuto &&
        current_code != StartSymbol(code_set))
      illegal();
  }
  return out;
}

std::vector<uint8_t> Barcode128::GetBarsCode128Raw(std::vector<int> codes) {
  int chk = codes[0];
  for (size_t k = 1; k < codes.size(); ++k)
    chk += static_cast<int>(k) * codes[k];
  codes.push_back(chk % 103);
  std::vector<uint8_t> bars;
  for (int code : codes)
    bars.insert(bars.end(), kBars128[code], kBars128[code] + 6);
  bars.insert(bars.end(), kBars128Stop, kBars128Stop + 7);
  return bars;
}

std::string Barcode128::HumanReadable() const {
  return TextOrAlt(RemoveFnc1(code_));
}

Rectangle Barcode128::GetBarcodeSize() const {
  float font_x = 0, font_y = 0;
  if (font_) {
    font_y = baseline_ > 0 ? baseline_ - GetDescender() : -baseline_ + size_;
    font_x = font_->GetWidth(HumanReadable(), size_);
  }
  auto raw = GetRawText(code_, code_type_ == kCode128Ucc, code_set_);
  float full_width = (raw.size() + 2) * 11 * x_ + 2 * x_;
  return Rectangle(std::max(full_width, font_x), bar_height_ + font_y);
}

Rectangle Barcode128::PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                                   const ColorPtr& text_color) {
  std::string full_code = HumanReadable();
  float font_x = font_ ? font_->GetWidth(full_code, size_) : 0;
  auto raw = GetRawText(code_, code_type_ == kCode128Ucc, code_set_);
  float full_width = (raw.size() + 2) * 11 * x_ + 2 * x_;
  float bar_start_x = 0, text_start_x = 0;
  switch (text_alignment_) {
    case kAlignLeft:
      break;
    case kAlignRight:
      if (font_x > full_width)
        bar_start_x = font_x - full_width;
      else
        text_start_x = full_width - font_x;
      break;
    default:
      if (font_x > full_width)
        bar_start_x = (font_x - full_width) / 2;
      else
        text_start_x = (full_width - font_x) / 2;
      break;
  }
  float bar_start_y = 0, text_start_y = 0;
  if (font_) {
    if (baseline_ <= 0) {
      text_start_y = bar_height_ - baseline_;
    } else {
      text_start_y = -GetDescender();
      bar_start_y = text_start_y + baseline_;
    }
  }
  if (bar_color) canvas.SetFillColor(bar_color);
  DrawBars(canvas, GetBarsCode128Raw(raw), bar_start_x, bar_start_y, x_, n_,
           bar_height_, ink_spreading_, true);
  if (font_) {
    if (text_color) canvas.SetFillColor(text_color);
    canvas.BeginText()
        .SetFontAndSize(font_, size_)
        .SetTextMatrix(text_start_x, text_start_y)
        .ShowText(full_code)
        .EndText();
  }
  return GetBarcodeSize();
}

// ============================================================ BarcodeEAN
BarcodeEAN::BarcodeEAN(PdfDocument* document) : Barcode1D(document) {
  x_ = 0.8f;
  font_ = kernel::PdfFontFactory::CreateFont();
  size_ = 8;
  baseline_ = size_;
  bar_height_ = size_ * 3;
  guard_bars_ = true;
  code_type_ = kEan13;
}

int BarcodeEAN::CalculateEanParity(const std::string& code) {
  int mul = 3, total = 0;
  for (int k = static_cast<int>(code.size()) - 1; k >= 0; --k) {
    int n = code[k] - '0';
    total += mul * n;
    mul ^= 2;
  }
  return (10 - (total % 10)) % 10;
}

namespace {
/// The bar tables are indexed by digit value: reject codes of the wrong length
/// or with non-digit characters (Java fails with an
/// ArrayIndexOutOfBoundsException there).
void CheckEanDigits(const std::string& code, size_t expected_length) {
  bool ok = code.size() == expected_length;
  for (size_t k = 0; ok && k < code.size(); ++k)
    ok = code[k] >= '0' && code[k] <= '9';
  if (!ok)
    throw kernel::PdfException("Invalid EAN/UPC code '" + code +
                               "': expected " +
                               std::to_string(expected_length) + " digits.");
}
}  // namespace

std::vector<uint8_t> BarcodeEAN::GetBarsEan13(const std::string& code_str) {
  CheckEanDigits(code_str, 13);
  std::vector<int> code;
  for (char c : code_str) code.push_back(c - '0');
  std::vector<uint8_t> bars{1, 1, 1};
  const int* sequence = kParity13[code[0]];
  for (int k = 0; k < 6; ++k) {
    const uint8_t* stripes = kBarsEan[code[k + 1]];
    if (sequence[k] == 0)
      bars.insert(bars.end(), {stripes[0], stripes[1], stripes[2], stripes[3]});
    else
      bars.insert(bars.end(), {stripes[3], stripes[2], stripes[1], stripes[0]});
  }
  bars.insert(bars.end(), {1, 1, 1, 1, 1});
  for (int k = 7; k < 13; ++k) {
    const uint8_t* stripes = kBarsEan[code[k]];
    bars.insert(bars.end(), stripes, stripes + 4);
  }
  bars.insert(bars.end(), {1, 1, 1});
  return bars;
}

std::vector<uint8_t> BarcodeEAN::GetBarsEan8(const std::string& code_str) {
  CheckEanDigits(code_str, 8);
  std::vector<uint8_t> bars{1, 1, 1};
  for (int k = 0; k < 4; ++k) {
    const uint8_t* stripes = kBarsEan[code_str[k] - '0'];
    bars.insert(bars.end(), stripes, stripes + 4);
  }
  bars.insert(bars.end(), {1, 1, 1, 1, 1});
  for (int k = 4; k < 8; ++k) {
    const uint8_t* stripes = kBarsEan[code_str[k] - '0'];
    bars.insert(bars.end(), stripes, stripes + 4);
  }
  bars.insert(bars.end(), {1, 1, 1});
  return bars;
}

Rectangle BarcodeEAN::GetBarcodeSize() const {
  float width = 0, height = bar_height_;
  if (font_)
    height += baseline_ > 0 ? baseline_ - GetDescender() : -baseline_ + size_;
  switch (code_type_) {
    case kEan13:
      CheckEanDigits(code_, 13);
      width = x_ * (11 + 12 * 7);
      if (font_) width += font_->GetWidth(std::string(1, code_[0]), size_);
      break;
    case kEan8:
      CheckEanDigits(code_, 8);
      width = x_ * (11 + 8 * 7);
      break;
    case kUpca:
      CheckEanDigits(code_, 12);
      width = x_ * (11 + 12 * 7);
      if (font_)
        width += font_->GetWidth(std::string(1, code_[0]), size_) +
                 font_->GetWidth(std::string(1, code_.back()), size_);
      break;
    default:
      throw kernel::PdfException("Invalid code type");
  }
  return Rectangle(width, height);
}

Rectangle BarcodeEAN::PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                                   const ColorPtr& text_color) {
  Rectangle rect = GetBarcodeSize();
  float bar_start_x = 0, bar_start_y = 0, text_start_y = 0;
  if (font_) {
    if (baseline_ <= 0) {
      text_start_y = bar_height_ - baseline_;
    } else {
      text_start_y = -GetDescender();
      bar_start_y = text_start_y + baseline_;
    }
  }
  if ((code_type_ == kEan13 || code_type_ == kUpca) && font_)
    bar_start_x += font_->GetWidth(std::string(1, code_[0]), size_);
  std::vector<uint8_t> bars;
  const int* guard = nullptr;
  int guard_len = 0;
  switch (code_type_) {
    case kEan13:
      bars = GetBarsEan13(code_);
      guard = kGuardEan13;
      guard_len = 6;
      break;
    case kEan8:
      bars = GetBarsEan8(code_);
      guard = kGuardEan8;
      guard_len = 6;
      break;
    case kUpca:
      bars = GetBarsEan13("0" + code_);
      guard = kGuardUpca;
      guard_len = static_cast<int>(sizeof(kGuardUpca) / sizeof(int));
      break;
  }
  float keep_bar_x = bar_start_x;
  bool print = true;
  float gd = (font_ && baseline_ > 0 && guard_bars_) ? baseline_ / 2 : 0;
  if (bar_color) canvas.SetFillColor(bar_color);
  for (size_t k = 0; k < bars.size(); ++k) {
    float w = bars[k] * x_;
    if (print) {
      if (std::binary_search(guard, guard + guard_len, static_cast<int>(k)))
        canvas.Rectangle(bar_start_x, bar_start_y - gd, w - ink_spreading_,
                         bar_height_ + gd);
      else
        canvas.Rectangle(bar_start_x, bar_start_y, w - ink_spreading_,
                         bar_height_);
    }
    print = !print;
    bar_start_x += w;
  }
  canvas.Fill();
  if (font_) {
    if (text_color) canvas.SetFillColor(text_color);
    canvas.BeginText().SetFontAndSize(font_, size_);
    auto put = [&](char ch, float px) {
      canvas.SetTextMatrix(px, text_start_y);
      canvas.ShowText(std::string(1, ch));
    };
    switch (code_type_) {
      case kEan13:
        put(code_[0], 0);
        for (int k = 1; k < 13; ++k) {
          float len = font_->GetWidth(std::string(1, code_[k]), size_);
          put(code_[k], keep_bar_x + kTextposEan13[k - 1] * x_ - len / 2);
        }
        break;
      case kEan8:
        for (int k = 0; k < 8; ++k) {
          float len = font_->GetWidth(std::string(1, code_[k]), size_);
          put(code_[k], kTextposEan8[k] * x_ - len / 2);
        }
        break;
      case kUpca:
        put(code_[0], 0);
        for (int k = 1; k < 11; ++k) {
          float len = font_->GetWidth(std::string(1, code_[k]), size_);
          put(code_[k], keep_bar_x + kTextposEan13[k] * x_ - len / 2);
        }
        put(code_[11], keep_bar_x + x_ * (11 + 12 * 7));
        break;
    }
    canvas.EndText();
  }
  return rect;
}

// ============================================================ Barcode39
Barcode39::Barcode39(PdfDocument* document) : Barcode1D(document) {
  x_ = 0.8f;
  n_ = 2;
  font_ = kernel::PdfFontFactory::CreateFont();
  size_ = 8;
  baseline_ = size_;
  bar_height_ = size_ * 3;
  text_alignment_ = kAlignCenter;
  start_stop_text_ = true;
}

std::vector<uint8_t> Barcode39::GetBarsCode39(const std::string& text) {
  std::string t = "*" + text + "*";
  std::vector<uint8_t> bars;
  for (size_t k = 0; k < t.size(); ++k) {
    const char* p = std::strchr(kChars39, t[k]);
    if (!p || t[k] == '\0')
      throw kernel::PdfException("Illegal character in Barcode39: " +
                                 std::string(1, t[k]));
    int idx = static_cast<int>(p - kChars39);
    bars.insert(bars.end(), kBars39[idx], kBars39[idx] + 9);
    if (k + 1 < t.size()) bars.push_back(0);
  }
  return bars;
}

std::string Barcode39::GetCode39Ex(const std::string& text) {
  std::string out;
  for (char ch : text) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c > 127)
      throw kernel::PdfException("Illegal character in Barcode39 extended");
    char c1 = kExtended39[c * 2], c2 = kExtended39[c * 2 + 1];
    if (c1 != ' ') out += c1;
    out += c2;
  }
  return out;
}

char Barcode39::GetChecksum(const std::string& text) {
  int chk = 0;
  for (char c : text) {
    const char* p = std::strchr(kChars39, c);
    if (!p) throw kernel::PdfException("Illegal character in Barcode39");
    chk += static_cast<int>(p - kChars39);
  }
  return kChars39[chk % 43];
}

std::string Barcode39::BarcodeText() const {
  std::string b_code = extended_ ? GetCode39Ex(code_) : code_;
  if (generate_checksum_) b_code += GetChecksum(b_code);
  return b_code;
}

Rectangle Barcode39::GetBarcodeSize() const {
  float font_x = 0, font_y = 0;
  std::string f_code = code_;
  if (generate_checksum_ && checksum_text_)
    f_code += GetChecksum(extended_ ? GetCode39Ex(code_) : code_);
  if (start_stop_text_) f_code = "*" + f_code + "*";
  if (font_) {
    font_y = baseline_ > 0 ? baseline_ - GetDescender() : -baseline_ + size_;
    font_x = font_->GetWidth(TextOrAlt(f_code), size_);
  }
  size_t len = BarcodeText().size() + 2;
  float full_width = len * (6 * x_ + 3 * x_ * n_) + (len - 1) * x_;
  return Rectangle(std::max(full_width, font_x), bar_height_ + font_y);
}

Rectangle Barcode39::PlaceBarcode(PdfCanvas& canvas, const ColorPtr& bar_color,
                                  const ColorPtr& text_color) {
  std::string full_code = code_;
  std::string b_code = BarcodeText();
  if (generate_checksum_ && checksum_text_) full_code += b_code.back();
  if (start_stop_text_) full_code = "*" + full_code + "*";
  full_code = TextOrAlt(full_code);
  float font_x = font_ ? font_->GetWidth(full_code, size_) : 0;
  size_t len = b_code.size() + 2;
  float full_width = len * (6 * x_ + 3 * x_ * n_) + (len - 1) * x_;
  float bar_start_x = 0, text_start_x = 0;
  switch (text_alignment_) {
    case kAlignLeft:
      break;
    case kAlignRight:
      if (font_x > full_width)
        bar_start_x = font_x - full_width;
      else
        text_start_x = full_width - font_x;
      break;
    default:
      if (font_x > full_width)
        bar_start_x = (font_x - full_width) / 2;
      else
        text_start_x = (full_width - font_x) / 2;
      break;
  }
  float bar_start_y = 0, text_start_y = 0;
  if (font_) {
    if (baseline_ <= 0)
      text_start_y = bar_height_ - baseline_;
    else {
      text_start_y = -GetDescender();
      bar_start_y = text_start_y + baseline_;
    }
  }
  if (bar_color) canvas.SetFillColor(bar_color);
  DrawBars(canvas, GetBarsCode39(b_code), bar_start_x, bar_start_y, x_, n_,
           bar_height_, ink_spreading_, false);
  if (font_) {
    if (text_color) canvas.SetFillColor(text_color);
    canvas.BeginText()
        .SetFontAndSize(font_, size_)
        .SetTextMatrix(text_start_x, text_start_y)
        .ShowText(full_code)
        .EndText();
  }
  return GetBarcodeSize();
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
  int Mul(int a, int b) const {
    return (a == 0 || b == 0) ? 0 : exp[log[a] + log[b]];
  }
};
const Gf256& Gf() {
  static Gf256 g;
  return g;
}

class BitBuffer {
 public:
  void Append(uint32_t value, int num_bits) {
    for (int i = num_bits - 1; i >= 0; --i)
      bits.push_back(((value >> i) & 1) != 0);
  }
  size_t size() const { return bits.size(); }
  std::vector<bool> bits;
};

int FindMsbSet(uint32_t v) {
  int n = 0;
  while (v) {
    ++n;
    v >>= 1;
  }
  return n;
}

uint32_t BchCode(uint32_t value, uint32_t poly) {
  int msb_set = FindMsbSet(poly);
  value <<= msb_set - 1;
  while (FindMsbSet(value) >= msb_set)
    value ^= poly << (FindMsbSet(value) - msb_set);
  return value;
}

const char* const kAlphanumeric =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

int CharCountBits(int mode, int version) {
  int idx = version <= 9 ? 0 : (version <= 26 ? 1 : 2);
  switch (mode) {
    case 1: {
      static const int kB[] = {10, 12, 14};
      return kB[idx];
    }
    case 2: {
      static const int kB[] = {9, 11, 13};
      return kB[idx];
    }
    default: {
      static const int kB[] = {8, 16, 16};
      return kB[idx];
    }
  }
}

using Matrix = std::vector<std::vector<int>>;  // -1 empty, 0 light, 1 dark

void EmbedFinder(Matrix& m, int x, int y) {
  for (int dy = -1; dy <= 7; ++dy)
    for (int dx = -1; dx <= 7; ++dx) {
      int xx = x + dx, yy = y + dy;
      if (xx < 0 || yy < 0 || yy >= static_cast<int>(m.size()) ||
          xx >= static_cast<int>(m.size()))
        continue;
      bool dark = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                  (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                   (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
      m[yy][xx] = dark ? 1 : 0;
    }
}

void BuildFunctionPatterns(Matrix& m, int version) {
  int size = static_cast<int>(m.size());
  EmbedFinder(m, 0, 0);
  EmbedFinder(m, size - 7, 0);
  EmbedFinder(m, 0, size - 7);
  const QrVersion& v = kQrVersions[version - 1];
  for (int a = 0; a < v.num_align; ++a)
    for (int b = 0; b < v.num_align; ++b) {
      int cy = v.alignment[a], cx = v.alignment[b];
      if (m[cy][cx] != -1) continue;  // overlaps a finder pattern
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
          m[cy + dy][cx + dx] =
              (std::max(std::abs(dx), std::abs(dy)) != 1) ? 1 : 0;
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
    uint32_t version_bits =
        (static_cast<uint32_t>(version) << 12) | BchCode(version, 0x1F25);
    int bit_index = 17;
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 3; ++j) {
        // zxing: versionInfoBits.at(bitIndex) where index 0 is the MSB
        int bit = (version_bits >> (17 - bit_index)) & 1;
        --bit_index;
        m[size - 11 + j][i] = bit;
        m[i][size - 11 + j] = bit;
      }
  }
}

bool MaskBit(int mask, int x, int y) {
  int temp;
  switch (mask) {
    case 0:
      return ((y + x) & 1) == 0;
    case 1:
      return (y & 1) == 0;
    case 2:
      return x % 3 == 0;
    case 3:
      return (y + x) % 3 == 0;
    case 4:
      return (((y >> 1) + (x / 3)) & 1) == 0;
    case 5:
      temp = y * x;
      return ((temp & 1) + (temp % 3)) == 0;
    case 6:
      temp = y * x;
      return (((temp & 1) + (temp % 3)) & 1) == 0;
    default:
      temp = y * x;
      return (((temp % 3) + ((y + x) & 1)) & 1) == 0;
  }
}

void EmbedFormat(Matrix& m, int ec_bits, int mask) {
  static const int kCoords[15][2] = {{8, 0}, {8, 1}, {8, 2}, {8, 3}, {8, 4},
                                     {8, 5}, {8, 7}, {8, 8}, {7, 8}, {5, 8},
                                     {4, 8}, {3, 8}, {2, 8}, {1, 8}, {0, 8}};
  int size = static_cast<int>(m.size());
  uint32_t data =
      (static_cast<uint32_t>(ec_bits) << 3) | static_cast<uint32_t>(mask);
  uint32_t bits = ((data << 10) | BchCode(data, 0x537)) ^ 0x5412;
  for (int i = 0; i < 15; ++i) {
    int bit = (bits >> i) & 1;  // LSB first
    m[kCoords[i][1]][kCoords[i][0]] = bit;
    if (i < 8)
      m[8][size - i - 1] = bit;
    else
      m[size - 7 + (i - 8)][8] = bit;
  }
}

void EmbedData(Matrix& m, const std::vector<bool>& data_bits, int mask) {
  int size = static_cast<int>(m.size());
  size_t bit_index = 0;
  int direction = -1;
  int x = size - 1, y = size - 1;
  while (x > 0) {
    if (x == 6) x -= 1;
    while (y >= 0 && y < size) {
      for (int i = 0; i < 2; ++i) {
        int xx = x - i;
        if (m[y][xx] != -1) continue;
        bool bit =
            bit_index < data_bits.size() ? data_bits[bit_index++] : false;
        if (MaskBit(mask, xx, y)) bit = !bit;
        m[y][xx] = bit ? 1 : 0;
      }
      y += direction;
    }
    direction = -direction;
    y += direction;
    x -= 2;
  }
}

// Port of MaskUtil (same quirks as iText's zxing-derived implementation, so
// mask choice matches).
int Penalty(const Matrix& m) {
  int size = static_cast<int>(m.size());
  int total = 0;
  // rule 1 (prevBit deliberately carries over between rows, as in iText)
  for (int pass = 0; pass < 2; ++pass) {
    int num_same_bit_cells = 0, prev_bit = -1;
    for (int i = 0; i < size; ++i) {
      for (int j = 0; j < size; ++j) {
        int bit = pass == 0 ? m[i][j] : m[j][i];
        if (bit == prev_bit) {
          num_same_bit_cells += 1;
          if (num_same_bit_cells == 5)
            total += 3;
          else if (num_same_bit_cells > 5)
            total += 1;
        } else {
          num_same_bit_cells = 1;
          prev_bit = bit;
        }
      }
      num_same_bit_cells = 0;
    }
  }
  // rule 2
  for (int y = 0; y < size - 1; ++y)
    for (int x = 0; x < size - 1; ++x) {
      int v = m[y][x];
      if (v == m[y][x + 1] && v == m[y + 1][x] && v == m[y + 1][x + 1])
        total += 3;
    }
  // rule 3
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      auto h = [&](int k) { return m[y][x + k]; };
      if (x + 6 < size && h(0) == 1 && h(1) == 0 && h(2) == 1 && h(3) == 1 &&
          h(4) == 1 && h(5) == 0 && h(6) == 1 &&
          ((x + 10 < size && h(7) == 0 && h(8) == 0 && h(9) == 0 &&
            h(10) == 0) ||
           (x - 4 >= 0 && h(-1) == 0 && h(-2) == 0 && h(-3) == 0 &&
            h(-4) == 0)))
        total += 40;
      auto v = [&](int k) { return m[y + k][x]; };
      if (y + 6 < size && v(0) == 1 && v(1) == 0 && v(2) == 1 && v(3) == 1 &&
          v(4) == 1 && v(5) == 0 && v(6) == 1 &&
          ((y + 10 < size && v(7) == 0 && v(8) == 0 && v(9) == 0 &&
            v(10) == 0) ||
           (y - 4 >= 0 && v(-1) == 0 && v(-2) == 0 && v(-3) == 0 &&
            v(-4) == 0)))
        total += 40;
    }
  // rule 4
  int dark = 0;
  for (const auto& row : m)
    for (int c : row) dark += c;
  double dark_ratio = static_cast<double>(dark) / (size * size);
  total += std::abs(static_cast<int>(dark_ratio * 100 - 50)) / 5 * 10;
  return total;
}
}  // namespace

std::vector<uint8_t> QrEncoder::GenerateEcBytes(
    const std::vector<uint8_t>& data, int num_ec_bytes) {
  const Gf256& g = Gf();
  // generator polynomial
  std::vector<int> gen{1};
  for (int i = 0; i < num_ec_bytes; ++i) {
    std::vector<int> next(gen.size() + 1, 0);
    for (size_t j = 0; j < gen.size(); ++j) {
      next[j] ^= gen[j];
      next[j + 1] ^= g.Mul(gen[j], g.exp[i]);
    }
    gen = next;
  }
  std::vector<int> rem(num_ec_bytes, 0);
  for (uint8_t d : data) {
    int factor = d ^ rem[0];
    rem.erase(rem.begin());
    rem.push_back(0);
    for (int j = 0; j < num_ec_bytes; ++j) rem[j] ^= g.Mul(gen[j + 1], factor);
  }
  return std::vector<uint8_t>(rem.begin(), rem.end());
}

std::vector<std::vector<bool>> QrEncoder::Encode(const std::string& content,
                                                 ErrorCorrectionLevel level,
                                                 int min_version,
                                                 int& version_out) {
  // mode selection
  bool numeric = !content.empty(), alnum = !content.empty();
  for (char c : content) {
    if (c < '0' || c > '9') numeric = false;
    if (!std::strchr(kAlphanumeric, c) || c == '\0') alnum = false;
  }
  int mode = numeric ? 1 : (alnum ? 2 : 4);
  Bytes bytes;
  bool needs_eci = false;
  if (mode == 4) {
    auto cps = TextUtil::Utf8ToCodePoints(content);
    bool latin1 =
        std::all_of(cps.begin(), cps.end(), [](uint32_t c) { return c < 256; });
    if (latin1)
      for (uint32_t c : cps) bytes.push_back(static_cast<uint8_t>(c));
    else {
      bytes.assign(content.begin(), content.end());
      needs_eci = true;  // UTF-8 (ECI 26)
    }
  }
  int level_idx = static_cast<int>(level);
  int version = 0;
  BitBuffer bits;
  for (int v = std::max(1, min_version); v <= 40; ++v) {
    const QrEcBlocks& ec = kQrVersions[v - 1].ec[level_idx];
    int data_codewords = ec.blocks[0].count * ec.blocks[0].data_codewords +
                         ec.blocks[1].count * ec.blocks[1].data_codewords;
    size_t count = mode == 4 ? bytes.size() : content.size();
    size_t needed = (needs_eci ? 12 : 0) + 4 + CharCountBits(mode, v);
    if (mode == 1)
      needed +=
          10 * (count / 3) + (count % 3 == 2 ? 7 : (count % 3 == 1 ? 4 : 0));
    else if (mode == 2)
      needed += 11 * (count / 2) + 6 * (count % 2);
    else
      needed += 8 * count;
    if ((needed + 7) / 8 <= static_cast<size_t>(data_codewords)) {
      version = v;
      break;
    }
  }
  if (version == 0) throw kernel::PdfException("Data too big for a QR code");
  version_out = version;
  const QrEcBlocks& ec = kQrVersions[version - 1].ec[level_idx];
  int data_codewords = ec.blocks[0].count * ec.blocks[0].data_codewords +
                       ec.blocks[1].count * ec.blocks[1].data_codewords;
  if (needs_eci) {
    bits.Append(0x7, 4);
    bits.Append(26, 8);
  }
  bits.Append(static_cast<uint32_t>(mode), 4);
  if (mode == 1) {
    bits.Append(static_cast<uint32_t>(content.size()),
                CharCountBits(1, version));
    for (size_t i = 0; i < content.size(); i += 3) {
      size_t n = std::min<size_t>(3, content.size() - i);
      uint32_t v = static_cast<uint32_t>(std::stoi(content.substr(i, n)));
      bits.Append(v, n == 3 ? 10 : (n == 2 ? 7 : 4));
    }
  } else if (mode == 2) {
    bits.Append(static_cast<uint32_t>(content.size()),
                CharCountBits(2, version));
    auto code = [](char c) {
      return static_cast<uint32_t>(std::strchr(kAlphanumeric, c) -
                                   kAlphanumeric);
    };
    for (size_t i = 0; i < content.size(); i += 2) {
      if (i + 1 < content.size())
        bits.Append(code(content[i]) * 45 + code(content[i + 1]), 11);
      else
        bits.Append(code(content[i]), 6);
    }
  } else {
    bits.Append(static_cast<uint32_t>(bytes.size()), CharCountBits(4, version));
    for (uint8_t b : bytes) bits.Append(b, 8);
  }
  size_t capacity = static_cast<size_t>(data_codewords) * 8;
  for (int i = 0; i < 4 && bits.size() < capacity; ++i)
    bits.bits.push_back(false);
  while (bits.size() % 8) bits.bits.push_back(false);
  std::vector<uint8_t> data;
  for (size_t i = 0; i < bits.size(); i += 8) {
    uint8_t b = 0;
    for (int k = 0; k < 8; ++k)
      b = static_cast<uint8_t>((b << 1) | (bits.bits[i + k] ? 1 : 0));
    data.push_back(b);
  }
  for (bool pad = true; static_cast<int>(data.size()) < data_codewords;
       pad = !pad)
    data.push_back(pad ? 0xEC : 0x11);
  // split into blocks
  std::vector<std::vector<uint8_t>> data_blocks, ec_blocks;
  size_t offset = 0;
  for (const auto& group : ec.blocks) {
    for (int b = 0; b < group.count; ++b) {
      std::vector<uint8_t> block(data.begin() + offset,
                                 data.begin() + offset + group.data_codewords);
      offset += group.data_codewords;
      ec_blocks.push_back(GenerateEcBytes(block, ec.ec_codewords_per_block));
      data_blocks.push_back(std::move(block));
    }
  }
  std::vector<uint8_t> interleaved;
  size_t max_data = 0;
  for (const auto& b : data_blocks) max_data = std::max(max_data, b.size());
  for (size_t i = 0; i < max_data; ++i)
    for (const auto& b : data_blocks)
      if (i < b.size()) interleaved.push_back(b[i]);
  for (int i = 0; i < ec.ec_codewords_per_block; ++i)
    for (const auto& b : ec_blocks) interleaved.push_back(b[i]);
  std::vector<bool> data_bits;
  for (uint8_t b : interleaved)
    for (int k = 7; k >= 0; --k) data_bits.push_back(((b >> k) & 1) != 0);
  // matrix with the best mask
  int size = 17 + 4 * version;
  static const int kEcBits[4] = {1, 0, 3, 2};  // L, M, Q, H
  Matrix best;
  int best_penalty = -1;
  for (int mask = 0; mask < 8; ++mask) {
    Matrix m(size, std::vector<int>(size, -1));
    BuildFunctionPatterns(m, version);
    EmbedFormat(m, kEcBits[level_idx], mask);
    EmbedData(m, data_bits, mask);
    int p = Penalty(m);
    if (best_penalty < 0 || p < best_penalty) {
      best_penalty = p;
      best = std::move(m);
    }
  }
  std::vector<std::vector<bool>> result(size, std::vector<bool>(size));
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) result[y][x] = best[y][x] == 1;
  return result;
}

// ============================================================ BarcodeQRCode
void BarcodeQRCode::Regenerate() {
  if (code_.empty()) {
    matrix_.clear();
    return;
  }
  auto m = QrEncoder::Encode(code_, level_, min_version_, version_);
  const int quiet = 4;
  int size = static_cast<int>(m.size()) + 2 * quiet;
  matrix_.assign(size, std::vector<bool>(size, false));
  for (size_t y = 0; y < m.size(); ++y)
    for (size_t x = 0; x < m.size(); ++x)
      matrix_[y + quiet][x + quiet] = m[y][x];
}

Rectangle BarcodeQRCode::GetBarcodeSize(float module_size) const {
  float n = static_cast<float>(matrix_.size());
  return Rectangle(0, 0, n * module_size, n * module_size);
}

Rectangle BarcodeQRCode::PlaceBarcode(PdfCanvas& canvas,
                                      const ColorPtr& foreground,
                                      float module_side) {
  int height = static_cast<int>(matrix_.size());
  if (foreground) canvas.SetFillColor(foreground);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < height; ++x)
      if (matrix_[y][x])
        canvas.Rectangle(x * module_side, (height - y - 1) * module_side,
                         module_side, module_side);
  canvas.Fill();
  return GetBarcodeSize(module_side);
}

std::shared_ptr<kernel::PdfFormXObject> BarcodeQRCode::CreateFormXObject(
    PdfDocument* document, const ColorPtr& foreground, float module_side) {
  auto x_object =
      std::make_shared<kernel::PdfFormXObject>(Rectangle(0, 0, 0, 0));
  PdfCanvas canvas(*x_object, document);
  x_object->SetBBox(PlaceBarcode(canvas, foreground, module_side));
  return x_object;
}

}  // namespace barcodes
}  // namespace pdfcpp
