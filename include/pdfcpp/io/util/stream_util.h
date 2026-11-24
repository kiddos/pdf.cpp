#ifndef PDFCPP_IO_UTIL_STREAM_UTIL_H_
#define PDFCPP_IO_UTIL_STREAM_UTIL_H_

#include <istream>
#include <string>

#include "pdfcpp/io/source/byte_buffer.h"
#include "pdfcpp/io/source/output_stream.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.util.StreamUtil.
class StreamUtil {
 public:
  static Bytes CreateEscapedString(const Bytes& bytes) {
    return CreateBufferedEscapedString(bytes).ToByteArray();
  }
  static void WriteEscapedString(OutputStream& output_stream,
                                 const Bytes& bytes);
  static void WriteHexedString(OutputStream& output_stream, const Bytes& bytes);
  /// Returns "(escaped)" including parentheses.
  static ByteBuffer CreateBufferedEscapedString(const Bytes& bytes);
  /// Returns "<hex>" including angle brackets.
  static ByteBuffer CreateBufferedHexedString(const Bytes& bytes);
  static Bytes InputStreamToArray(std::istream& stream);
  static Bytes ReadFile(const std::string& path);
  static void WriteFile(const std::string& path, const Bytes& data);
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_UTIL_STREAM_UTIL_H_
