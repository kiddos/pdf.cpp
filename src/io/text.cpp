#include <iconv.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unordered_map>

#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/TextUtil.h"

namespace pdfcpp {
namespace io {

#include "generated/EncodingTables.inc"

// ---------------------------------------------------------------- TextUtil
std::vector<uint32_t> TextUtil::utf8ToCodePoints(const std::string& s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        uint32_t cp;
        int extra;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= n || (static_cast<uint8_t>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

void TextUtil::appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
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

std::string TextUtil::codePointToUtf8(uint32_t cp) {
    std::string s;
    appendUtf8(s, cp);
    return s;
}

std::string TextUtil::codePointsToUtf8(const std::vector<uint32_t>& cps) {
    std::string s;
    s.reserve(cps.size());
    for (uint32_t cp : cps) appendUtf8(s, cp);
    return s;
}

std::u16string TextUtil::convertFromUtf32(uint32_t cp) {
    if (cp < 0x10000) return std::u16string(1, static_cast<char16_t>(cp));
    cp -= 0x10000;
    return std::u16string{static_cast<char16_t>((cp >> 10) + 0xD800), static_cast<char16_t>((cp & 0x3FF) + 0xDC00)};
}

std::u16string TextUtil::utf8ToUtf16(const std::string& s) {
    std::u16string out;
    for (uint32_t cp : utf8ToCodePoints(s)) out += convertFromUtf32(cp);
    return out;
}

std::string TextUtil::utf16ToUtf8(const std::u16string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (isSurrogatePair(s, i)) {
            appendUtf8(out, convertToUtf32(s[i], s[i + 1]));
            ++i;
        } else {
            appendUtf8(out, s[i]);
        }
    }
    return out;
}

std::string TextUtil::latin1ToUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) appendUtf8(out, static_cast<uint8_t>(c));
    return out;
}

bool TextUtil::isWhiteSpace(uint32_t c) {
    // Java Character.isWhitespace semantics
    if (c == 0x00A0 || c == 0x2007 || c == 0x202F) return false;
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20)) return true;
    return c == 0x1680 || (c >= 0x2000 && c <= 0x2006) || (c >= 0x2008 && c <= 0x200A) || c == 0x2028 ||
           c == 0x2029 || c == 0x205F || c == 0x3000;
}

bool TextUtil::isDiacritic(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F);
}

// ---------------------------------------------------------------- PdfEncodings
namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct ReverseTables {
    std::unordered_map<uint32_t, int> winansi, pdfdoc, symbol, zapf;
    ReverseTables() {
        for (int k = 128; k < 161; ++k) {
            if (WINANSI_BYTE_TO_CHAR[k] != 65533) winansi[WINANSI_BYTE_TO_CHAR[k]] = k;
            if (PDFDOC_BYTE_TO_CHAR[k] != 65533) pdfdoc[PDFDOC_BYTE_TO_CHAR[k]] = k;
        }
        for (int k = 0; k < 256; ++k) {
            if (SYMBOL_TABLE[k] != 0) symbol[SYMBOL_TABLE[k]] = k;
            if (ZAPF_TABLE[k] != 0) zapf[ZAPF_TABLE[k]] = k;
        }
    }
};

const ReverseTables& tables() {
    static ReverseTables t;
    return t;
}

// Mac Roman upper half
const uint16_t MAC_ROMAN[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9,
    0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9,
    0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8,
    0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
    0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0,
    0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178,
    0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB,
    0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

std::string iconvName(const std::string& encoding) {
    std::string lower = toLower(encoding);
    if (lower == "unicodebigunmarked" || lower == "utf-16be") return "UTF-16BE";
    if (lower == "unicodelittleunmarked" || lower == "utf-16le") return "UTF-16LE";
    if (lower == "unicodebig") return "UTF-16BE";
    if (lower == "utf-8" || lower == "utf8") return "UTF-8";
    if (lower.rfind("cp", 0) == 0) return "CP" + encoding.substr(2);
    if (lower == "macroman") return "MACINTOSH";
    return encoding;
}

std::string runIconv(const std::string& from, const std::string& to, const char* data, size_t len) {
    iconv_t cd = iconv_open(to.c_str(), from.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) throw IoException(format(IoExceptionMessageConstant::UNSUPPORTED_ENCODING, from + "->" + to));
    std::string out;
    std::vector<char> inbuf(data, data + len);
    char* inptr = inbuf.data();
    size_t inleft = len;
    char buf[4096];
    while (inleft > 0) {
        char* outptr = buf;
        size_t outleft = sizeof buf;
        size_t r = iconv(cd, &inptr, &inleft, &outptr, &outleft);
        out.append(buf, sizeof buf - outleft);
        if (r == static_cast<size_t>(-1)) {
            if (errno == E2BIG) continue;
            // skip one unconvertible input byte
            ++inptr;
            --inleft;
        }
    }
    iconv_close(cd);
    return out;
}

}  // namespace

