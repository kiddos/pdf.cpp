#ifndef PDFCPP_IO_EXCEPTIONS_IO_EXCEPTION_H_
#define PDFCPP_IO_EXCEPTIONS_IO_EXCEPTION_H_

#include <stdexcept>
#include <string>
#include <vector>

namespace pdfcpp {

/// Base exception for all pdfcpp errors (port of
/// com.itextpdf.commons.exceptions.ITextException).
class ITextException : public std::runtime_error {
 public:
  explicit ITextException(const std::string& message)
      : std::runtime_error(message) {}
};

/// Formats a message like Java's MessageFormat: "{0} {1}" placeholders.
std::string FormatMessage(const std::string& pattern,
                          const std::vector<std::string>& params);

template <typename... Args>
std::string Format(const std::string& pattern, const Args&... args);

namespace detail {
inline std::string ToStr(const std::string& s) { return s; }
inline std::string ToStr(const char* s) { return s ? s : ""; }
template <typename T>
std::string ToStr(const T& v) {
  return std::to_string(v);
}
}  // namespace detail

template <typename... Args>
std::string Format(const std::string& pattern, const Args&... args) {
  return FormatMessage(pattern,
                       std::vector<std::string>{detail::ToStr(args)...});
}

namespace io {

/// Port of com.itextpdf.io.exceptions.IOException.
class IoException : public ITextException {
 public:
  explicit IoException(const std::string& message) : ITextException(message) {}
};

/// Messages from IoExceptionMessageConstant.
namespace io_exception_message_constant {
inline constexpr const char* kPdfHeaderNotFound = "PDF header not found.";
inline constexpr const char* kPdfStartxrefNotFound = "PDF startxref not found.";
inline constexpr const char* kPdfEofNotFound = "PDF %%EOF marker not found.";
inline constexpr const char* kFdfStartxrefNotFound = "FDF startxref not found.";
inline constexpr const char* kGtNotExpected = "'>' not expected.";
inline constexpr const char* kErrorReadingString = "Error reading string.";
inline constexpr const char* kErrorAtFilePointer = "Error at file pointer {0}.";
inline constexpr const char* kUnexpectedEndOfFile = "Unexpected end of file.";
inline constexpr const char* kCannotWriteBytes = "Cannot write bytes.";
inline constexpr const char* kCannotOpenFile = "Cannot open file {0}.";
inline constexpr const char* kFontFileNotFound = "Font file {0} not found.";
inline constexpr const char* kFontIsNotRecognized = "Font is not recognized.";
inline constexpr const char* kNotFoundAsFileOrResource =
    "{0} not found as file or resource.";
inline constexpr const char* kInvalidAfmOrPfmFontFile =
    "Invalid afm or pfm font file.";
inline constexpr const char* kStartCharMetricsIsMissingInTheMetricsFile =
    "StartCharMetrics is missing in {0}.";
inline constexpr const char* kEndCharMetricsIsMissingInTheMetricsFile =
    "EndCharMetrics is missing in {0}.";
inline constexpr const char* kMissingRequiredField =
    "Missing required field {0}.";
inline constexpr const char* kTableDoesNotExistsIn =
    "Table {0} does not exist in {1}.";
inline constexpr const char* kNotATruetypeOrOpentypeFont =
    "{0} is not a true type or open type font.";
inline constexpr const char* kTtcIndexDoesntExistInThisTtcFile =
    "TTC index doesn't exist in this TTC file.";
inline constexpr const char* kImageFormatCannotBeRecognized =
    "Image format cannot be recognized.";
inline constexpr const char* kUnsupportedEncoding =
    "Unsupported encoding: {0}.";
}  // namespace io_exception_message_constant

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_EXCEPTIONS_IO_EXCEPTION_H_
