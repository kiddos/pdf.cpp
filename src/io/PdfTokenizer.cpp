#include "pdfcpp/io/source/PdfTokenizer.h"

#include <algorithm>
#include <cstdlib>

#include "pdfcpp/io/exceptions/IoException.h"

namespace pdfcpp {
namespace io {

const Bytes PdfTokenizer::Obj = toBytes("obj");
const Bytes PdfTokenizer::R = toBytes("R");
const Bytes PdfTokenizer::Xref = toBytes("xref");
const Bytes PdfTokenizer::Startxref = toBytes("startxref");
const Bytes PdfTokenizer::Stream = toBytes("stream");
const Bytes PdfTokenizer::Trailer = toBytes("trailer");
const Bytes PdfTokenizer::N = toBytes("n");
const Bytes PdfTokenizer::F = toBytes("f");
const Bytes PdfTokenizer::Null = toBytes("null");
const Bytes PdfTokenizer::True = toBytes("true");
const Bytes PdfTokenizer::False = toBytes("false");

bool PdfTokenizer::isDelimiterWhitespace(int ch) {
    // index ch+1 in Java's delims table; -1 (EOF) is a delimiter
    if (ch == -1) return true;
    if (ch < 0 || ch > 255) return false;
    switch (ch) {
        case 0: case 9: case 10: case 12: case 13: case 32:
        case '(': case ')': case '<': case '>': case '[': case ']': case '/': case '%':
            return true;
        default:
            return false;
    }
}

PdfTokenizer::PdfTokenizer(std::unique_ptr<RandomAccessFileOrArray> file) : file_(std::move(file)) {}

void PdfTokenizer::close() {
    if (closeStream_) file_->close();
}

std::string PdfTokenizer::readString(int size) {
    std::string buf;
    while ((size--) > 0) {
        int ch = read();
        if (ch == -1) break;
        buf += static_cast<char>(ch);
    }
    return buf;
}

Bytes PdfTokenizer::getDecodedStringContent() const {
    return decodeStringContent(outBuf_.getInternalBuffer(), 0, outBuf_.size() - 1, isHexString());
}

bool PdfTokenizer::tokenValueEqualsTo(const Bytes& cmp) const {
    int size = static_cast<int>(cmp.size());
    if (outBuf_.size() != size) return false;
    const uint8_t* buf = outBuf_.getInternalBuffer();
    for (int i = 0; i < size; i++)
        if (cmp[i] != buf[i]) return false;
    return true;
}

void PdfTokenizer::backOnePosition(int ch) {
    if (ch != -1) file_->pushBack(static_cast<uint8_t>(ch));
}

int PdfTokenizer::getHeaderOffset() {
    std::string str = readString(1024);
    auto idx = str.find("%PDF-");
    if (idx == std::string::npos) {
        idx = str.find("%FDF-");
        if (idx == std::string::npos) throw IoException(IoExceptionMessageConstant::PDF_HEADER_NOT_FOUND);
    }
    return static_cast<int>(idx);
}

std::string PdfTokenizer::checkPdfHeader() {
    file_->seek(0);
    std::string str = readString(1024);
    auto idx = str.find("%PDF-");
    if (idx != 0) throw IoException(IoExceptionMessageConstant::PDF_HEADER_NOT_FOUND);
    return str.substr(idx + 1, 7);
}

void PdfTokenizer::checkFdfHeader() {
    file_->seek(0);
    std::string str = readString(1024);
    if (str.find("%FDF-") != 0) throw IoException(IoExceptionMessageConstant::FDF_STARTXREF_NOT_FOUND);
}

int64_t PdfTokenizer::getStartxref() {
    const int arrLength = 1024;
    int64_t fileLength = file_->length();
    int64_t pos = fileLength - arrLength;
    if (pos < 1) pos = 1;
    while (pos > 0) {
        file_->seek(pos);
        std::string str = readString(arrLength);
        auto idx = str.rfind("startxref");
        if (idx != std::string::npos) return pos + static_cast<int64_t>(idx);
        pos = pos - arrLength + 9;
    }
    throw IoException(IoExceptionMessageConstant::PDF_STARTXREF_NOT_FOUND);
}

bool PdfTokenizer::isLineEndingAt(int64_t position) {
    int64_t saved = file_->getPosition();
    file_->seek(position);
    int b = file_->read();
    file_->seek(saved);
    return b == '\n' || b == '\r';
}

int64_t PdfTokenizer::getNextEof() {
    const int arrLength = 128;
    std::string str;
    do {
        int64_t currentPosition = file_->getPosition();
        str = readString(arrLength);
        size_t searchFrom = 0;
        size_t eofPosition;
        while ((eofPosition = str.find("%%EOF", searchFrom)) != std::string::npos) {
            int64_t absoluteEofPos = currentPosition + static_cast<int64_t>(eofPosition);
            bool lineStartsWithEof;
            if (eofPosition > 0) {
                char preceding = str[eofPosition - 1];
                lineStartsWithEof = preceding == '\n' || preceding == '\r';
            } else {
                lineStartsWithEof = absoluteEofPos == 0 || isLineEndingAt(absoluteEofPos - 1);
            }
            if (lineStartsWithEof) {
                file_->seek(absoluteEofPos + 5);
                std::string remaining = readString(4);
                int eolCount = 0;
                for (char b : remaining) {
                    if (b == '\n' || b == '\r') eolCount++;
                    else return absoluteEofPos + eolCount + 5;
                }
                return absoluteEofPos + eolCount + 5;
            }
            searchFrom = eofPosition + 5;
        }
        // Step back so a "%%EOF" straddling two chunks is still found; never seek before the start of the file.
        file_->seek(std::max<int64_t>(0, file_->getPosition() - 4));
    } while (str.size() > 4);
    throw IoException(IoExceptionMessageConstant::PDF_EOF_NOT_FOUND);
}

static bool parseIntStrict(const Bytes& b, int& out) {
    if (b.empty()) return false;
    std::string s(b.begin(), b.end());
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v > 2147483647L || v < -2147483648L) return false;
    out = static_cast<int>(v);
    return true;
}

void PdfTokenizer::nextValidToken() {
    int level = 0;
    Bytes n1, n2;
    int64_t ptr = 0;
    while (nextToken()) {
        if (type_ == TokenType::Comment) continue;
        switch (level) {
            case 0: {
                if (type_ != TokenType::Number) return;
                ptr = file_->getPosition();
                n1 = getByteContent();
                ++level;
                break;
            }
            case 1: {
                if (type_ != TokenType::Number) {
                    file_->seek(ptr);
                    type_ = TokenType::Number;
                    outBuf_.reset().append(n1);
                    return;
                }
                n2 = getByteContent();
                ++level;
                break;
            }
            case 2: {
                if (type_ == TokenType::Other) {
                    if (tokenValueEqualsTo(R)) {
                        type_ = TokenType::Ref;
                        if (!parseIntStrict(n1, reference_) || !parseIntStrict(n2, generation_)) {
                            reference_ = -1;
                            generation_ = 0;
                        }
                        return;
                    } else if (tokenValueEqualsTo(Obj)) {
                        type_ = TokenType::Obj;
                        if (!parseIntStrict(n1, reference_) || !parseIntStrict(n2, generation_))
                            throw IoException("Invalid object number");
                        return;
                    }
                }
                file_->seek(ptr);
                type_ = TokenType::Number;
                outBuf_.reset().append(n1);
                return;
            }
        }
    }
    if (level == 1) {
        type_ = TokenType::Number;
        outBuf_.reset().append(n1);
    }
}

bool PdfTokenizer::nextToken() {
    int ch;
    outBuf_.reset();
    do {
        ch = file_->read();
    } while (ch != -1 && isWhitespace(ch));
    if (ch == -1) {
        type_ = TokenType::EndOfFile;
        return false;
    }
    switch (ch) {
        case '[':
            type_ = TokenType::StartArray;
            break;
        case ']':
            type_ = TokenType::EndArray;
            break;
        case '/': {
            type_ = TokenType::Name;
            while (true) {
                ch = file_->read();
                if (isDelimiterWhitespace(ch)) break;
                outBuf_.append(ch);
            }
            backOnePosition(ch);
            break;
        }
        case '>': {
            ch = file_->read();
            if (ch != '>') throwError(IoExceptionMessageConstant::GT_NOT_EXPECTED);
            type_ = TokenType::EndDic;
            break;
        }
        case '<': {
            int v1 = file_->read();
            if (v1 == '<') {
                type_ = TokenType::StartDic;
                break;
            }
            type_ = TokenType::String;
            hexString_ = true;
            int v2 = 0;
            while (true) {
                while (isWhitespace(v1)) v1 = file_->read();
                if (v1 == '>') break;
                outBuf_.append(v1);
                v1 = ByteBuffer::getHex(v1);
                if (v1 < 0) break;
                v2 = file_->read();
                while (isWhitespace(v2)) v2 = file_->read();
                if (v2 == '>') break;
                outBuf_.append(v2);
                v2 = ByteBuffer::getHex(v2);
                if (v2 < 0) break;
                v1 = file_->read();
            }
            if (v1 < 0 || v2 < 0) throwError(IoExceptionMessageConstant::ERROR_READING_STRING);
            break;
        }
        case '%': {
            type_ = TokenType::Comment;
            do {
                ch = file_->read();
            } while (ch != -1 && ch != '\r' && ch != '\n');
            break;
        }
        case '(': {
            type_ = TokenType::String;
            hexString_ = false;
            int nesting = 0;
            while (true) {
                ch = file_->read();
                if (ch == -1) break;
                if (ch == '(') {
                    ++nesting;
                } else if (ch == ')') {
                    --nesting;
                    if (nesting == -1) break;
                } else if (ch == '\\') {
                    outBuf_.append('\\');
                    ch = file_->read();
                    if (ch < 0) break;
                }
                outBuf_.append(ch);
            }
            if (ch == -1) throwError(IoExceptionMessageConstant::ERROR_READING_STRING);
            break;
        }
        default: {
            if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9')) {
                type_ = TokenType::Number;
                bool isReal = false;
                int numberOfMinuses = 0;
                if (ch == '-') {
                    do {
                        ++numberOfMinuses;
                        ch = file_->read();
                    } while (ch == '-');
                    outBuf_.append('-');
                } else {
                    outBuf_.append(ch);
                    ch = file_->read();
                }
                while (ch >= '0' && ch <= '9') {
                    outBuf_.append(ch);
                    ch = file_->read();
                }
                if (ch == '.') {
                    isReal = true;
                    outBuf_.append(ch);
                    ch = file_->read();
                    int numberOfMinusesAfterDot = 0;
                    if (ch == '-') {
                        numberOfMinusesAfterDot++;
                        ch = file_->read();
                    }
                    while (ch >= '0' && ch <= '9') {
                        if (numberOfMinusesAfterDot == 0) outBuf_.append(ch);
                        ch = file_->read();
                    }
                }
                if (numberOfMinuses > 1 && !isReal) {
                    outBuf_.reset();
                    outBuf_.append('0');
                }
            } else {
                type_ = TokenType::Other;
                do {
                    outBuf_.append(ch);
                    ch = file_->read();
                } while (!isDelimiterWhitespace(ch));
            }
            if (ch != -1) backOnePosition(ch);
            break;
        }
    }
    return true;
}