int PdfEncodings::standardEncoding(int b) { return (b >= 0 && b < 256) ? STANDARD_ENCODING[b] : 0; }
int PdfEncodings::winAnsiToUnicode(int b) { return (b >= 0 && b < 256) ? WINANSI_BYTE_TO_CHAR[b] : -1; }
int PdfEncodings::pdfDocToUnicode(int b) { return (b >= 0 && b < 256) ? PDFDOC_BYTE_TO_CHAR[b] : -1; }
int PdfEncodings::symbolToUnicode(int b) { return (b >= 0 && b < 256) ? SYMBOL_TABLE[b] : -1; }
int PdfEncodings::zapfDingbatsToUnicode(int b) { return (b >= 0 && b < 256) ? ZAPF_TABLE[b] : -1; }
int PdfEncodings::macRomanToUnicode(int b) {
    if (b < 0 || b > 255) return -1;
    return b < 128 ? b : MAC_ROMAN[b - 128];
}

namespace {
// Accept the same spellings as byteToUnicode (Java only matches the exact "Cp1252" / "PDF" constants).
bool isWinAnsiAlias(const std::string& lower) {
    return lower == "cp1252" || lower == "winansi" || lower == "winansiencoding";
}
bool isPdfDocAlias(const std::string& lower) { return lower == "pdf" || lower == "pdfdocencoding"; }
}  // namespace

int PdfEncodings::byteToUnicode(int b, const std::string& encoding) {
    std::string e = toLower(encoding);
    if (e.empty()) return b & 0xff;
    if (e == "cp1252" || e == "winansi" || e == "winansiencoding") return winAnsiToUnicode(b);
    if (e == "pdf" || e == "pdfdocencoding") return pdfDocToUnicode(b);
    if (e == "symbol") return symbolToUnicode(b);
    if (e == "zapfdingbats") return zapfDingbatsToUnicode(b);
    if (e == "macroman" || e == "macromanencoding") return macRomanToUnicode(b);
    if (e == "standardencoding") return standardEncoding(b);
    std::string s = convertToString(Bytes{static_cast<uint8_t>(b)}, encoding);
    auto cps = TextUtil::utf8ToCodePoints(s);
    return cps.empty() ? -1 : static_cast<int>(cps[0]);
}

