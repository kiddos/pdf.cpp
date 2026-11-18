#pragma once

#include <map>
#include <set>
#include <string>

#include "pdfcpp/io/font/FontProgram.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.font.TrueTypeFont (with the OpenTypeParser folded in).
/// Supports TrueType (glyf) and CFF-flavoured OpenType fonts, TTC collections, cmap formats 0/4/6/12,
/// legacy 'kern' table kerning and glyf subsetting. GSUB/GPOS shaping is not ported.
class TrueTypeFont : public FontProgram {
public:
    /// Parses a font file (TTF/OTF). For TTC use ttcIndex.
    explicit TrueTypeFont(const Bytes& ttf, int ttcIndex = -1);
    static std::shared_ptr<TrueTypeFont> createFromFile(const std::string& path, int ttcIndex = -1);

    bool hasKernPairs() const override { return !kerning_.empty(); }
    int getKerning(const Glyph& first, const Glyph& second) const override;
    bool isCff() const { return cff_; }
    /// The selected cmap: unicode -> {glyph id, width}.
    const std::map<int, std::pair<int, int>>& getActiveCmap() const { return activeCmap_; }
    /// Raw font file (the whole TTF/OTF, or the CFF table for CFF fonts).
    Bytes getFontStreamBytes() const;
    int getPdfFontFlags() const override;
    /// Builds a subset font containing the given glyph ids (plus composite dependencies). Glyph ids are preserved.
    Bytes getSubset(const std::set<int>& glyphs, bool subsetTables = true) const;
    bool isCmapPresent(int platformID, int encodingID) const;
    int getNumberOfCmaps() const { return static_cast<int>(cmapsPresent_.size()); }
    bool isBuiltWith(const std::string& fontProgram) const override { return fontProgram == path_; }
    void setPath(const std::string& p) { path_ = p; }
    const std::array<uint8_t, 12>& getPanose() const { return panose_; }
    bool isVertical() const { return false; }

private:
    struct Table {
        uint32_t offset;
        uint32_t length;
    };
    void parse(int ttcIndex);
    const Table* table(const std::string& tag) const;
    int u8(size_t pos) const;
    int u16(size_t pos) const;
    int s16(size_t pos) const;
    uint32_t u32(size_t pos) const;
    void readNames();
    void readCmaps();
    std::map<int, std::pair<int, int>> readCmapSubtable(size_t offset);
    void readKerning();
    std::vector<std::array<int, 4>> readBboxes();

    Bytes data_;
    uint32_t directoryOffset_ = 0;
    std::map<std::string, Table> tables_;
    bool cff_ = false;
    int unitsPerEm_ = 1000;
    std::vector<int> glyphWidthsByIndex_;
    std::map<int, std::pair<int, int>> activeCmap_;
    std::set<std::pair<int, int>> cmapsPresent_;
    std::unordered_map<int64_t, int> kerning_;
    std::array<uint8_t, 12> panose_{};
    std::string path_;
    bool locaShortFormat_ = false;
};

/// Port of FontProgramFactory (subset): loads standard Type1, AFM/PFM-less Type1 and TrueType/OpenType fonts.
class FontProgramFactory {
public:
    /// Standard font name or font file path.
    static std::shared_ptr<FontProgram> createFont(const std::string& fontProgram);
    static std::shared_ptr<FontProgram> createFont(const Bytes& fontProgram);
};

}  // namespace io
}  // namespace pdfcpp
