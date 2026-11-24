#ifndef PDFCPP_IO_FONT_TRUE_TYPE_FONT_H_
#define PDFCPP_IO_FONT_TRUE_TYPE_FONT_H_

#include <map>
#include <set>
#include <string>

#include "pdfcpp/io/font/font_program.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.font.TrueTypeFont (with the OpenTypeParser folded
/// in). Supports TrueType (glyf) and CFF-flavoured OpenType fonts, TTC
/// collections, cmap formats 0/4/6/12, legacy 'kern' table kerning and glyf
/// subsetting. GSUB/GPOS shaping is not ported.
class TrueTypeFont : public FontProgram {
 public:
  /// Parses a font file (TTF/OTF). For TTC use ttc_index.
  explicit TrueTypeFont(const Bytes& ttf, int ttc_index = -1);
  static std::shared_ptr<TrueTypeFont> CreateFromFile(const std::string& path,
                                                      int ttc_index = -1);

  bool HasKernPairs() const override { return !kerning_.empty(); }
  int GetKerning(const Glyph& first, const Glyph& second) const override;
  bool IsCff() const { return cff_; }
  /// The selected cmap: unicode -> {glyph id, width}.
  const std::map<int, std::pair<int, int>>& GetActiveCmap() const {
    return active_cmap_;
  }
  /// Raw font file (the whole TTF/OTF, or the CFF table for CFF fonts).
  Bytes GetFontStreamBytes() const;
  int GetPdfFontFlags() const override;
  /// Builds a subset font containing the given glyph ids (plus composite
  /// dependencies). Glyph ids are preserved.
  Bytes GetSubset(const std::set<int>& glyphs, bool subset_tables = true) const;
  bool IsCmapPresent(int platform_id, int encoding_id) const;
  int GetNumberOfCmaps() const {
    return static_cast<int>(cmaps_present_.size());
  }
  bool IsBuiltWith(const std::string& font_program) const override {
    return font_program == path_;
  }
  void SetPath(const std::string& p) { path_ = p; }
  const std::array<uint8_t, 12>& GetPanose() const { return panose_; }
  bool IsVertical() const { return false; }

 private:
  struct Table {
    uint32_t offset;
    uint32_t length;
  };
  void Parse(int ttc_index);
  const Table* GetTable(const std::string& tag) const;
  int U8(size_t pos) const;
  int U16(size_t pos) const;
  int S16(size_t pos) const;
  uint32_t U32(size_t pos) const;
  void ReadNames();
  void ReadCmaps();
  std::map<int, std::pair<int, int>> ReadCmapSubtable(size_t offset);
  void ReadKerning();
  std::vector<std::array<int, 4>> ReadBboxes();

  Bytes data_;
  uint32_t directory_offset_ = 0;
  std::map<std::string, Table> tables_;
  bool cff_ = false;
  int units_per_em_ = 1000;
  std::vector<int> glyph_widths_by_index_;
  std::map<int, std::pair<int, int>> active_cmap_;
  std::set<std::pair<int, int>> cmaps_present_;
  std::unordered_map<int64_t, int> kerning_;
  std::array<uint8_t, 12> panose_{};
  std::string path_;
  bool loca_short_format_ = false;
};

/// Port of FontProgramFactory (subset): loads standard Type1, AFM/PFM-less
/// Type1 and TrueType/OpenType fonts.
class FontProgramFactory {
 public:
  /// Standard font name or font file path.
  static std::shared_ptr<FontProgram> CreateFont(
      const std::string& font_program);
  static std::shared_ptr<FontProgram> CreateFont(const Bytes& font_program);
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_FONT_TRUE_TYPE_FONT_H_
