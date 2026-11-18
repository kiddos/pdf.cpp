#pragma once

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/io/font/CMap.h"
#include "pdfcpp/io/font/FontProgram.h"
#include "pdfcpp/io/font/TrueTypeFont.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfOutputStream.h"

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.font.PdfFont.
/// Text arguments are UTF-8.
class PdfFont : public PdfObjectWrapper<PdfDictionary>, public std::enable_shared_from_this<PdfFont> {
public:
    static constexpr int SIMPLE_FONT_MAX_CHAR_CODE_VALUE = 255;
    ~PdfFont() override = default;

    /// Glyph for a unicode code point (nullptr if unavailable).
    virtual const io::Glyph* getGlyph(int unicode) = 0;
    virtual bool containsGlyph(int unicode);
    virtual io::GlyphLine createGlyphLine(const std::string& content);
    /// Appends glyphs for code points [from, to] of `text`; returns number of code points processed.
    virtual int appendGlyphs(const std::u32string& text, int from, int to, std::vector<io::Glyph>& glyphs);
    virtual int appendAnyGlyph(const std::u32string& text, int from, std::vector<io::Glyph>& glyphs);
    virtual Bytes convertToBytes(const std::string& text) = 0;
    virtual Bytes convertToBytes(const io::GlyphLine& glyphLine) = 0;
    virtual Bytes convertToBytes(const io::Glyph& glyph) = 0;
    /// Decodes a string shown with this font into UTF-8 text.
    virtual std::string decode(const PdfString& content);
    virtual io::GlyphLine decodeIntoGlyphLine(const PdfString& characterCodes) = 0;
    /// Width of the string in text space units * 1000 (glyph space).
    virtual float getContentWidth(const PdfString& content);
    virtual void writeText(const io::GlyphLine& text, int from, int to, PdfOutputStream& stream);
    virtual void writeText(const std::string& text, PdfOutputStream& stream);

    int getWidth(int unicode);
    float getWidth(int unicode, float fontSize) { return io::FontProgram::convertGlyphSpaceToTextSpace(getWidth(unicode) * fontSize); }
    int getWidth(const std::string& text);
    float getWidth(const std::string& text, float fontSize) {
        return io::FontProgram::convertGlyphSpaceToTextSpace(getWidth(text) * fontSize);
    }
    float getDescent(const std::string& text, float fontSize);
    float getDescent(int unicode, float fontSize);
    float getAscent(const std::string& text, float fontSize);
    float getAscent(int unicode, float fontSize);
    io::FontProgram* getFontProgram() const { return fontProgram_.get(); }
    std::shared_ptr<io::FontProgram> getFontProgramPtr() const { return fontProgram_; }
    bool isEmbedded() const { return embedded_; }
    bool isSubset() const { return subset_; }
    void setSubset(bool subset) { subset_ = subset; }
    /// Splits text into lines not wider than maxWidth.
    std::vector<std::string> splitString(const std::string& text, float fontSize, float maxWidth);
    bool isBuiltWith(const std::string& fontProgram, const std::string& encoding) const;
    void flush() override;
    /// Whether the font was created from an existing PDF dictionary (read mode).
    bool isNewFont() const { return newFont_; }
    /// Width of a glyph from the /Widths or /W arrays when reading (glyph space).
    virtual float getGlyphWidthForCode(int code) const { return 0; }
    /// Font matrix (Type3 only differs from 0.001 scaling).
    virtual std::array<double, 6> getFontMatrix() const { return {0.001, 0, 0, 0.001, 0, 0}; }
    /// Unicode text of each code in the string with the code's width; used by text extraction.
    struct DecodedCode {
        std::string text;
        float width;  // glyph space (1/1000 em, before Tz/Tc/Tw)
        bool isSpace; // single byte 32 (word spacing applies)
    };
    virtual std::vector<DecodedCode> decodeCodes(const PdfString& content) = 0;

protected:
    PdfFont() = default;
    explicit PdfFont(PdfDictionaryPtr fontDictionary);
    virtual void flushFontData() = 0;
    static std::string updateSubsetPrefix(const std::string& fontName, bool isSubset, bool isEmbedded);
    PdfStreamPtr getPdfFontStream(const Bytes& fontStreamBytes, const std::vector<int>& fontStreamLengths);
    PdfDictionaryPtr getFontDescriptor(const std::string& fontName);

    std::shared_ptr<io::FontProgram> fontProgram_;
    std::map<int, io::Glyph> notdefGlyphs_;
    bool newFont_ = true;
    bool embedded_ = false;
    bool subset_ = true;
    bool flushed_ = false;
};

/// Port of PdfSimpleFont + PdfType1Font + PdfTrueTypeFont (one byte per glyph).
class PdfSimpleFont : public PdfFont {
public:
    enum class Kind { Type1, TrueType, Type3 };

