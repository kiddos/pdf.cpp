#pragma once

#include <memory>
#include <string>

#include "pdfcpp/io/source/RandomAccessSource.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.source.RandomAccessFileOrArray: positioned reader over a byte source,
/// with one byte of push-back and big/little endian primitive readers.
class RandomAccessFileOrArray {
public:
    explicit RandomAccessFileOrArray(RandomAccessSourcePtr byteSource) : byteSource_(std::move(byteSource)) {}

    std::unique_ptr<RandomAccessFileOrArray> createView() const;
    RandomAccessSourcePtr createSourceView() const;

    void pushBack(uint8_t b) {
        back_ = b;
        isBack_ = true;
    }
    int read();
    int peek();
    int peek(uint8_t* buffer, int length);
    int read(uint8_t* b, int off, int len);
    int read(Bytes& b) { return read(b.data(), 0, static_cast<int>(b.size())); }
    void readFully(uint8_t* b, int off, int len);
    void readFully(Bytes& b) { readFully(b.data(), 0, static_cast<int>(b.size())); }
    int64_t skip(int64_t n);
    int skipBytes(int n) { return static_cast<int>(skip(n)); }
    void close();
    int64_t length() const { return byteSource_->length(); }
    void seek(int64_t pos) {
        byteSourcePosition_ = pos;
        isBack_ = false;
    }
    int64_t getPosition() const { return byteSourcePosition_ - (isBack_ ? 1 : 0); }

    bool readBoolean();
    int8_t readByte();
    int readUnsignedByte();
    int16_t readShort();
    int16_t readShortLE();
    int readUnsignedShort();
    int readUnsignedShortLE();
    char16_t readChar();
    char16_t readCharLE();
    int32_t readInt();
    int32_t readIntLE();
    int64_t readUnsignedInt();
    int64_t readUnsignedIntLE();
    int64_t readLong();
    int64_t readLongLE();
    float readFloat();
    float readFloatLE();
    double readDouble();
    double readDoubleLE();
    std::string readLine();
    /// Reads `length` bytes as a string in the given encoding (latin-1 style for byte encodings).
    std::string readString(int length, const std::string& encoding);

    IRandomAccessSource* getByteSource() const { return byteSource_.get(); }
    const RandomAccessSourcePtr& getByteSourcePtr() const { return byteSource_; }

private:
    RandomAccessSourcePtr byteSource_;
    int64_t byteSourcePosition_ = 0;
    uint8_t back_ = 0;
    bool isBack_ = false;
};

}  // namespace io
}  // namespace pdfcpp
