#include "pdfcpp/io/util/stream_util.h"

#include <fstream>
#include <iterator>

#include "pdfcpp/io/exceptions/io_exception.h"

namespace pdfcpp {
namespace io {

ByteBuffer StreamUtil::CreateBufferedEscapedString(const Bytes& bytes) {
  ByteBuffer buf(static_cast<int>(bytes.size()) * 2 + 2);
  buf.Append('(');
  for (uint8_t b : bytes) {
    switch (b) {
      case '\r':
        buf.Append("\\r");
        break;
      case '\n':
        buf.Append("\\n");
        break;
      case '\t':
        buf.Append("\\t");
        break;
      case '\b':
        buf.Append("\\b");
        break;
      case '\f':
        buf.Append("\\f");
        break;
      case '(':
      case ')':
      case '\\':
        buf.Append('\\').Append(b);
        break;
      default:
        if (b < 8) {
          buf.Append("\\00").Append(static_cast<uint8_t>('0' + b));
        } else if (b >= 8 && b < 32) {
          buf.Append("\\0")
              .Append(static_cast<uint8_t>('0' + (b >> 3)))
              .Append(static_cast<uint8_t>('0' + (b & 7)));
        } else {
          buf.Append(b);
        }
    }
  }
  buf.Append(')');
  return buf;
}

ByteBuffer StreamUtil::CreateBufferedHexedString(const Bytes& bytes) {
  ByteBuffer buf(static_cast<int>(bytes.size()) * 2 + 2);
  buf.Append('<');
  for (uint8_t b : bytes) buf.AppendHex(b);
  buf.Append('>');
  return buf;
}

void StreamUtil::WriteEscapedString(OutputStream& output_stream,
                                    const Bytes& bytes) {
  ByteBuffer buf = CreateBufferedEscapedString(bytes);
  output_stream.Write(buf.GetInternalBuffer(), 0, buf.size());
}

void StreamUtil::WriteHexedString(OutputStream& output_stream,
                                  const Bytes& bytes) {
  ByteBuffer buf = CreateBufferedHexedString(bytes);
  output_stream.Write(buf.GetInternalBuffer(), 0, buf.size());
}

Bytes StreamUtil::InputStreamToArray(std::istream& stream) {
  return Bytes((std::istreambuf_iterator<char>(stream)),
               std::istreambuf_iterator<char>());
}

Bytes StreamUtil::ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw IoException(
        Format(io_exception_message_constant::kCannotOpenFile, path));
  return InputStreamToArray(in);
}

void StreamUtil::WriteFile(const std::string& path, const Bytes& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    throw IoException(
        Format(io_exception_message_constant::kCannotOpenFile, path));
  out.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

}  // namespace io
}  // namespace pdfcpp
