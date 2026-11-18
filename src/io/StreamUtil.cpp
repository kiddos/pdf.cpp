#include "pdfcpp/io/util/StreamUtil.h"

#include <fstream>
#include <iterator>

#include "pdfcpp/io/exceptions/IoException.h"

namespace pdfcpp {
namespace io {

ByteBuffer StreamUtil::createBufferedEscapedString(const Bytes& bytes) {
    ByteBuffer buf(static_cast<int>(bytes.size()) * 2 + 2);
    buf.append('(');
    for (uint8_t b : bytes) {
        switch (b) {
            case '\r': buf.append("\\r"); break;
            case '\n': buf.append("\\n"); break;
            case '\t': buf.append("\\t"); break;
            case '\b': buf.append("\\b"); break;
            case '\f': buf.append("\\f"); break;
            case '(':
            case ')':
            case '\\':
                buf.append('\\').append(b);
                break;
            default:
                if (b < 8) {
                    buf.append("\\00").append(static_cast<uint8_t>('0' + b));
                } else if (b >= 8 && b < 32) {
                    buf.append("\\0").append(static_cast<uint8_t>('0' + (b >> 3))).append(static_cast<uint8_t>('0' + (b & 7)));
                } else {
                    buf.append(b);
                }
        }
    }
    buf.append(')');
    return buf;
}

ByteBuffer StreamUtil::createBufferedHexedString(const Bytes& bytes) {
    ByteBuffer buf(static_cast<int>(bytes.size()) * 2 + 2);
    buf.append('<');
    for (uint8_t b : bytes) buf.appendHex(b);
    buf.append('>');
    return buf;
}

void StreamUtil::writeEscapedString(OutputStream& outputStream, const Bytes& bytes) {
    ByteBuffer buf = createBufferedEscapedString(bytes);
    outputStream.write(buf.getInternalBuffer(), 0, buf.size());
}

void StreamUtil::writeHexedString(OutputStream& outputStream, const Bytes& bytes) {
    ByteBuffer buf = createBufferedHexedString(bytes);
    outputStream.write(buf.getInternalBuffer(), 0, buf.size());
}

Bytes StreamUtil::inputStreamToArray(std::istream& stream) {
    return Bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

Bytes StreamUtil::readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw IoException(format(IoExceptionMessageConstant::CANNOT_OPEN_FILE, path));
    return inputStreamToArray(in);
}

void StreamUtil::writeFile(const std::string& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw IoException(format(IoExceptionMessageConstant::CANNOT_OPEN_FILE, path));
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace io
}  // namespace pdfcpp
