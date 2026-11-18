#include "pdfcpp/kernel/font/PdfFont.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <sstream>

#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/io/util/TextUtil.h"

namespace pdfcpp {
namespace kernel {

using io::Glyph;
using io::GlyphLine;
using io::TextUtil;

static std::u32string toU32(const std::string& utf8) {
    std::u32string s;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(utf8)) s += static_cast<char32_t>(cp);
    return s;
}

static std::string u32ToUtf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) TextUtil::appendUtf8(out, c);
    return out;
}

// ============================================================ PdfFont
PdfFont::PdfFont(PdfDictionaryPtr fontDictionary) : PdfObjectWrapper<PdfDictionary>(std::move(fontDictionary)) {
    newFont_ = false;
}

bool PdfFont::containsGlyph(int unicode) {
    const Glyph* glyph = getGlyph(unicode);
    if (!glyph) return false;
    if (glyph->getCode() == 0 && !getFontProgram()->isFontSpecific()) return false;  // .notdef
    return true;
}

GlyphLine PdfFont::createGlyphLine(const std::string& content) {
    std::vector<Glyph> glyphs;
    std::u32string text = toU32(content);
    for (size_t i = 0; i < text.size(); ++i) {
        const Glyph* glyph = getGlyph(static_cast<int>(text[i]));
        if (glyph) glyphs.push_back(*glyph);
    }
    return GlyphLine(glyphs);
}

int PdfFont::appendGlyphs(const std::u32string& text, int from, int to, std::vector<Glyph>& glyphs) {
    int processed = 0;
    for (int i = from; i <= to; ++i) {
        const Glyph* glyph = getGlyph(static_cast<int>(text[i]));
        if (glyph && (containsGlyph(static_cast<int>(text[i])) || TextUtil::isWhitespaceOrNonPrintable(text[i]))) {
            glyphs.push_back(*glyph);
            ++processed;
        } else {
            break;
        }
    }
    return processed;
}

int PdfFont::appendAnyGlyph(const std::u32string& text, int from, std::vector<Glyph>& glyphs) {
    const Glyph* glyph = getGlyph(static_cast<int>(text[from]));
    if (glyph) glyphs.push_back(*glyph);
    return 1;
}

std::string PdfFont::decode(const PdfString& content) {
    std::string out;
    for (const auto& c : decodeCodes(content)) out += c.text;
    return out;
}

float PdfFont::getContentWidth(const PdfString& content) {
    float width = 0;
    for (const auto& c : decodeCodes(content)) width += c.width;
    return width;
}

void PdfFont::writeText(const GlyphLine& text, int from, int to, PdfOutputStream& stream) {
    std::vector<Glyph> glyphs;
    for (int i = from; i <= to && i < text.size(); ++i) glyphs.push_back(text.get(i));
    Bytes bytes = convertToBytes(GlyphLine(glyphs));
    io::StreamUtil::writeEscapedString(stream, bytes);
}

void PdfFont::writeText(const std::string& text, PdfOutputStream& stream) {
    io::StreamUtil::writeEscapedString(stream, convertToBytes(text));
}

int PdfFont::getWidth(int unicode) {
    const Glyph* glyph = getGlyph(unicode);
    return glyph ? glyph->getWidth() : 0;
}

int PdfFont::getWidth(const std::string& text) {
    int total = 0;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
        const Glyph* glyph = getGlyph(static_cast<int>(cp));
        if (glyph) total += glyph->getWidth();
    }
    return total;
}

float PdfFont::getDescent(const std::string& text, float fontSize) {
    int min = 0;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
        const Glyph* glyph = getGlyph(static_cast<int>(cp));
        const auto* bbox = glyph ? glyph->getBbox() : nullptr;
        if (bbox) min = std::min(min, (*bbox)[1]);
        else if (fontProgram_) min = std::min(min, fontProgram_->getFontMetrics().getTypoDescender());
    }
    return min * fontSize / io::FontProgram::UNITS_NORMALIZATION;
}

float PdfFont::getDescent(int unicode, float fontSize) {
    const Glyph* glyph = getGlyph(unicode);
    const auto* bbox = glyph ? glyph->getBbox() : nullptr;
    int min = bbox ? std::min(0, (*bbox)[1]) : (fontProgram_ ? fontProgram_->getFontMetrics().getTypoDescender() : 0);
    return min * fontSize / io::FontProgram::UNITS_NORMALIZATION;
}

float PdfFont::getAscent(const std::string& text, float fontSize) {
    int max = 0;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
        const Glyph* glyph = getGlyph(static_cast<int>(cp));
        const auto* bbox = glyph ? glyph->getBbox() : nullptr;
        if (bbox) max = std::max(max, (*bbox)[3]);
        else if (fontProgram_) max = std::max(max, fontProgram_->getFontMetrics().getTypoAscender());
    }
    return max * fontSize / io::FontProgram::UNITS_NORMALIZATION;
}

float PdfFont::getAscent(int unicode, float fontSize) {
    const Glyph* glyph = getGlyph(unicode);
    const auto* bbox = glyph ? glyph->getBbox() : nullptr;
    int max = bbox ? std::max(0, (*bbox)[3]) : (fontProgram_ ? fontProgram_->getFontMetrics().getTypoAscender() : 0);
    return max * fontSize / io::FontProgram::UNITS_NORMALIZATION;
}

