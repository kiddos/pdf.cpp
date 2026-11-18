#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pdfcpp {
namespace io {

/// Unicode helpers. pdfcpp represents Java `String` as UTF-8 encoded std::string.
class TextUtil {
public:
    /// Decodes UTF-8 into code points (invalid sequences become U+FFFD).
    static std::vector<uint32_t> utf8ToCodePoints(const std::string& s);
    static std::string codePointsToUtf8(const std::vector<uint32_t>& cps);
    static std::string codePointToUtf8(uint32_t cp);
    static void appendUtf8(std::string& out, uint32_t cp);
    /// UTF-8 -> UTF-16 code units.
    static std::u16string utf8ToUtf16(const std::string& s);
    static std::string utf16ToUtf8(const std::u16string& s);
    /// Converts a char sequence where each byte is a Latin-1 code point to UTF-8.
    static std::string latin1ToUtf8(const std::string& s);

    static bool isSurrogateHigh(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
    static bool isSurrogateLow(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
    static bool isSurrogatePair(const std::u16string& text, size_t idx) {
        return idx + 1 < text.size() && isSurrogateHigh(text[idx]) && isSurrogateLow(text[idx + 1]);
    }
    static uint32_t convertToUtf32(char16_t highSurrogate, char16_t lowSurrogate) {
        return ((highSurrogate - 0xd800) * 0x400) + (lowSurrogate - 0xdc00) + 0x10000;
    }
    static std::u16string convertFromUtf32(uint32_t codePoint);
    static bool isWhiteSpace(uint32_t unicode);
    static bool isNewLine(uint32_t unicode) { return unicode == '\n' || unicode == '\r'; }
    static bool isNonBreakingHyphen(uint32_t unicode) { return unicode == 0x2011; }
    static bool isWhitespaceOrNonPrintable(uint32_t code) { return isWhiteSpace(code) || code < 32; }
    static bool isDiacritic(uint32_t cp);
};

}  // namespace io
}  // namespace pdfcpp
