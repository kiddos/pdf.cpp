#ifndef PDFCPP_IO_FONT_FONT_PROGRAM_H_
#define PDFCPP_IO_FONT_FONT_PROGRAM_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Returns an embedded resource ("font/afm/Helvetica.afm",
/// "font/AdobeGlyphList.txt"); empty if missing.
class ResourceUtil {
 public:
  static const Bytes* GetResource(const std::string& name);
};

/// Port of com.itextpdf.io.font.otf.Glyph.
class Glyph {
 public:
  Glyph() = default;
  Glyph(int code, int width, int unicode)
      : Glyph(code, width, unicode, std::u32string(), false) {}
  Glyph(int code, int width, std::u32string chars)
      : code_(code), width_(width), chars_(std::move(chars)) {
    unicode_ = CodePoint(chars_);
  }
  Glyph(int code, int width, int unicode, const std::array<int, 4>& bbox)
      : Glyph(code, width, unicode) {
    bbox_ = bbox;
    has_bbox_ = true;
  }
  Glyph(int width, int unicode)
      : Glyph(-1, width, unicode, std::u32string(), false) {}
  Glyph(int code, int width, int unicode, std::u32string chars, bool is_mark)
      : code_(code), width_(width), unicode_(unicode), is_mark_(is_mark) {
    chars_ =
        !chars.empty()
            ? std::move(chars)
            : (unicode > -1 ? std::u32string(1, static_cast<char32_t>(unicode))
                            : std::u32string());
  }
  Glyph(const Glyph& glyph, int unicode)
      : Glyph(glyph.code_, glyph.width_, unicode, std::u32string(),
              glyph.is_mark_) {
    bbox_ = glyph.bbox_;
    has_bbox_ = glyph.has_bbox_;
  }
  Glyph(const Glyph& glyph) = default;
  Glyph& operator=(const Glyph&) = default;

  int GetCode() const { return code_; }
  int GetWidth() const { return width_; }
  const std::array<int, 4>* GetBbox() const {
    return has_bbox_ ? &bbox_ : nullptr;
  }
  bool HasValidUnicode() const { return unicode_ > -1; }
  int GetUnicode() const { return unicode_; }
  void SetUnicode(int unicode) {
    unicode_ = unicode;
    chars_ = unicode > -1 ? std::u32string(1, static_cast<char32_t>(unicode))
                          : std::u32string();
  }
  const std::u32string& GetChars() const { return chars_; }
  void SetChars(std::u32string chars) { chars_ = std::move(chars); }
  bool IsMark() const { return is_mark_; }
  int16_t GetXPlacement() const { return x_placement_; }
  void SetXPlacement(int16_t v) { x_placement_ = v; }
  int16_t GetYPlacement() const { return y_placement_; }
  void SetYPlacement(int16_t v) { y_placement_ = v; }
  int16_t GetXAdvance() const { return x_advance_; }
  void SetXAdvance(int16_t v) { x_advance_ = v; }
  int16_t GetYAdvance() const { return y_advance_; }
  void SetYAdvance(int16_t v) { y_advance_ = v; }
  int16_t GetAnchorDelta() const { return anchor_delta_; }
  void SetAnchorDelta(int16_t v) { anchor_delta_ = v; }
  bool HasOffsets() const { return HasAdvance() || HasPlacement(); }
  bool HasPlacement() const {
    return anchor_delta_ != 0 || x_placement_ != 0 || y_placement_ != 0;
  }
  bool HasAdvance() const { return x_advance_ != 0 || y_advance_ != 0; }
  /// Unicode text of this glyph as UTF-8.
  std::string GetUnicodeString() const;
  bool operator==(const Glyph& o) const {
    return code_ == o.code_ && width_ == o.width_ && unicode_ == o.unicode_ &&
           chars_ == o.chars_;
  }