std::vector<std::string> PdfFont::splitString(const std::string& text, float fontSize, float maxWidth) {
    std::vector<std::string> resultString;
    auto cps = TextUtil::utf8ToCodePoints(text);
    size_t lastWhiteSpace = 0;
    size_t startPos = 0;
    float tokenLength = 0;
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t ch = cps[i];
        if (ch == ' ') lastWhiteSpace = i;
        float currentCharWidth = getWidth(static_cast<int>(ch), fontSize);
        if (tokenLength + currentCharWidth >= maxWidth || ch == '\n') {
            if (startPos < lastWhiteSpace) {
                resultString.push_back(TextUtil::codePointsToUtf8(std::vector<uint32_t>(cps.begin() + startPos, cps.begin() + lastWhiteSpace)));
                startPos = lastWhiteSpace + 1;
                tokenLength = 0;
                i = lastWhiteSpace;
                continue;
            } else if (startPos != i) {
                resultString.push_back(TextUtil::codePointsToUtf8(std::vector<uint32_t>(cps.begin() + startPos, cps.begin() + i)));
                startPos = i;
                tokenLength = currentCharWidth;
            } else {
                resultString.push_back(TextUtil::codePointsToUtf8(std::vector<uint32_t>(cps.begin() + startPos, cps.begin() + startPos + 1)));
                startPos = i + 1;
                tokenLength = 0;
            }
        } else {
            tokenLength += currentCharWidth;
        }
    }
    if (startPos < cps.size())
        resultString.push_back(TextUtil::codePointsToUtf8(std::vector<uint32_t>(cps.begin() + startPos, cps.end())));
    return resultString;
}

bool PdfFont::isBuiltWith(const std::string& fontProgram, const std::string& encoding) const {
    return fontProgram_ && fontProgram_->isBuiltWith(fontProgram) && encoding.empty();
}

void PdfFont::flush() {
    if (flushed_) return;
    if (newFont_) {
        flushFontData();
    }
    flushed_ = true;
    if (getPdfObject()->getIndirectReference()) getPdfObject()->flush();
}

std::string PdfFont::updateSubsetPrefix(const std::string& fontName, bool isSubset, bool isEmbedded) {
    if (!isSubset || !isEmbedded) return fontName;
    static std::mt19937 rng(std::random_device{}());
    std::string prefix;
    for (int k = 0; k < 6; ++k) prefix += static_cast<char>('A' + rng() % 26);
    return prefix + "+" + fontName;
}

PdfStreamPtr PdfFont::getPdfFontStream(const Bytes& fontStreamBytes, const std::vector<int>& fontStreamLengths) {
    auto fontStream = std::make_shared<PdfStream>(fontStreamBytes);
    fontStream->makeIndirect(getPdfObject()->getIndirectReference()->getDocument());
    for (size_t k = 0; k < fontStreamLengths.size(); ++k)
        fontStream->put(std::make_shared<PdfName>("Length" + std::to_string(k + 1)), makeNumber(fontStreamLengths[k]));
    return fontStream;
}

PdfDictionaryPtr PdfFont::getFontDescriptor(const std::string& fontName) {
    const io::FontMetrics& fontMetrics = fontProgram_->getFontMetrics();
    const io::FontNames& fontNames = fontProgram_->getFontNames();
    auto fontDescriptor = std::make_shared<PdfDictionary>();
    fontDescriptor->makeIndirect(getPdfObject()->getIndirectReference()->getDocument());
    fontDescriptor->put(PdfName::Type, PdfName::FontDescriptor);
    fontDescriptor->put(PdfName::FontName, std::make_shared<PdfName>(fontName));
    fontDescriptor->put(PdfName::Ascent, makeNumber(fontMetrics.getTypoAscender()));
    fontDescriptor->put(PdfName::CapHeight, makeNumber(fontMetrics.getCapHeight()));
    fontDescriptor->put(PdfName::Descent, makeNumber(fontMetrics.getTypoDescender()));
    const auto& bbox = fontMetrics.getBbox();
    fontDescriptor->put(PdfName::FontBBox, std::make_shared<PdfArray>(std::vector<int>{bbox[0], bbox[1], bbox[2], bbox[3]}));
    fontDescriptor->put(PdfName::ItalicAngle, makeNumber(static_cast<double>(fontMetrics.getItalicAngle())));
    fontDescriptor->put(PdfName::StemV, makeNumber(fontMetrics.getStemV()));
    if (fontMetrics.getXHeight() > 0) fontDescriptor->put(PdfName::XHeight, makeNumber(fontMetrics.getXHeight()));
    if (fontMetrics.getStemH() > 0) fontDescriptor->put(PdfName::StemH, makeNumber(fontMetrics.getStemH()));
    if (fontNames.getFontWeight() > 0) fontDescriptor->put(PdfName::FontWeight, makeNumber(fontNames.getFontWeight()));
    if (!fontNames.getFamilyName().empty())
        fontDescriptor->put(PdfName::FontFamily, std::make_shared<PdfString>(fontNames.getFamilyName()));
    return fontDescriptor;
}

// ============================================================ PdfSimpleFont
PdfSimpleFont::PdfSimpleFont(std::shared_ptr<io::FontProgram> fontProgram, const std::string& encodingIn, bool embedded,
                             Kind kind)
    : kind_(kind) {
    setPdfObject(std::make_shared<PdfDictionary>());
    fontProgram_ = std::move(fontProgram);
    std::string encoding = encodingIn;
    if (encoding.empty() && fontProgram_->isFontSpecific() && kind == Kind::Type1) encoding = io::FontEncoding::FONT_SPECIFIC;
    std::string lower = encoding;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "fontspecific") fontEncoding_ = io::FontEncoding::createFontSpecificEncoding();
    else fontEncoding_ = io::FontEncoding::createFontEncoding(encoding);
    auto* type1 = dynamic_cast<io::Type1Font*>(fontProgram_.get());
    embedded_ = embedded && !(type1 && type1->isBuiltInFont());
    if (kind == Kind::TrueType) embedded_ = embedded;
    subset_ = true;
    newFont_ = true;
}

