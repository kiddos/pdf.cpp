#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pdfcpp {

/// Binary data container used everywhere Java used byte[].
using Bytes = std::vector<uint8_t>;

inline Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline std::string toLatin1String(const Bytes& b) { return std::string(b.begin(), b.end()); }

namespace io {

/// Growable byte buffer (port of com.itextpdf.io.source.ByteBuffer).
class ByteBuffer {
public:
    ByteBuffer() : ByteBuffer(128) {}
    explicit ByteBuffer(int size);

    static int getHex(int v);

    ByteBuffer& append(uint8_t b);
    ByteBuffer& append(const uint8_t* b, int off, int len);
    ByteBuffer& append(const Bytes& b) { return b.empty() ? *this : append(b.data(), 0, static_cast<int>(b.size())); }
    ByteBuffer& append(int b) { return append(static_cast<uint8_t>(b)); }
    ByteBuffer& append(char b) { return append(static_cast<uint8_t>(b)); }
    ByteBuffer& append(const std::string& str);
    ByteBuffer& append(const char* str) { return append(std::string(str)); }
    ByteBuffer& appendHex(uint8_t b);

    uint8_t get(int index) const;
    const uint8_t* getInternalBuffer() const { return buffer_.data(); }
    uint8_t* getInternalBuffer() { return buffer_.data(); }
    int size() const { return count_; }
    bool isEmpty() const { return count_ == 0; }
    int capacity() const { return static_cast<int>(buffer_.size()); }
    ByteBuffer& reset() {
        count_ = 0;
        return *this;
    }
    Bytes toByteArray(int off, int len) const;
    Bytes toByteArray() const { return toByteArray(0, count_); }
    bool startsWith(const Bytes& b) const;
    std::string toString() const { return std::string(reinterpret_cast<const char*>(buffer_.data()), count_); }

    // Prepend support (fills from the end of the internal buffer); used by number formatting.
    ByteBuffer& prepend(uint8_t b);
    ByteBuffer& prepend(const Bytes& b);
    /// Returns the bytes written with prepend() (they live at the tail of the buffer).
    Bytes prependedBytes() const { return Bytes(buffer_.end() - count_, buffer_.end()); }

private:
    int count_ = 0;
    Bytes buffer_;
};

}  // namespace io
}  // namespace pdfcpp