 private:
  static int CodePoint(const std::u32string& s) {
    return s.size() == 1 ? static_cast<int>(s[0]) : -1;
  }
  int code_ = -1;
  int width_ = 0;
  int unicode_ = -1;
  std::u32string chars_;
  bool is_mark_ = false;
  std::array<int, 4> bbox_{};
  bool has_bbox_ = false;
  int16_t x_placement_ = 0, y_placement_ = 0, x_advance_ = 0, y_advance_ = 0,
          anchor_delta_ = 0;
};

/// Port of GlyphLine: a list of glyphs with a [start, end) window and actual
/// text.
class GlyphLine {
 public:
  GlyphLine() = default;
  explicit GlyphLine(std::vector<Glyph> glyphs)
      : glyphs_(std::move(glyphs)), end_(static_cast<int>(glyphs_.size())) {}
  GlyphLine(std::vector<Glyph> glyphs, int start, int end)
      : glyphs_(std::move(glyphs)), start_(start), end_(end) {}
  int GetStart() const { return start_; }
  int GetEnd() const { return end_; }
  void SetStart(int s) { start_ = s; }
  void SetEnd(int e) { end_ = e; }
  int size() const { return static_cast<int>(glyphs_.size()); }
  const Glyph& Get(int index) const { return glyphs_[index]; }
  Glyph& Get(int index) { return glyphs_[index]; }
  void Add(const Glyph& g) {
    glyphs_.push_back(g);
    end_ = static_cast<int>(glyphs_.size());
  }
  void Set(int index, const Glyph& g) { glyphs_[index] = g; }
  const std::vector<Glyph>& GetGlyphs() const { return glyphs_; }
  /// UTF-8 text of [start, end).
  std::string ToUnicodeString(int start, int end) const;
  std::string ToString() const { return ToUnicodeString(start_, end_); }

 private:
  std::vector<Glyph> glyphs_;
  int start_ = 0;
  int end_ = 0;
};

/// Port of FontMetrics.
class FontMetrics {
 public:
  float GetUnitsPerEm() const { return units_per_em_; }
  int GetNumberOfGlyphs() const { return num_of_glyphs_; }
  const std::vector<int>& GetGlyphWidths() const { return glyph_widths_; }
  int GetTypoAscender() const { return typo_ascender_; }
  int GetTypoDescender() const { return typo_descender_; }
  int GetCapHeight() const { return cap_height_; }
  int GetXHeight() const { return x_height_; }
  float GetItalicAngle() const { return italic_angle_; }
  const std::array<int, 4>& GetBbox() const { return bbox_; }
  int GetAscender() const { return ascender_; }
  int GetDescender() const { return descender_; }
  int GetLineGap() const { return line_gap_; }
  int GetWinAscender() const { return win_ascender_; }
  int GetWinDescender() const { return win_descender_; }
  int GetAdvanceWidthMax() const { return advance_width_max_; }
  int GetUnderlinePosition() const { return underline_position_; }
  int GetUnderlineThickness() const { return underline_thickness_; }
  int GetStrikeoutPosition() const { return strikeout_position_; }
  int GetStrikeoutSize() const { return strikeout_size_; }
  int GetSubscriptSize() const { return subscript_size_; }
  int GetSubscriptOffset() const { return subscript_offset_; }
  int GetSuperscriptSize() const { return superscript_size_; }
  int GetSuperscriptOffset() const { return superscript_offset_; }
  int GetStemV() const { return stem_v_; }
  int GetStemH() const { return stem_h_; }
  bool IsFixedPitch() const { return is_fixed_pitch_; }