PdfSimpleFont::PdfSimpleFont(PdfDictionaryPtr fontDictionary) : PdfFont(std::move(fontDictionary)) { initFromDictionary(); }

static std::shared_ptr<io::FontEncoding> readEncoding(const PdfObjectPtr& encodingObj, std::shared_ptr<io::FontEncoding> base) {
    if (!encodingObj) return base;
    if (encodingObj->isName()) {
        return io::FontEncoding::createFontEncoding(static_cast<PdfName&>(*encodingObj).getValue());
    }
    if (encodingObj->isDictionary()) {
        auto dict = std::static_pointer_cast<PdfDictionary>(encodingObj);
        std::shared_ptr<io::FontEncoding> enc;
        auto baseName = dict->getAsName(PdfName::BaseEncoding);
        if (baseName) enc = io::FontEncoding::createFontEncoding(baseName->getValue());
        else enc = base;
        if (!enc) enc = io::FontEncoding::createEmptyFontEncoding();
        // make a private copy since differences modify it
        auto copy = std::make_shared<io::FontEncoding>(*enc);
        auto differences = dict->getAsArray(PdfName::Differences);
        if (differences) {
            copy->initDifferences();
            int currentNumber = 0;
            for (int k = 0; k < differences->size(); ++k) {
                PdfObjectPtr obj = differences->get(k);
                if (obj->isNumber()) {
                    currentNumber = std::static_pointer_cast<PdfNumber>(obj)->intValue();
                } else if (obj->isName() && currentNumber >= 0 && currentNumber < 256) {
                    std::string glyphName = std::static_pointer_cast<PdfName>(obj)->getValue();
                    int unicode = io::AdobeGlyphList::nameToUnicode(glyphName);
                    if (unicode != -1) copy->setCodeToUnicode(currentNumber, unicode);
                    else copy->setCodeToUnicode(currentNumber, -1);
                    copy->setDifference(currentNumber, glyphName);
                    ++currentNumber;
                }
            }
        }
        return copy;
    }
    return base;
}

void PdfSimpleFont::initFromDictionary() {
    auto& dict = *getPdfObject();
    auto subtype = dict.getAsName(PdfName::Subtype);
    if (nameEquals(subtype, PdfName::TrueType)) kind_ = Kind::TrueType;
    else if (nameEquals(subtype, PdfName::Type3)) kind_ = Kind::Type3;
    else kind_ = Kind::Type1;
    auto baseFont = dict.getAsName(PdfName::BaseFont);
    std::string fontName = baseFont ? baseFont->getValue() : std::string();
    std::string stripped = fontName;
    if (stripped.size() > 7 && stripped[6] == '+') stripped = stripped.substr(7);
    std::shared_ptr<io::Type1Font> standard;
    // map common aliases (Arial -> Helvetica etc.) only for exact standard names
    if (kind_ != Kind::Type3 && io::StandardFonts::isStandardFont(stripped)) {
        try {
            standard = io::Type1Font::createStandardFont(stripped);
        } catch (...) {
        }
    }
    auto descriptor = dict.getAsDictionary(PdfName::FontDescriptor);
    bool symbolic = false;
    if (descriptor) {
        auto flags = descriptor->getAsNumber(PdfName::Flags);
        if (flags) symbolic = (flags->intValue() & 4) != 0 && (flags->intValue() & 32) == 0;
        if (auto mw = descriptor->getAsNumber(PdfName::MissingWidth)) missingWidth_ = mw->floatValue();
    }
    std::shared_ptr<io::FontEncoding> base;
    if (standard) {
        fontProgram_ = standard;
        if (standard->isFontSpecific()) {
            base = io::FontEncoding::createFontSpecificEncoding();
            // map codes via built-in AFM codes
            auto enc = io::FontEncoding::createEmptyFontEncoding();
            for (const auto& kv : standard->getCodeToGlyph())
                if (kv.first >= 0 && kv.first < 256 && kv.second.hasValidUnicode()) enc->setCodeToUnicode(kv.first, kv.second.getUnicode());
            base = enc;
        } else {
            auto enc = io::FontEncoding::createEmptyFontEncoding();
            enc->fillStandardEncoding();
            base = enc;
        }
    } else {
        auto t1 = std::make_shared<io::Type1Font>(fontName);
        fontProgram_ = t1;
        if (kind_ == Kind::TrueType && symbolic) {
            base = io::FontEncoding::createFontSpecificEncoding();
        } else if (kind_ == Kind::TrueType) {
            base = io::FontEncoding::createFontEncoding(io::PdfEncodings::WINANSI);
        } else {
            auto enc = io::FontEncoding::createEmptyFontEncoding();
            enc->fillStandardEncoding();
            base = enc;
        }
    }
    fontEncoding_ = readEncoding(dict.get(PdfName::Encoding), base);
    if (!fontEncoding_) fontEncoding_ = base;
    if (auto tu = dict.getAsStream(PdfName::ToUnicode)) {
        try {
            toUnicode_ = io::CMapToUnicode::parse(tu->getBytes());
        } catch (...) {
        }
    }
    // widths
    auto widths = dict.getAsArray(PdfName::Widths);
    auto firstCharN = dict.getAsNumber(PdfName::FirstChar);
    int firstChar = firstCharN ? firstCharN->intValue() : 0;
    if (widths) {
        for (int i = 0; i < widths->size(); ++i) {
            int code = firstChar + i;
            if (code < 0 || code > 255) continue;
            auto w = widths->getAsNumber(i);
            if (w) {
                widths_[code] = w->floatValue();
                hasWidth_[code] = true;
            }
        }
    }
    if (kind_ == Kind::Type3) {
        if (auto fm = dict.getAsArray(PdfName::FontMatrix)) {
            auto d = fm->toDoubleArray();
            if (d.size() == 6) std::copy(d.begin(), d.end(), fontMatrix_.begin());
        }
    }
    if (standard && !widths) {
        for (int code = 0; code < 256; ++code) {
            int uni = fontEncoding_->getUnicode(code);
            const Glyph* g = nullptr;
            if (standard->isFontSpecific()) g = standard->getGlyphByCode(code);
            else if (uni >= 0) g = standard->getGlyph(uni);
            if (g) {
                widths_[code] = static_cast<float>(g->getWidth());
                hasWidth_[code] = true;
            }
        }
    }
}

