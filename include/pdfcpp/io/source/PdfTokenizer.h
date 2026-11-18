#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "pdfcpp/io/source/ByteBuffer.h"
#include "pdfcpp/io/source/RandomAccessFileOrArray.h"

namespace pdfcpp {
namespace io {

/// Lexer for PDF syntax (port of com.itextpdf.io.source.PdfTokenizer).
class PdfTokenizer {
public:
    enum class TokenType { Number, String, Name, Comment, StartArray, EndArray, StartDic, EndDic, Ref, Obj, EndObj, Other, EndOfFile };

    static const Bytes Obj;
    static const Bytes R;
    static const Bytes Xref;
    static const Bytes Startxref;
    static const Bytes Stream;
    static const Bytes Trailer;
    static const Bytes N;
    static const Bytes F;
    static const Bytes Null;
    static const Bytes True;
    static const Bytes False;

    explicit PdfTokenizer(std::unique_ptr<RandomAccessFileOrArray> file);
    explicit PdfTokenizer(RandomAccessSourcePtr source)
        : PdfTokenizer(std::make_unique<RandomAccessFileOrArray>(std::move(source))) {}

    void seek(int64_t pos) { file_->seek(pos); }
    void readFully(Bytes& bytes) { file_->readFully(bytes); }
    int64_t getPosition() const { return file_->getPosition(); }
    void close();
    int64_t length() const { return file_->length(); }
    int read() { return file_->read(); }
    int peek() { return file_->peek(); }
    int peek(Bytes& buffer) { return file_->peek(buffer.data(), static_cast<int>(buffer.size())); }
    std::string readString(int size);

    TokenType getTokenType() const { return type_; }
    Bytes getByteContent() const { return outBuf_.toByteArray(); }
    std::string getStringValue() const { return outBuf_.toString(); }
    Bytes getDecodedStringContent() const;
    bool tokenValueEqualsTo(const Bytes& cmp) const;
    int getObjNr() const { return reference_; }
    int getGenNr() const { return generation_; }
    void backOnePosition(int ch);

    int getHeaderOffset();
    std::string checkPdfHeader();
    void checkFdfHeader();
    int64_t getStartxref();
    int64_t getNextEof();

    void nextValidToken();
    bool nextToken();

    int64_t getLongValue() const;
    int getIntValue() const;
    bool isHexString() const { return hexString_; }
    bool isCloseStream() const { return closeStream_; }
    void setCloseStream(bool closeStream) { closeStream_ = closeStream; }
    std::unique_ptr<RandomAccessFileOrArray> getSafeFile() const { return file_->createView(); }
    RandomAccessFileOrArray& getFile() { return *file_; }

    static Bytes decodeStringContent(const uint8_t* content, int from, int to, bool hexWriting);
    static Bytes decodeStringContent(const Bytes& content, bool hexWriting) {
        return decodeStringContent(content.data(), 0, static_cast<int>(content.size()) - 1, hexWriting);
    }
    static bool isWhitespace(int ch) { return isWhitespace(ch, true); }
    static bool isWhitespace(int ch, bool isWhitespace) {
        return ((isWhitespace && ch == 0) || ch == 9 || ch == 10 || ch == 12 || ch == 13 || ch == 32);
    }
    static bool isDelimiter(int ch) {
        return (ch == '(' || ch == ')' || ch == '<' || ch == '>' || ch == '[' || ch == ']' || ch == '/' || ch == '%');
    }
    static bool isDelimiterWhitespace(int ch);
    [[noreturn]] void throwError(const std::string& error);
    static bool checkTrailer(const ByteBuffer& line);
    bool readLineSegment(ByteBuffer& buffer, bool isNullWhitespace = true);
    static std::optional<std::array<int, 2>> checkObjectStart(PdfTokenizer& lineTokenizer);

protected:
    TokenType type_ = TokenType::EndOfFile;
    int reference_ = 0;
    int generation_ = 0;
    bool hexString_ = false;
    ByteBuffer outBuf_;

private:
    bool isLineEndingAt(int64_t position);
    std::unique_ptr<RandomAccessFileOrArray> file_;
    bool closeStream_ = true;
};

}  // namespace io
}  // namespace pdfcpp
