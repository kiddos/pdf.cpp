#ifndef PDFCPP_IO_FONT_PDF_ENCODINGS_H_
#define PDFCPP_IO_FONT_PDF_ENCODINGS_H_

#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.font.PdfEncodings.
/// Text arguments/results are UTF-8 std::string (the C++ stand-in for Java
/// String).
class PdfEncodings {
 public:
  static constexpr const char* kIdentityH = "Identity-H";
  static constexpr const char* kIdentityV = "Identity-V";
  static constexpr const char* kCP1250 = "Cp1250";
  static constexpr const char* kCP1252 = "Cp1252";
  static constexpr const char* kCP1253 = "Cp1253";
  static constexpr const char* kCP1257 = "Cp1257";
  static constexpr const char* kWinansi = "Cp1252";
  static constexpr const char* kMacroman = "MacRoman";
  static constexpr const char* kSymbol = "Symbol";
  static constexpr const char* kZapfdingbats = "ZapfDingbats";
  static constexpr const char* kUnicodeBig = "UnicodeBig";
  static constexpr const char* kUnicodeBigUnmarked = "UnicodeBigUnmarked";
  static constexpr const char* kPdfDocEncoding = "PDF";
  static constexpr const char* kUtf8 = "UTF-8";

  /// Converts text to bytes in the given encoding. Empty encoding: each code
  /// point cast to a byte.
  static Bytes ConvertToBytes(const std::string& text,
                              const std::string& encoding);
  /// Converts a single unicode code point.
  static Bytes ConvertToBytes(uint32_t ch, const std::string& encoding);
  /// Converts bytes to text. Empty encoding: each byte is a Latin-1 code point.
  static std::string ConvertToString(const Bytes& bytes,
                                     const std::string& encoding);
  static bool IsPdfDocEncoding(const std::string& text);

  /// Unicode value of byte b in the given simple encoding; -1 if unknown.
  static int ByteToUnicode(int b, const std::string& encoding);
  /// The Adobe StandardEncoding table (byte -> unicode, 0 if undefined).
  static int StandardEncoding(int b);
  static int WinAnsiToUnicode(int b);
  static int PdfDocToUnicode(int b);
  static int SymbolToUnicode(int b);
  static int ZapfDingbatsToUnicode(int b);
  static int MacRomanToUnicode(int b);
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_FONT_PDF_ENCODINGS_H_