float PdfSimpleFont::getGlyphWidthForCode(int code) const {
    if (code >= 0 && code < 256 && hasWidth_[code]) {
        if (kind_ == Kind::Type3) return static_cast<float>(widths_[code] * fontMatrix_[0] * 1000);
        return widths_[code];
    }
    return missingWidth_;
}

const Glyph* PdfSimpleFont::getGlyph(int unicode) {
    if (!fontEncoding_->canEncode(unicode)) return nullptr;
    const Glyph* glyph = nullptr;
    if (fontEncoding_->isFontSpecific()) {
        glyph = kind_ == Kind::TrueType ? fontProgram_->getGlyph(unicode) : fontProgram_->getGlyphByCode(unicode);
    } else {
        glyph = fontProgram_->getGlyph(fontEncoding_->getUnicodeDifference(unicode));
        if (!glyph) {
            auto it = notdefGlyphs_.find(unicode);
            if (it != notdefGlyphs_.end()) return &it->second;
            const Glyph* notdef = fontProgram_->getGlyphByCode(0);
            if (notdef) {
                auto ins = notdefGlyphs_.emplace(unicode, Glyph(*notdef, unicode));
                return &ins.first->second;
            }
        }
    }
    return glyph;
}

bool PdfSimpleFont::containsGlyph(int unicode) {
    if (fontEncoding_->canEncode(unicode)) {
        if (fontEncoding_->isFontSpecific()) {
            return (kind_ == Kind::TrueType ? fontProgram_->getGlyph(unicode) : fontProgram_->getGlyphByCode(unicode)) != nullptr;
        }
        return fontProgram_->getGlyph(fontEncoding_->getUnicodeDifference(unicode)) != nullptr;
    }
    return false;
}

GlyphLine PdfSimpleFont::createGlyphLine(const std::string& content) {
    std::vector<Glyph> glyphs;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(content)) {
        if (fontEncoding_->isFontSpecific()) {
            const Glyph* g = getGlyph(static_cast<int>(cp));
            if (g) glyphs.push_back(*g);
        } else {
            int unicode = static_cast<int>(cp);
            if (fontEncoding_->canEncode(unicode)) {
                const Glyph* g = getGlyph(unicode);
                if (g) glyphs.push_back(*g);
            }
        }
    }
    return GlyphLine(glyphs);
}

Bytes PdfSimpleFont::convertToBytes(const std::string& text) {
    Bytes bytes;
    if (fontEncoding_->isFontSpecific()) {
        for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
            const Glyph* g = getGlyph(static_cast<int>(cp));
            if (!g) continue;
            int code = kind_ == Kind::TrueType ? static_cast<int>(cp & 0xff) : g->getCode();
            if (code < 0 || code > 255) continue;
            bytes.push_back(static_cast<uint8_t>(code));
        }
    } else {
        bytes = fontEncoding_->convertToBytes(text);
    }
    for (uint8_t b : bytes) usedGlyphs_[b] = true;
    return bytes;
}

Bytes PdfSimpleFont::convertToBytes(const Glyph& glyph) {
    int code;
    if (fontEncoding_->isFontSpecific()) code = kind_ == Kind::TrueType ? (glyph.getUnicode() & 0xff) : glyph.getCode();
    else code = fontEncoding_->convertToByte(glyph.getUnicode());
    if (code < 0 || code > 255) return Bytes();
    usedGlyphs_[code] = true;
    return Bytes{static_cast<uint8_t>(code)};
}

Bytes PdfSimpleFont::convertToBytes(const GlyphLine& glyphLine) {
    Bytes bytes;
    for (int i = glyphLine.getStart(); i < glyphLine.getEnd(); ++i) {
        Bytes b = convertToBytes(glyphLine.get(i));
        bytes.insert(bytes.end(), b.begin(), b.end());
    }
    return bytes;
}

std::vector<PdfFont::DecodedCode> PdfSimpleFont::decodeCodes(const PdfString& content) {
    std::vector<DecodedCode> out;
    Bytes bytes = content.getValueBytes();
    for (uint8_t b : bytes) {
        DecodedCode dc;
        dc.isSpace = b == 32;
        const std::u32string* tu = toUnicode_ ? toUnicode_->lookup(&b, 1) : nullptr;
        if (tu) {
            dc.text = u32ToUtf8(*tu);
        } else {
            int uni = fontEncoding_->getUnicode(b);
            if (uni > 0) dc.text = TextUtil::codePointToUtf8(static_cast<uint32_t>(uni));
            else if (fontEncoding_->isFontSpecific() || kind_ == Kind::TrueType) dc.text = TextUtil::codePointToUtf8(b);
        }
        if (!newFont_) {
            dc.width = getGlyphWidthForCode(b);
        } else {
            const Glyph* g = nullptr;
            if (fontEncoding_->isFontSpecific()) g = kind_ == Kind::TrueType ? fontProgram_->getGlyph(b) : fontProgram_->getGlyphByCode(b);
            else g = fontProgram_->getGlyph(fontEncoding_->getUnicodeDifference(fontEncoding_->getUnicode(b)));
            dc.width = g ? static_cast<float>(g->getWidth()) : 0.f;
        }
        out.push_back(std::move(dc));
    }
    return out;
}

