#ifndef PDFCPP_IO_UTIL_TEXT_UTIL_H_
#define PDFCPP_IO_UTIL_TEXT_UTIL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace pdfcpp {
namespace io {

/// Unicode helpers. pdfcpp represents Java `String` as UTF-8 encoded
/// std::string.
class TextUtil {
 public:
  /// Decodes UTF-8 into code points (invalid sequences become U+FFFD).
  static std::vector<uint32_t> Utf8ToCodePoints(const std::string& s);
  static std::string CodePointsToUtf8(const std::vector<uint32_t>& cps);
  static std::string CodePointToUtf8(uint32_t cp);
  static void AppendUtf8(std::string& out, uint32_t cp);
  /// UTF-8 -> UTF-16 code units.
  static std::u16string Utf8ToUtf16(const std::string& s);
  static std::string Utf16ToUtf8(const std::u16string& s);
  /// Converts a char sequence where each byte is a Latin-1 code point to UTF-8.
  static std::string Latin1ToUtf8(const std::string& s);

  static bool IsSurrogateHigh(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
  static bool IsSurrogateLow(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
  static bool IsSurrogatePair(const std::u16string& text, size_t idx) {
    return idx + 1 < text.size() && IsSurrogateHigh(text[idx]) &&
           IsSurrogateLow(text[idx + 1]);
  }
  static uint32_t ConvertToUtf32(char16_t high_surrogate,
                                 char16_t low_surrogate) {
    return ((high_surrogate - 0xd800) * 0x400) + (low_surrogate - 0xdc00) +
           0x10000;
  }
  static std::u16string ConvertFromUtf32(uint32_t code_point);
  static bool IsWhiteSpace(uint32_t unicode);
  static bool IsNewLine(uint32_t unicode) {
    return unicode == '\n' || unicode == '\r';
  }
  static bool IsNonBreakingHyphen(uint32_t unicode) {
    return unicode == 0x2011;
  }
  static bool IsWhitespaceOrNonPrintable(uint32_t code) {
    return IsWhiteSpace(code) || code < 32;
  }
  static bool IsDiacritic(uint32_t cp);
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_UTIL_TEXT_UTIL_H_
