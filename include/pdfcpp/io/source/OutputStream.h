#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Minimal equivalent of java.io.OutputStream.
class OutputStream {
public:
    virtual ~OutputStream() = default;
    virtual void write(int b) = 0;
    virtual void write(const uint8_t* b, int off, int len) {
        for (int i = 0; i < len; ++i) write(b[off + i]);
    }
    void write(const Bytes& b) {
        if (!b.empty()) write(b.data(), 0, static_cast<int>(b.size()));
    }
    void write(const std::string& s) {
        if (!s.empty()) write(reinterpret_cast<const uint8_t*>(s.data()), 0, static_cast<int>(s.size()));
    }
    virtual void flush() {}
    virtual void close() {}
};

/// Something that has a "finish" step (writes trailing data without closing the target).
class IFinishable {
public:
    virtual ~IFinishable() = default;
    virtual void finish() = 0;
};

/// In-memory output stream (port of java.io.ByteArrayOutputStream).
class ByteArrayOutputStream : public OutputStream {
public:
    ByteArrayOutputStream() = default;
    explicit ByteArrayOutputStream(size_t reserve) { buf_.reserve(reserve); }
    void write(int b) override { buf_.push_back(static_cast<uint8_t>(b)); }
    void write(const uint8_t* b, int off, int len) override { buf_.insert(buf_.end(), b + off, b + off + len); }
    using OutputStream::write;
    const Bytes& getBuffer() const { return buf_; }
    Bytes& getBuffer() { return buf_; }
    Bytes toByteArray() const { return buf_; }
    size_t size() const { return buf_.size(); }
    void reset() { buf_.clear(); }
    void assignBytes(const uint8_t* bytes, int count) { buf_.assign(bytes, bytes + count); }
    void writeTo(OutputStream& out) const { out.write(buf_); }

private:
    Bytes buf_;
};

/// Output stream writing to a file.
class FileOutputStream : public OutputStream {
public:
    explicit FileOutputStream(const std::string& path);
    ~FileOutputStream() override;
    void write(int b) override;
    void write(const uint8_t* b, int off, int len) override;
    using OutputStream::write;
    void flush() override;
    void close() override;

private:
    std::ofstream out_;
};

/// Wraps a std::ostream (not owned).
class StdOutputStream : public OutputStream {
public:
    explicit StdOutputStream(std::ostream& out) : out_(out) {}
    void write(int b) override { out_.put(static_cast<char>(b)); }
    void write(const uint8_t* b, int off, int len) override { out_.write(reinterpret_cast<const char*>(b + off), len); }
    using OutputStream::write;
    void flush() override { out_.flush(); }

private:
    std::ostream& out_;
};

/// Counts bytes without storing them (port of CountOutputStream).
class CountOutputStream : public OutputStream {
public:
    void write(int) override { ++amount_; }
    void write(const uint8_t*, int, int len) override { amount_ += len; }
    using OutputStream::write;
    int64_t getAmountOfWrittenBytes() const { return amount_; }

private:
    int64_t amount_ = 0;
};

/// Port of HighPrecisionOutputStream: an output stream with helpers to write PDF numbers/tokens.
/// The wrapped stream may be owned (shared_ptr) or borrowed.
class HighPrecisionOutputStream : public OutputStream {
public:
    static bool getHighPrecision();
    static void setHighPrecision(bool value);

    explicit HighPrecisionOutputStream(std::shared_ptr<OutputStream> outputStream, bool localHighPrecision = false);
    ~HighPrecisionOutputStream() override = default;

    bool getLocalHighPrecision() const { return localHighPrecision_; }
    void setLocalHighPrecision(bool value) { localHighPrecision_ = value; }

    void write(int b) override;
    void write(const uint8_t* b, int off, int len) override;
    using OutputStream::write;
    void flush() override;
    void close() override;