GlyphLine PdfSimpleFont::decodeIntoGlyphLine(const PdfString& characterCodes) {
    std::vector<Glyph> glyphs;
    Bytes bytes = characterCodes.getValueBytes();
    auto decoded = decodeCodes(characterCodes);
    for (size_t i = 0; i < bytes.size(); ++i) {
        std::u32string chars = toU32(decoded[i].text);
        glyphs.emplace_back(bytes[i], static_cast<int>(decoded[i].width), chars);
    }
    return GlyphLine(glyphs);
}

float PdfSimpleFont::getContentWidth(const PdfString& content) { return PdfFont::getContentWidth(content); }

void PdfSimpleFont::flushFontData() {
    auto& dict = *getPdfObject();
    PdfDocument* doc = dict.getIndirectReference() ? dict.getIndirectReference()->getDocument() : nullptr;
    auto* type1 = dynamic_cast<io::Type1Font*>(fontProgram_.get());
    auto* ttf = dynamic_cast<io::TrueTypeFont*>(fontProgram_.get());
    bool builtIn = type1 && type1->isBuiltInFont();
    std::string fontName = fontProgram_->getFontNames().getFontName();
    if (!builtIn) fontName = updateSubsetPrefix(fontName, subset_ && embedded_ && ttf != nullptr, embedded_);

    dict.put(PdfName::Type, PdfName::Font);
    dict.put(PdfName::Subtype, kind_ == Kind::TrueType ? PdfName::TrueType : PdfName::Type1);
    dict.put(PdfName::BaseFont, std::make_shared<PdfName>(fontName));

    int firstChar = 0, lastChar = 255;
    for (firstChar = 0; firstChar <= 255; ++firstChar)
        if (usedGlyphs_[firstChar]) break;
    for (lastChar = 255; lastChar >= firstChar; --lastChar)
        if (usedGlyphs_[lastChar]) break;
    if (firstChar > 255) {
        firstChar = 255;
        lastChar = 255;
    }
    if (!fontEncoding_->isFontSpecific() || fontEncoding_->hasDifferences()) {
        if (fontEncoding_->hasDifferences() && !fontEncoding_->isFontSpecific()) {
            auto enc = std::make_shared<PdfDictionary>();
            enc->put(PdfName::Type, PdfName::Encoding);
            auto diff = std::make_shared<PdfArray>();
            bool gap = true;
            for (int k = firstChar; k <= lastChar; ++k) {
                if (usedGlyphs_[k]) {
                    if (gap) {
                        diff->add(makeNumber(k));
                        gap = false;
                    }
                    std::string name = fontEncoding_->getDifference(k);
                    diff->add(std::make_shared<PdfName>(name.empty() ? std::string(io::FontEncoding::NOTDEF) : name));
                } else {
                    gap = true;
                }
            }
            enc->put(PdfName::Differences, diff);
            dict.put(PdfName::Encoding, enc);
        } else if (!fontEncoding_->isFontSpecific()) {
            const std::string& be = fontEncoding_->getBaseEncoding();
            if (be == io::PdfEncodings::WINANSI) dict.put(PdfName::Encoding, PdfName::WinAnsiEncoding);
            else if (be == io::PdfEncodings::MACROMAN) dict.put(PdfName::Encoding, PdfName::MacRomanEncoding);
        }
    }
    if (forceWidthsOutput_ || !builtIn || fontEncoding_->hasDifferences()) {
        dict.put(PdfName::FirstChar, makeNumber(firstChar));
        dict.put(PdfName::LastChar, makeNumber(lastChar));
        auto wd = std::make_shared<PdfArray>();
        for (int k = firstChar; k <= lastChar; ++k) {
            int width = 0;
            if (usedGlyphs_[k]) {
                const Glyph* g = nullptr;
                if (fontEncoding_->isFontSpecific()) g = ttf ? fontProgram_->getGlyph(k) : fontProgram_->getGlyphByCode(k);
                else g = fontProgram_->getGlyph(fontEncoding_->getUnicodeDifference(fontEncoding_->getUnicode(k)));
                if (g) width = g->getWidth();
            }
            wd->add(makeNumber(width));
        }
        dict.put(PdfName::Widths, wd);
    }
    if (!builtIn && doc) {
        auto fd = getFontDescriptor(fontName);
        int flags = fontProgram_->getPdfFontFlags();
        flags &= ~(4 | 32);
        flags |= fontEncoding_->isFontSpecific() ? 4 : 32;
        fd->put(PdfName::Flags, makeNumber(flags));
        if (embedded_) {
            if (ttf) {
                std::set<int> gids;
                for (int k = firstChar; k <= lastChar; ++k) {
                    if (!usedGlyphs_[k]) continue;
                    const Glyph* g = fontEncoding_->isFontSpecific()
                                         ? fontProgram_->getGlyph(k)
                                         : fontProgram_->getGlyph(fontEncoding_->getUnicodeDifference(fontEncoding_->getUnicode(k)));
                    if (g) gids.insert(g->getCode());
                }
                Bytes fontBytes = (subset_ && !ttf->isCff()) ? ttf->getSubset(gids, true) : ttf->getFontStreamBytes();
                auto stream = getPdfFontStream(fontBytes, {static_cast<int>(fontBytes.size())});
                if (ttf->isCff()) {
                    stream->remove(std::make_shared<PdfName>("Length1"));
                    stream->put(PdfName::Subtype, PdfName::of("OpenType"));
                    fd->put(PdfName::FontFile3, stream);
                } else {
                    fd->put(PdfName::FontFile2, stream);
                }
            } else if (type1) {
                std::array<int, 3> lengths{};
                Bytes fontBytes = type1->getFontStreamBytes(lengths);
                if (!fontBytes.empty()) {
                    auto stream = getPdfFontStream(fontBytes, {lengths[0], lengths[1], lengths[2]});
                    fd->put(PdfName::FontFile, stream);
                }
            }
        }
        dict.put(PdfName::FontDescriptor, fd);
    }
}

