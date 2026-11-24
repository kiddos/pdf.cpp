#ifndef PDFCPP_IO_SOURCE_RANDOM_ACCESS_SOURCE_H_
#define PDFCPP_IO_SOURCE_RANDOM_ACCESS_SOURCE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Port of IRandomAccessSource: random access to a byte source.
class IRandomAccessSource {
 public:
  virtual ~IRandomAccessSource() = default;
  /// Returns the byte at the given position or -1 if past the end.
  virtual int Get(int64_t position) = 0;
  /// Reads up to len bytes into bytes[off..]. Returns number of bytes read or
  /// -1 at EOF.
  virtual int Get(int64_t position, uint8_t* bytes, int off, int len) = 0;
  virtual int64_t Length() = 0;
  virtual void Close() = 0;
};

using RandomAccessSourcePtr = std::shared_ptr<IRandomAccessSource>;

/// Source backed by an in-memory array (port of ArrayRandomAccessSource).
class ArrayRandomAccessSource : public IRandomAccessSource {
 public:
  explicit ArrayRandomAccessSource(Bytes array)
      : array_(std::make_shared<Bytes>(std::move(array))) {}
  explicit ArrayRandomAccessSource(std::shared_ptr<const Bytes> array)
      : array_(std::move(array)) {}
  int Get(int64_t position) override;
  int Get(int64_t position, uint8_t* bytes, int off, int len) override;
  int64_t Length() override {
    return array_ ? static_cast<int64_t>(array_->size()) : 0;
  }
  void Close() override { array_.reset(); }

 private:
  std::shared_ptr<const Bytes> array_;
};

/// A view on another source that never closes the underlying source (port of
/// IndependentRandomAccessSource).
class IndependentRandomAccessSource : public IRandomAccessSource {
 public:
  explicit IndependentRandomAccessSource(RandomAccessSourcePtr source)
      : source_(std::move(source)) {}
  int Get(int64_t position) override { return source_->Get(position); }
  int Get(int64_t position, uint8_t* bytes, int off, int len) override {
    return source_->Get(position, bytes, off, len);
  }
  int64_t Length() override { return source_->Length(); }
  void Close() override {}

 private:
  RandomAccessSourcePtr source_;
};

/// A window [offset, offset+length) of another source (port of
/// WindowRandomAccessSource).
class WindowRandomAccessSource : public IRandomAccessSource {
 public:
  WindowRandomAccessSource(RandomAccessSourcePtr source, int64_t offset);
  WindowRandomAccessSource(RandomAccessSourcePtr source, int64_t offset,
                           int64_t length)
      : source_(std::move(source)), offset_(offset), length_(length) {}
  int Get(int64_t position) override;
  int Get(int64_t position, uint8_t* bytes, int off, int len) override;
  int64_t Length() override { return length_; }
  void Close() override { source_->Close(); }

 private:
  RandomAccessSourcePtr source_;
  int64_t offset_;
  int64_t length_;
};

/// Concatenation of several sources (port of GroupedRandomAccessSource).
class GroupedRandomAccessSource : public IRandomAccessSource {
 public:
  explicit GroupedRandomAccessSource(
      std::vector<RandomAccessSourcePtr> sources);
  int Get(int64_t position) override;
  int Get(int64_t position, uint8_t* bytes, int off, int len) override;
  int64_t Length() override { return size_; }
  void Close() override;

 private:
  struct Entry {
    RandomAccessSourcePtr source;
    int64_t first_byte;
    int64_t last_byte;
  };
  int FindIndex(int64_t position) const;
  std::vector<Entry> sources_;
  int64_t size_ = 0;
};

/// Factory for random access sources (port of RandomAccessSourceFactory).
class RandomAccessSourceFactory {
 public:
  RandomAccessSourcePtr CreateSource(const Bytes& data) const;
  RandomAccessSourcePtr CreateSource(Bytes&& data) const;
  /// Creates a source for the file at the given path (reads it into memory).
  RandomAccessSourcePtr CreateBestSource(const std::string& filename) const;
  RandomAccessSourcePtr CreateSource(std::istream& stream) const;
  RandomAccessSourcePtr CreateRanged(RandomAccessSourcePtr source,
                                     const std::vector<int64_t>& ranges) const;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_RANDOM_ACCESS_SOURCE_H_
