#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Returns an embedded resource ("font/afm/Helvetica.afm", "font/AdobeGlyphList.txt"); empty if missing.
class ResourceUtil {
public:
    static const Bytes* getResource(const std::string& name);
};

/// Port of com.itextpdf.io.font.otf.Glyph.
class Glyph {
public:
    Glyph() = default;
    Glyph(int code, int width, int unicode) : Glyph(code, width, unicode, std::u32string(), false) {}
    Glyph(int code, int width, std::u32string chars) : code_(code), width_(width), chars_(std::move(chars)) {
        unicode_ = codePoint(chars_);
    }
    Glyph(int code, int width, int unicode, const std::array<int, 4>& bbox) : Glyph(code, width, unicode) {
        bbox_ = bbox;
        hasBbox_ = true;
    }
    Glyph(int width, int unicode) : Glyph(-1, width, unicode, std::u32string(), false) {}
    Glyph(int code, int width, int unicode, std::u32string chars, bool isMark)
        : code_(code), width_(width), unicode_(unicode), isMark_(isMark) {
        chars_ = !chars.empty() ? std::move(chars) : (unicode > -1 ? std::u32string(1, static_cast<char32_t>(unicode)) : std::u32string());
    }
    Glyph(const Glyph& glyph, int unicode) : Glyph(glyph.code_, glyph.width_, unicode, std::u32string(), glyph.isMark_) {
        bbox_ = glyph.bbox_;
        hasBbox_ = glyph.hasBbox_;
    }
    Glyph(const Glyph& glyph) = default;
    Glyph& operator=(const Glyph&) = default;

    int getCode() const { return code_; }
    int getWidth() const { return width_; }
    const std::array<int, 4>* getBbox() const { return hasBbox_ ? &bbox_ : nullptr; }
    bool hasValidUnicode() const { return unicode_ > -1; }
    int getUnicode() const { return unicode_; }
    void setUnicode(int unicode) {
        unicode_ = unicode;
        chars_ = unicode > -1 ? std::u32string(1, static_cast<char32_t>(unicode)) : std::u32string();
    }
    const std::u32string& getChars() const { return chars_; }
    void setChars(std::u32string chars) { chars_ = std::move(chars); }
    bool isMark() const { return isMark_; }
    int16_t getXPlacement() const { return xPlacement_; }
    void setXPlacement(int16_t v) { xPlacement_ = v; }
    int16_t getYPlacement() const { return yPlacement_; }
    void setYPlacement(int16_t v) { yPlacement_ = v; }
    int16_t getXAdvance() const { return xAdvance_; }
    void setXAdvance(int16_t v) { xAdvance_ = v; }
    int16_t getYAdvance() const { return yAdvance_; }
    void setYAdvance(int16_t v) { yAdvance_ = v; }
    int16_t getAnchorDelta() const { return anchorDelta_; }
    void setAnchorDelta(int16_t v) { anchorDelta_ = v; }
    bool hasOffsets() const { return hasAdvance() || hasPlacement(); }
    bool hasPlacement() const { return anchorDelta_ != 0 || xPlacement_ != 0 || yPlacement_ != 0; }
    bool hasAdvance() const { return xAdvance_ != 0 || yAdvance_ != 0; }
    /// Unicode text of this glyph as UTF-8.
    std::string getUnicodeString() const;
    bool operator==(const Glyph& o) const {
        return code_ == o.code_ && width_ == o.width_ && unicode_ == o.unicode_ && chars_ == o.chars_;
    }

private:
    static int codePoint(const std::u32string& s) { return s.size() == 1 ? static_cast<int>(s[0]) : -1; }
    int code_ = -1;
    int width_ = 0;
    int unicode_ = -1;
    std::u32string chars_;
    bool isMark_ = false;
    std::array<int, 4> bbox_{};
    bool hasBbox_ = false;
    int16_t xPlacement_ = 0, yPlacement_ = 0, xAdvance_ = 0, yAdvance_ = 0, anchorDelta_ = 0;
};