// ============================================================ PdfType0Font
PdfType0Font::PdfType0Font(std::shared_ptr<io::TrueTypeFont> ttf, const std::string& cmap, bool embedded) {
    if (cmap != io::PdfEncodings::IDENTITY_H && cmap != io::PdfEncodings::IDENTITY_V)
        throw PdfException("Only Identity CMaps supported on writing: " + cmap);
    setPdfObject(std::make_shared<PdfDictionary>());
    fontProgram_ = std::move(ttf);
    vertical_ = cmap == io::PdfEncodings::IDENTITY_V;
    cmapEncoding_ = io::CMapEncoding::identity(vertical_);
    embedded_ = embedded;
    subset_ = true;
    newFont_ = true;
}

PdfType0Font::PdfType0Font(PdfDictionaryPtr fontDictionary) : PdfFont(std::move(fontDictionary)) {
    auto& dict = *getPdfObject();
    PdfObjectPtr encoding = dict.get(PdfName::Encoding);
    if (encoding && encoding->isName()) {
        cmapEncoding_ = io::CMapEncoding::fromName(std::static_pointer_cast<PdfName>(encoding)->getValue());
    } else if (encoding && encoding->isStream()) {
        cmapEncoding_ = io::CMapEncoding::fromStream(std::static_pointer_cast<PdfStream>(encoding)->getBytes());
    } else {
        cmapEncoding_ = io::CMapEncoding::identity();
    }
    auto baseFont = dict.getAsName(PdfName::BaseFont);
    fontProgram_ = std::make_shared<io::Type1Font>(baseFont ? baseFont->getValue() : std::string());
    if (auto tu = dict.getAsStream(PdfName::ToUnicode)) {
        try {
            toUnicode_ = io::CMapToUnicode::parse(tu->getBytes());
        } catch (...) {
        }
    } else {
        auto tuName = dict.getAsName(PdfName::ToUnicode);
        if (tuName && (tuName->getValue() == "Identity-H" || tuName->getValue() == "Identity-V"))
            toUnicode_ = io::CMapToUnicode::getIdentity();
    }
    auto descendants = dict.getAsArray(PdfName::DescendantFonts);
    PdfDictionaryPtr cidFont = descendants ? descendants->getAsDictionary(0) : nullptr;
    if (cidFont) {
        if (auto dw = cidFont->getAsNumber(PdfName::DW)) defaultWidth_ = dw->floatValue();
        if (auto w = cidFont->getAsArray(PdfName::W)) {
            int i = 0;
            while (i < w->size()) {
                auto first = w->getAsNumber(i);
                if (!first) break;
                int c = first->intValue();
                PdfObjectPtr next = w->get(i + 1);
                if (next && next->isArray()) {
                    auto arr = std::static_pointer_cast<PdfArray>(next);
                    for (int k = 0; k < arr->size(); ++k)
                        if (auto n = arr->getAsNumber(k)) cidWidths_[c + k] = n->floatValue();
                    i += 2;
                } else {
                    auto last = w->getAsNumber(i + 1);
                    auto width = w->getAsNumber(i + 2);
                    if (!last || !width) break;
                    for (int k = c; k <= last->intValue() && k - c < 65536; ++k) cidWidths_[k] = width->floatValue();
                    i += 3;
                }
            }
        }
        // Without /ToUnicode, try to recover text from the embedded TrueType cmap (Identity CIDToGIDMap).
        if (!toUnicode_) {
            auto fd = cidFont->getAsDictionary(PdfName::FontDescriptor);
            auto ff2 = fd ? fd->getAsStream(PdfName::FontFile2) : nullptr;
            if (ff2) {
                try {
                    io::TrueTypeFont ttf(ff2->getBytes());
                    auto m = std::make_shared<io::CMapToUnicode>();
                    for (const auto& kv : ttf.getActiveCmap()) m->addChar(kv.second.first, 2, std::u32string(1, static_cast<char32_t>(kv.first)));
                    toUnicode_ = m;
                } catch (...) {
                }
            }
        }
        if (!toUnicode_) {
            auto info = cidFont->getAsDictionary(PdfName::CIDSystemInfo);
            auto ordering = info ? info->getAsString(PdfName::Ordering) : nullptr;
            if (ordering && ordering->getValue() == "Identity" && cmapEncoding_->isDirect()) {
                // Unknown mapping; fall back to identity (may be wrong for subset fonts)
                toUnicode_ = io::CMapToUnicode::getIdentity();
            }
        }
    }
}