  void SetUnitsPerEm(int v) { units_per_em_ = static_cast<float>(v); }
  void SetNumberOfGlyphs(int v) { num_of_glyphs_ = v; }
  void SetGlyphWidths(std::vector<int> w) { glyph_widths_ = std::move(w); }
  void SetTypoAscender(int v) { typo_ascender_ = v; }
  void SetTypoDescender(int v) { typo_descender_ = v; }
  void SetCapHeight(int v) { cap_height_ = v; }
  void SetXHeight(int v) { x_height_ = v; }
  void SetItalicAngle(float v) { italic_angle_ = v; }
  void SetBbox(int llx, int lly, int urx, int ury) {
    bbox_ = {llx, lly, urx, ury};
  }
  void SetAscender(int v) { ascender_ = v; }
  void SetDescender(int v) { descender_ = v; }
  void SetLineGap(int v) { line_gap_ = v; }
  void SetWinAscender(int v) { win_ascender_ = v; }
  void SetWinDescender(int v) { win_descender_ = v; }
  void SetAdvanceWidthMax(int v) { advance_width_max_ = v; }
  void SetUnderlinePosition(int v) { underline_position_ = v; }
  void SetUnderlineThickness(int v) { underline_thickness_ = v; }
  void SetStrikeoutPosition(int v) { strikeout_position_ = v; }
  void SetStrikeoutSize(int v) { strikeout_size_ = v; }
  void SetSubscriptSize(int v) { subscript_size_ = v; }
  void SetSubscriptOffset(int v) { subscript_offset_ = v; }
  void SetSuperscriptSize(int v) { superscript_size_ = v; }
  void SetSuperscriptOffset(int v) { superscript_offset_ = v; }
  void SetStemV(int v) { stem_v_ = v; }
  void SetStemH(int v) { stem_h_ = v; }
  void SetIsFixedPitch(bool v) { is_fixed_pitch_ = v; }
  void UpdateBbox(float llx, float lly, float urx, float ury) {
    bbox_ = {static_cast<int>(llx), static_cast<int>(lly),
             static_cast<int>(urx), static_cast<int>(ury)};
  }

 private:
  float units_per_em_ = 1000;
  int num_of_glyphs_ = 0;
  std::vector<int> glyph_widths_;
  int typo_ascender_ = 800;
  int typo_descender_ = -200;
  int cap_height_ = 700;
  int x_height_ = 0;
  float italic_angle_ = 0;
  std::array<int, 4> bbox_{-50, -200, 1000, 900};
  int ascender_ = 0, descender_ = 0, line_gap_ = 0, win_ascender_ = 0,
      win_descender_ = 0, advance_width_max_ = 0;
  int underline_position_ = -100, underline_thickness_ = 50;
  int strikeout_position_ = 0, strikeout_size_ = 0;
  int subscript_size_ = 0, subscript_offset_ = 0, superscript_size_ = 0,
      superscript_offset_ = 0;
  int stem_v_ = 80, stem_h_ = 0;
  bool is_fixed_pitch_ = false;
};

/// Port of FontNames.
class FontNames {
 public:
  const std::string& GetFontName() const { return font_name_; }
  void SetFontName(const std::string& n) { font_name_ = n; }
  const std::string& GetFullName() const { return full_name_; }
  void SetFullName(const std::string& n) { full_name_ = n; }
  const std::string& GetFamilyName() const { return family_name_; }
  void SetFamilyName(const std::string& n) { family_name_ = n; }
  const std::string& GetStyle() const { return style_; }
  void SetStyle(const std::string& s) { style_ = s; }
  int GetFontWeight() const { return font_weight_; }
  void SetFontWeight(int w) { font_weight_ = w; }
  const std::string& GetFontStretch() const { return font_stretch_; }
  void SetFontStretch(const std::string& s) { font_stretch_ = s; }
  int GetMacStyle() const { return mac_style_; }
  void SetMacStyle(int m) { mac_style_ = m; }
  bool AllowEmbedding() const { return allow_embedding_; }
  void SetAllowEmbedding(bool v) { allow_embedding_ = v; }
  bool IsBold() const { return (mac_style_ & 1) != 0; }
  bool IsItalic() const { return (mac_style_ & 2) != 0; }
  /// All name records: nameId -> list of {platform, encoding, language, value}.
  std::map<int, std::vector<std::array<std::string, 4>>>& GetAllNames() {
    return all_names_;
  }

 private:
  std::string font_name_, full_name_, family_name_, style_;
  std::string font_stretch_ = "Normal";
  int font_weight_ = 400;
  int mac_style_ = 0;
  bool allow_embedding_ = true;
  std::map<int, std::vector<std::array<std::string, 4>>> all_names_;
};