Bytes PdfEncodings::convertToBytes(const std::string& text, const std::string& encoding) {
    if (encoding.empty()) {
        Bytes b;
        for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) b.push_back(static_cast<uint8_t>(cp));
        return b;
    }
    std::string lower = toLower(encoding);
    const auto& t = tables();
    const std::unordered_map<uint32_t, int>* extra = nullptr;
    if (lower == "symbol") extra = &t.symbol;
    else if (lower == "zapfdingbats") extra = &t.zapf;
    if (extra) {
        Bytes b;
        for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
            auto it = extra->find(cp);
            if (it != extra->end() && it->second != 0) b.push_back(static_cast<uint8_t>(it->second));
        }
        return b;
    }
    if (lower == "symboltt") {
        Bytes b;
        for (uint32_t cp : TextUtil::utf8ToCodePoints(text))
            if ((cp & 0xff00) == 0 || (cp & 0xff00) == 0xf000) b.push_back(static_cast<uint8_t>(cp));
        return b;
    }
    const std::unordered_map<uint32_t, int>* hash = nullptr;
    if (isWinAnsiAlias(lower)) hash = &t.winansi;
    else if (isPdfDocAlias(lower)) hash = &t.pdfdoc;
    if (hash) {
        Bytes b;
        for (uint32_t ch : TextUtil::utf8ToCodePoints(text)) {
            int c;
            if (ch < 128 || (ch > 160 && ch <= 255)) {
                c = static_cast<int>(ch);
            } else {
                auto it = hash->find(ch);
                c = it == hash->end() ? 0 : it->second;
            }
            if (c != 0) b.push_back(static_cast<uint8_t>(c));
        }
        return b;
    }
    if (lower == "utf-8" || lower == "utf8") return Bytes(text.begin(), text.end());
    if (lower == "unicodebig") {
        std::u16string u = TextUtil::utf8ToUtf16(text);
        Bytes b{0xFE, 0xFF};
        for (char16_t c : u) {
            b.push_back(static_cast<uint8_t>(c >> 8));
            b.push_back(static_cast<uint8_t>(c & 0xff));
        }
        return b;
    }
    if (lower == "unicodebigunmarked" || lower == "utf-16be") {
        std::u16string u = TextUtil::utf8ToUtf16(text);
        Bytes b;
        for (char16_t c : u) {
            b.push_back(static_cast<uint8_t>(c >> 8));
            b.push_back(static_cast<uint8_t>(c & 0xff));
        }
        return b;
    }
    if (lower == "macroman" || lower == "macromanencoding") {
        Bytes b;
        for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
            if (cp < 128) {
                b.push_back(static_cast<uint8_t>(cp));
                continue;
            }
            for (int k = 0; k < 128; ++k)
                if (MAC_ROMAN[k] == cp) {
                    b.push_back(static_cast<uint8_t>(k + 128));
                    break;
                }
        }
        return b;
    }
    std::string r = runIconv("UTF-8", iconvName(encoding), text.data(), text.size());
    return Bytes(r.begin(), r.end());
}

Bytes PdfEncodings::convertToBytes(uint32_t ch, const std::string& encoding) {
    return convertToBytes(TextUtil::codePointToUtf8(ch), encoding);
}

std::string PdfEncodings::convertToString(const Bytes& bytes, const std::string& encoding) {
    if (encoding.empty()) {
        std::string s;
        for (uint8_t b : bytes) TextUtil::appendUtf8(s, b);
        return s;
    }
    std::string lower = toLower(encoding);
    const uint16_t* table = nullptr;
    if (lower == "symbol") table = SYMBOL_TABLE;
    else if (lower == "zapfdingbats") table = ZAPF_TABLE;
    else if (isWinAnsiAlias(lower)) table = WINANSI_BYTE_TO_CHAR;
    else if (isPdfDocAlias(lower)) table = PDFDOC_BYTE_TO_CHAR;
    if (table) {
        // Unmapped Symbol/ZapfDingbats bytes become U+0000, exactly as in Java's SymbolConversion.byteToChar.
        std::string s;
        for (uint8_t b : bytes) TextUtil::appendUtf8(s, table[b]);
        return s;
    }
    if (lower == "utf-8" || lower == "utf8") {
        size_t start = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
        return std::string(bytes.begin() + start, bytes.end());
    }
    if (lower == "unicodebig" || lower == "unicodebigunmarked" || lower == "utf-16be") {
        size_t start = 0;
        if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) start = 2;
        std::u16string u;
        for (size_t i = start; i + 1 < bytes.size(); i += 2) u += static_cast<char16_t>((bytes[i] << 8) | bytes[i + 1]);
        return TextUtil::utf16ToUtf8(u);
    }
    if (lower == "macroman" || lower == "macromanencoding") {
        std::string s;
        for (uint8_t b : bytes) TextUtil::appendUtf8(s, macRomanToUnicode(b));
        return s;
    }
    return runIconv(iconvName(encoding), "UTF-8", reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool PdfEncodings::isPdfDocEncoding(const std::string& text) {
    const auto& t = tables();
    for (uint32_t ch : TextUtil::utf8ToCodePoints(text)) {
        if (ch < 128 || (ch > 160 && ch <= 255)) continue;
        if (t.pdfdoc.find(ch) == t.pdfdoc.end()) return false;
    }
    return true;
}

}  // namespace io
}  // namespace pdfcpp
