#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Port of IRandomAccessSource: random access to a byte source.
class IRandomAccessSource {
public:
    virtual ~IRandomAccessSource() = default;
    /// Returns the byte at the given position or -1 if past the end.
    virtual int get(int64_t position) = 0;
    /// Reads up to len bytes into bytes[off..]. Returns number of bytes read or -1 at EOF.
    virtual int get(int64_t position, uint8_t* bytes, int off, int len) = 0;
    virtual int64_t length() = 0;
    virtual void close() = 0;
};

using RandomAccessSourcePtr = std::shared_ptr<IRandomAccessSource>;

/// Source backed by an in-memory array (port of ArrayRandomAccessSource).
class ArrayRandomAccessSource : public IRandomAccessSource {
public:
    explicit ArrayRandomAccessSource(Bytes array) : array_(std::make_shared<Bytes>(std::move(array))) {}
    explicit ArrayRandomAccessSource(std::shared_ptr<const Bytes> array) : array_(std::move(array)) {}
    int get(int64_t position) override;
    int get(int64_t position, uint8_t* bytes, int off, int len) override;
    int64_t length() override { return array_ ? static_cast<int64_t>(array_->size()) : 0; }
    void close() override { array_.reset(); }

private:
    std::shared_ptr<const Bytes> array_;
};

/// A view on another source that never closes the underlying source (port of IndependentRandomAccessSource).
class IndependentRandomAccessSource : public IRandomAccessSource {
public:
    explicit IndependentRandomAccessSource(RandomAccessSourcePtr source) : source_(std::move(source)) {}
    int get(int64_t position) override { return source_->get(position); }
    int get(int64_t position, uint8_t* bytes, int off, int len) override { return source_->get(position, bytes, off, len); }
    int64_t length() override { return source_->length(); }
    void close() override {}

private:
    RandomAccessSourcePtr source_;
};

/// A window [offset, offset+length) of another source (port of WindowRandomAccessSource).
class WindowRandomAccessSource : public IRandomAccessSource {
public:
    WindowRandomAccessSource(RandomAccessSourcePtr source, int64_t offset);
    WindowRandomAccessSource(RandomAccessSourcePtr source, int64_t offset, int64_t length)
        : source_(std::move(source)), offset_(offset), length_(length) {}
    int get(int64_t position) override;
    int get(int64_t position, uint8_t* bytes, int off, int len) override;
    int64_t length() override { return length_; }
    void close() override { source_->close(); }

private:
    RandomAccessSourcePtr source_;
    int64_t offset_;
    int64_t length_;
};

/// Concatenation of several sources (port of GroupedRandomAccessSource).
class GroupedRandomAccessSource : public IRandomAccessSource {
public:
    explicit GroupedRandomAccessSource(std::vector<RandomAccessSourcePtr> sources);
    int get(int64_t position) override;
    int get(int64_t position, uint8_t* bytes, int off, int len) override;
    int64_t length() override { return size_; }
    void close() override;

private:
    struct Entry {
        RandomAccessSourcePtr source;
        int64_t firstByte;
        int64_t lastByte;
    };
    int findIndex(int64_t position) const;
    std::vector<Entry> sources_;
    int64_t size_ = 0;
};

/// Factory for random access sources (port of RandomAccessSourceFactory).
class RandomAccessSourceFactory {
public:
    RandomAccessSourcePtr createSource(const Bytes& data) const;
    RandomAccessSourcePtr createSource(Bytes&& data) const;
    /// Creates a source for the file at the given path (reads it into memory).
    RandomAccessSourcePtr createBestSource(const std::string& filename) const;
    RandomAccessSourcePtr createSource(std::istream& stream) const;
    RandomAccessSourcePtr createRanged(RandomAccessSourcePtr source, const std::vector<int64_t>& ranges) const;
};

}  // namespace io
}  // namespace pdfcpp
