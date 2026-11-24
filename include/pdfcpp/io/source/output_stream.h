#ifndef PDFCPP_IO_SOURCE_OUTPUT_STREAM_H_
#define PDFCPP_IO_SOURCE_OUTPUT_STREAM_H_

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Minimal equivalent of java.io.OutputStream.
class OutputStream {
 public:
  virtual ~OutputStream() = default;
  virtual void Write(int b) = 0;
  virtual void Write(const uint8_t* b, int off, int len) {
    for (int i = 0; i < len; ++i) Write(b[off + i]);
  }
  void Write(const Bytes& b) {
    if (!b.empty()) Write(b.data(), 0, static_cast<int>(b.size()));
  }
  void Write(const std::string& s) {
    if (!s.empty())
      Write(reinterpret_cast<const uint8_t*>(s.data()), 0,
            static_cast<int>(s.size()));
  }
  virtual void Flush() {}
  virtual void Close() {}
};

/// Something that has a "finish" step (writes trailing data without closing the
/// target).
class IFinishable {
 public:
  virtual ~IFinishable() = default;
  virtual void Finish() = 0;
};

/// In-memory output stream (port of java.io.ByteArrayOutputStream).
class ByteArrayOutputStream : public OutputStream {
 public:
  ByteArrayOutputStream() = default;
  explicit ByteArrayOutputStream(size_t reserve) { buf_.reserve(reserve); }
  void Write(int b) override { buf_.push_back(static_cast<uint8_t>(b)); }
  void Write(const uint8_t* b, int off, int len) override {
    buf_.insert(buf_.end(), b + off, b + off + len);
  }
  using OutputStream::Write;
  const Bytes& GetBuffer() const { return buf_; }
  Bytes& GetBuffer() { return buf_; }
  Bytes ToByteArray() const { return buf_; }
  size_t size() const { return buf_.size(); }
  void Reset() { buf_.clear(); }
  void AssignBytes(const uint8_t* bytes, int count) {
    buf_.assign(bytes, bytes + count);
  }
  void WriteTo(OutputStream& out) const { out.Write(buf_); }

 private:
  Bytes buf_;
};

/// Output stream writing to a file.
class FileOutputStream : public OutputStream {
 public:
  explicit FileOutputStream(const std::string& path);
  ~FileOutputStream() override;
  void Write(int b) override;
  void Write(const uint8_t* b, int off, int len) override;
  using OutputStream::Write;
  void Flush() override;
  void Close() override;

 private:
  std::ofstream out_;
};

/// Wraps a std::ostream (not owned).
class StdOutputStream : public OutputStream {
 public:
  explicit StdOutputStream(std::ostream& out) : out_(out) {}
  void Write(int b) override { out_.put(static_cast<char>(b)); }
  void Write(const uint8_t* b, int off, int len) override {
    out_.write(reinterpret_cast<const char*>(b + off), len);
  }
  using OutputStream::Write;
  void Flush() override { out_.flush(); }

 private:
  std::ostream& out_;
};

/// Counts bytes without storing them (port of CountOutputStream).
class CountOutputStream : public OutputStream {
 public:
  void Write(int) override { ++amount_; }
  void Write(const uint8_t*, int, int len) override { amount_ += len; }
  using OutputStream::Write;
  int64_t GetAmountOfWrittenBytes() const { return amount_; }

 private:
  int64_t amount_ = 0;
};

/// Port of HighPrecisionOutputStream: an output stream with helpers to write
/// PDF numbers/tokens. The wrapped stream may be owned (shared_ptr) or
/// borrowed.
class HighPrecisionOutputStream : public OutputStream {
 public:
  static bool GetHighPrecision();
  static void SetHighPrecision(bool value);

  explicit HighPrecisionOutputStream(
      std::shared_ptr<OutputStream> output_stream,
      bool local_high_precision = false);
  ~HighPrecisionOutputStream() override = default;

  bool GetLocalHighPrecision() const { return local_high_precision_; }
  void SetLocalHighPrecision(bool value) { local_high_precision_ = value; }

  void Write(int b) override;
  void Write(const uint8_t* b, int off, int len) override;
  using OutputStream::Write;
  void Flush() override;
  void Close() override;