const Glyph* PdfType0Font::getGlyph(int unicode) {
    const Glyph* glyph = fontProgram_->getGlyph(unicode);
    if (!glyph) {
        auto it = notdefGlyphs_.find(unicode);
        if (it != notdefGlyphs_.end()) return &it->second;
        const Glyph* notdef = fontProgram_->getGlyphByCode(0);
        if (notdef) {
            auto ins = notdefGlyphs_.emplace(unicode, Glyph(*notdef, -1));
            return &ins.first->second;
        }
    }
    return glyph;
}

bool PdfType0Font::containsGlyph(int unicode) {
    const Glyph* glyph = fontProgram_->getGlyph(unicode);
    return glyph && glyph->getCode() > 0;
}

Bytes PdfType0Font::convertToBytes(const Glyph& glyph) {
    usedGlyphs_[glyph.getCode()] = glyph;
    return Bytes{static_cast<uint8_t>(glyph.getCode() >> 8), static_cast<uint8_t>(glyph.getCode())};
}

Bytes PdfType0Font::convertToBytes(const std::string& text) {
    Bytes out;
    for (uint32_t cp : TextUtil::utf8ToCodePoints(text)) {
        const Glyph* g = getGlyph(static_cast<int>(cp));
        if (!g) continue;
        Bytes b = convertToBytes(*g);
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

Bytes PdfType0Font::convertToBytes(const GlyphLine& glyphLine) {
    Bytes out;
    for (int i = glyphLine.getStart(); i < glyphLine.getEnd(); ++i) {
        Bytes b = convertToBytes(glyphLine.get(i));
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

void PdfType0Font::writeText(const GlyphLine& text, int from, int to, PdfOutputStream& stream) {
    PdfFont::writeText(text, from, to, stream);
}

void PdfType0Font::writeText(const std::string& text, PdfOutputStream& stream) {
    io::StreamUtil::writeEscapedString(stream, convertToBytes(text));
}

float PdfType0Font::getGlyphWidthForCode(int cid) const {
    auto it = cidWidths_.find(cid);
    return it == cidWidths_.end() ? defaultWidth_ : it->second;
}

std::vector<PdfFont::DecodedCode> PdfType0Font::decodeCodes(const PdfString& content) {
    std::vector<DecodedCode> out;
    Bytes bytes = content.getValueBytes();
    size_t pos = 0;
    for (const auto& code : cmapEncoding_->decode(bytes)) {
        DecodedCode dc;
        dc.isSpace = code.length == 1 && code.code == 32;
        const std::u32string* tu = toUnicode_ ? toUnicode_->lookup(bytes.data() + pos, code.length) : nullptr;
        if (tu) {
            dc.text = u32ToUtf8(*tu);
        } else if (newFont_) {
            const Glyph* g = fontProgram_->getGlyphByCode(code.cid);
            if (g && !g->getChars().empty()) dc.text = g->getUnicodeString();
        }
        if (newFont_) {
            const Glyph* g = fontProgram_->getGlyphByCode(code.cid);
            dc.width = g ? static_cast<float>(g->getWidth()) : 0.f;
        } else {
            dc.width = getGlyphWidthForCode(code.cid);
        }
        pos += code.length;
        out.push_back(std::move(dc));
    }
    return out;
}

GlyphLine PdfType0Font::decodeIntoGlyphLine(const PdfString& characterCodes) {
    std::vector<Glyph> glyphs;
    Bytes bytes = characterCodes.getValueBytes();
    auto codes = cmapEncoding_->decode(bytes);
    auto decoded = decodeCodes(characterCodes);
    for (size_t i = 0; i < codes.size(); ++i)
        glyphs.emplace_back(codes[i].cid, static_cast<int>(decoded[i].width), toU32(decoded[i].text));
    return GlyphLine(glyphs);
}

float PdfType0Font::getContentWidth(const PdfString& content) { return PdfFont::getContentWidth(content); }

static std::string hex4(int v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04X", v & 0xFFFF);
    return buf;
}

PdfStreamPtr PdfType0Font::getToUnicode() {
    std::ostringstream buf;
    buf << "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo\n<< /Registry (Adobe)\n"
           "/Ordering (UCS)\n/Supplement 0\n>> def\n/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
           "1 begincodespacerange\n<0000><FFFF>\nendcodespacerange\n";
    std::vector<std::pair<int, std::u32string>> entries;
    for (const auto& kv : usedGlyphs_)
        if (!kv.second.getChars().empty()) entries.emplace_back(kv.first, kv.second.getChars());
    for (size_t i = 0; i < entries.size(); i += 100) {
        size_t n = std::min<size_t>(100, entries.size() - i);
        buf << n << " beginbfchar\n";
        for (size_t k = i; k < i + n; ++k) {
            buf << "<" << hex4(entries[k].first) << "> <";
            for (char32_t c : entries[k].second)
                for (char16_t u : TextUtil::convertFromUtf32(c)) buf << hex4(u);
            buf << ">\n";
        }
        buf << "endbfchar\n";
    }
    buf << "endcmap\nCMapName currentdict /CMap defineresource pop\nend end\n";
    auto stream = std::make_shared<PdfStream>(toBytes(buf.str()));
    stream->makeIndirect(getPdfObject()->getIndirectReference()->getDocument());
    return stream;
}

void PdfType0Font::flushFontData() {
    auto& dict = *getPdfObject();
    PdfDocument* doc = dict.getIndirectReference()->getDocument();
    auto* ttf = dynamic_cast<io::TrueTypeFont*>(fontProgram_.get());
    std::string fontName = updateSubsetPrefix(fontProgram_->getFontNames().getFontName(), subset_ && !ttf->isCff(), embedded_);

    auto fd = getFontDescriptor(fontName);
    fd->put(PdfName::Flags, makeNumber(fontProgram_->getPdfFontFlags()));
    if (embedded_) {
        std::set<int> gids;
        for (const auto& kv : usedGlyphs_) gids.insert(kv.first);
        Bytes fontBytes = (subset_ && !ttf->isCff()) ? ttf->getSubset(gids, true) : ttf->getFontStreamBytes();
        auto stream = std::make_shared<PdfStream>(fontBytes);
        stream->makeIndirect(doc);
        if (ttf->isCff()) {
            stream->put(PdfName::Subtype, PdfName::of("OpenType"));
            fd->put(PdfName::FontFile3, stream);
        } else {
            stream->put(PdfName::Length1, makeNumber(static_cast<int>(fontBytes.size())));
            fd->put(PdfName::FontFile2, stream);
        }
    }
    auto cidFont = std::make_shared<PdfDictionary>();
    cidFont->makeIndirect(doc);
    cidFont->put(PdfName::Type, PdfName::Font);
    cidFont->put(PdfName::Subtype, ttf->isCff() ? PdfName::CIDFontType0 : PdfName::CIDFontType2);
    cidFont->put(PdfName::BaseFont, std::make_shared<PdfName>(fontName));
    cidFont->put(PdfName::FontDescriptor, fd);
    if (!ttf->isCff()) cidFont->put(PdfName::CIDToGIDMap, PdfName::Identity);
    auto csi = std::make_shared<PdfDictionary>();
    csi->put(PdfName::Registry, std::make_shared<PdfString>("Adobe"));
    csi->put(PdfName::Ordering, std::make_shared<PdfString>("Identity"));
    csi->put(PdfName::Supplement, makeNumber(0));
    cidFont->put(PdfName::CIDSystemInfo, csi);
    cidFont->put(PdfName::DW, makeNumber(1000));
    auto w = std::make_shared<PdfArray>();
    int lastGid = -10;
    PdfArrayPtr run;
    for (const auto& kv : usedGlyphs_) {
        if (kv.second.getWidth() == 1000) {
            lastGid = -10;
            continue;
        }
        if (kv.first != lastGid + 1 || !run) {
            run = std::make_shared<PdfArray>();
            w->add(makeNumber(kv.first));
            w->add(run);
        }
        run->add(makeNumber(kv.second.getWidth()));
        lastGid = kv.first;
    }
    if (!w->isEmpty()) cidFont->put(PdfName::W, w);

    dict.put(PdfName::Type, PdfName::Font);
    dict.put(PdfName::Subtype, PdfName::Type0);
    dict.put(PdfName::BaseFont, std::make_shared<PdfName>(ttf->isCff() ? fontName + "-" + cmapEncoding_->getCmapName() : fontName));
    dict.put(PdfName::Encoding, std::make_shared<PdfName>(cmapEncoding_->getCmapName()));
    auto descendants = std::make_shared<PdfArray>();
    descendants->add(cidFont);
    dict.put(PdfName::DescendantFonts, descendants);
    dict.put(PdfName::ToUnicode, getToUnicode());
    cidFont->flush();
    fd->flush();
}

// ============================================================ PdfFontFactory
std::shared_ptr<PdfFont> PdfFontFactory::createFont() { return createFont(io::StandardFonts::HELVETICA, ""); }

std::shared_ptr<PdfFont> PdfFontFactory::createFont(std::shared_ptr<io::FontProgram> fontProgram, const std::string& encoding,
                                                    EmbeddingStrategy strategy) {
    bool embedded = strategy == EmbeddingStrategy::FORCE_EMBEDDED || strategy == EmbeddingStrategy::PREFER_EMBEDDED;
    if (auto type1 = std::dynamic_pointer_cast<io::Type1Font>(fontProgram))
        return std::make_shared<PdfSimpleFont>(type1, encoding, embedded, PdfSimpleFont::Kind::Type1);
    auto ttf = std::dynamic_pointer_cast<io::TrueTypeFont>(fontProgram);
    if (!ttf) throw PdfException(KernelExceptionMessageConstant::UNSUPPORTED_FONT);
    if (!ttf->getFontNames().allowEmbedding()) {
        if (strategy == EmbeddingStrategy::FORCE_EMBEDDED) throw PdfException("Font cannot be embedded due to licensing restrictions.");
        embedded = false;
    }
    if (encoding == io::PdfEncodings::IDENTITY_H || encoding == io::PdfEncodings::IDENTITY_V)
        return std::make_shared<PdfType0Font>(ttf, encoding, true);
    return std::make_shared<PdfSimpleFont>(ttf, encoding, embedded, PdfSimpleFont::Kind::TrueType);
}

std::shared_ptr<PdfFont> PdfFontFactory::createFont(const std::string& fontProgram, const std::string& encoding,
                                                    EmbeddingStrategy strategy) {
    return createFont(io::FontProgramFactory::createFont(fontProgram), encoding, strategy);
}

std::shared_ptr<PdfFont> PdfFontFactory::createFont(const Bytes& fontProgram, const std::string& encoding,
                                                    EmbeddingStrategy strategy) {
    return createFont(io::FontProgramFactory::createFont(fontProgram), encoding, strategy);
}

std::shared_ptr<PdfFont> PdfFontFactory::createFont(const PdfDictionaryPtr& fontDictionary) {
    if (!fontDictionary) throw PdfException("Cannot create font from null pdf dictionary.");
    auto subtype = fontDictionary->getAsName(PdfName::Subtype);
    if (nameEquals(subtype, PdfName::Type0)) return std::make_shared<PdfType0Font>(fontDictionary);
    return std::make_shared<PdfSimpleFont>(fontDictionary);
}

}  // namespace kernel
}  // namespace pdfcpp
