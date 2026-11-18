#include "pdfcpp/io/font/FontProgram.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/TextUtil.h"

namespace pdfcpp {
namespace io {

namespace detail {
const std::map<std::string, std::pair<const uint8_t*, size_t>>& embeddedResources();
}

const Bytes* ResourceUtil::getResource(const std::string& name) {
    static std::map<std::string, Bytes> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return &it->second;
    const auto& res = detail::embeddedResources();
    auto r = res.find(name);
    if (r == res.end()) return nullptr;
    auto ins = cache.emplace(name, Bytes(r->second.first, r->second.first + r->second.second));
    return &ins.first->second;
}

// ============================================================ Glyph / GlyphLine
std::string Glyph::getUnicodeString() const {
    std::string s;
    for (char32_t c : chars_) TextUtil::appendUtf8(s, c);
    return s;
}

std::string GlyphLine::toUnicodeString(int start, int end) const {
    std::string s;
    for (int i = start; i < end && i < size(); ++i) {
        const Glyph& g = glyphs_[i];
        if (!g.getChars().empty()) s += g.getUnicodeString();
        else if (g.hasValidUnicode()) TextUtil::appendUtf8(s, static_cast<uint32_t>(g.getUnicode()));
    }
    return s;
}

// ============================================================ AdobeGlyphList
namespace {
struct GlyphListData {
    std::unordered_map<std::string, int> names2unicode;
    std::unordered_map<int, std::string> unicode2names;
    GlyphListData() {
        const Bytes* data = ResourceUtil::getResource("font/AdobeGlyphList.txt");
        if (!data) return;
        std::string s(data->begin(), data->end());
        std::istringstream in(s);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto semi = line.find(';');
            if (semi == std::string::npos) continue;
            std::string name = line.substr(0, semi);
            std::string hex = line.substr(semi + 1);
            if (hex.find(' ') != std::string::npos) continue;
            int num = static_cast<int>(std::strtol(hex.c_str(), nullptr, 16));
            unicode2names[num] = name;
            names2unicode[name] = num;
        }
    }
};
const GlyphListData& glyphList() {
    static GlyphListData d;
    return d;
}
}  // namespace

int AdobeGlyphList::nameToUnicode(const std::string& name) {
    const auto& d = glyphList();
    auto it = d.names2unicode.find(name);
    if (it != d.names2unicode.end()) return it->second;
    if (name.size() == 7 && (name.compare(0, 3, "uni") == 0 || name.compare(0, 3, "UNI") == 0)) {
        char* end = nullptr;
        long v = std::strtol(name.c_str() + 3, &end, 16);
        if (end && *end == '\0') return static_cast<int>(v);
    }
    return -1;
}

std::string AdobeGlyphList::unicodeToName(int unicode) {
    const auto& d = glyphList();
    auto it = d.unicode2names.find(unicode);
    return it == d.unicode2names.end() ? std::string() : it->second;
}

// ============================================================ FontEncoding
std::string FontEncoding::normalizeEncoding(const std::string& enc) {
    std::string tmp = enc;
    for (auto& c : tmp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (tmp.empty() || tmp == "winansi" || tmp == "winansiencoding") return PdfEncodings::WINANSI;
    if (tmp == "macroman" || tmp == "macromanencoding") return PdfEncodings::MACROMAN;
    if (tmp == "zapfdingbatsencoding") return PdfEncodings::ZAPFDINGBATS;
    return enc;
}

std::shared_ptr<FontEncoding> FontEncoding::createFontEncoding(const std::string& baseEncoding) {
    std::shared_ptr<FontEncoding> encoding(new FontEncoding());
    encoding->baseEncoding_ = normalizeEncoding(baseEncoding);
    if (!encoding->baseEncoding_.empty() && encoding->baseEncoding_[0] == '#') encoding->fillCustomEncoding();
    else encoding->fillNamedEncoding();
    return encoding;
}

std::shared_ptr<FontEncoding> FontEncoding::createEmptyFontEncoding() {
    std::shared_ptr<FontEncoding> encoding(new FontEncoding());
    encoding->baseEncoding_.clear();
    encoding->fontSpecific_ = false;
    encoding->initDifferences();
    for (int ch = 0; ch < 256; ch++) encoding->unicodeDifferences_[ch] = ch;
    return encoding;
}

std::shared_ptr<FontEncoding> FontEncoding::createFontSpecificEncoding() {
    std::shared_ptr<FontEncoding> encoding(new FontEncoding());
    encoding->fontSpecific_ = true;
    fillFontEncoding(*encoding);
    return encoding;
}

void FontEncoding::fillFontEncoding(FontEncoding& encoding) {
    for (int ch = 0; ch < 256; ch++) {
        encoding.unicodeToCode_[ch] = ch;
        encoding.codeToUnicode_[ch] = ch;
        encoding.unicodeDifferences_[ch] = ch;
    }
}

bool FontEncoding::addSymbol(int code, int unicode) {
    if (code < 0 || code > 255) return false;
    std::string glyphName = AdobeGlyphList::unicodeToName(unicode);
    if (glyphName.empty()) return false;
    unicodeToCode_[unicode] = code;
    codeToUnicode_[code] = unicode;
    if (!hasDifferences_) initDifferences();
    differences_[code] = glyphName;
    unicodeDifferences_[unicode] = unicode;
    return true;
}

int FontEncoding::getUnicodeDifference(int index) const {
    auto it = unicodeDifferences_.find(index);
    return it == unicodeDifferences_.end() ? 0 : it->second;
}

Bytes FontEncoding::convertToBytes(const std::string& text) const {
    Bytes bytes;
    for (uint32_t ch : TextUtil::utf8ToCodePoints(text)) {
        auto it = unicodeToCode_.find(static_cast<int>(ch));
        if (it != unicodeToCode_.end()) bytes.push_back(static_cast<uint8_t>(it->second));
    }
    return bytes;
}

int FontEncoding::convertToByte(int unicode) const {
    auto it = unicodeToCode_.find(unicode);
    return it == unicodeToCode_.end() ? 0 : it->second;
}

bool FontEncoding::canEncode(int unicode) const {
    return unicodeToCode_.count(unicode) > 0 || unicode < 32 || TextUtil::isNewLine(unicode);
}

void FontEncoding::fillCustomEncoding() {
    initDifferences();
    std::string spec = baseEncoding_.substr(1);
    for (auto& c : spec)
        if (c == ',' || c == '\t' || c == '\n' || c == '\r' || c == '\f') c = ' ';
    std::istringstream tok(spec);
    std::string first;
    tok >> first;
    if (first == "full") {
        std::string order, name, uniHex;
        while (tok >> order >> name >> uniHex) {
            int uni = static_cast<int>(std::strtol(uniHex.c_str(), nullptr, 16));
            int uniName = AdobeGlyphList::nameToUnicode(name);
            int orderK = order[0] == '\'' ? static_cast<unsigned char>(order[1]) : std::atoi(order.c_str());
            orderK %= 256;
            unicodeToCode_[uni] = orderK;
            codeToUnicode_[orderK] = uni;
            differences_[orderK] = name;
            unicodeDifferences_[uni] = uniName;
        }
    } else {
        int k = std::atoi(first.c_str());
        std::string hex;
        while (tok >> hex && k < 256) {
            int uni = static_cast<int>(std::strtol(hex.c_str(), nullptr, 16) % 0x10000);
            std::string name = AdobeGlyphList::unicodeToName(uni);
            if (name.empty()) name = "uni" + hex;
            unicodeToCode_[uni] = k;
            codeToUnicode_[k] = uni;
            differences_[k] = name;
            unicodeDifferences_[uni] = uni;
            k++;
        }
    }
    for (int k = 0; k < 256; k++)
        if (differences_[k].empty()) differences_[k] = NOTDEF;
}

void FontEncoding::fillNamedEncoding() {
    bool stdEncoding = baseEncoding_ == PdfEncodings::WINANSI || baseEncoding_ == PdfEncodings::MACROMAN;
    if (!stdEncoding && !hasDifferences_) initDifferences();
    for (int ch = 0; ch < 256; ++ch) {
        int uni = PdfEncodings::byteToUnicode(ch, baseEncoding_);
        std::string name = uni >= 0 ? AdobeGlyphList::unicodeToName(uni) : std::string();
        if (name.empty()) {
            name = NOTDEF;
        } else {
            unicodeToCode_[uni] = ch;
            codeToUnicode_[ch] = uni;
            unicodeDifferences_[uni] = uni;
        }
        if (hasDifferences_) differences_[ch] = name;
    }
}

void FontEncoding::fillStandardEncoding() {
    for (int ch = 0; ch < 256; ++ch) {
        int uni = PdfEncodings::standardEncoding(ch);
        std::string name = AdobeGlyphList::unicodeToName(uni);
        if (name.empty()) {
            name = NOTDEF;
        } else {
            unicodeToCode_[uni] = ch;
            codeToUnicode_[ch] = uni;
            unicodeDifferences_[uni] = uni;
        }
        if (hasDifferences_) differences_[ch] = name;
    }
}

// ============================================================ FontProgram
int FontProgram::getWidth(int unicode) const {
    const Glyph* glyph = getGlyph(unicode);
    return glyph ? glyph->getWidth() : 0;
}

const std::array<int, 4>* FontProgram::getCharBBox(int unicode) const {
    const Glyph* glyph = getGlyph(unicode);
    return glyph ? glyph->getBbox() : nullptr;
}

const Glyph* FontProgram::getGlyph(int unicode) const {
    auto it = unicodeToGlyph_.find(unicode);
    return it == unicodeToGlyph_.end() ? nullptr : &it->second;
}

const Glyph* FontProgram::getGlyphByCode(int charCode) const {
    auto it = codeToGlyph_.find(charCode);
    return it == codeToGlyph_.end() ? nullptr : &it->second;
}

int FontProgram::getKerning(int first, int second) const {
    const Glyph* a = getGlyph(first);
    const Glyph* b = getGlyph(second);
    if (!a || !b) return 0;
    return getKerning(*a, *b);
}

void FontProgram::fixSpaceIssue() {
    auto it = unicodeToGlyph_.find(32);
    if (it != unicodeToGlyph_.end()) codeToGlyph_[it->second.getCode()] = it->second;
}

// ============================================================ StandardFonts
bool StandardFonts::isStandardFont(const std::string& fontName) {
    for (const char* n : {COURIER, COURIER_BOLD, COURIER_BOLDOBLIQUE, COURIER_OBLIQUE, HELVETICA, HELVETICA_BOLD,
                          HELVETICA_BOLDOBLIQUE, HELVETICA_OBLIQUE, SYMBOL, TIMES_BOLD, TIMES_BOLDITALIC, TIMES_ITALIC,
                          TIMES_ROMAN, ZAPFDINGBATS})
        if (fontName == n) return true;
    return false;
}

// ============================================================ Type1Font
static int fontWeightFromType1(const std::string& weight) {
    std::string w;
    for (char c : weight) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (w == "ultralight") return 100;
    if (w == "thin" || w == "extralight") return 200;
    if (w == "light") return 300;
    if (w == "book" || w == "regular" || w == "normal") return 400;
    if (w == "medium") return 500;
    if (w == "demibold" || w == "semibold") return 600;
    if (w == "bold") return 700;
    if (w == "extrabold" || w == "ultrabold") return 800;
    if (w == "heavy" || w == "black" || w == "ultra" || w == "ultrablack") return 900;
    if (w == "fat" || w == "extrablack") return 900;
    return 400;
}

std::shared_ptr<Type1Font> Type1Font::createStandardFont(const std::string& name) {
    if (!StandardFonts::isStandardFont(name)) throw IoException(name + " is not a standard type1 font.");
    const Bytes* afm = ResourceUtil::getResource("font/afm/" + name + ".afm");
    if (!afm) throw IoException(format(IoExceptionMessageConstant::NOT_FOUND_AS_FILE_OR_RESOURCE, name + ".afm"));
    std::shared_ptr<Type1Font> font(new Type1Font());
    font->builtIn_ = true;
    font->afmPath_ = name;
    font->process(*afm);
    return font;
}

std::shared_ptr<Type1Font> Type1Font::createFromAfm(const Bytes& afm, const Bytes& pfb, const std::string& afmPath) {
    std::shared_ptr<Type1Font> font(new Type1Font());
    font->pfb_ = pfb;
    font->afmPath_ = afmPath;
    font->process(afm);
    return font;
}

Type1Font::Type1Font(const std::string& baseFont) { fontNames_.setFontName(baseFont); }

void Type1Font::initializeGlyphs(const FontEncoding& fontEncoding) {
    for (int i = 0; i < 256; i++) {
        int unicode = fontEncoding.getUnicode(i);
        auto it = unicodeToGlyph_.find(unicode);
        if (it == unicodeToGlyph_.end()) continue;
        Glyph glyph(i, it->second.getWidth(), unicode, it->second.getChars(), false);
        codeToGlyph_[i] = glyph;
        unicodeToGlyph_[glyph.getUnicode()] = glyph;
    }
}

int Type1Font::getPdfFontFlags() const {
    int flags = 0;
    if (fontMetrics_.isFixedPitch()) flags |= 1;
    flags |= isFontSpecific() ? 4 : 32;
    if (fontMetrics_.getItalicAngle() < 0) flags |= 64;
    const std::string& n = fontNames_.getFontName();
    if (n.find("Caps") != std::string::npos || (n.size() >= 2 && n.compare(n.size() - 2, 2, "SC") == 0)) flags |= 131072;
    if (fontNames_.isBold() || fontNames_.getFontWeight() > 500) flags |= 262144;
    return flags;
}

int Type1Font::getKerning(const Glyph& first, const Glyph& second) const {
    if (first.hasValidUnicode() && second.hasValidUnicode()) {
        int64_t record = (static_cast<int64_t>(first.getUnicode()) << 32) + second.getUnicode();
        auto it = kernPairs_.find(record);
        return it == kernPairs_.end() ? 0 : it->second;
    }
    return 0;
}

bool Type1Font::setKerning(int first, int second, int kern) {
    kernPairs_[(static_cast<int64_t>(first) << 32) + second] = kern;
    return true;
}

const Glyph* Type1Font::getGlyphByName(const std::string& name) const {
    int unicode = AdobeGlyphList::nameToUnicode(name);
    return unicode != -1 ? getGlyph(unicode) : nullptr;
}

Bytes Type1Font::getFontStreamBytes(std::array<int, 3>& lengths) const {
    if (builtIn_ || pfb_.size() < 18) return Bytes();
    static const int PFB_TYPES[] = {1, 2, 1};
    Bytes out;
    size_t ptr = 0;
    for (int k = 0; k < 3; ++k) {
        if (ptr + 6 > pfb_.size() || pfb_[ptr] != 0x80 || pfb_[ptr + 1] != PFB_TYPES[k]) return Bytes();
        size_t size = pfb_[ptr + 2] | (pfb_[ptr + 3] << 8) | (pfb_[ptr + 4] << 16) | (static_cast<size_t>(pfb_[ptr + 5]) << 24);
        ptr += 6;
        if (ptr + size > pfb_.size()) return Bytes();
        lengths[k] = static_cast<int>(size);
        out.insert(out.end(), pfb_.begin() + ptr, pfb_.begin() + ptr + size);
        ptr += size;
    }
    return out;
}

static std::vector<std::string> splitTokens(const std::string& s, const char* delims) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::strchr(delims, c)) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string restOfLine(const std::string& line, const std::string& ident) {
    std::string rest = line.substr(line.find(ident) + ident.size());
    size_t b = rest.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    rest = rest.substr(b);
    while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back();
    return rest;
}

void Type1Font::process(const Bytes& afm) {
    std::string text(afm.begin(), afm.end());
    std::istringstream in(text);
    std::string line;
    bool startCharMetrics = false;
    while (!startCharMetrics && std::getline(in, line)) {
        auto tok = splitTokens(line, " ,\n\r\t\f");
        if (tok.empty()) continue;
        const std::string& ident = tok[0];
        auto num = [&](size_t i) { return i < tok.size() ? std::strtof(tok[i].c_str(), nullptr) : 0.f; };
        if (ident == "FontName") fontNames_.setFontName(restOfLine(line, ident));
        else if (ident == "FullName") fontNames_.setFullName(restOfLine(line, ident));
        else if (ident == "FamilyName") fontNames_.setFamilyName(restOfLine(line, ident));
        else if (ident == "Weight") fontNames_.setFontWeight(fontWeightFromType1(restOfLine(line, ident)));
        else if (ident == "ItalicAngle") fontMetrics_.setItalicAngle(num(1));
        else if (ident == "IsFixedPitch") fontMetrics_.setIsFixedPitch(tok.size() > 1 && tok[1] == "true");
        else if (ident == "CharacterSet") characterSet_ = restOfLine(line, ident);
        else if (ident == "FontBBox") fontMetrics_.setBbox(static_cast<int>(num(1)), static_cast<int>(num(2)), static_cast<int>(num(3)), static_cast<int>(num(4)));
        else if (ident == "UnderlinePosition") fontMetrics_.setUnderlinePosition(static_cast<int>(num(1)));
        else if (ident == "UnderlineThickness") fontMetrics_.setUnderlineThickness(static_cast<int>(num(1)));
        else if (ident == "EncodingScheme") encodingScheme_ = restOfLine(line, ident);
        else if (ident == "CapHeight") fontMetrics_.setCapHeight(static_cast<int>(num(1)));
        else if (ident == "XHeight") fontMetrics_.setXHeight(static_cast<int>(num(1)));
        else if (ident == "Ascender") fontMetrics_.setTypoAscender(static_cast<int>(num(1)));
        else if (ident == "Descender") fontMetrics_.setTypoDescender(static_cast<int>(num(1)));
        else if (ident == "StdHW") fontMetrics_.setStemH(static_cast<int>(num(1)));
        else if (ident == "StdVW") fontMetrics_.setStemV(static_cast<int>(num(1)));
        else if (ident == "StartCharMetrics") startCharMetrics = true;
    }
    if (!startCharMetrics) throw IoException(format(IoExceptionMessageConstant::START_CHAR_METRICS_IS_MISSING_IN_THE_METRICS_FILE, afmPath_));
    avgWidth_ = 0;
    int widthCount = 0;
    bool endCharMetrics = false;
    while (std::getline(in, line)) {
        auto tok = splitTokens(line, " \t\r");
        if (tok.empty()) continue;
        if (tok[0] == "EndCharMetrics") {
            endCharMetrics = true;
            break;
        }
        int C = -1, WX = 250;
        std::string N;
        std::array<int, 4> B{};
        bool hasB = false;
        for (const auto& part : splitTokens(line, ";")) {
            auto t = splitTokens(part, " \t\r");
            if (t.empty()) continue;
            if (t[0] == "C" && t.size() > 1) C = std::atoi(t[1].c_str());
            else if (t[0] == "WX" && t.size() > 1) WX = static_cast<int>(std::strtof(t[1].c_str(), nullptr));
            else if (t[0] == "N" && t.size() > 1) N = t[1];
            else if (t[0] == "B" && t.size() > 4) {
                B = {std::atoi(t[1].c_str()), std::atoi(t[2].c_str()), std::atoi(t[3].c_str()), std::atoi(t[4].c_str())};
                hasB = true;
            }
        }
        int unicode = AdobeGlyphList::nameToUnicode(N);
        Glyph glyph = hasB ? Glyph(C, WX, unicode, B) : Glyph(C, WX, unicode);
        if (C >= 0) codeToGlyph_[C] = glyph;
        if (unicode != -1) unicodeToGlyph_[unicode] = glyph;
        avgWidth_ += WX;
        widthCount++;
    }
    if (widthCount != 0) avgWidth_ /= widthCount;
    if (!endCharMetrics) throw IoException(format(IoExceptionMessageConstant::END_CHAR_METRICS_IS_MISSING_IN_THE_METRICS_FILE, afmPath_));
    if (!unicodeToGlyph_.count(0x00A0)) {
        auto sp = unicodeToGlyph_.find(0x0020);
        if (sp != unicodeToGlyph_.end()) {
            const auto* bb = sp->second.getBbox();
            unicodeToGlyph_[0x00A0] = bb ? Glyph(sp->second.getCode(), sp->second.getWidth(), 0x00A0, *bb)
                                         : Glyph(sp->second.getCode(), sp->second.getWidth(), 0x00A0);
        }
    }
    bool startKernPairs = false;
    while (std::getline(in, line)) {
        auto tok = splitTokens(line, " \t\r");
        if (tok.empty()) continue;
        if (tok[0] == "EndFontMetrics") break;
        if (tok[0] == "StartKernPairs") {
            startKernPairs = true;
            break;
        }
    }
    if (startKernPairs) {
        while (std::getline(in, line)) {
            auto tok = splitTokens(line, " \t\r");
            if (tok.empty()) continue;
            if (tok[0] == "KPX" && tok.size() >= 4) {
                int firstUni = AdobeGlyphList::nameToUnicode(tok[1]);
                int secondUni = AdobeGlyphList::nameToUnicode(tok[2]);
                if (firstUni != -1 && secondUni != -1)
                    kernPairs_[(static_cast<int64_t>(firstUni) << 32) + secondUni] = static_cast<int>(std::strtof(tok[3].c_str(), nullptr));
            } else if (tok[0] == "EndKernPairs") {
                break;
            }
        }
    }
    isFontSpecific_ = !(encodingScheme_ == "AdobeStandardEncoding" || encodingScheme_ == "StandardEncoding");
}

}  // namespace io
}  // namespace pdfcpp
