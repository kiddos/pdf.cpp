#ifndef PDFCPP_IO_SOURCE_BYTE_BUFFER_H_
#define PDFCPP_IO_SOURCE_BYTE_BUFFER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace pdfcpp {

/// Binary data container used everywhere Java used byte[].
using Bytes = std::vector<uint8_t>;

inline Bytes ToBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
inline std::string ToLatin1String(const Bytes& b) {
  return std::string(b.begin(), b.end());
}

namespace io {

/// Growable byte buffer (port of com.itextpdf.io.source.ByteBuffer).
class ByteBuffer {
 public:
  ByteBuffer() : ByteBuffer(128) {}
  explicit ByteBuffer(int size);

  static int GetHex(int v);

  ByteBuffer& Append(uint8_t b);
  ByteBuffer& Append(const uint8_t* b, int off, int len);
  ByteBuffer& Append(const Bytes& b) {
    return b.empty() ? *this : Append(b.data(), 0, static_cast<int>(b.size()));
  }
  ByteBuffer& Append(int b) { return Append(static_cast<uint8_t>(b)); }
  ByteBuffer& Append(char b) { return Append(static_cast<uint8_t>(b)); }
  ByteBuffer& Append(const std::string& str);
  ByteBuffer& Append(const char* str) { return Append(std::string(str)); }
  ByteBuffer& AppendHex(uint8_t b);

  uint8_t Get(int index) const;
  const uint8_t* GetInternalBuffer() const { return buffer_.data(); }
  uint8_t* GetInternalBuffer() { return buffer_.data(); }
  int size() const { return count_; }
  bool IsEmpty() const { return count_ == 0; }
  int Capacity() const { return static_cast<int>(buffer_.size()); }
  ByteBuffer& Reset() {
    count_ = 0;
    return *this;
  }
  Bytes ToByteArray(int off, int len) const;
  Bytes ToByteArray() const { return ToByteArray(0, count_); }
  bool StartsWith(const Bytes& b) const;
  std::string ToString() const {
    return std::string(reinterpret_cast<const char*>(buffer_.data()), count_);
  }

  // Prepend support (fills from the end of the internal buffer); used by number
  // formatting.
  ByteBuffer& Prepend(uint8_t b);
  ByteBuffer& Prepend(const Bytes& b);
  /// Returns the bytes written with prepend() (they live at the tail of the
  /// buffer).
  Bytes PrependedBytes() const {
    return Bytes(buffer_.end() - count_, buffer_.end());
  }

 private:
  int count_ = 0;
  Bytes buffer_;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_BYTE_BUFFER_H_