/// Port of GlyphLine: a list of glyphs with a [start, end) window and actual text.
class GlyphLine {
public:
    GlyphLine() = default;
    explicit GlyphLine(std::vector<Glyph> glyphs) : glyphs_(std::move(glyphs)), end_(static_cast<int>(glyphs_.size())) {}
    GlyphLine(std::vector<Glyph> glyphs, int start, int end) : glyphs_(std::move(glyphs)), start_(start), end_(end) {}
    int getStart() const { return start_; }
    int getEnd() const { return end_; }
    void setStart(int s) { start_ = s; }
    void setEnd(int e) { end_ = e; }
    int size() const { return static_cast<int>(glyphs_.size()); }
    const Glyph& get(int index) const { return glyphs_[index]; }
    Glyph& get(int index) { return glyphs_[index]; }
    void add(const Glyph& g) {
        glyphs_.push_back(g);
        end_ = static_cast<int>(glyphs_.size());
    }
    void set(int index, const Glyph& g) { glyphs_[index] = g; }
    const std::vector<Glyph>& getGlyphs() const { return glyphs_; }
    /// UTF-8 text of [start, end).
    std::string toUnicodeString(int start, int end) const;
    std::string toString() const { return toUnicodeString(start_, end_); }

private:
    std::vector<Glyph> glyphs_;
    int start_ = 0;
    int end_ = 0;
};

/// Port of FontMetrics.
class FontMetrics {
public:
    float getUnitsPerEm() const { return unitsPerEm_; }
    int getNumberOfGlyphs() const { return numOfGlyphs_; }
    const std::vector<int>& getGlyphWidths() const { return glyphWidths_; }
    int getTypoAscender() const { return typoAscender_; }
    int getTypoDescender() const { return typoDescender_; }
    int getCapHeight() const { return capHeight_; }
    int getXHeight() const { return xHeight_; }
    float getItalicAngle() const { return italicAngle_; }
    const std::array<int, 4>& getBbox() const { return bbox_; }
    int getAscender() const { return ascender_; }
    int getDescender() const { return descender_; }
    int getLineGap() const { return lineGap_; }
    int getWinAscender() const { return winAscender_; }
    int getWinDescender() const { return winDescender_; }
    int getAdvanceWidthMax() const { return advanceWidthMax_; }
    int getUnderlinePosition() const { return underlinePosition_; }
    int getUnderlineThickness() const { return underlineThickness_; }
    int getStrikeoutPosition() const { return strikeoutPosition_; }
    int getStrikeoutSize() const { return strikeoutSize_; }
    int getSubscriptSize() const { return subscriptSize_; }
    int getSubscriptOffset() const { return subscriptOffset_; }
    int getSuperscriptSize() const { return superscriptSize_; }
    int getSuperscriptOffset() const { return superscriptOffset_; }
    int getStemV() const { return stemV_; }
    int getStemH() const { return stemH_; }
    bool isFixedPitch() const { return isFixedPitch_; }

