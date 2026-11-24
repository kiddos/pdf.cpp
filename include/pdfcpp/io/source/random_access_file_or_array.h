#ifndef PDFCPP_IO_SOURCE_RANDOM_ACCESS_FILE_OR_ARRAY_H_
#define PDFCPP_IO_SOURCE_RANDOM_ACCESS_FILE_OR_ARRAY_H_

#include <memory>
#include <string>

#include "pdfcpp/io/source/random_access_source.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.source.RandomAccessFileOrArray: positioned reader
/// over a byte source, with one byte of push-back and big/little endian
/// primitive readers.
class RandomAccessFileOrArray {
 public:
  explicit RandomAccessFileOrArray(RandomAccessSourcePtr byte_source)
      : byte_source_(std::move(byte_source)) {}

  std::unique_ptr<RandomAccessFileOrArray> CreateView() const;
  RandomAccessSourcePtr CreateSourceView() const;

  void PushBack(uint8_t b) {
    back_ = b;
    is_back_ = true;
  }
  int Read();
  int Peek();
  int Peek(uint8_t* buffer, int length);
  int Read(uint8_t* b, int off, int len);
  int Read(Bytes& b) { return Read(b.data(), 0, static_cast<int>(b.size())); }
  void ReadFully(uint8_t* b, int off, int len);
  void ReadFully(Bytes& b) {
    ReadFully(b.data(), 0, static_cast<int>(b.size()));
  }
  int64_t Skip(int64_t n);
  int SkipBytes(int n) { return static_cast<int>(Skip(n)); }
  void Close();
  int64_t Length() const { return byte_source_->Length(); }
  void Seek(int64_t pos) {
    byte_source_position_ = pos;
    is_back_ = false;
  }
  int64_t GetPosition() const {
    return byte_source_position_ - (is_back_ ? 1 : 0);
  }

  bool ReadBoolean();
  int8_t ReadByte();
  int ReadUnsignedByte();
  int16_t ReadShort();
  int16_t ReadShortLe();
  int ReadUnsignedShort();
  int ReadUnsignedShortLe();
  char16_t ReadChar();
  char16_t ReadCharLe();
  int32_t ReadInt();
  int32_t ReadIntLe();
  int64_t ReadUnsignedInt();
  int64_t ReadUnsignedIntLe();
  int64_t ReadLong();
  int64_t ReadLongLe();
  float ReadFloat();
  float ReadFloatLe();
  double ReadDouble();
  double ReadDoubleLe();
  std::string ReadLine();
  /// Reads `length` bytes as a string in the given encoding (latin-1 style for
  /// byte encodings).
  std::string ReadString(int length, const std::string& encoding);

  IRandomAccessSource* GetByteSource() const { return byte_source_.get(); }
  const RandomAccessSourcePtr& GetByteSourcePtr() const { return byte_source_; }

 private:
  RandomAccessSourcePtr byte_source_;
  int64_t byte_source_position_ = 0;
  uint8_t back_ = 0;
  bool is_back_ = false;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_RANDOM_ACCESS_FILE_OR_ARRAY_H_