    HighPrecisionOutputStream& writeLong(int64_t value);
    HighPrecisionOutputStream& writeInteger(int value);
    HighPrecisionOutputStream& writeFloat(float value) { return writeDouble(value); }
    HighPrecisionOutputStream& writeFloat(float value, bool highPrecision) { return writeDouble(value, highPrecision); }
    HighPrecisionOutputStream& writeFloats(const std::vector<float>& value);
    HighPrecisionOutputStream& writeDouble(double value);
    HighPrecisionOutputStream& writeDouble(double value, bool highPrecision);
    HighPrecisionOutputStream& writeByte(int value);
    HighPrecisionOutputStream& writeSpace() { return writeByte(' '); }
    HighPrecisionOutputStream& writeNewLine() { return writeByte('\n'); }
    HighPrecisionOutputStream& writeString(const std::string& value);
    HighPrecisionOutputStream& writeBytes(const Bytes& b);
    HighPrecisionOutputStream& writeBytes(const uint8_t* b, int off, int len);

    int64_t getCurrentPos() const { return currentPos_; }
    OutputStream* getOutputStream() const { return outputStream_.get(); }
    std::shared_ptr<OutputStream> getOutputStreamPtr() const { return outputStream_; }
    bool isCloseStream() const { return closeStream_; }
    void setCloseStream(bool closeStream) { closeStream_ = closeStream; }
    /// Only valid when the wrapped stream is a ByteArrayOutputStream.
    void assignBytes(const Bytes& bytes, int count);
    void reset();

protected:
    std::shared_ptr<OutputStream> outputStream_;
    int64_t currentPos_ = 0;
    bool closeStream_ = true;
    bool localHighPrecision_ = false;
    ByteBuffer numBuffer_{32};
};

/// Port of DeflaterOutputStream using zlib.
class DeflaterOutputStream : public OutputStream, public IFinishable {
public:
    DeflaterOutputStream(std::shared_ptr<OutputStream> out, int level = -1);
    ~DeflaterOutputStream() override;
    void write(int b) override;
    void write(const uint8_t* b, int off, int len) override;
    using OutputStream::write;
    void finish() override;
    void close() override;

private:
    void deflateChunk(int flush);
    std::shared_ptr<OutputStream> out_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool finished_ = false;
};

/// Port of ASCIIHexOutputStream.
class ASCIIHexOutputStream : public OutputStream, public IFinishable {
public:
    explicit ASCIIHexOutputStream(std::shared_ptr<OutputStream> out) : out_(std::move(out)) {}
    void write(int b) override;
    using OutputStream::write;
    void finish() override;
    void close() override {
        finish();
        out_->close();
    }

private:
    std::shared_ptr<OutputStream> out_;
    bool finished_ = false;
};

/// Port of ASCII85OutputStream.
class ASCII85OutputStream : public OutputStream, public IFinishable {
public:
    explicit ASCII85OutputStream(std::shared_ptr<OutputStream> out) : out_(std::move(out)) {}
    void write(int b) override;
    using OutputStream::write;
    void finish() override;
    void close() override {
        finish();
        out_->close();
    }

private:
    void encodeBlock(int count);
    std::shared_ptr<OutputStream> out_;
    uint8_t block_[4] = {0, 0, 0, 0};
    int blockLen_ = 0;
    bool finished_ = false;
};

/// Port of RunLengthOutputStream.
class RunLengthOutputStream : public OutputStream, public IFinishable {
public:
    explicit RunLengthOutputStream(std::shared_ptr<OutputStream> out) : out_(std::move(out)) {}
    void write(int b) override { data_.push_back(static_cast<uint8_t>(b)); }
    using OutputStream::write;
    void finish() override;
    void close() override {
        finish();
        out_->close();
    }

private:
    std::shared_ptr<OutputStream> out_;
    Bytes data_;
    bool finished_ = false;
};

/// A non-owning shared_ptr wrapper helper for streams owned elsewhere.
template <typename T>
std::shared_ptr<T> borrow(T& obj) {
    return std::shared_ptr<T>(&obj, [](T*) {});
}

}  // namespace io
}  // namespace pdfcpp