    void setUnitsPerEm(int v) { unitsPerEm_ = static_cast<float>(v); }
    void setNumberOfGlyphs(int v) { numOfGlyphs_ = v; }
    void setGlyphWidths(std::vector<int> w) { glyphWidths_ = std::move(w); }
    void setTypoAscender(int v) { typoAscender_ = v; }
    void setTypoDescender(int v) { typoDescender_ = v; }
    void setCapHeight(int v) { capHeight_ = v; }
    void setXHeight(int v) { xHeight_ = v; }
    void setItalicAngle(float v) { italicAngle_ = v; }
    void setBbox(int llx, int lly, int urx, int ury) { bbox_ = {llx, lly, urx, ury}; }
    void setAscender(int v) { ascender_ = v; }
    void setDescender(int v) { descender_ = v; }
    void setLineGap(int v) { lineGap_ = v; }
    void setWinAscender(int v) { winAscender_ = v; }
    void setWinDescender(int v) { winDescender_ = v; }
    void setAdvanceWidthMax(int v) { advanceWidthMax_ = v; }
    void setUnderlinePosition(int v) { underlinePosition_ = v; }
    void setUnderlineThickness(int v) { underlineThickness_ = v; }
    void setStrikeoutPosition(int v) { strikeoutPosition_ = v; }
    void setStrikeoutSize(int v) { strikeoutSize_ = v; }
    void setSubscriptSize(int v) { subscriptSize_ = v; }
    void setSubscriptOffset(int v) { subscriptOffset_ = v; }
    void setSuperscriptSize(int v) { superscriptSize_ = v; }
    void setSuperscriptOffset(int v) { superscriptOffset_ = v; }
    void setStemV(int v) { stemV_ = v; }
    void setStemH(int v) { stemH_ = v; }
    void setIsFixedPitch(bool v) { isFixedPitch_ = v; }
    void updateBbox(float llx, float lly, float urx, float ury) {
        bbox_ = {static_cast<int>(llx), static_cast<int>(lly), static_cast<int>(urx), static_cast<int>(ury)};
    }

private:
    float unitsPerEm_ = 1000;
    int numOfGlyphs_ = 0;
    std::vector<int> glyphWidths_;
    int typoAscender_ = 800;
    int typoDescender_ = -200;
    int capHeight_ = 700;
    int xHeight_ = 0;
    float italicAngle_ = 0;
    std::array<int, 4> bbox_{-50, -200, 1000, 900};
    int ascender_ = 0, descender_ = 0, lineGap_ = 0, winAscender_ = 0, winDescender_ = 0, advanceWidthMax_ = 0;
    int underlinePosition_ = -100, underlineThickness_ = 50;
    int strikeoutPosition_ = 0, strikeoutSize_ = 0;
    int subscriptSize_ = 0, subscriptOffset_ = 0, superscriptSize_ = 0, superscriptOffset_ = 0;
    int stemV_ = 80, stemH_ = 0;
    bool isFixedPitch_ = false;
};

/// Port of FontNames.
class FontNames {
public:
    const std::string& getFontName() const { return fontName_; }
    void setFontName(const std::string& n) { fontName_ = n; }
    const std::string& getFullName() const { return fullName_; }
    void setFullName(const std::string& n) { fullName_ = n; }
    const std::string& getFamilyName() const { return familyName_; }
    void setFamilyName(const std::string& n) { familyName_ = n; }
    const std::string& getStyle() const { return style_; }
    void setStyle(const std::string& s) { style_ = s; }
    int getFontWeight() const { return fontWeight_; }
    void setFontWeight(int w) { fontWeight_ = w; }
    const std::string& getFontStretch() const { return fontStretch_; }
    void setFontStretch(const std::string& s) { fontStretch_ = s; }
    int getMacStyle() const { return macStyle_; }
    void setMacStyle(int m) { macStyle_ = m; }
    bool allowEmbedding() const { return allowEmbedding_; }
    void setAllowEmbedding(bool v) { allowEmbedding_ = v; }
    bool isBold() const { return (macStyle_ & 1) != 0; }
    bool isItalic() const { return (macStyle_ & 2) != 0; }
    /// All name records: nameId -> list of {platform, encoding, language, value}.
    std::map<int, std::vector<std::array<std::string, 4>>>& getAllNames() { return allNames_; }

private:
    std::string fontName_, fullName_, familyName_, style_;
    std::string fontStretch_ = "Normal";
    int fontWeight_ = 400;
    int macStyle_ = 0;
    bool allowEmbedding_ = true;
    std::map<int, std::vector<std::array<std::string, 4>>> allNames_;
};

/// Port of AdobeGlyphList.
class AdobeGlyphList {
public:
    static int nameToUnicode(const std::string& name);
    /// Returns empty string if not found.
    static std::string unicodeToName(int unicode);
};

