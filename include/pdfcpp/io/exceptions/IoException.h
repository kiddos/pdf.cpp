#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace pdfcpp {

/// Base exception for all pdfcpp errors (port of com.itextpdf.commons.exceptions.ITextException).
class ITextException : public std::runtime_error {
public:
    explicit ITextException(const std::string& message) : std::runtime_error(message) {}
};

/// Formats a message like Java's MessageFormat: "{0} {1}" placeholders.
std::string formatMessage(const std::string& pattern, const std::vector<std::string>& params);

template <typename... Args>
std::string format(const std::string& pattern, const Args&... args);

namespace detail {
inline std::string toStr(const std::string& s) { return s; }
inline std::string toStr(const char* s) { return s ? s : ""; }
template <typename T>
std::string toStr(const T& v) { return std::to_string(v); }
}  // namespace detail

template <typename... Args>
std::string format(const std::string& pattern, const Args&... args) {
    return formatMessage(pattern, std::vector<std::string>{detail::toStr(args)...});
}

namespace io {

/// Port of com.itextpdf.io.exceptions.IOException.
class IoException : public ITextException {
public:
    explicit IoException(const std::string& message) : ITextException(message) {}
};

/// Messages from IoExceptionMessageConstant.
namespace IoExceptionMessageConstant {
inline constexpr const char* PDF_HEADER_NOT_FOUND = "PDF header not found.";
inline constexpr const char* PDF_STARTXREF_NOT_FOUND = "PDF startxref not found.";
inline constexpr const char* PDF_EOF_NOT_FOUND = "PDF %%EOF marker not found.";
inline constexpr const char* FDF_STARTXREF_NOT_FOUND = "FDF startxref not found.";
inline constexpr const char* GT_NOT_EXPECTED = "'>' not expected.";
inline constexpr const char* ERROR_READING_STRING = "Error reading string.";
inline constexpr const char* ERROR_AT_FILE_POINTER = "Error at file pointer {0}.";
inline constexpr const char* UNEXPECTED_END_OF_FILE = "Unexpected end of file.";
inline constexpr const char* CANNOT_WRITE_BYTES = "Cannot write bytes.";
inline constexpr const char* CANNOT_OPEN_FILE = "Cannot open file {0}.";
inline constexpr const char* FONT_FILE_NOT_FOUND = "Font file {0} not found.";
inline constexpr const char* FONT_IS_NOT_RECOGNIZED = "Font is not recognized.";
inline constexpr const char* NOT_FOUND_AS_FILE_OR_RESOURCE = "{0} not found as file or resource.";
inline constexpr const char* INVALID_AFM_OR_PFM_FONT_FILE = "Invalid afm or pfm font file.";
inline constexpr const char* START_CHAR_METRICS_IS_MISSING_IN_THE_METRICS_FILE = "StartCharMetrics is missing in {0}.";
inline constexpr const char* END_CHAR_METRICS_IS_MISSING_IN_THE_METRICS_FILE = "EndCharMetrics is missing in {0}.";
inline constexpr const char* MISSING_REQUIRED_FIELD = "Missing required field {0}.";
inline constexpr const char* TABLE_DOES_NOT_EXISTS_IN = "Table {0} does not exist in {1}.";
inline constexpr const char* NOT_A_TRUETYPE_OR_OPENTYPE_FONT = "{0} is not a true type or open type font.";
inline constexpr const char* TTC_INDEX_DOESNT_EXIST_IN_THIS_TTC_FILE = "TTC index doesn't exist in this TTC file.";
inline constexpr const char* IMAGE_FORMAT_CANNOT_BE_RECOGNIZED = "Image format cannot be recognized.";
inline constexpr const char* UNSUPPORTED_ENCODING = "Unsupported encoding: {0}.";
}  // namespace IoExceptionMessageConstant

}  // namespace io
}  // namespace pdfcpp