int64_t PdfTokenizer::getLongValue() const {
    std::string s = getStringValue();
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') throw IoException("Number format error: " + s);
    return v;
}

int PdfTokenizer::getIntValue() const {
    std::string s = getStringValue();
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') throw IoException("Number format error: " + s);
    return static_cast<int>(v);
}

Bytes PdfTokenizer::decodeStringContent(const uint8_t* content, int from, int to, bool hexWriting) {
    ByteBuffer buffer(to - from + 1);
    if (hexWriting) {
        int i = from;
        while (i <= to) {
            int v1 = ByteBuffer::getHex(content[i++]);
            if (i > to) {
                buffer.append(v1 << 4);
                break;
            }
            int v2 = ByteBuffer::getHex(content[i++]);
            buffer.append((v1 << 4) + v2);
        }
    } else {
        int i = from;
        while (i <= to) {
            int ch = content[i++];
            if (ch == '\\') {
                bool lineBreak = false;
                if (i > to) break;
                ch = content[i++];
                switch (ch) {
                    case 'n': ch = '\n'; break;
                    case 'r': ch = '\r'; break;
                    case 't': ch = '\t'; break;
                    case 'b': ch = '\b'; break;
                    case 'f': ch = '\f'; break;
                    case '(': case ')': case '\\': break;
                    case '\r':
                        lineBreak = true;
                        if (i <= to && content[i++] != '\n') i--;
                        break;
                    case '\n':
                        lineBreak = true;
                        break;
                    default: {
                        if (ch < '0' || ch > '7') break;
                        int octal = ch - '0';
                        if (i > to) {
                            ch = octal;
                            break;
                        }
                        ch = content[i++];
                        if (ch < '0' || ch > '7') {
                            i--;
                            ch = octal;
                            break;
                        }
                        octal = (octal << 3) + ch - '0';
                        if (i > to) {
                            ch = octal;
                            break;
                        }
                        ch = content[i++];
                        if (ch < '0' || ch > '7') {
                            i--;
                            ch = octal;
                            break;
                        }
                        octal = (octal << 3) + ch - '0';
                        ch = octal & 0xff;
                        break;
                    }
                }
                if (lineBreak) continue;
            } else if (ch == '\r') {
                ch = '\n';
                if (i <= to && content[i++] != '\n') i--;
            }
            buffer.append(ch);
        }
    }
    return buffer.toByteArray();
}