/// Port of FontEncoding.
class FontEncoding {
public:
    static constexpr const char* FONT_SPECIFIC = "FontSpecific";
    static constexpr const char* NOTDEF = ".notdef";

    static std::shared_ptr<FontEncoding> createFontEncoding(const std::string& baseEncoding);
    static std::shared_ptr<FontEncoding> createEmptyFontEncoding();
    static std::shared_ptr<FontEncoding> createFontSpecificEncoding();
    static void fillFontEncoding(FontEncoding& encoding);

    const std::string& getBaseEncoding() const { return baseEncoding_; }
    bool isFontSpecific() const { return fontSpecific_; }
    bool addSymbol(int code, int unicode);
    int getUnicode(int index) const { return (index >= 0 && index < 256) ? codeToUnicode_[index] : -1; }
    int getUnicodeDifference(int index) const;
    bool hasDifferences() const { return hasDifferences_; }
    std::string getDifference(int index) const { return hasDifferences_ ? differences_[index] : std::string(); }
    void setDifference(int index, const std::string& difference) {
        if (hasDifferences_ && index >= 0 && index < 256) differences_[index] = difference;
    }
    void setCodeToUnicode(int code, int unicode) {
        if (code >= 0 && code < 256) codeToUnicode_[code] = unicode;
        if (unicode >= 0) unicodeToCode_[unicode] = code;
    }
    Bytes convertToBytes(const std::string& text) const;
    int convertToByte(int unicode) const;
    bool canEncode(int unicode) const;
    bool canDecode(int code) const { return code >= 0 && code < 256 && codeToUnicode_[code] > -1; }
    bool isBuiltWith(const std::string& encoding) const { return normalizeEncoding(encoding) == baseEncoding_; }
    static std::string normalizeEncoding(const std::string& enc);
    void fillStandardEncoding();
    void initDifferences() {
        hasDifferences_ = true;
        differences_.assign(256, std::string());
    }

protected:
    FontEncoding() { codeToUnicode_.fill(-1); }
    void fillCustomEncoding();
    void fillNamedEncoding();

    std::string baseEncoding_;
    bool fontSpecific_ = false;
    std::unordered_map<int, int> unicodeToCode_;
    std::array<int, 256> codeToUnicode_;
    std::vector<std::string> differences_;
    bool hasDifferences_ = false;
    std::unordered_map<int, int> unicodeDifferences_;
};

/// Port of com.itextpdf.io.font.FontProgram.
class FontProgram {
public:
    static constexpr int HORIZONTAL_SCALING_FACTOR = 100;
    static constexpr int DEFAULT_WIDTH = 1000;
    static constexpr int UNITS_NORMALIZATION = 1000;

    virtual ~FontProgram() = default;
    // Note: names follow the Java API exactly (TextSpaceToGlyphSpace divides by 1000).
    static float convertTextSpaceToGlyphSpace(float value) { return value / UNITS_NORMALIZATION; }
    static float convertGlyphSpaceToTextSpace(float value) { return value * UNITS_NORMALIZATION; }
    static double convertGlyphSpaceToTextSpace(double value) { return value * UNITS_NORMALIZATION; }

