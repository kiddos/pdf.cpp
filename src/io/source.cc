#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/source/byte_buffer.h"
#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/io/source/random_access_file_or_array.h"
#include "pdfcpp/io/source/random_access_source.h"

namespace pdfcpp {

std::string FormatMessage(const std::string& pattern,
                          const std::vector<std::string>& params) {
  std::string out;
  out.reserve(pattern.size() + 16);
  for (size_t i = 0; i < pattern.size(); ++i) {
    char c = pattern[i];
    if (c == '{') {
      size_t close = pattern.find('}', i);
      if (close != std::string::npos && close > i + 1) {
        std::string idx = pattern.substr(i + 1, close - i - 1);
        bool digits = std::all_of(idx.begin(), idx.end(), [](char ch) {
          return ch >= '0' && ch <= '9';
        });
        if (digits) {
          size_t n = std::stoul(idx);
          out += n < params.size() ? params[n] : ("{" + idx + "}");
          i = close;
          continue;
        }
      }
    }
    out += c;
  }
  return out;
}

namespace io {

// ---------------------------------------------------------------- ByteBuffer
static const uint8_t kHexDigits[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

ByteBuffer::ByteBuffer(int size) {
  if (size < 1) size = 128;
  buffer_.resize(size);
}

int ByteBuffer::GetHex(int v) {
  if (v >= '0' && v <= '9') return v - '0';
  if (v >= 'A' && v <= 'F') return v - 'A' + 10;
  if (v >= 'a' && v <= 'f') return v - 'a' + 10;
  return -1;
}

ByteBuffer& ByteBuffer::Append(uint8_t b) {
  int new_count = count_ + 1;
  if (new_count > static_cast<int>(buffer_.size()))
    buffer_.resize(std::max(static_cast<int>(buffer_.size()) << 1, new_count));
  buffer_[count_] = b;
  count_ = new_count;
  return *this;
}

ByteBuffer& ByteBuffer::Append(const uint8_t* b, int off, int len) {
  if (off < 0 || len <= 0) return *this;
  int new_count = count_ + len;
  if (new_count > static_cast<int>(buffer_.size()))
    buffer_.resize(std::max(static_cast<int>(buffer_.size()) << 1, new_count));
  std::memcpy(buffer_.data() + count_, b + off, len);
  count_ = new_count;
  return *this;
}

ByteBuffer& ByteBuffer::Append(const std::string& str) {
  return str.empty() ? *this
                     : Append(reinterpret_cast<const uint8_t*>(str.data()), 0,
                              static_cast<int>(str.size()));
}

ByteBuffer& ByteBuffer::AppendHex(uint8_t b) {
  Append(kHexDigits[(b >> 4) & 0x0f]);
  return Append(kHexDigits[b & 0x0f]);
}

uint8_t ByteBuffer::Get(int index) const {
  if (index >= count_ || index < 0)
    throw std::out_of_range(Format("Index: {0}, Size: {1}", index, count_));
  return buffer_[index];
}

Bytes ByteBuffer::ToByteArray(int off, int len) const {
  return Bytes(buffer_.begin() + off, buffer_.begin() + off + len);
}

bool ByteBuffer::StartsWith(const Bytes& b) const {
  if (size() < static_cast<int>(b.size())) return false;
  return std::equal(b.begin(), b.end(), buffer_.begin());
}

ByteBuffer& ByteBuffer::Prepend(uint8_t b) {
  buffer_[buffer_.size() - count_ - 1] = b;
  count_++;
  return *this;
}

ByteBuffer& ByteBuffer::Prepend(const Bytes& b) {
  std::memcpy(buffer_.data() + buffer_.size() - count_ - b.size(), b.data(),
              b.size());
  count_ += static_cast<int>(b.size());
  return *this;
}

// ---------------------------------------------------------------- ByteUtils
bool ByteUtils::high_precision_ = false;

Bytes ByteUtils::GetIsoBytes(const std::string& text) {
  return Bytes(text.begin(), text.end());
}

Bytes ByteUtils::GetIsoBytes(uint8_t pre, const std::string& text,
                             uint8_t post) {
  Bytes b;
  b.reserve(text.size() + 2);
  if (pre != 0) b.push_back(pre);
  b.insert(b.end(), text.begin(), text.end());
  if (post != 0) b.push_back(post);
  return b;
}

Bytes ByteUtils::GetIsoBytes(int n) {
  return GetIsoBytes(static_cast<long long>(n));
}

Bytes ByteUtils::GetIsoBytes(long long n) {
  std::string s = std::to_string(n);
  return Bytes(s.begin(), s.end());
}

Bytes ByteUtils::GetIsoBytes(double d) {
  return GetIsoBytes(d, high_precision_);
}

static std::string FormatHighPrecision(double d) {
  // Equivalent of DecimalFormat "0.######"
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6f", d);
  std::string s(buf);
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

Bytes ByteUtils::GetIsoBytes(double d, bool high_precision) {
  if (high_precision) {
    if (std::fabs(d) < 0.000001) return {'0'};
    if (std::isnan(d)) d = 0;
    return ToBytes(FormatHighPrecision(d));
  }
  if (std::isnan(d)) d = 0;
  if (std::fabs(d) < 0.000015) return {'0'};
  bool negative = false;
  if (d < 0) {
    negative = true;
    d = -d;
  }
  std::string out;
  if (d < 1.0) {
    d += 0.000005;
    if (d >= 1) return negative ? Bytes{'-', '1'} : Bytes{'1'};
    int v = static_cast<int>(d * 100000);
    int len = 5;
    for (; len > 0; len--) {
      if (v % 10 != 0) break;
      v /= 10;
    }
    std::string frac(len, '0');
    for (int i = len - 1; i >= 0; --i) {
      frac[i] = static_cast<char>('0' + v % 10);
      v /= 10;
    }
    out = std::string(negative ? "-" : "") + "0." + frac;
  } else if (d <= 32767) {
    d += 0.005;
    int v = static_cast<int>(d * 100);
    int int_len;
    if (v >= 1000000)
      int_len = 5;
    else if (v >= 100000)
      int_len = 4;
    else if (v >= 10000)
      int_len = 3;
    else if (v >= 1000)
      int_len = 2;
    else
      int_len = 1;
    int frac_len = 0;
    if (v % 100 != 0) {
      frac_len = 2;
      if (v % 10 != 0)
        frac_len++;
      else
        v /= 10;
    } else {
      v /= 100;
    }
    std::string s;
    for (int i = 0; i < frac_len - 1; i++) {
      s.insert(s.begin(), static_cast<char>('0' + v % 10));
      v /= 10;
    }
    if (frac_len > 0) s.insert(s.begin(), '.');
    for (int i = 0; i < int_len; i++) {
      s.insert(s.begin(), static_cast<char>('0' + v % 10));
      v /= 10;
    }
    if (negative) s.insert(s.begin(), '-');
    out = s;
  } else {
    d += 0.5;
    long long v =
        d > 9.2e18 ? 9223372036854775807LL : static_cast<long long>(d);
    out = (negative ? "-" : "") + std::to_string(v);
  }
  return ToBytes(out);
}

// ---------------------------------------------------------------- Sources
int ArrayRandomAccessSource::Get(int64_t position) {
  if (!array_ || position < 0 ||
      position >= static_cast<int64_t>(array_->size()))
    return -1;
  return (*array_)[static_cast<size_t>(position)];
}

int ArrayRandomAccessSource::Get(int64_t position, uint8_t* bytes, int off,
                                 int len) {
  if (!array_) throw std::logic_error("Already closed");
  if (position < 0 || position >= static_cast<int64_t>(array_->size()))
    return -1;
  if (position + len > static_cast<int64_t>(array_->size()))
    len = static_cast<int>(array_->size() - position);
  std::memcpy(bytes + off, array_->data() + position, len);
  return len;
}

WindowRandomAccessSource::WindowRandomAccessSource(RandomAccessSourcePtr source,
                                                   int64_t offset)
    : source_(std::move(source)), offset_(offset) {
  length_ = source_->Length() - offset;
}

int WindowRandomAccessSource::Get(int64_t position) {
  if (position < 0 || position >= length_) return -1;
  return source_->Get(offset_ + position);
}

int WindowRandomAccessSource::Get(int64_t position, uint8_t* bytes, int off,
                                  int len) {
  if (position < 0 || position >= length_) return -1;
  int64_t to_read = std::min<int64_t>(len, length_ - position);
  return source_->Get(offset_ + position, bytes, off,
                      static_cast<int>(to_read));
}

GroupedRandomAccessSource::GroupedRandomAccessSource(
    std::vector<RandomAccessSourcePtr> sources) {
  int64_t total = 0;
  for (auto& s : sources) {
    int64_t len = s->Length();
    sources_.push_back({s, total, total + len - 1});
    total += len;
  }
  size_ = total;
}

int GroupedRandomAccessSource::FindIndex(int64_t position) const {
  if (position < 0) return -1;
  for (size_t i = 0; i < sources_.size(); ++i)
    if (position <= sources_[i].last_byte) return static_cast<int>(i);
  return -1;
}

int GroupedRandomAccessSource::Get(int64_t position) {
  int i = FindIndex(position);
  if (i < 0) return -1;
  return sources_[i].source->Get(position - sources_[i].first_byte);
}

int GroupedRandomAccessSource::Get(int64_t position, uint8_t* bytes, int off,
                                   int len) {
  int i = FindIndex(position);
  if (i < 0) return -1;
  int remaining = len;
  int total = 0;
  while (remaining > 0 && i < static_cast<int>(sources_.size())) {
    auto& e = sources_[i];
    if (position > e.last_byte) {
      ++i;
      continue;
    }
    int count = e.source->Get(position - e.first_byte, bytes, off, remaining);
    if (count <= 0) break;
    total += count;
    position += count;
    off += count;
    remaining -= count;
    ++i;
  }
  return total == 0 ? -1 : total;
}

void GroupedRandomAccessSource::Close() {
  for (auto& e : sources_) e.source->Close();
}

RandomAccessSourcePtr RandomAccessSourceFactory::CreateSource(
    const Bytes& data) const {
  return std::make_shared<ArrayRandomAccessSource>(data);
}

RandomAccessSourcePtr RandomAccessSourceFactory::CreateSource(
    Bytes&& data) const {
  return std::make_shared<ArrayRandomAccessSource>(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::CreateBestSource(
    const std::string& filename) const {
  std::ifstream in(filename, std::ios::binary);
  if (!in)
    throw IoException(
        Format(io_exception_message_constant::kCannotOpenFile, filename));
  Bytes data((std::istreambuf_iterator<char>(in)),
             std::istreambuf_iterator<char>());
  return CreateSource(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::CreateSource(
    std::istream& stream) const {
  Bytes data((std::istreambuf_iterator<char>(stream)),
             std::istreambuf_iterator<char>());
  return CreateSource(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::CreateRanged(
    RandomAccessSourcePtr source, const std::vector<int64_t>& ranges) const {
  std::vector<RandomAccessSourcePtr> sources;
  for (size_t i = 0; i + 1 < ranges.size(); i += 2) {
    sources.push_back(std::make_shared<WindowRandomAccessSource>(
        std::make_shared<IndependentRandomAccessSource>(source), ranges[i],
        ranges[i + 1]));
  }
  return std::make_shared<GroupedRandomAccessSource>(std::move(sources));
}

// ----------------------------------------------------------------
// RandomAccessFileOrArray
std::unique_ptr<RandomAccessFileOrArray> RandomAccessFileOrArray::CreateView()
    const {
  return std::make_unique<RandomAccessFileOrArray>(
      std::make_shared<IndependentRandomAccessSource>(byte_source_));
}

RandomAccessSourcePtr RandomAccessFileOrArray::CreateSourceView() const {
  return std::make_shared<IndependentRandomAccessSource>(byte_source_);
}

int RandomAccessFileOrArray::Read() {
  if (is_back_) {
    is_back_ = false;
    return back_;
  }
  return byte_source_->Get(byte_source_position_++);
}

int RandomAccessFileOrArray::Peek() {
  if (is_back_) return back_;
  return byte_source_->Get(byte_source_position_);
}

int RandomAccessFileOrArray::Peek(uint8_t* buffer, int length) {
  int offset = 0;
  int count = 0;
  if (is_back_ && length > 0) {
    buffer[offset++] = back_;
    --length;
    ++count;
  }
  if (length > 0) {
    int n = byte_source_->Get(byte_source_position_, buffer, offset, length);
    if (n > 0) count += n;
  }
  return count;
}

int RandomAccessFileOrArray::Read(uint8_t* b, int off, int len) {
  if (len == 0) return 0;
  int count = 0;
  if (is_back_ && len > 0) {
    is_back_ = false;
    b[off++] = back_;
    --len;
    count++;
  }
  if (len > 0) {
    int n = byte_source_->Get(byte_source_position_, b, off, len);
    if (n > 0) {
      count += n;
      byte_source_position_ += n;
    }
  }
  if (count == 0) return -1;
  return count;
}

void RandomAccessFileOrArray::ReadFully(uint8_t* b, int off, int len) {
  int n = 0;
  while (n < len) {
    int count = Read(b, off + n, len - n);
    if (count < 0)
      throw IoException(io_exception_message_constant::kUnexpectedEndOfFile);
    n += count;
  }
}

int64_t RandomAccessFileOrArray::Skip(int64_t n) {
  if (n <= 0) return 0;
  int adj = 0;
  if (is_back_) {
    is_back_ = false;
    if (n == 1) return 1;
    --n;
    adj = 1;
  }
  int64_t pos = GetPosition();
  int64_t len = Length();
  int64_t newpos = pos + n;
  if (newpos > len) newpos = len;
  Seek(newpos);
  return newpos - pos + adj;
}

void RandomAccessFileOrArray::Close() {
  is_back_ = false;
  byte_source_->Close();
}

static int ReadOrThrow(RandomAccessFileOrArray& f) {
  int ch = f.Read();
  if (ch < 0)
    throw IoException(io_exception_message_constant::kUnexpectedEndOfFile);
  return ch;
}

bool RandomAccessFileOrArray::ReadBoolean() { return ReadOrThrow(*this) != 0; }
int8_t RandomAccessFileOrArray::ReadByte() {
  return static_cast<int8_t>(ReadOrThrow(*this));
}
int RandomAccessFileOrArray::ReadUnsignedByte() { return ReadOrThrow(*this); }

int16_t RandomAccessFileOrArray::ReadShort() {
  int ch1 = ReadOrThrow(*this), ch2 = ReadOrThrow(*this);
  return static_cast<int16_t>((ch1 << 8) + ch2);
}
int16_t RandomAccessFileOrArray::ReadShortLe() {
  int ch1 = ReadOrThrow(*this), ch2 = ReadOrThrow(*this);
  return static_cast<int16_t>((ch2 << 8) + ch1);
}
int RandomAccessFileOrArray::ReadUnsignedShort() {
  int ch1 = ReadOrThrow(*this), ch2 = ReadOrThrow(*this);
  return (ch1 << 8) + ch2;
}
int RandomAccessFileOrArray::ReadUnsignedShortLe() {
  int ch1 = ReadOrThrow(*this), ch2 = ReadOrThrow(*this);
  return (ch2 << 8) + ch1;
}
char16_t RandomAccessFileOrArray::ReadChar() {
  return static_cast<char16_t>(ReadUnsignedShort());
}
char16_t RandomAccessFileOrArray::ReadCharLe() {
  return static_cast<char16_t>(ReadUnsignedShortLe());
}

int32_t RandomAccessFileOrArray::ReadInt() {
  uint32_t a = ReadOrThrow(*this), b = ReadOrThrow(*this),
           c = ReadOrThrow(*this), d = ReadOrThrow(*this);
  return static_cast<int32_t>((a << 24) | (b << 16) | (c << 8) | d);
}
int32_t RandomAccessFileOrArray::ReadIntLe() {
  uint32_t a = ReadOrThrow(*this), b = ReadOrThrow(*this),
           c = ReadOrThrow(*this), d = ReadOrThrow(*this);
  return static_cast<int32_t>((d << 24) | (c << 16) | (b << 8) | a);
}
int64_t RandomAccessFileOrArray::ReadUnsignedInt() {
  return static_cast<uint32_t>(ReadInt());
}
int64_t RandomAccessFileOrArray::ReadUnsignedIntLe() {
  return static_cast<uint32_t>(ReadIntLe());
}
int64_t RandomAccessFileOrArray::ReadLong() {
  uint64_t hi = static_cast<uint32_t>(ReadInt());
  uint64_t lo = static_cast<uint32_t>(ReadInt());
  return static_cast<int64_t>((hi << 32) | lo);
}
int64_t RandomAccessFileOrArray::ReadLongLe() {
  uint64_t lo = static_cast<uint32_t>(ReadIntLe());
  uint64_t hi = static_cast<uint32_t>(ReadIntLe());
  return static_cast<int64_t>((hi << 32) | lo);
}
float RandomAccessFileOrArray::ReadFloat() {
  int32_t i = ReadInt();
  float f;
  std::memcpy(&f, &i, 4);
  return f;
}
float RandomAccessFileOrArray::ReadFloatLe() {
  int32_t i = ReadIntLe();
  float f;
  std::memcpy(&f, &i, 4);
  return f;
}
double RandomAccessFileOrArray::ReadDouble() {
  int64_t i = ReadLong();
  double d;
  std::memcpy(&d, &i, 8);
  return d;
}
double RandomAccessFileOrArray::ReadDoubleLe() {
  int64_t i = ReadLongLe();
  double d;
  std::memcpy(&d, &i, 8);
  return d;
}

std::string RandomAccessFileOrArray::ReadLine() {
  std::string input;
  int c = -1;
  bool eol = false;
  while (!eol) {
    switch (c = Read()) {
      case -1:
      case '\n':
        eol = true;
        break;
      case '\r': {
        eol = true;
        int64_t cur = GetPosition();
        if (Read() != '\n') Seek(cur);
        break;
      }
      default:
        input += static_cast<char>(c);
        break;
    }
  }
  if (c == -1 && input.empty()) return std::string();
  return input;
}

std::string RandomAccessFileOrArray::ReadString(int length,
                                                const std::string& encoding) {
  Bytes buf(length);
  ReadFully(buf);
  if (encoding == "UnicodeBigUnmarked" || encoding == "UTF-16BE") {
    // convert UTF-16BE to UTF-8
    std::string out;
    for (size_t i = 0; i + 1 < buf.size(); i += 2) {
      uint32_t cp = (buf[i] << 8) | buf[i + 1];
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < buf.size()) {
        uint32_t lo = (buf[i + 2] << 8) | buf[i + 3];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          i += 2;
        }
      }
      if (cp < 0x80)
        out += static_cast<char>(cp);
      else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      }
    }
    return out;
  }
  return std::string(buf.begin(), buf.end());
}

// ---------------------------------------------------------------- Output
// streams
FileOutputStream::FileOutputStream(const std::string& path)
    : out_(path, std::ios::binary | std::ios::trunc) {
  if (!out_)
    throw IoException(
        Format(io_exception_message_constant::kCannotOpenFile, path));
}
FileOutputStream::~FileOutputStream() {
  if (out_.is_open()) out_.close();
}
void FileOutputStream::Write(int b) { out_.put(static_cast<char>(b)); }
void FileOutputStream::Write(const uint8_t* b, int off, int len) {
  out_.write(reinterpret_cast<const char*>(b + off), len);
}
void FileOutputStream::Flush() { out_.flush(); }
void FileOutputStream::Close() {
  if (out_.is_open()) out_.close();
}

static bool g_high_precision = false;
bool HighPrecisionOutputStream::GetHighPrecision() { return g_high_precision; }
void HighPrecisionOutputStream::SetHighPrecision(bool value) {
  g_high_precision = value;
}

HighPrecisionOutputStream::HighPrecisionOutputStream(
    std::shared_ptr<OutputStream> output_stream, bool local_high_precision)
    : output_stream_(std::move(output_stream)),
      local_high_precision_(local_high_precision) {}

void HighPrecisionOutputStream::Write(int b) {
  output_stream_->Write(b);
  current_pos_++;
}
void HighPrecisionOutputStream::Write(const uint8_t* b, int off, int len) {
  output_stream_->Write(b, off, len);
  current_pos_ += len;
}
void HighPrecisionOutputStream::Flush() { output_stream_->Flush(); }
void HighPrecisionOutputStream::Close() {
  if (close_stream_) output_stream_->Close();
}

HighPrecisionOutputStream& HighPrecisionOutputStream::WriteLong(int64_t value) {
  std::string s = std::to_string(value);
  Write(reinterpret_cast<const uint8_t*>(s.data()), 0,
        static_cast<int>(s.size()));
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteInteger(int value) {
  return WriteLong(value);
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteFloats(
    const std::vector<float>& value) {
  for (size_t i = 0; i < value.size(); i++) {
    WriteFloat(value[i]);
    if (i < value.size() - 1) WriteSpace();
  }
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteDouble(
    double value) {
  return WriteDouble(value, local_high_precision_ || g_high_precision);
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteDouble(
    double value, bool high_precision) {
  Bytes b = ByteUtils::GetIsoBytes(value, high_precision);
  Write(b.data(), 0, static_cast<int>(b.size()));
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteByte(int value) {
  Write(value);
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteString(
    const std::string& value) {
  Write(reinterpret_cast<const uint8_t*>(value.data()), 0,
        static_cast<int>(value.size()));
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteBytes(
    const Bytes& b) {
  if (!b.empty()) Write(b.data(), 0, static_cast<int>(b.size()));
  return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::WriteBytes(
    const uint8_t* b, int off, int len) {
  Write(b, off, len);
  return *this;
}
void HighPrecisionOutputStream::AssignBytes(const Bytes& bytes, int count) {
  if (auto* baos = dynamic_cast<ByteArrayOutputStream*>(output_stream_.get())) {
    baos->AssignBytes(bytes.data(), count);
    current_pos_ = count;
  } else {
    throw IoException("Bytes can be assigned to ByteArrayOutputStream only.");
  }
}
void HighPrecisionOutputStream::Reset() {
  if (auto* baos = dynamic_cast<ByteArrayOutputStream*>(output_stream_.get())) {
    baos->Reset();
    current_pos_ = 0;
  } else {
    throw IoException("Bytes can be reset in ByteArrayOutputStream only.");
  }
}

struct DeflaterOutputStream::Impl {
  z_stream zs{};
  bool init = false;
};

DeflaterOutputStream::DeflaterOutputStream(std::shared_ptr<OutputStream> out,
                                           int level)
    : out_(std::move(out)), impl_(std::make_unique<Impl>()) {
  if (level < -1 || level > 9) level = -1;
  if (deflateInit(&impl_->zs, level) != Z_OK)
    throw IoException("Cannot initialize deflater");
  impl_->init = true;
}

DeflaterOutputStream::~DeflaterOutputStream() {
  if (impl_->init) deflateEnd(&impl_->zs);
}

void DeflaterOutputStream::Write(int b) {
  uint8_t c = static_cast<uint8_t>(b);
  Write(&c, 0, 1);
}

void DeflaterOutputStream::Write(const uint8_t* b, int off, int len) {
  if (len <= 0) return;
  impl_->zs.next_in = const_cast<Bytef*>(b + off);
  impl_->zs.avail_in = static_cast<uInt>(len);
  DeflateChunk(Z_NO_FLUSH);
}

void DeflaterOutputStream::DeflateChunk(int flush) {
  uint8_t buf[16384];
  int ret;
  do {
    impl_->zs.next_out = buf;
    impl_->zs.avail_out = sizeof buf;
    ret = deflate(&impl_->zs, flush);
    int have = static_cast<int>(sizeof buf - impl_->zs.avail_out);
    if (have > 0) out_->Write(buf, 0, have);
  } while (impl_->zs.avail_out == 0 ||
           (flush == Z_FINISH && ret != Z_STREAM_END));
}

void DeflaterOutputStream::Finish() {
  if (finished_) return;
  finished_ = true;
  impl_->zs.next_in = nullptr;
  impl_->zs.avail_in = 0;
  DeflateChunk(Z_FINISH);
}

void DeflaterOutputStream::Close() {
  Finish();
  out_->Close();
}

void ASCIIHexOutputStream::Write(int b) {
  out_->Write(kHexDigits[(b >> 4) & 0x0f]);
  out_->Write(kHexDigits[b & 0x0f]);
}
void ASCIIHexOutputStream::Finish() {
  if (finished_) return;
  finished_ = true;
  out_->Write('>');
}

void ASCII85OutputStream::Write(int b) {
  block_[block_len_++] = static_cast<uint8_t>(b);
  if (block_len_ == 4) EncodeBlock(4);
}

void ASCII85OutputStream::EncodeBlock(int count) {
  uint32_t word = (static_cast<uint32_t>(block_[0]) << 24) |
                  (static_cast<uint32_t>(block_[1]) << 16) |
                  (static_cast<uint32_t>(block_[2]) << 8) | block_[3];
  if (count == 4 && word == 0) {
    out_->Write('z');
  } else {
    char enc[5];
    for (int i = 4; i >= 0; --i) {
      enc[i] = static_cast<char>(word % 85 + '!');
      word /= 85;
    }
    for (int i = 0; i < count + 1; ++i) out_->Write(enc[i]);
  }
  block_len_ = 0;
  block_[0] = block_[1] = block_[2] = block_[3] = 0;
}

void ASCII85OutputStream::Finish() {
  if (finished_) return;
  finished_ = true;
  if (block_len_ > 0) {
    int n = block_len_;
    for (int i = n; i < 4; ++i) block_[i] = 0;
    uint32_t word = (static_cast<uint32_t>(block_[0]) << 24) |
                    (static_cast<uint32_t>(block_[1]) << 16) |
                    (static_cast<uint32_t>(block_[2]) << 8) | block_[3];
    char enc[5];
    for (int i = 4; i >= 0; --i) {
      enc[i] = static_cast<char>(word % 85 + '!');
      word /= 85;
    }
    for (int i = 0; i < n + 1; ++i) out_->Write(enc[i]);
  }
  out_->Write('~');
  out_->Write('>');
}

void RunLengthOutputStream::Finish() {
  if (finished_) return;
  finished_ = true;
  size_t i = 0;
  const size_t n = data_.size();
  while (i < n) {
    // find run
    size_t run = 1;
    while (i + run < n && run < 128 && data_[i + run] == data_[i]) ++run;
    if (run >= 2) {
      out_->Write(static_cast<int>(257 - run));
      out_->Write(data_[i]);
      i += run;
    } else {
      size_t start = i;
      size_t len = 0;
      while (i < n && len < 128) {
        if (i + 1 < n && data_[i + 1] == data_[i]) break;
        ++i;
        ++len;
      }
      if (len == 0) {
        len = 1;
        ++i;
      }
      out_->Write(static_cast<int>(len - 1));
      out_->Write(data_.data(), static_cast<int>(start), static_cast<int>(len));
    }
  }
  out_->Write(128);
}

}  // namespace io
}  // namespace pdfcpp