void PdfTokenizer::throwError(const std::string& error) {
    throw IoException(format(IoExceptionMessageConstant::ERROR_AT_FILE_POINTER, std::to_string(file_->getPosition())) +
                      " " + error);
}

bool PdfTokenizer::checkTrailer(const ByteBuffer& line) {
    if (static_cast<int>(Trailer.size()) > line.size()) return false;
    for (size_t i = 0; i < Trailer.size(); i++)
        if (Trailer[i] != line.get(static_cast<int>(i))) return false;
    return true;
}

bool PdfTokenizer::readLineSegment(ByteBuffer& buffer, bool isNullWhitespace) {
    int c;
    bool eol = false;
    while (isWhitespace((c = read()), isNullWhitespace)) {
    }
    bool prevWasWhitespace = false;
    while (!eol) {
        switch (c) {
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
            case 9:
            case 12:
            case 32:
                if (prevWasWhitespace) break;
                prevWasWhitespace = true;
                buffer.append(static_cast<uint8_t>(c));
                break;
            default:
                prevWasWhitespace = false;
                buffer.append(static_cast<uint8_t>(c));
                break;
        }
        if (eol || buffer.size() == buffer.capacity()) {
            eol = true;
        } else {
            c = read();
        }
    }
    if (buffer.size() == buffer.capacity()) {
        eol = false;
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
            }
        }
    }
    return !(c == -1 && buffer.isEmpty());
}

std::optional<std::array<int, 2>> PdfTokenizer::checkObjectStart(PdfTokenizer& lineTokenizer) {
    try {
        lineTokenizer.seek(0);
        if (!lineTokenizer.nextToken() || lineTokenizer.getTokenType() != TokenType::Number) return std::nullopt;
        int num = lineTokenizer.getIntValue();
        if (!lineTokenizer.nextToken() || lineTokenizer.getTokenType() != TokenType::Number) return std::nullopt;
        int gen = lineTokenizer.getIntValue();
        if (!lineTokenizer.nextToken()) return std::nullopt;
        if (lineTokenizer.getByteContent() != Obj) return std::nullopt;
        return std::array<int, 2>{num, gen};
    } catch (...) {
    }
    return std::nullopt;
}

}  // namespace io
}  // namespace pdfcpp