  HighPrecisionOutputStream& WriteLong(int64_t value);
  HighPrecisionOutputStream& WriteInteger(int value);
  HighPrecisionOutputStream& WriteFloat(float value) {
    return WriteDouble(value);
  }
  HighPrecisionOutputStream& WriteFloat(float value, bool high_precision) {
    return WriteDouble(value, high_precision);
  }
  HighPrecisionOutputStream& WriteFloats(const std::vector<float>& value);
  HighPrecisionOutputStream& WriteDouble(double value);
  HighPrecisionOutputStream& WriteDouble(double value, bool high_precision);
  HighPrecisionOutputStream& WriteByte(int value);
  HighPrecisionOutputStream& WriteSpace() { return WriteByte(' '); }
  HighPrecisionOutputStream& WriteNewLine() { return WriteByte('\n'); }
  HighPrecisionOutputStream& WriteString(const std::string& value);
  HighPrecisionOutputStream& WriteBytes(const Bytes& b);
  HighPrecisionOutputStream& WriteBytes(const uint8_t* b, int off, int len);

  int64_t GetCurrentPos() const { return current_pos_; }
  OutputStream* GetOutputStream() const { return output_stream_.get(); }
  std::shared_ptr<OutputStream> GetOutputStreamPtr() const {
    return output_stream_;
  }
  bool IsCloseStream() const { return close_stream_; }
  void SetCloseStream(bool close_stream) { close_stream_ = close_stream; }
  /// Only valid when the wrapped stream is a ByteArrayOutputStream.
  void AssignBytes(const Bytes& bytes, int count);
  void Reset();

 protected:
  std::shared_ptr<OutputStream> output_stream_;
  int64_t current_pos_ = 0;
  bool close_stream_ = true;
  bool local_high_precision_ = false;
  ByteBuffer num_buffer_{32};
};

/// Port of DeflaterOutputStream using zlib.
class DeflaterOutputStream : public OutputStream, public IFinishable {
 public:
  DeflaterOutputStream(std::shared_ptr<OutputStream> out, int level = -1);
  ~DeflaterOutputStream() override;
  void Write(int b) override;
  void Write(const uint8_t* b, int off, int len) override;
  using OutputStream::Write;
  void Finish() override;
  void Close() override;

 private:
  void DeflateChunk(int flush);
  std::shared_ptr<OutputStream> out_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool finished_ = false;
};

/// Port of ASCIIHexOutputStream.
class ASCIIHexOutputStream : public OutputStream, public IFinishable {
 public:
  explicit ASCIIHexOutputStream(std::shared_ptr<OutputStream> out)
      : out_(std::move(out)) {}
  void Write(int b) override;
  using OutputStream::Write;
  void Finish() override;
  void Close() override {
    Finish();
    out_->Close();
  }

 private:
  std::shared_ptr<OutputStream> out_;
  bool finished_ = false;
};

/// Port of ASCII85OutputStream.
class ASCII85OutputStream : public OutputStream, public IFinishable {
 public:
  explicit ASCII85OutputStream(std::shared_ptr<OutputStream> out)
      : out_(std::move(out)) {}
  void Write(int b) override;
  using OutputStream::Write;
  void Finish() override;
  void Close() override {
    Finish();
    out_->Close();
  }

 private:
  void EncodeBlock(int count);
  std::shared_ptr<OutputStream> out_;
  uint8_t block_[4] = {0, 0, 0, 0};
  int block_len_ = 0;
  bool finished_ = false;
};

/// Port of RunLengthOutputStream.
class RunLengthOutputStream : public OutputStream, public IFinishable {
 public:
  explicit RunLengthOutputStream(std::shared_ptr<OutputStream> out)
      : out_(std::move(out)) {}
  void Write(int b) override { data_.push_back(static_cast<uint8_t>(b)); }
  using OutputStream::Write;
  void Finish() override;
  void Close() override {
    Finish();
    out_->Close();
  }

 private:
  std::shared_ptr<OutputStream> out_;
  Bytes data_;
  bool finished_ = false;
};

/// A non-owning shared_ptr wrapper helper for streams owned elsewhere.
template <typename T>
std::shared_ptr<T> Borrow(T& obj) {
  return std::shared_ptr<T>(&obj, [](T*) {});
}

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_OUTPUT_STREAM_H_
