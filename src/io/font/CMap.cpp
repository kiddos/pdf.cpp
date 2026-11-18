#include "pdfcpp/io/font/CMap.h"

#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/io/source/RandomAccessSource.h"

namespace pdfcpp {
namespace io {

namespace {
uint32_t bytesToCode(const Bytes& b) {
    uint32_t v = 0;
    for (uint8_t c : b) v = (v << 8) | c;
    return v;
}

std::u32string utf16beToU32(const Bytes& b) {
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
    enum Kind { Str, Num, Name, Other, ArrStart, ArrEnd, DictStart, DictEnd, End } kind;
    Bytes bytes;
    std::string text;
};

class CMapLexer {
public:
    explicit CMapLexer(const Bytes& data) : tok_(RandomAccessSourceFactory().createSource(data)) {}
    Token next() {
        using TT = PdfTokenizer::TokenType;
        while (true) {
            if (!tok_.nextToken()) return {Token::End, {}, {}};
            switch (tok_.getTokenType()) {
                case TT::Comment:
                    continue;
                case TT::String:
                    return {Token::Str, tok_.getDecodedStringContent(), {}};
                case TT::Number:
                    return {Token::Num, {}, tok_.getStringValue()};
                case TT::Name:
                    return {Token::Name, {}, tok_.getStringValue()};
                case TT::StartArray:
                    return {Token::ArrStart, {}, {}};
                case TT::EndArray:
                    return {Token::ArrEnd, {}, {}};
                case TT::StartDic:
                    return {Token::DictStart, {}, {}};
                case TT::EndDic:
                    return {Token::DictEnd, {}, {}};
                default:
                    return {Token::Other, {}, tok_.getStringValue()};
            }
        }
    }

private:
    PdfTokenizer tok_;
};
}  // namespace

CMapParser::Result CMapParser::parse(const Bytes& cmap) {
    Result r;
    CMapLexer lex(cmap);
    std::vector<Token> stack;
    try {
        while (true) {
            Token t = lex.next();
            if (t.kind == Token::End) break;
            if (t.kind == Token::Other) {
                const std::string& op = t.text;
                if (op == "usecmap" && !stack.empty() && stack.back().kind == Token::Name) {
                    r.useCMap = stack.back().text;
                } else if (op == "begincodespacerange") {
                    while (true) {
                        Token a = lex.next();
                        if (a.kind != Token::Str) break;
                        Token b = lex.next();
                        r.codeSpaceRanges.push_back({a.bytes, b.bytes});
                    }
                } else if (op == "beginbfchar") {
                    while (true) {
                        Token a = lex.next();
                        if (a.kind != Token::Str) break;
                        Token b = lex.next();
                        std::u32string uni;
                        if (b.kind == Token::Str) uni = utf16beToU32(b.bytes);
                        else if (b.kind == Token::Name) {
                            // glyph name destination; resolved by caller (rare)
                            continue;
                        }
                        r.bfMap[{static_cast<int>(a.bytes.size()), bytesToCode(a.bytes)}] = uni;
                    }
                } else if (op == "beginbfrange") {
                    while (true) {
                        Token a = lex.next();
                        if (a.kind != Token::Str) break;
                        Token b = lex.next();
                        Token c = lex.next();
                        int len = static_cast<int>(a.bytes.size());
                        uint32_t start = bytesToCode(a.bytes), end = bytesToCode(b.bytes);
                        if (end < start || end - start > 0xFFFF) {
                            if (c.kind == Token::ArrStart)
                                while (lex.next().kind != Token::ArrEnd) {
                                }
                            continue;
                        }
                        if (c.kind == Token::Str) {
                            std::u32string base = utf16beToU32(c.bytes);
                            Bytes dst = c.bytes;
                            for (uint32_t code = start; code <= end; ++code) {
                                std::u32string u = utf16beToU32(dst);
                                r.bfMap[{len, code}] = u;
                                // increment last byte (with carry into previous, as iText does)
                                for (int i = static_cast<int>(dst.size()) - 1; i >= 0; --i) {
                                    if (++dst[i] != 0) break;
                                }
                            }
                        } else if (c.kind == Token::ArrStart) {
                            uint32_t code = start;
                            while (true) {
                                Token d = lex.next();
                                if (d.kind == Token::ArrEnd || d.kind == Token::End) break;
                                if (d.kind == Token::Str && code <= end) r.bfMap[{len, code}] = utf16beToU32(d.bytes);
                                ++code;
                            }
                        }
                    }
                } else if (op == "begincidchar") {
                    while (true) {
                        Token a = lex.next();
                        if (a.kind != Token::Str) break;
                        Token b = lex.next();
                        r.cidMap[{static_cast<int>(a.bytes.size()), bytesToCode(a.bytes)}] = std::atoi(b.text.c_str());
                    }
                } else if (op == "begincidrange") {
                    while (true) {
                        Token a = lex.next();
                        if (a.kind != Token::Str) break;
                        Token b = lex.next();
                        Token c = lex.next();
                        int len = static_cast<int>(a.bytes.size());
                        uint32_t start = bytesToCode(a.bytes), end = bytesToCode(b.bytes);
                        int cid = std::atoi(c.text.c_str());
                        if (end < start || end - start > 0x10000) continue;
                        for (uint32_t code = start; code <= end; ++code) r.cidMap[{len, code}] = cid++;
                    }
                } else if (op == "def" && stack.size() >= 2 && stack[stack.size() - 2].kind == Token::Name &&
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
std::shared_ptr<CMapToUnicode> CMapToUnicode::getIdentity() {
    static std::shared_ptr<CMapToUnicode> identity = [] {
        auto m = std::make_shared<CMapToUnicode>();
        m->identity_ = true;
        m->has2Byte_ = true;
        return m;
    }();
    return identity;
}

std::shared_ptr<CMapToUnicode> CMapToUnicode::parse(const Bytes& cmap) {
    auto r = CMapParser::parse(cmap);
    auto m = std::make_shared<CMapToUnicode>();
    for (auto& kv : r.bfMap) m->addChar(static_cast<int>(kv.first.second), kv.first.first, kv.second);
    m->codeSpaceRanges_ = r.codeSpaceRanges;
    if (r.useCMap == "Identity-H" || r.useCMap == "Identity-V") m->has2Byte_ = true;
    return m;
}

void CMapToUnicode::addChar(int code, int codeLength, const std::u32string& unicode) {
    map_[{codeLength, static_cast<uint32_t>(code)}] = unicode;
    if (codeLength == 1) has1Byte_ = true;
    else has2Byte_ = true;
}

const std::u32string* CMapToUnicode::lookup(const uint8_t* code, int length) const {
    uint32_t v = 0;
    for (int i = 0; i < length; ++i) v = (v << 8) | code[i];
    auto it = map_.find({length, v});
    if (it != map_.end()) return &it->second;
    if (identity_ && length == 2) {
        static thread_local std::u32string tmp;
        tmp = std::u32string(1, static_cast<char32_t>(v));
        return &tmp;
    }
    return nullptr;
}

std::map<int, int> CMapToUnicode::createDirectMapping() const {
    std::map<int, int> m;
    for (const auto& kv : map_)
        if (kv.second.size() == 1) m[static_cast<int>(kv.first.second)] = static_cast<int>(kv.second[0]);
    return m;
}

std::map<int, int> CMapToUnicode::createReverseMapping() const {
    std::map<int, int> m;
    for (const auto& kv : map_)
        if (kv.second.size() == 1) m[static_cast<int>(kv.second[0])] = static_cast<int>(kv.first.second);
    return m;
}

// ============================================================ CMapEncoding
std::shared_ptr<CMapEncoding> CMapEncoding::identity(bool vertical) {
    auto e = std::make_shared<CMapEncoding>();
    e->direct_ = true;
    e->name_ = vertical ? "Identity-V" : "Identity-H";
    e->codeSpaceRanges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
    return e;
}

std::shared_ptr<CMapEncoding> CMapEncoding::fromStream(const Bytes& cmap) {
    auto r = CMapParser::parse(cmap);
    auto e = std::make_shared<CMapEncoding>();
    e->codeSpaceRanges_ = r.codeSpaceRanges;
    e->cidMap_ = r.cidMap;
    if (r.cidMap.empty() && (r.useCMap == "Identity-H" || r.useCMap == "Identity-V")) {
        e->direct_ = true;
        if (e->codeSpaceRanges_.empty()) e->codeSpaceRanges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
    }
    if (e->codeSpaceRanges_.empty()) e->codeSpaceRanges_.push_back({Bytes{0, 0}, Bytes{0xff, 0xff}});
    return e;
}

std::shared_ptr<CMapEncoding> CMapEncoding::fromName(const std::string& name) {
    if (name == "Identity-V") return identity(true);
    auto e = identity(false);
    e->name_ = name;
    return e;
}

std::vector<CMapEncoding::Code> CMapEncoding::decode(const Bytes& bytes) const {
    std::vector<Code> out;
    size_t i = 0;
    while (i < bytes.size()) {
        int matchedLen = 0;
        for (int len = 1; len <= 4 && i + len <= bytes.size() && !matchedLen; ++len) {
            for (const auto& r : codeSpaceRanges_) {
                if (static_cast<int>(r.low.size()) != len) continue;
                bool inRange = true;
                for (int k = 0; k < len; ++k) {
                    uint8_t b = bytes[i + k];
                    if (b < r.low[k] || b > r.high[k]) {
                        inRange = false;
                        break;
                    }
                }
                if (inRange) {
                    matchedLen = len;
                    break;
                }
            }
        }
        if (!matchedLen) {
            // fall back to the shortest codespace length
            matchedLen = codeSpaceRanges_.empty() ? 2 : static_cast<int>(codeSpaceRanges_.front().low.size());
            if (i + matchedLen > bytes.size()) matchedLen = static_cast<int>(bytes.size() - i);
        }
        uint32_t code = 0;
        for (int k = 0; k < matchedLen; ++k) code = (code << 8) | bytes[i + k];
        int cid;
        if (direct_) {
            cid = static_cast<int>(code);
        } else {
            auto it = cidMap_.find({matchedLen, code});
            cid = it == cidMap_.end() ? 0 : it->second;
        }
        out.push_back({static_cast<int>(code), matchedLen, cid});
        i += matchedLen;
    }
    return out;
}

}  // namespace io
}  // namespace pdfcpp