    /// Creates a new simple font for writing.
    PdfSimpleFont(std::shared_ptr<io::FontProgram> fontProgram, const std::string& encoding, bool embedded, Kind kind);
    /// Wraps an existing font dictionary (reading).
    explicit PdfSimpleFont(PdfDictionaryPtr fontDictionary);

    const io::Glyph* getGlyph(int unicode) override;
    bool containsGlyph(int unicode) override;
    io::GlyphLine createGlyphLine(const std::string& content) override;
    Bytes convertToBytes(const std::string& text) override;
    Bytes convertToBytes(const io::GlyphLine& glyphLine) override;
    Bytes convertToBytes(const io::Glyph& glyph) override;
    io::GlyphLine decodeIntoGlyphLine(const PdfString& characterCodes) override;
    float getContentWidth(const PdfString& content) override;
    std::vector<DecodedCode> decodeCodes(const PdfString& content) override;
    float getGlyphWidthForCode(int code) const override;
    std::array<double, 6> getFontMatrix() const override { return fontMatrix_; }
    io::FontEncoding* getFontEncoding() const { return fontEncoding_.get(); }
    void setForceWidthsOutput(bool v) { forceWidthsOutput_ = v; }

protected:
    void flushFontData() override;

private:
    void initFromDictionary();
    std::shared_ptr<io::FontEncoding> fontEncoding_;
    std::shared_ptr<io::CMapToUnicode> toUnicode_;
    std::array<bool, 256> usedGlyphs_{};
    std::array<float, 256> widths_{};
    std::array<bool, 256> hasWidth_{};
    float missingWidth_ = 0;
    std::array<double, 6> fontMatrix_{0.001, 0, 0, 0.001, 0, 0};
    Kind kind_ = Kind::Type1;
    bool forceWidthsOutput_ = false;
};

/// Port of PdfType0Font: composite fonts (Identity-H with embedded TrueType/OpenType when writing).
class PdfType0Font : public PdfFont {
public:
    PdfType0Font(std::shared_ptr<io::TrueTypeFont> ttf, const std::string& cmap, bool embedded);
    explicit PdfType0Font(PdfDictionaryPtr fontDictionary);

    const io::Glyph* getGlyph(int unicode) override;
    bool containsGlyph(int unicode) override;
    Bytes convertToBytes(const std::string& text) override;
    Bytes convertToBytes(const io::GlyphLine& glyphLine) override;
    Bytes convertToBytes(const io::Glyph& glyph) override;
    io::GlyphLine decodeIntoGlyphLine(const PdfString& characterCodes) override;
    float getContentWidth(const PdfString& content) override;
    std::vector<DecodedCode> decodeCodes(const PdfString& content) override;
    float getGlyphWidthForCode(int code) const override;
    void writeText(const io::GlyphLine& text, int from, int to, PdfOutputStream& stream) override;
    void writeText(const std::string& text, PdfOutputStream& stream) override;

protected:
    void flushFontData() override;

private:
    PdfStreamPtr getToUnicode();
    std::map<int, io::Glyph> usedGlyphs_;  // gid -> glyph
    std::shared_ptr<io::CMapEncoding> cmapEncoding_;
    std::shared_ptr<io::CMapToUnicode> toUnicode_;
    std::map<int, float> cidWidths_;
    float defaultWidth_ = 1000;
    bool vertical_ = false;
};

/// Embedding strategy (port of PdfFontFactory.EmbeddingStrategy).
enum class EmbeddingStrategy { FORCE_EMBEDDED, FORCE_NOT_EMBEDDED, PREFER_EMBEDDED, PREFER_NOT_EMBEDDED };

/// Port of PdfFontFactory.
class PdfFontFactory {
public:
    /// Helvetica with WinAnsi encoding.
    static std::shared_ptr<PdfFont> createFont();
    /// Standard font name or path to a TTF/OTF/AFM file.
    static std::shared_ptr<PdfFont> createFont(const std::string& fontProgram, const std::string& encoding = "",
                                               EmbeddingStrategy strategy = EmbeddingStrategy::PREFER_EMBEDDED);
    static std::shared_ptr<PdfFont> createFont(const Bytes& fontProgram, const std::string& encoding = "",
                                               EmbeddingStrategy strategy = EmbeddingStrategy::PREFER_EMBEDDED);
    static std::shared_ptr<PdfFont> createFont(std::shared_ptr<io::FontProgram> fontProgram, const std::string& encoding = "",
                                               EmbeddingStrategy strategy = EmbeddingStrategy::PREFER_EMBEDDED);
    /// Wraps an existing font dictionary (from a read document).
    static std::shared_ptr<PdfFont> createFont(const PdfDictionaryPtr& fontDictionary);
};

}  // namespace kernel
}  // namespace pdfcpp