/// Port of AdobeGlyphList.
class AdobeGlyphList {
 public:
  static int NameToUnicode(const std::string& name);
  /// Returns empty string if not found.
  static std::string UnicodeToName(int unicode);
};

/// Port of FontEncoding.
class FontEncoding {
 public:
  static constexpr const char* kFontSpecific = "FontSpecific";
  static constexpr const char* kNotdef = ".notdef";

  static std::shared_ptr<FontEncoding> CreateFontEncoding(
      const std::string& base_encoding);
  static std::shared_ptr<FontEncoding> CreateEmptyFontEncoding();
  static std::shared_ptr<FontEncoding> CreateFontSpecificEncoding();
  static void FillFontEncoding(FontEncoding& encoding);

  const std::string& GetBaseEncoding() const { return base_encoding_; }
  bool IsFontSpecific() const { return font_specific_; }
  bool AddSymbol(int code, int unicode);
  int GetUnicode(int index) const {
    return (index >= 0 && index < 256) ? code_to_unicode_[index] : -1;
  }
  int GetUnicodeDifference(int index) const;
  bool HasDifferences() const { return has_differences_; }
  std::string GetDifference(int index) const {
    return has_differences_ && index >= 0 && index < 256 ? differences_[index]
                                                         : std::string();
  }
  void SetDifference(int index, const std::string& difference) {
    if (has_differences_ && index >= 0 && index < 256)
      differences_[index] = difference;
  }
  void SetCodeToUnicode(int code, int unicode) {
    if (code >= 0 && code < 256) code_to_unicode_[code] = unicode;
    if (unicode >= 0) unicode_to_code_[unicode] = code;
  }
  Bytes ConvertToBytes(const std::string& text) const;
  int ConvertToByte(int unicode) const;
  bool CanEncode(int unicode) const;
  bool CanDecode(int code) const {
    return code >= 0 && code < 256 && code_to_unicode_[code] > -1;
  }
  bool IsBuiltWith(const std::string& encoding) const {
    return NormalizeEncoding(encoding) == base_encoding_;
  }
  static std::string NormalizeEncoding(const std::string& enc);
  void FillStandardEncoding();
  void InitDifferences() {
    has_differences_ = true;
    differences_.assign(256, std::string());
  }

 protected:
  FontEncoding() { code_to_unicode_.fill(-1); }
  void FillCustomEncoding();
  void FillNamedEncoding();

  std::string base_encoding_;
  bool font_specific_ = false;
  std::unordered_map<int, int> unicode_to_code_;
  std::array<int, 256> code_to_unicode_;
  std::vector<std::string> differences_;
  bool has_differences_ = false;
  std::unordered_map<int, int> unicode_differences_;
};

/// Port of com.itextpdf.io.font.FontProgram.
class FontProgram {
 public:
  static constexpr int kHorizontalScalingFactor = 100;
  static constexpr int kDefaultWidth = 1000;
  static constexpr int kUnitsNormalization = 1000;

  virtual ~FontProgram() = default;
  // Note: names follow the Java API exactly (TextSpaceToGlyphSpace divides by
  // 1000).
  static float ConvertTextSpaceToGlyphSpace(float value) {
    return value / kUnitsNormalization;
  }
  static float ConvertGlyphSpaceToTextSpace(float value) {
    return value * kUnitsNormalization;
  }
  static double ConvertGlyphSpaceToTextSpace(double value) {
    return value * kUnitsNormalization;
  }

