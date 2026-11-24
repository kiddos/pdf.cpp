#ifndef PDFCPP_IO_SOURCE_BYTE_UTILS_H_
#define PDFCPP_IO_SOURCE_BYTE_UTILS_H_

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.source.ByteUtils: compact ISO-8859-1 number
/// formatting for PDF output.
class ByteUtils {
 public:
  static bool high_precision_;

  static Bytes GetIsoBytes(const std::string& text);
  static Bytes GetIsoBytes(uint8_t pre, const std::string& text,
                           uint8_t post = 0);
  static Bytes GetIsoBytes(int n);
  static Bytes GetIsoBytes(long long n);
  static Bytes GetIsoBytes(double d);
  static Bytes GetIsoBytes(double d, bool high_precision);

  /// Formats a number into its compact PDF textual representation.
  static std::string FormatDouble(double d) {
    return ToLatin1String(GetIsoBytes(d));
  }
  static std::string FormatDouble(double d, bool high_precision) {
    return ToLatin1String(GetIsoBytes(d, high_precision));
  }
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_BYTE_UTILS_H_
