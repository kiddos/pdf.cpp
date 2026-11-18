#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/source/ByteBuffer.h"
#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/io/source/RandomAccessFileOrArray.h"
#include "pdfcpp/io/source/RandomAccessSource.h"

namespace pdfcpp {

std::string formatMessage(const std::string& pattern, const std::vector<std::string>& params) {
    std::string out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '{') {
            size_t close = pattern.find('}', i);
            if (close != std::string::npos && close > i + 1) {
                std::string idx = pattern.substr(i + 1, close - i - 1);
                bool digits = std::all_of(idx.begin(), idx.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
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
static const uint8_t HEX_DIGITS[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

ByteBuffer::ByteBuffer(int size) {
    if (size < 1) size = 128;
    buffer_.resize(size);
}

int ByteBuffer::getHex(int v) {
    if (v >= '0' && v <= '9') return v - '0';
    if (v >= 'A' && v <= 'F') return v - 'A' + 10;
    if (v >= 'a' && v <= 'f') return v - 'a' + 10;
    return -1;
}

ByteBuffer& ByteBuffer::append(uint8_t b) {
    int newCount = count_ + 1;
    if (newCount > static_cast<int>(buffer_.size())) buffer_.resize(std::max(static_cast<int>(buffer_.size()) << 1, newCount));
    buffer_[count_] = b;
    count_ = newCount;
    return *this;
}

ByteBuffer& ByteBuffer::append(const uint8_t* b, int off, int len) {
    if (off < 0 || len <= 0) return *this;
    int newCount = count_ + len;
    if (newCount > static_cast<int>(buffer_.size())) buffer_.resize(std::max(static_cast<int>(buffer_.size()) << 1, newCount));
    std::memcpy(buffer_.data() + count_, b + off, len);
    count_ = newCount;
    return *this;
}

ByteBuffer& ByteBuffer::append(const std::string& str) {
    return str.empty() ? *this : append(reinterpret_cast<const uint8_t*>(str.data()), 0, static_cast<int>(str.size()));
}

ByteBuffer& ByteBuffer::appendHex(uint8_t b) {
    append(HEX_DIGITS[(b >> 4) & 0x0f]);
    return append(HEX_DIGITS[b & 0x0f]);
}

uint8_t ByteBuffer::get(int index) const {
    if (index >= count_ || index < 0) throw std::out_of_range(format("Index: {0}, Size: {1}", index, count_));
    return buffer_[index];
}

Bytes ByteBuffer::toByteArray(int off, int len) const { return Bytes(buffer_.begin() + off, buffer_.begin() + off + len); }

bool ByteBuffer::startsWith(const Bytes& b) const {
    if (size() < static_cast<int>(b.size())) return false;
    return std::equal(b.begin(), b.end(), buffer_.begin());
}

ByteBuffer& ByteBuffer::prepend(uint8_t b) {
    buffer_[buffer_.size() - count_ - 1] = b;
    count_++;
    return *this;
}

ByteBuffer& ByteBuffer::prepend(const Bytes& b) {
    std::memcpy(buffer_.data() + buffer_.size() - count_ - b.size(), b.data(), b.size());
    count_ += static_cast<int>(b.size());
    return *this;
}

// ---------------------------------------------------------------- ByteUtils
bool ByteUtils::HighPrecision = false;

Bytes ByteUtils::getIsoBytes(const std::string& text) { return Bytes(text.begin(), text.end()); }

Bytes ByteUtils::getIsoBytes(uint8_t pre, const std::string& text, uint8_t post) {
    Bytes b;
    b.reserve(text.size() + 2);
    if (pre != 0) b.push_back(pre);
    b.insert(b.end(), text.begin(), text.end());
    if (post != 0) b.push_back(post);
    return b;
}

Bytes ByteUtils::getIsoBytes(int n) { return getIsoBytes(static_cast<long long>(n)); }

Bytes ByteUtils::getIsoBytes(long long n) {
    std::string s = std::to_string(n);
    return Bytes(s.begin(), s.end());
}

Bytes ByteUtils::getIsoBytes(double d) { return getIsoBytes(d, HighPrecision); }

static std::string formatHighPrecision(double d) {
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

Bytes ByteUtils::getIsoBytes(double d, bool highPrecision) {
    if (highPrecision) {
        if (std::fabs(d) < 0.000001) return {'0'};
        if (std::isnan(d)) d = 0;
        return toBytes(formatHighPrecision(d));
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
        int intLen;
        if (v >= 1000000) intLen = 5;
        else if (v >= 100000) intLen = 4;
        else if (v >= 10000) intLen = 3;
        else if (v >= 1000) intLen = 2;
        else intLen = 1;
        int fracLen = 0;
        if (v % 100 != 0) {
            fracLen = 2;
            if (v % 10 != 0) fracLen++;
            else v /= 10;
        } else {
            v /= 100;
        }
        std::string s;
        for (int i = 0; i < fracLen - 1; i++) {
            s.insert(s.begin(), static_cast<char>('0' + v % 10));
            v /= 10;
        }
        if (fracLen > 0) s.insert(s.begin(), '.');
        for (int i = 0; i < intLen; i++) {
            s.insert(s.begin(), static_cast<char>('0' + v % 10));
            v /= 10;
        }
        if (negative) s.insert(s.begin(), '-');
        out = s;
    } else {
        d += 0.5;
        long long v = d > 9.2e18 ? 9223372036854775807LL : static_cast<long long>(d);
        out = (negative ? "-" : "") + std::to_string(v);
    }
    return toBytes(out);
}

// ---------------------------------------------------------------- Sources
int ArrayRandomAccessSource::get(int64_t position) {
    if (!array_ || position < 0 || position >= static_cast<int64_t>(array_->size())) return -1;
    return (*array_)[static_cast<size_t>(position)];
}

int ArrayRandomAccessSource::get(int64_t position, uint8_t* bytes, int off, int len) {
    if (!array_) throw std::logic_error("Already closed");
    if (position < 0 || position >= static_cast<int64_t>(array_->size())) return -1;
    if (position + len > static_cast<int64_t>(array_->size())) len = static_cast<int>(array_->size() - position);
    std::memcpy(bytes + off, array_->data() + position, len);
    return len;
}

WindowRandomAccessSource::WindowRandomAccessSource(RandomAccessSourcePtr source, int64_t offset)
    : source_(std::move(source)), offset_(offset) {
    length_ = source_->length() - offset;
}

int WindowRandomAccessSource::get(int64_t position) {
    if (position >= length_) return -1;
    return source_->get(offset_ + position);
}

int WindowRandomAccessSource::get(int64_t position, uint8_t* bytes, int off, int len) {
    if (position >= length_) return -1;
    int64_t toRead = std::min<int64_t>(len, length_ - position);
    return source_->get(offset_ + position, bytes, off, static_cast<int>(toRead));
}

GroupedRandomAccessSource::GroupedRandomAccessSource(std::vector<RandomAccessSourcePtr> sources) {
    int64_t total = 0;
    for (auto& s : sources) {
        int64_t len = s->length();
        sources_.push_back({s, total, total + len - 1});
        total += len;
    }
    size_ = total;
}

int GroupedRandomAccessSource::findIndex(int64_t position) const {
    for (size_t i = 0; i < sources_.size(); ++i)
        if (position <= sources_[i].lastByte) return static_cast<int>(i);
    return -1;
}

int GroupedRandomAccessSource::get(int64_t position) {
    int i = findIndex(position);
    if (i < 0) return -1;
    return sources_[i].source->get(position - sources_[i].firstByte);
}

int GroupedRandomAccessSource::get(int64_t position, uint8_t* bytes, int off, int len) {
    int i = findIndex(position);
    if (i < 0) return -1;
    int remaining = len;
    int total = 0;
    while (remaining > 0 && i < static_cast<int>(sources_.size())) {
        auto& e = sources_[i];
        if (position > e.lastByte) {
            ++i;
            continue;
        }
        int count = e.source->get(position - e.firstByte, bytes, off, remaining);
        if (count <= 0) break;
        total += count;
        position += count;
        off += count;
        remaining -= count;
        ++i;
    }
    return total == 0 ? -1 : total;
}

void GroupedRandomAccessSource::close() {
    for (auto& e : sources_) e.source->close();
}

RandomAccessSourcePtr RandomAccessSourceFactory::createSource(const Bytes& data) const {
    return std::make_shared<ArrayRandomAccessSource>(data);
}

RandomAccessSourcePtr RandomAccessSourceFactory::createSource(Bytes&& data) const {
    return std::make_shared<ArrayRandomAccessSource>(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::createBestSource(const std::string& filename) const {
    std::ifstream in(filename, std::ios::binary);
    if (!in) throw IoException(format(IoExceptionMessageConstant::CANNOT_OPEN_FILE, filename));
    Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return createSource(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::createSource(std::istream& stream) const {
    Bytes data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return createSource(std::move(data));
}

RandomAccessSourcePtr RandomAccessSourceFactory::createRanged(RandomAccessSourcePtr source,
                                                              const std::vector<int64_t>& ranges) const {
    std::vector<RandomAccessSourcePtr> sources;
    for (size_t i = 0; i + 1 < ranges.size(); i += 2) {
        sources.push_back(std::make_shared<WindowRandomAccessSource>(
            std::make_shared<IndependentRandomAccessSource>(source), ranges[i], ranges[i + 1]));
    }
    return std::make_shared<GroupedRandomAccessSource>(std::move(sources));
}

// ---------------------------------------------------------------- RandomAccessFileOrArray
std::unique_ptr<RandomAccessFileOrArray> RandomAccessFileOrArray::createView() const {
    return std::make_unique<RandomAccessFileOrArray>(std::make_shared<IndependentRandomAccessSource>(byteSource_));
}

RandomAccessSourcePtr RandomAccessFileOrArray::createSourceView() const {
    return std::make_shared<IndependentRandomAccessSource>(byteSource_);
}

int RandomAccessFileOrArray::read() {
    if (isBack_) {
        isBack_ = false;
        return back_;
    }
    return byteSource_->get(byteSourcePosition_++);
}

int RandomAccessFileOrArray::peek() {
    if (isBack_) return back_;
    return byteSource_->get(byteSourcePosition_);
}

int RandomAccessFileOrArray::peek(uint8_t* buffer, int length) {
    int offset = 0;
    int count = 0;
    if (isBack_ && length > 0) {
        buffer[offset++] = back_;
        --length;
        ++count;
    }
    if (length > 0) {
        int n = byteSource_->get(byteSourcePosition_, buffer, offset, length);
        if (n > 0) count += n;
    }
    return count;
}

int RandomAccessFileOrArray::read(uint8_t* b, int off, int len) {
    if (len == 0) return 0;
    int count = 0;
    if (isBack_ && len > 0) {
        isBack_ = false;
        b[off++] = back_;
        --len;
        count++;
    }
    if (len > 0) {
        int n = byteSource_->get(byteSourcePosition_, b, off, len);
        if (n > 0) {
            count += n;
            byteSourcePosition_ += n;
        }
    }
    if (count == 0) return -1;
    return count;
}

void RandomAccessFileOrArray::readFully(uint8_t* b, int off, int len) {
    int n = 0;
    while (n < len) {
        int count = read(b, off + n, len - n);
        if (count < 0) throw IoException(IoExceptionMessageConstant::UNEXPECTED_END_OF_FILE);
        n += count;
    }
}

int64_t RandomAccessFileOrArray::skip(int64_t n) {
    if (n <= 0) return 0;
    int adj = 0;
    if (isBack_) {
        isBack_ = false;
        if (n == 1) return 1;
        --n;
        adj = 1;
    }
    int64_t pos = getPosition();
    int64_t len = length();
    int64_t newpos = pos + n;
    if (newpos > len) newpos = len;
    seek(newpos);
    return newpos - pos + adj;
}

void RandomAccessFileOrArray::close() {
    isBack_ = false;
    byteSource_->close();
}

static int readOrThrow(RandomAccessFileOrArray& f) {
    int ch = f.read();
    if (ch < 0) throw IoException(IoExceptionMessageConstant::UNEXPECTED_END_OF_FILE);
    return ch;
}

bool RandomAccessFileOrArray::readBoolean() { return readOrThrow(*this) != 0; }
int8_t RandomAccessFileOrArray::readByte() { return static_cast<int8_t>(readOrThrow(*this)); }
int RandomAccessFileOrArray::readUnsignedByte() { return readOrThrow(*this); }

int16_t RandomAccessFileOrArray::readShort() {
    int ch1 = readOrThrow(*this), ch2 = readOrThrow(*this);
    return static_cast<int16_t>((ch1 << 8) + ch2);
}
int16_t RandomAccessFileOrArray::readShortLE() {
    int ch1 = readOrThrow(*this), ch2 = readOrThrow(*this);
    return static_cast<int16_t>((ch2 << 8) + ch1);
}
int RandomAccessFileOrArray::readUnsignedShort() {
    int ch1 = readOrThrow(*this), ch2 = readOrThrow(*this);
    return (ch1 << 8) + ch2;
}
int RandomAccessFileOrArray::readUnsignedShortLE() {
    int ch1 = readOrThrow(*this), ch2 = readOrThrow(*this);
    return (ch2 << 8) + ch1;
}
char16_t RandomAccessFileOrArray::readChar() { return static_cast<char16_t>(readUnsignedShort()); }
char16_t RandomAccessFileOrArray::readCharLE() { return static_cast<char16_t>(readUnsignedShortLE()); }

int32_t RandomAccessFileOrArray::readInt() {
    uint32_t a = readOrThrow(*this), b = readOrThrow(*this), c = readOrThrow(*this), d = readOrThrow(*this);
    return static_cast<int32_t>((a << 24) | (b << 16) | (c << 8) | d);
}
int32_t RandomAccessFileOrArray::readIntLE() {
    uint32_t a = readOrThrow(*this), b = readOrThrow(*this), c = readOrThrow(*this), d = readOrThrow(*this);
    return static_cast<int32_t>((d << 24) | (c << 16) | (b << 8) | a);
}
int64_t RandomAccessFileOrArray::readUnsignedInt() { return static_cast<uint32_t>(readInt()); }
int64_t RandomAccessFileOrArray::readUnsignedIntLE() { return static_cast<uint32_t>(readIntLE()); }
int64_t RandomAccessFileOrArray::readLong() {
    uint64_t hi = static_cast<uint32_t>(readInt());
    uint64_t lo = static_cast<uint32_t>(readInt());
    return static_cast<int64_t>((hi << 32) | lo);
}
int64_t RandomAccessFileOrArray::readLongLE() {
    uint64_t lo = static_cast<uint32_t>(readIntLE());
    uint64_t hi = static_cast<uint32_t>(readIntLE());
    return static_cast<int64_t>((hi << 32) | lo);
}
float RandomAccessFileOrArray::readFloat() {
    int32_t i = readInt();
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}
float RandomAccessFileOrArray::readFloatLE() {
    int32_t i = readIntLE();
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}
double RandomAccessFileOrArray::readDouble() {
    int64_t i = readLong();
    double d;
    std::memcpy(&d, &i, 8);
    return d;
}
double RandomAccessFileOrArray::readDoubleLE() {
    int64_t i = readLongLE();
    double d;
    std::memcpy(&d, &i, 8);
    return d;
}

std::string RandomAccessFileOrArray::readLine() {
    std::string input;
    int c = -1;
    bool eol = false;
    while (!eol) {
        switch (c = read()) {
            case -1:
            case '\n':
                eol = true;
                break;
            case '\r': {
                eol = true;
                int64_t cur = getPosition();
                if (read() != '\n') seek(cur);
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

std::string RandomAccessFileOrArray::readString(int length, const std::string& encoding) {
    Bytes buf(length);
    readFully(buf);
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
            if (cp < 0x80) out += static_cast<char>(cp);
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

// ---------------------------------------------------------------- Output streams
FileOutputStream::FileOutputStream(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) throw IoException(format(IoExceptionMessageConstant::CANNOT_OPEN_FILE, path));
}
FileOutputStream::~FileOutputStream() {
    if (out_.is_open()) out_.close();
}
void FileOutputStream::write(int b) { out_.put(static_cast<char>(b)); }
void FileOutputStream::write(const uint8_t* b, int off, int len) { out_.write(reinterpret_cast<const char*>(b + off), len); }
void FileOutputStream::flush() { out_.flush(); }
void FileOutputStream::close() {
    if (out_.is_open()) out_.close();
}

static bool g_highPrecision = false;
bool HighPrecisionOutputStream::getHighPrecision() { return g_highPrecision; }
void HighPrecisionOutputStream::setHighPrecision(bool value) { g_highPrecision = value; }

HighPrecisionOutputStream::HighPrecisionOutputStream(std::shared_ptr<OutputStream> outputStream, bool localHighPrecision)
    : outputStream_(std::move(outputStream)), localHighPrecision_(localHighPrecision) {}

void HighPrecisionOutputStream::write(int b) {
    outputStream_->write(b);
    currentPos_++;
}
void HighPrecisionOutputStream::write(const uint8_t* b, int off, int len) {
    outputStream_->write(b, off, len);
    currentPos_ += len;
}
void HighPrecisionOutputStream::flush() { outputStream_->flush(); }
void HighPrecisionOutputStream::close() {
    if (closeStream_) outputStream_->close();
}

HighPrecisionOutputStream& HighPrecisionOutputStream::writeLong(int64_t value) {
    std::string s = std::to_string(value);
    write(reinterpret_cast<const uint8_t*>(s.data()), 0, static_cast<int>(s.size()));
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeInteger(int value) { return writeLong(value); }
HighPrecisionOutputStream& HighPrecisionOutputStream::writeFloats(const std::vector<float>& value) {
    for (size_t i = 0; i < value.size(); i++) {
        writeFloat(value[i]);
        if (i < value.size() - 1) writeSpace();
    }
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeDouble(double value) {
    return writeDouble(value, localHighPrecision_ || g_highPrecision);
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeDouble(double value, bool highPrecision) {
    Bytes b = ByteUtils::getIsoBytes(value, highPrecision);
    write(b.data(), 0, static_cast<int>(b.size()));
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeByte(int value) {
    write(value);
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeString(const std::string& value) {
    write(reinterpret_cast<const uint8_t*>(value.data()), 0, static_cast<int>(value.size()));
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeBytes(const Bytes& b) {
    if (!b.empty()) write(b.data(), 0, static_cast<int>(b.size()));
    return *this;
}
HighPrecisionOutputStream& HighPrecisionOutputStream::writeBytes(const uint8_t* b, int off, int len) {
    write(b, off, len);
    return *this;
}
void HighPrecisionOutputStream::assignBytes(const Bytes& bytes, int count) {
    if (auto* baos = dynamic_cast<ByteArrayOutputStream*>(outputStream_.get())) {
        baos->assignBytes(bytes.data(), count);
        currentPos_ = count;
    } else {
        throw IoException("Bytes can be assigned to ByteArrayOutputStream only.");
    }
}
void HighPrecisionOutputStream::reset() {
    if (auto* baos = dynamic_cast<ByteArrayOutputStream*>(outputStream_.get())) {
        baos->reset();
        currentPos_ = 0;
    } else {
        throw IoException("Bytes can be reset in ByteArrayOutputStream only.");
    }
}

struct DeflaterOutputStream::Impl {
    z_stream zs{};
    bool init = false;
};

DeflaterOutputStream::DeflaterOutputStream(std::shared_ptr<OutputStream> out, int level)
    : out_(std::move(out)), impl_(std::make_unique<Impl>()) {
    if (level < -1 || level > 9) level = -1;
    if (deflateInit(&impl_->zs, level) != Z_OK) throw IoException("Cannot initialize deflater");
    impl_->init = true;
}

DeflaterOutputStream::~DeflaterOutputStream() {
    if (impl_->init) deflateEnd(&impl_->zs);
}

void DeflaterOutputStream::write(int b) {
    uint8_t c = static_cast<uint8_t>(b);
    write(&c, 0, 1);
}

void DeflaterOutputStream::write(const uint8_t* b, int off, int len) {
    if (len <= 0) return;
    impl_->zs.next_in = const_cast<Bytef*>(b + off);
    impl_->zs.avail_in = static_cast<uInt>(len);
    deflateChunk(Z_NO_FLUSH);
}

void DeflaterOutputStream::deflateChunk(int flush) {
    uint8_t buf[16384];
    int ret;
    do {
        impl_->zs.next_out = buf;
        impl_->zs.avail_out = sizeof buf;
        ret = deflate(&impl_->zs, flush);
        int have = static_cast<int>(sizeof buf - impl_->zs.avail_out);
        if (have > 0) out_->write(buf, 0, have);
    } while (impl_->zs.avail_out == 0 || (flush == Z_FINISH && ret != Z_STREAM_END));
}

void DeflaterOutputStream::finish() {
    if (finished_) return;
    finished_ = true;
    impl_->zs.next_in = nullptr;
    impl_->zs.avail_in = 0;
    deflateChunk(Z_FINISH);
}

void DeflaterOutputStream::close() {
    finish();
    out_->close();
}

void ASCIIHexOutputStream::write(int b) {
    out_->write(HEX_DIGITS[(b >> 4) & 0x0f]);
    out_->write(HEX_DIGITS[b & 0x0f]);
}
void ASCIIHexOutputStream::finish() {
    if (finished_) return;
    finished_ = true;
    out_->write('>');
}

void ASCII85OutputStream::write(int b) {
    block_[blockLen_++] = static_cast<uint8_t>(b);
    if (blockLen_ == 4) encodeBlock(4);
}

void ASCII85OutputStream::encodeBlock(int count) {
    uint32_t word = (static_cast<uint32_t>(block_[0]) << 24) | (static_cast<uint32_t>(block_[1]) << 16) |
                    (static_cast<uint32_t>(block_[2]) << 8) | block_[3];
    if (count == 4 && word == 0) {
        out_->write('z');
    } else {
        char enc[5];
        for (int i = 4; i >= 0; --i) {
            enc[i] = static_cast<char>(word % 85 + '!');
            word /= 85;
        }
        for (int i = 0; i < count + 1; ++i) out_->write(enc[i]);
    }
    blockLen_ = 0;
    block_[0] = block_[1] = block_[2] = block_[3] = 0;
}

void ASCII85OutputStream::finish() {
    if (finished_) return;
    finished_ = true;
    if (blockLen_ > 0) {
        int n = blockLen_;
        for (int i = n; i < 4; ++i) block_[i] = 0;
        uint32_t word = (static_cast<uint32_t>(block_[0]) << 24) | (static_cast<uint32_t>(block_[1]) << 16) |
                        (static_cast<uint32_t>(block_[2]) << 8) | block_[3];
        char enc[5];
        for (int i = 4; i >= 0; --i) {
            enc[i] = static_cast<char>(word % 85 + '!');
            word /= 85;
        }
        for (int i = 0; i < n + 1; ++i) out_->write(enc[i]);
    }
    out_->write('~');
    out_->write('>');
}

void RunLengthOutputStream::finish() {
    if (finished_) return;
    finished_ = true;
    size_t i = 0;
    const size_t n = data_.size();
    while (i < n) {
        // find run
        size_t run = 1;
        while (i + run < n && run < 128 && data_[i + run] == data_[i]) ++run;
        if (run >= 2) {
            out_->write(static_cast<int>(257 - run));
            out_->write(data_[i]);
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
            out_->write(static_cast<int>(len - 1));
            out_->write(data_.data(), static_cast<int>(start), static_cast<int>(len));
        }
    }
    out_->write(128);
}

}  // namespace io
}  // namespace pdfcpp