    int countOfGlyphs() const { return std::max(static_cast<int>(codeToGlyph_.size()), fontMetrics_.getNumberOfGlyphs()); }
    const FontNames& getFontNames() const { return fontNames_; }
    FontNames& getFontNames() { return fontNames_; }
    const FontMetrics& getFontMetrics() const { return fontMetrics_; }
    FontMetrics& getFontMetrics() { return fontMetrics_; }
    const std::string& getRegistry() const { return registry_; }
    virtual int getPdfFontFlags() const = 0;
    bool isFontSpecific() const { return isFontSpecific_; }
    int getWidth(int unicode) const;
    int getAvgWidth() const { return avgWidth_; }
    const std::array<int, 4>* getCharBBox(int unicode) const;
    /// Glyph for a unicode code point or nullptr.
    virtual const Glyph* getGlyph(int unicode) const;
    virtual const Glyph* getGlyphByCode(int charCode) const;
    virtual bool hasKernPairs() const { return false; }
    int getKerning(int first, int second) const;
    virtual int getKerning(const Glyph& first, const Glyph& second) const = 0;
    virtual bool isBuiltWith(const std::string& fontName) const { return false; }
    const std::string& getEncodingScheme() const { return encodingScheme_; }
    const std::unordered_map<int, Glyph>& getCodeToGlyph() const { return codeToGlyph_; }
    const std::unordered_map<int, Glyph>& getUnicodeToGlyph() const { return unicodeToGlyph_; }

protected:
    void fixSpaceIssue();
    std::unordered_map<int, Glyph> codeToGlyph_;
    std::unordered_map<int, Glyph> unicodeToGlyph_;
    bool isFontSpecific_ = false;
    FontNames fontNames_;
    FontMetrics fontMetrics_;
    int avgWidth_ = 0;
    std::string encodingScheme_ = FontEncoding::FONT_SPECIFIC;
    std::string registry_;
};

/// Port of StandardFonts.
namespace StandardFonts {
inline constexpr const char* COURIER = "Courier";
inline constexpr const char* COURIER_BOLD = "Courier-Bold";
inline constexpr const char* COURIER_BOLDOBLIQUE = "Courier-BoldOblique";
inline constexpr const char* COURIER_OBLIQUE = "Courier-Oblique";
inline constexpr const char* HELVETICA = "Helvetica";
inline constexpr const char* HELVETICA_BOLD = "Helvetica-Bold";
inline constexpr const char* HELVETICA_BOLDOBLIQUE = "Helvetica-BoldOblique";
inline constexpr const char* HELVETICA_OBLIQUE = "Helvetica-Oblique";
inline constexpr const char* SYMBOL = "Symbol";
inline constexpr const char* TIMES_BOLD = "Times-Bold";
inline constexpr const char* TIMES_BOLDITALIC = "Times-BoldItalic";
inline constexpr const char* TIMES_ITALIC = "Times-Italic";
inline constexpr const char* TIMES_ROMAN = "Times-Roman";
inline constexpr const char* ZAPFDINGBATS = "ZapfDingbats";
bool isStandardFont(const std::string& fontName);
}  // namespace StandardFonts

/// Port of Type1Font (AFM metrics + optional PFB program).
class Type1Font : public FontProgram {
public:
    /// Loads one of the 14 standard fonts from embedded AFM metrics.
    static std::shared_ptr<Type1Font> createStandardFont(const std::string& name);
    /// Loads AFM metrics (and an optional PFB binary for embedding).
    static std::shared_ptr<Type1Font> createFromAfm(const Bytes& afm, const Bytes& pfb = Bytes(), const std::string& afmPath = "");
    /// Font known only by name (non-embedded, from a PDF).
    explicit Type1Font(const std::string& baseFont);

    void initializeGlyphs(const FontEncoding& fontEncoding);
    bool isBuiltInFont() const { return builtIn_; }
    int getPdfFontFlags() const override;
    const std::string& getCharacterSet() const { return characterSet_; }
    bool hasKernPairs() const override { return !kernPairs_.empty(); }
    int getKerning(const Glyph& first, const Glyph& second) const override;
    bool setKerning(int first, int second, int kern);
    const Glyph* getGlyphByName(const std::string& name) const;
    /// Returns the PFB data split into the 3 segments (empty if not embeddable).
    Bytes getFontStreamBytes(std::array<int, 3>& lengths) const;
    bool isBuiltWith(const std::string& fontProgram) const override { return afmPath_ == fontProgram; }

protected:
    Type1Font() = default;
    void process(const Bytes& afm);

private:
    std::string characterSet_;
    std::unordered_map<int64_t, int> kernPairs_;
    Bytes pfb_;
    std::string afmPath_;
    bool builtIn_ = false;
};

}  // namespace io
}  // namespace pdfcpp