  int CountOfGlyphs() const {
    return std::max(static_cast<int>(code_to_glyph_.size()),
                    font_metrics_.GetNumberOfGlyphs());
  }
  const FontNames& GetFontNames() const { return font_names_; }
  FontNames& GetFontNames() { return font_names_; }
  const FontMetrics& GetFontMetrics() const { return font_metrics_; }
  FontMetrics& GetFontMetrics() { return font_metrics_; }
  const std::string& GetRegistry() const { return registry_; }
  virtual int GetPdfFontFlags() const = 0;
  bool IsFontSpecific() const { return is_font_specific_; }
  int GetWidth(int unicode) const;
  int GetAvgWidth() const { return avg_width_; }
  const std::array<int, 4>* GetCharBBox(int unicode) const;
  /// Glyph for a unicode code point or nullptr.
  virtual const Glyph* GetGlyph(int unicode) const;
  virtual const Glyph* GetGlyphByCode(int char_code) const;
  virtual bool HasKernPairs() const { return false; }
  int GetKerning(int first, int second) const;
  virtual int GetKerning(const Glyph& first, const Glyph& second) const = 0;
  virtual bool IsBuiltWith(const std::string& font_name) const { return false; }
  const std::string& GetEncodingScheme() const { return encoding_scheme_; }
  const std::unordered_map<int, Glyph>& GetCodeToGlyph() const {
    return code_to_glyph_;
  }
  const std::unordered_map<int, Glyph>& GetUnicodeToGlyph() const {
    return unicode_to_glyph_;
  }

 protected:
  void FixSpaceIssue();
  std::unordered_map<int, Glyph> code_to_glyph_;
  std::unordered_map<int, Glyph> unicode_to_glyph_;
  bool is_font_specific_ = false;
  FontNames font_names_;
  FontMetrics font_metrics_;
  int avg_width_ = 0;
  std::string encoding_scheme_ = FontEncoding::kFontSpecific;
  std::string registry_;
};

/// Port of StandardFonts.
namespace standard_fonts {
inline constexpr const char* kCourier = "Courier";
inline constexpr const char* kCourierBold = "Courier-Bold";
inline constexpr const char* kCourierBoldoblique = "Courier-BoldOblique";
inline constexpr const char* kCourierOblique = "Courier-Oblique";
inline constexpr const char* kHelvetica = "Helvetica";
inline constexpr const char* kHelveticaBold = "Helvetica-Bold";
inline constexpr const char* kHelveticaBoldoblique = "Helvetica-BoldOblique";
inline constexpr const char* kHelveticaOblique = "Helvetica-Oblique";
inline constexpr const char* kSymbol = "Symbol";
inline constexpr const char* kTimesBold = "Times-Bold";
inline constexpr const char* kTimesBolditalic = "Times-BoldItalic";
inline constexpr const char* kTimesItalic = "Times-Italic";
inline constexpr const char* kTimesRoman = "Times-Roman";
inline constexpr const char* kZapfdingbats = "ZapfDingbats";
bool IsStandardFont(const std::string& font_name);
}  // namespace standard_fonts

/// Port of Type1Font (AFM metrics + optional PFB program).
class Type1Font : public FontProgram {
 public:
  /// Loads one of the 14 standard fonts from embedded AFM metrics.
  static std::shared_ptr<Type1Font> CreateStandardFont(const std::string& name);
  /// Loads AFM metrics (and an optional PFB binary for embedding).
  static std::shared_ptr<Type1Font> CreateFromAfm(
      const Bytes& afm, const Bytes& pfb = Bytes(),
      const std::string& afm_path = "");
  /// Font known only by name (non-embedded, from a PDF).
  explicit Type1Font(const std::string& base_font);

  void InitializeGlyphs(const FontEncoding& font_encoding);
  bool IsBuiltInFont() const { return built_in_; }
  int GetPdfFontFlags() const override;
  const std::string& GetCharacterSet() const { return character_set_; }
  bool HasKernPairs() const override { return !kern_pairs_.empty(); }
  int GetKerning(const Glyph& first, const Glyph& second) const override;
  bool SetKerning(int first, int second, int kern);
  const Glyph* GetGlyphByName(const std::string& name) const;
  /// Returns the PFB data split into the 3 segments (empty if not embeddable).
  Bytes GetFontStreamBytes(std::array<int, 3>& lengths) const;
  bool IsBuiltWith(const std::string& font_program) const override {
    return afm_path_ == font_program;
  }

 protected:
  Type1Font() = default;
  void Process(const Bytes& afm);

 private:
  std::string character_set_;
  std::unordered_map<int64_t, int> kern_pairs_;
  Bytes pfb_;
  std::string afm_path_;
  bool built_in_ = false;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_FONT_FONT_PROGRAM_H_
