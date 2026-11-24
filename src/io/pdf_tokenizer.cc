#include "pdfcpp/io/source/pdf_tokenizer.h"

#include <algorithm>
#include <cstdlib>

#include "pdfcpp/io/exceptions/io_exception.h"

namespace pdfcpp {
namespace io {

const Bytes PdfTokenizer::kObj = ToBytes("obj");
const Bytes PdfTokenizer::kR = ToBytes("R");
const Bytes PdfTokenizer::kXref = ToBytes("xref");
const Bytes PdfTokenizer::kStartxref = ToBytes("startxref");
const Bytes PdfTokenizer::kStream = ToBytes("stream");
const Bytes PdfTokenizer::kTrailer = ToBytes("trailer");
const Bytes PdfTokenizer::kN = ToBytes("n");
const Bytes PdfTokenizer::kF = ToBytes("f");
const Bytes PdfTokenizer::kNull = ToBytes("null");
const Bytes PdfTokenizer::kTrue = ToBytes("true");
const Bytes PdfTokenizer::kFalse = ToBytes("false");

bool PdfTokenizer::IsDelimiterWhitespace(int ch) {
  // index ch+1 in Java's delims table; -1 (EOF) is a delimiter
  if (ch == -1) return true;
  if (ch < 0 || ch > 255) return false;
  switch (ch) {
    case 0:
    case 9:
    case 10:
    case 12:
    case 13:
    case 32:
    case '(':
    case ')':
    case '<':
    case '>':
    case '[':
    case ']':
    case '/':
    case '%':
      return true;
    default:
      return false;
  }
}

PdfTokenizer::PdfTokenizer(std::unique_ptr<RandomAccessFileOrArray> file)
    : file_(std::move(file)) {}

void PdfTokenizer::Close() {
  if (close_stream_) file_->Close();
}

std::string PdfTokenizer::ReadString(int size) {
  std::string buf;
  while ((size--) > 0) {
    int ch = Read();
    if (ch == -1) break;
    buf += static_cast<char>(ch);
  }
  return buf;
}

Bytes PdfTokenizer::GetDecodedStringContent() const {
  return DecodeStringContent(out_buf_.GetInternalBuffer(), 0,
                             out_buf_.size() - 1, IsHexString());
}

bool PdfTokenizer::TokenValueEqualsTo(const Bytes& cmp) const {
  int size = static_cast<int>(cmp.size());
  if (out_buf_.size() != size) return false;
  const uint8_t* buf = out_buf_.GetInternalBuffer();
  for (int i = 0; i < size; i++)
    if (cmp[i] != buf[i]) return false;
  return true;
}

void PdfTokenizer::BackOnePosition(int ch) {
  if (ch != -1) file_->PushBack(static_cast<uint8_t>(ch));
}

int PdfTokenizer::GetHeaderOffset() {
  std::string str = ReadString(1024);
  auto idx = str.find("%PDF-");
  if (idx == std::string::npos) {
    idx = str.find("%FDF-");
    if (idx == std::string::npos)
      throw IoException(io_exception_message_constant::kPdfHeaderNotFound);
  }
  return static_cast<int>(idx);
}

std::string PdfTokenizer::CheckPdfHeader() {
  file_->Seek(0);
  std::string str = ReadString(1024);
  auto idx = str.find("%PDF-");
  if (idx != 0)
    throw IoException(io_exception_message_constant::kPdfHeaderNotFound);
  return str.substr(idx + 1, 7);
}

void PdfTokenizer::CheckFdfHeader() {
  file_->Seek(0);
  std::string str = ReadString(1024);
  if (str.find("%FDF-") != 0)
    throw IoException(io_exception_message_constant::kFdfStartxrefNotFound);
}

int64_t PdfTokenizer::GetStartxref() {
  const int arr_length = 1024;
  int64_t file_length = file_->Length();
  int64_t pos = file_length - arr_length;
  if (pos < 1) pos = 1;
  while (pos > 0) {
    file_->Seek(pos);
    std::string str = ReadString(arr_length);
    auto idx = str.rfind("startxref");
    if (idx != std::string::npos) return pos + static_cast<int64_t>(idx);
    pos = pos - arr_length + 9;
  }
  throw IoException(io_exception_message_constant::kPdfStartxrefNotFound);
}

bool PdfTokenizer::IsLineEndingAt(int64_t position) {
  int64_t saved = file_->GetPosition();
  file_->Seek(position);
  int b = file_->Read();
  file_->Seek(saved);
  return b == '\n' || b == '\r';
}

int64_t PdfTokenizer::GetNextEof() {
  const int arr_length = 128;
  std::string str;
  do {
    int64_t current_position = file_->GetPosition();
    str = ReadString(arr_length);
    size_t search_from = 0;
    size_t eof_position;
    while ((eof_position = str.find("%%EOF", search_from)) !=
           std::string::npos) {
      int64_t absolute_eof_pos =
          current_position + static_cast<int64_t>(eof_position);
      bool line_starts_with_eof;
      if (eof_position > 0) {
        char preceding = str[eof_position - 1];
        line_starts_with_eof = preceding == '\n' || preceding == '\r';
      } else {
        line_starts_with_eof =
            absolute_eof_pos == 0 || IsLineEndingAt(absolute_eof_pos - 1);
      }
      if (line_starts_with_eof) {
        file_->Seek(absolute_eof_pos + 5);
        std::string remaining = ReadString(4);
        int eol_count = 0;
        for (char b : remaining) {
          if (b == '\n' || b == '\r')
            eol_count++;
          else
            return absolute_eof_pos + eol_count + 5;
        }
        return absolute_eof_pos + eol_count + 5;
      }
      search_from = eof_position + 5;
    }
    // Step back so a "%%EOF" straddling two chunks is still found; never seek
    // before the start of the file.
    file_->Seek(std::max<int64_t>(0, file_->GetPosition() - 4));
  } while (str.size() > 4);
  throw IoException(io_exception_message_constant::kPdfEofNotFound);
}

static bool ParseIntStrict(const Bytes& b, int& out) {
  if (b.empty()) return false;
  std::string s(b.begin(), b.end());
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0' || v > 2147483647L || v < -2147483648L)
    return false;
  out = static_cast<int>(v);
  return true;
}

void PdfTokenizer::NextValidToken() {
  int level = 0;
  Bytes n1, n2;
  int64_t ptr = 0;
  while (NextToken()) {
    if (type_ == TokenType::kComment) continue;
    switch (level) {
      case 0: {
        if (type_ != TokenType::kNumber) return;
        ptr = file_->GetPosition();
        n1 = GetByteContent();
        ++level;
        break;
      }
      case 1: {
        if (type_ != TokenType::kNumber) {
          file_->Seek(ptr);
          type_ = TokenType::kNumber;
          out_buf_.Reset().Append(n1);
          return;
        }
        n2 = GetByteContent();
        ++level;
        break;
      }
      case 2: {
        if (type_ == TokenType::kOther) {
          if (TokenValueEqualsTo(kR)) {
            type_ = TokenType::kRef;
            if (!ParseIntStrict(n1, reference_) ||
                !ParseIntStrict(n2, generation_)) {
              reference_ = -1;
              generation_ = 0;
            }
            return;
          } else if (TokenValueEqualsTo(kObj)) {
            type_ = TokenType::kObj;
            if (!ParseIntStrict(n1, reference_) ||
                !ParseIntStrict(n2, generation_))
              throw IoException("Invalid object number");
            return;
          }
        }
        file_->Seek(ptr);
        type_ = TokenType::kNumber;
        out_buf_.Reset().Append(n1);
        return;
      }
    }
  }
  if (level == 1) {
    type_ = TokenType::kNumber;
    out_buf_.Reset().Append(n1);
  }
}

bool PdfTokenizer::NextToken() {
  int ch;
  out_buf_.Reset();
  do {
    ch = file_->Read();
  } while (ch != -1 && IsWhitespace(ch));
  if (ch == -1) {
    type_ = TokenType::kEndOfFile;
    return false;
  }
  switch (ch) {
    case '[':
      type_ = TokenType::kStartArray;
      break;
    case ']':
      type_ = TokenType::kEndArray;
      break;
    case '/': {
      type_ = TokenType::kName;
      while (true) {
        ch = file_->Read();
        if (IsDelimiterWhitespace(ch)) break;
        out_buf_.Append(ch);
      }
      BackOnePosition(ch);
      break;
    }
    case '>': {
      ch = file_->Read();
      if (ch != '>') ThrowError(io_exception_message_constant::kGtNotExpected);
      type_ = TokenType::kEndDic;
      break;
    }
    case '<': {
      int v1 = file_->Read();
      if (v1 == '<') {
        type_ = TokenType::kStartDic;
        break;
      }
      type_ = TokenType::kString;
      hex_string_ = true;
      int v2 = 0;
      while (true) {
        while (IsWhitespace(v1)) v1 = file_->Read();
        if (v1 == '>') break;
        out_buf_.Append(v1);
        v1 = ByteBuffer::GetHex(v1);
        if (v1 < 0) break;
        v2 = file_->Read();
        while (IsWhitespace(v2)) v2 = file_->Read();
        if (v2 == '>') break;
        out_buf_.Append(v2);
        v2 = ByteBuffer::GetHex(v2);
        if (v2 < 0) break;
        v1 = file_->Read();
      }
      if (v1 < 0 || v2 < 0)
        ThrowError(io_exception_message_constant::kErrorReadingString);
      break;
    }
    case '%': {
      type_ = TokenType::kComment;
      do {
        ch = file_->Read();
      } while (ch != -1 && ch != '\r' && ch != '\n');
      break;
    }
    case '(': {
      type_ = TokenType::kString;
      hex_string_ = false;
      int nesting = 0;
      while (true) {
        ch = file_->Read();
        if (ch == -1) break;
        if (ch == '(') {
          ++nesting;
        } else if (ch == ')') {
          --nesting;
          if (nesting == -1) break;
        } else if (ch == '\\') {
          out_buf_.Append('\\');
          ch = file_->Read();
          if (ch < 0) break;
        }
        out_buf_.Append(ch);
      }
      if (ch == -1)
        ThrowError(io_exception_message_constant::kErrorReadingString);
      break;
    }
    default: {
      if (ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9')) {
        type_ = TokenType::kNumber;
        bool is_real = false;
        int number_of_minuses = 0;
        if (ch == '-') {
          do {
            ++number_of_minuses;
            ch = file_->Read();
          } while (ch == '-');
          out_buf_.Append('-');
        } else {
          out_buf_.Append(ch);
          ch = file_->Read();
        }
        while (ch >= '0' && ch <= '9') {
          out_buf_.Append(ch);
          ch = file_->Read();
        }
        if (ch == '.') {
          is_real = true;
          out_buf_.Append(ch);
          ch = file_->Read();
          int number_of_minuses_after_dot = 0;
          if (ch == '-') {
            number_of_minuses_after_dot++;
            ch = file_->Read();
          }
          while (ch >= '0' && ch <= '9') {
            if (number_of_minuses_after_dot == 0) out_buf_.Append(ch);
            ch = file_->Read();
          }
        }
        if (number_of_minuses > 1 && !is_real) {
          out_buf_.Reset();
          out_buf_.Append('0');
        }
      } else {
        type_ = TokenType::kOther;
        do {
          out_buf_.Append(ch);
          ch = file_->Read();
        } while (!IsDelimiterWhitespace(ch));
      }
      if (ch != -1) BackOnePosition(ch);
      break;
    }
  }
  return true;
}

int64_t PdfTokenizer::GetLongValue() const {
  std::string s = GetStringValue();
  char* end = nullptr;
  long long v = std::strtoll(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0')
    throw IoException("Number format error: " + s);
  return v;
}

int PdfTokenizer::GetIntValue() const {
  std::string s = GetStringValue();
  char* end = nullptr;
  long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0')
    throw IoException("Number format error: " + s);
  return static_cast<int>(v);
}

Bytes PdfTokenizer::DecodeStringContent(const uint8_t* content, int from,
                                        int to, bool hex_writing) {
  ByteBuffer buffer(to - from + 1);
  if (hex_writing) {
    int i = from;
    while (i <= to) {
      int v1 = ByteBuffer::GetHex(content[i++]);
      if (i > to) {
        buffer.Append(v1 << 4);
        break;
      }
      int v2 = ByteBuffer::GetHex(content[i++]);
      buffer.Append((v1 << 4) + v2);
    }
  } else {
    int i = from;
    while (i <= to) {
      int ch = content[i++];
      if (ch == '\\') {
        bool line_break = false;
        if (i > to) break;
        ch = content[i++];
        switch (ch) {
          case 'n':
            ch = '\n';
            break;
          case 'r':
            ch = '\r';
            break;
          case 't':
            ch = '\t';
            break;
          case 'b':
            ch = '\b';
            break;
          case 'f':
            ch = '\f';
            break;
          case '(':
          case ')':
          case '\\':
            break;
          case '\r':
            line_break = true;
            if (i <= to && content[i++] != '\n') i--;
            break;
          case '\n':
            line_break = true;
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
        if (line_break) continue;
      } else if (ch == '\r') {
        ch = '\n';
        if (i <= to && content[i++] != '\n') i--;
      }
      buffer.Append(ch);
    }
  }
  return buffer.ToByteArray();
}

void PdfTokenizer::ThrowError(const std::string& error) {
  throw IoException(Format(io_exception_message_constant::kErrorAtFilePointer,
                           std::to_string(file_->GetPosition())) +
                    " " + error);
}

bool PdfTokenizer::CheckTrailer(const ByteBuffer& line) {
  if (static_cast<int>(kTrailer.size()) > line.size()) return false;
  for (size_t i = 0; i < kTrailer.size(); i++)
    if (kTrailer[i] != line.Get(static_cast<int>(i))) return false;
  return true;
}

bool PdfTokenizer::ReadLineSegment(ByteBuffer& buffer,
                                   bool is_null_whitespace) {
  int c;
  bool eol = false;
  while (IsWhitespace((c = Read()), is_null_whitespace)) {
  }
  bool prev_was_whitespace = false;
  while (!eol) {
    switch (c) {
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
      case 9:
      case 12:
      case 32:
        if (prev_was_whitespace) break;
        prev_was_whitespace = true;
        buffer.Append(static_cast<uint8_t>(c));
        break;
      default:
        prev_was_whitespace = false;
        buffer.Append(static_cast<uint8_t>(c));
        break;
    }
    if (eol || buffer.size() == buffer.Capacity()) {
      eol = true;
    } else {
      c = Read();
    }
  }
  if (buffer.size() == buffer.Capacity()) {
    eol = false;
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
      }
    }
  }
  return !(c == -1 && buffer.IsEmpty());
}

std::optional<std::array<int, 2>> PdfTokenizer::CheckObjectStart(
    PdfTokenizer& line_tokenizer) {
  try {
    line_tokenizer.Seek(0);
    if (!line_tokenizer.NextToken() ||
        line_tokenizer.GetTokenType() != TokenType::kNumber)
      return std::nullopt;
    int num = line_tokenizer.GetIntValue();
    if (!line_tokenizer.NextToken() ||
        line_tokenizer.GetTokenType() != TokenType::kNumber)
      return std::nullopt;
    int gen = line_tokenizer.GetIntValue();
    if (!line_tokenizer.NextToken()) return std::nullopt;
    if (line_tokenizer.GetByteContent() != kObj) return std::nullopt;
    return std::array<int, 2>{num, gen};
  } catch (...) {
  }
  return std::nullopt;
}

}  // namespace io
}  // namespace pdfcpp
