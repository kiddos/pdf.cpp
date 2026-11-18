#pragma once

#include <istream>
#include <string>

#include "pdfcpp/io/source/ByteBuffer.h"
#include "pdfcpp/io/source/OutputStream.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.util.StreamUtil.
class StreamUtil {
public:
    static Bytes createEscapedString(const Bytes& bytes) { return createBufferedEscapedString(bytes).toByteArray(); }
    static void writeEscapedString(OutputStream& outputStream, const Bytes& bytes);
    static void writeHexedString(OutputStream& outputStream, const Bytes& bytes);
    /// Returns "(escaped)" including parentheses.
    static ByteBuffer createBufferedEscapedString(const Bytes& bytes);
    /// Returns "<hex>" including angle brackets.
    static ByteBuffer createBufferedHexedString(const Bytes& bytes);
    static Bytes inputStreamToArray(std::istream& stream);
    static Bytes readFile(const std::string& path);
    static void writeFile(const std::string& path, const Bytes& data);
};

}  // namespace io
}  // namespace pdfcpp
