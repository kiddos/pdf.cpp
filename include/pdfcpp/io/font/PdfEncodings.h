#pragma once

#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.font.PdfEncodings.
/// Text arguments/results are UTF-8 std::string (the C++ stand-in for Java String).
class PdfEncodings {
public:
    static constexpr const char* IDENTITY_H = "Identity-H";
    static constexpr const char* IDENTITY_V = "Identity-V";
    static constexpr const char* CP1250 = "Cp1250";
    static constexpr const char* CP1252 = "Cp1252";
    static constexpr const char* CP1253 = "Cp1253";
    static constexpr const char* CP1257 = "Cp1257";
    static constexpr const char* WINANSI = "Cp1252";
    static constexpr const char* MACROMAN = "MacRoman";
    static constexpr const char* SYMBOL = "Symbol";
    static constexpr const char* ZAPFDINGBATS = "ZapfDingbats";
    static constexpr const char* UNICODE_BIG = "UnicodeBig";
    static constexpr const char* UNICODE_BIG_UNMARKED = "UnicodeBigUnmarked";
    static constexpr const char* PDF_DOC_ENCODING = "PDF";
    static constexpr const char* UTF8 = "UTF-8";

    /// Converts text to bytes in the given encoding. Empty encoding: each code point cast to a byte.
    static Bytes convertToBytes(const std::string& text, const std::string& encoding);
    /// Converts a single unicode code point.
    static Bytes convertToBytes(uint32_t ch, const std::string& encoding);
    /// Converts bytes to text. Empty encoding: each byte is a Latin-1 code point.
    static std::string convertToString(const Bytes& bytes, const std::string& encoding);
    static bool isPdfDocEncoding(const std::string& text);

    /// Unicode value of byte b in the given simple encoding; -1 if unknown.
    static int byteToUnicode(int b, const std::string& encoding);
    /// The Adobe StandardEncoding table (byte -> unicode, 0 if undefined).
    static int standardEncoding(int b);
    static int winAnsiToUnicode(int b);
    static int pdfDocToUnicode(int b);
    static int symbolToUnicode(int b);
    static int zapfDingbatsToUnicode(int b);
    static int macRomanToUnicode(int b);
};

}  // namespace io
}  // namespace pdfcpp
