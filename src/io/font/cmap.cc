#include "pdfcpp/io/font/cmap.h"

#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/io/source/random_access_source.h"

namespace pdfcpp {
namespace io {

namespace {
uint32_t BytesToCode(const Bytes& b) {
  uint32_t v = 0;
  for (uint8_t c : b) v = (v << 8) | c;
  return v;
}

std::u32string Utf16beToU32(const Bytes& b) {
  std::u32string out;
  for (size_t i = 0; i + 1 < b.size(); i += 2) {
    char32_t c = (b[i] << 8) | b[i + 1];
    if (c >= 0xD800 && c <= 0xDBFF && i + 3 < b.size()) {
      char32_t lo = (b[i + 2] << 8) | b[i + 3];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    out += c;
  }
  if (b.size() == 1) out += static_cast<char32_t>(b[0]);
  return out;
}

struct Token {
  enum Kind {
    kStr,
    kNum,
    kName,
    kOther,
    kArrStart,
    kArrEnd,
    kDictStart,
    kDictEnd,
    kEnd
  } kind;
  Bytes bytes;
  std::string text;
};

class CMapLexer {
 public:
  explicit CMapLexer(const Bytes& data)
      : tok_(RandomAccessSourceFactory().CreateSource(data)) {}
  Token Next() {
    using TT = PdfTokenizer::TokenType;
    while (true) {
      if (!tok_.NextToken()) return {Token::kEnd, {}, {}};
      switch (tok_.GetTokenType()) {
        case TT::kComment:
          continue;
        case TT::kString:
          return {Token::kStr, tok_.GetDecodedStringContent(), {}};
        case TT::kNumber:
          return {Token::kNum, {}, tok_.GetStringValue()};
        case TT::kName:
          return {Token::kName, {}, tok_.GetStringValue()};
        case TT::kStartArray:
          return {Token::kArrStart, {}, {}};
        case TT::kEndArray:
          return {Token::kArrEnd, {}, {}};
        case TT::kStartDic:
          return {Token::kDictStart, {}, {}};
        case TT::kEndDic:
          return {Token::kDictEnd, {}, {}};
        default:
          return {Token::kOther, {}, tok_.GetStringValue()};
      }
    }
  }

 private:
  PdfTokenizer tok_;
};
}  // namespace

CMapParser::Result CMapParser::Parse(const Bytes& cmap) {
  Result r;
  CMapLexer lex(cmap);
  std::vector<Token> stack;
  try {
    while (true) {
      Token t = lex.Next();
      if (t.kind == Token::kEnd) break;
      if (t.kind == Token::kOther) {
        const std::string& op = t.text;
        if (op == "usecmap" && !stack.empty() &&
            stack.back().kind == Token::kName) {
          r.use_c_map = stack.back().text;
        } else if (op == "begincodespacerange") {
          while (true) {
            Token a = lex.Next();
            if (a.kind != Token::kStr) break;
            Token b = lex.Next();
            r.code_space_ranges.push_back({a.bytes, b.bytes});
          }
        } else if (op == "beginbfchar") {
          while (true) {
            Token a = lex.Next();
            if (a.kind != Token::kStr) break;
            Token b = lex.Next();
            std::u32string uni;
            if (b.kind == Token::kStr)
              uni = Utf16beToU32(b.bytes);
            else if (b.kind == Token::kName) {
              // glyph name destination; resolved by caller (rare)
              continue;
            }
            r.bf_map[{static_cast<int>(a.bytes.size()), BytesToCode(a.bytes)}] =
                uni;
          }
        } else if (op == "beginbfrange") {
          while (true) {
            Token a = lex.Next();
            if (a.kind != Token::kStr) break;
            Token b = lex.Next();
            Token c = lex.Next();
            int len = static_cast<int>(a.bytes.size());
            uint32_t start = BytesToCode(a.bytes), end = BytesToCode(b.bytes);
            if (end < start || end - start > 0xFFFF) {
              if (c.kind == Token::kArrStart) {
                Token::Kind k;
                do {
                  k = lex.Next().kind;
                } while (k != Token::kArrEnd && k != Token::kEnd);
              }
              continue;
            }
            if (c.kind == Token::kStr) {
              std::u32string base = Utf16beToU32(c.bytes);
              Bytes dst = c.bytes;
              for (uint32_t code = start; code <= end; ++code) {
                std::u32string u = Utf16beToU32(dst);
                r.bf_map[{len, code}] = u;
                // increment last byte (with carry into previous, as iText does)
                for (int i = static_cast<int>(dst.size()) - 1; i >= 0; --i) {
                  if (++dst[i] != 0) break;
                }
              }
            } else if (c.kind == Token::kArrStart) {
              uint32_t code = start;
              while (true) {
                Token d = lex.Next();
                if (d.kind == Token::kArrEnd || d.kind == Token::kEnd) break;
                if (d.kind == Token::kStr && code <= end)
                  r.bf_map[{len, code}] = Utf16beToU32(d.bytes);
                ++code;
              }
            }
          }
        } else if (op == "begincidchar") {
          while (true) {
            Token a = lex.Next();
            if (a.kind != Token::kStr) break;
            Token b = lex.Next();
            r.cid_map[{static_cast<int>(a.bytes.size()),
                       BytesToCode(a.bytes)}] = std::atoi(b.text.c_str());
          }
        } else if (op == "begincidrange") {
          while (true) {
            Token a = lex.Next();
            if (a.kind != Token::kStr) break;
            Token b = lex.Next();
            Token c = lex.Next();
            int len = static_cast<int>(a.bytes.size());
            uint32_t start = BytesToCode(a.bytes), end = BytesToCode(b.bytes);
            int cid = std::atoi(c.text.c_str());
            if (end < start || end - start > 0x10000) continue;
            for (uint32_t code = start; code <= end; ++code)
              r.cid_map[{len, code}] = cid++;
          }
        } else if (op == "def" && stack.size() >= 2 &&
                   stack[stack.size() - 2].kind == Token::kName &&
                   stack[stack.size() - 2].text == "WMode") {
          r.wmode = std::atoi(stack.back().text.c_str());
        }
      }
      stack.push_back(t);
      if (stack.size() > 8) stack.erase(stack.begin());
    }
  } catch (...) {
    // be lenient with broken CMaps
  }
  return r;
}

// ============================================================ CMapToUnicode
std::shared_ptr<CMapToUnicode> CMapToUnicode::GetIdentity() {
  static std::shared_ptr<CMapToUnicode> identity = [] {
    auto m = std::make_shared<CMapToUnicode>();
    m->identity_ = true;
    m->has2_byte_ = true;
    return m;
  }();
  return identity;
}

std::shared_ptr<CMapToUnicode> CMapToUnicode::Parse(const Bytes& cmap) {
  auto r = CMapParser::Parse(cmap);
  auto m = std::make_shared<CMapToUnicode>();
  for (auto& kv : r.bf_map)
    m->AddChar(static_cast<int>(kv.first.second), kv.first.first, kv.second);
  m->code_space_ranges_ = r.code_space_ranges;
  if (r.use_c_map == "Identity-H" || r.use_c_map == "Identity-V")
    m->has2_byte_ = true;
  return m;
}

void CMapToUnicode::AddChar(int code, int code_length,
                            const std::u32string& unicode) {
  map_[{code_length, static_cast<uint32_t>(code)}] = unicode;
  if (code_length == 1)
    has1_byte_ = true;
  else
    has2_byte_ = true;
}

std::optional<std::u32string> CMapToUnicode::Lookup(const uint8_t* code,
                                                    int length) const {
  uint32_t v = 0;
  for (int i = 0; i < length; ++i) v = (v << 8) | code[i];
  auto it = map_.find({length, v});
  if (it != map_.end()) return it->second;
  if (identity_ && length == 2)
    return std::u32string(1, static_cast<char32_t>(v));
  return std::nullopt;
}

std::map<int, int> CMapToUnicode::CreateDirectMapping() const {
  std::map<int, int> m;
  for (const auto& kv : map_)
    if (kv.second.size() == 1)
      m[static_cast<int>(kv.first.second)] = static_cast<int>(kv.second[0]);
  return m;
}

std::map<int, int> CMapToUnicode::CreateReverseMapping() const {
  std::map<int, int> m;
  for (const auto& kv : map_)
    if (kv.second.size() == 1)
      m[static_cast<int>(kv.second[0])] = static_cast<int>(kv.first.second);
  return m;
}

// ============================================================ CMapEncoding
std::shared_ptr<CMapEncoding> CMapEncoding::Identity(bool vertical) {
  auto e = std::make_shared<CMapEncoding>();
  e->direct_ = true;
  e->name_ = vertical ? "Identity-V" : "Identity-H";
  e->code_space_ranges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
  return e;
}

std::shared_ptr<CMapEncoding> CMapEncoding::FromStream(const Bytes& cmap) {
  auto r = CMapParser::Parse(cmap);
  auto e = std::make_shared<CMapEncoding>();
  e->code_space_ranges_ = r.code_space_ranges;
  e->cid_map_ = r.cid_map;
  if (r.cid_map.empty() &&
      (r.use_c_map == "Identity-H" || r.use_c_map == "Identity-V")) {
    e->direct_ = true;
    if (e->code_space_ranges_.empty())
      e->code_space_ranges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
  }
  if (e->code_space_ranges_.empty())
    e->code_space_ranges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
  return e;
}

std::shared_ptr<CMapEncoding> CMapEncoding::FromName(const std::string& name) {
  if (name == "Identity-V") return Identity(true);
  auto e = Identity(false);
  e->name_ = name;
  return e;
}

std::vector<CMapEncoding::Code> CMapEncoding::Decode(const Bytes& bytes) const {
  std::vector<Code> out;
  size_t i = 0;
  while (i < bytes.size()) {
    int matched_len = 0;
    for (int len = 1; len <= 4 && i + len <= bytes.size() && !matched_len;
         ++len) {
      for (const auto& r : code_space_ranges_) {
        if (static_cast<int>(r.low.size()) != len) continue;
        bool in_range = true;
        for (int k = 0; k < len; ++k) {
          uint8_t b = bytes[i + k];
          if (b < r.low[k] || b > r.high[k]) {
            in_range = false;
            break;
          }
        }
        if (in_range) {
          matched_len = len;
          break;
        }
      }
    }
    if (!matched_len) {
      // fall back to the shortest codespace length
      matched_len =
          code_space_ranges_.empty()
              ? 2
              : static_cast<int>(code_space_ranges_.front().low.size());
      if (i + matched_len > bytes.size())
        matched_len = static_cast<int>(bytes.size() - i);
    }
    uint32_t code = 0;
    for (int k = 0; k < matched_len; ++k) code = (code << 8) | bytes[i + k];
    int cid;
    if (direct_) {
      cid = static_cast<int>(code);
    } else {
      auto it = cid_map_.find({matched_len, code});
      cid = it == cid_map_.end() ? 0 : it->second;
    }
    out.push_back({static_cast<int>(code), matched_len, cid});
    i += matched_len;
  }
  return out;
}

}  // namespace io
}  // namespace pdfcpp
