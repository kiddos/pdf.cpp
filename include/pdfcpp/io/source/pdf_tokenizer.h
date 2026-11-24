#ifndef PDFCPP_IO_SOURCE_PDF_TOKENIZER_H_
#define PDFCPP_IO_SOURCE_PDF_TOKENIZER_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "pdfcpp/io/source/byte_buffer.h"
#include "pdfcpp/io/source/random_access_file_or_array.h"

namespace pdfcpp {
namespace io {

/// Lexer for PDF syntax (port of com.itextpdf.io.source.PdfTokenizer).
class PdfTokenizer {
 public:
  enum class TokenType {
    kNumber,
    kString,
    kName,
    kComment,
    kStartArray,
    kEndArray,
    kStartDic,
    kEndDic,
    kRef,
    kObj,
    kEndObj,
    kOther,
    kEndOfFile
  };

  static const Bytes kObj;
  static const Bytes kR;
  static const Bytes kXref;
  static const Bytes kStartxref;
  static const Bytes kStream;
  static const Bytes kTrailer;
  static const Bytes kN;
  static const Bytes kF;
  static const Bytes kNull;
  static const Bytes kTrue;
  static const Bytes kFalse;

  explicit PdfTokenizer(std::unique_ptr<RandomAccessFileOrArray> file);
  explicit PdfTokenizer(RandomAccessSourcePtr source)
      : PdfTokenizer(
            std::make_unique<RandomAccessFileOrArray>(std::move(source))) {}

  void Seek(int64_t pos) { file_->Seek(pos); }
  void ReadFully(Bytes& bytes) { file_->ReadFully(bytes); }
  int64_t GetPosition() const { return file_->GetPosition(); }
  void Close();
  int64_t Length() const { return file_->Length(); }
  int Read() { return file_->Read(); }
  int Peek() { return file_->Peek(); }
  int Peek(Bytes& buffer) {
    return file_->Peek(buffer.data(), static_cast<int>(buffer.size()));
  }
  std::string ReadString(int size);

  TokenType GetTokenType() const { return type_; }
  Bytes GetByteContent() const { return out_buf_.ToByteArray(); }
  std::string GetStringValue() const { return out_buf_.ToString(); }
  Bytes GetDecodedStringContent() const;
  bool TokenValueEqualsTo(const Bytes& cmp) const;
  int GetObjNr() const { return reference_; }
  int GetGenNr() const { return generation_; }
  void BackOnePosition(int ch);

  int GetHeaderOffset();
  std::string CheckPdfHeader();
  void CheckFdfHeader();
  int64_t GetStartxref();
  int64_t GetNextEof();

  void NextValidToken();
  bool NextToken();

  int64_t GetLongValue() const;
  int GetIntValue() const;
  bool IsHexString() const { return hex_string_; }
  bool IsCloseStream() const { return close_stream_; }
  void SetCloseStream(bool close_stream) { close_stream_ = close_stream; }
  std::unique_ptr<RandomAccessFileOrArray> GetSafeFile() const {
    return file_->CreateView();
  }
  RandomAccessFileOrArray& GetFile() { return *file_; }

  static Bytes DecodeStringContent(const uint8_t* content, int from, int to,
                                   bool hex_writing);
  static Bytes DecodeStringContent(const Bytes& content, bool hex_writing) {
    return DecodeStringContent(
        content.data(), 0, static_cast<int>(content.size()) - 1, hex_writing);
  }
  static bool IsWhitespace(int ch) { return IsWhitespace(ch, true); }
  static bool IsWhitespace(int ch, bool is_whitespace) {
    return ((is_whitespace && ch == 0) || ch == 9 || ch == 10 || ch == 12 ||
            ch == 13 || ch == 32);
  }
  static bool IsDelimiter(int ch) {
    return (ch == '(' || ch == ')' || ch == '<' || ch == '>' || ch == '[' ||
            ch == ']' || ch == '/' || ch == '%');
  }
  static bool IsDelimiterWhitespace(int ch);
  [[noreturn]] void ThrowError(const std::string& error);
  static bool CheckTrailer(const ByteBuffer& line);
  bool ReadLineSegment(ByteBuffer& buffer, bool is_null_whitespace = true);
  static std::optional<std::array<int, 2>> CheckObjectStart(
      PdfTokenizer& line_tokenizer);

 protected:
  TokenType type_ = TokenType::kEndOfFile;
  int reference_ = 0;
  int generation_ = 0;
  bool hex_string_ = false;
  ByteBuffer out_buf_;

 private:
  bool IsLineEndingAt(int64_t position);
  std::unique_ptr<RandomAccessFileOrArray> file_;
  bool close_stream_ = true;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_SOURCE_PDF_TOKENIZER_H_
