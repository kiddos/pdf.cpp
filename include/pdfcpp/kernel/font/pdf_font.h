#ifndef PDFCPP_KERNEL_FONT_PDF_FONT_H_
#define PDFCPP_KERNEL_FONT_PDF_FONT_H_

#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/io/font/cmap.h"
#include "pdfcpp/io/font/font_program.h"
#include "pdfcpp/io/font/true_type_font.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_output_stream.h"

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.font.PdfFont.
/// Text arguments are UTF-8.
class PdfFont : public PdfObjectWrapper<PdfDictionary>,
                public std::enable_shared_from_this<PdfFont> {
 public:
  static constexpr int kSimpleFontMaxCharCodeValue = 255;
  ~PdfFont() override = default;

  /// Glyph for a unicode code point (nullptr if unavailable).
  virtual const io::Glyph* GetGlyph(int unicode) = 0;
  virtual bool ContainsGlyph(int unicode);
  virtual io::GlyphLine CreateGlyphLine(const std::string& content);
  /// Appends glyphs for code points [from, to] of `text`; returns number of
  /// code points processed.
  virtual int AppendGlyphs(const std::u32string& text, int from, int to,
                           std::vector<io::Glyph>& glyphs);
  virtual int AppendAnyGlyph(const std::u32string& text, int from,
                             std::vector<io::Glyph>& glyphs);
  virtual Bytes ConvertToBytes(const std::string& text) = 0;
  virtual Bytes ConvertToBytes(const io::GlyphLine& glyph_line) = 0;
  virtual Bytes ConvertToBytes(const io::Glyph& glyph) = 0;
  /// Decodes a string shown with this font into UTF-8 text.
  virtual std::string Decode(const PdfString& content);
  virtual io::GlyphLine DecodeIntoGlyphLine(
      const PdfString& character_codes) = 0;
  /// Width of the string in text space units * 1000 (glyph space).
  virtual float GetContentWidth(const PdfString& content);
  virtual void WriteText(const io::GlyphLine& text, int from, int to,
                         PdfOutputStream& stream);
  virtual void WriteText(const std::string& text, PdfOutputStream& stream);

  int GetWidth(int unicode);
  float GetWidth(int unicode, float font_size) {
    return io::FontProgram::ConvertTextSpaceToGlyphSpace(GetWidth(unicode) *
                                                         font_size);
  }
  int GetWidth(const std::string& text);
  float GetWidth(const std::string& text, float font_size) {
    return io::FontProgram::ConvertTextSpaceToGlyphSpace(GetWidth(text) *
                                                         font_size);
  }
  float GetDescent(const std::string& text, float font_size);
  float GetDescent(int unicode, float font_size);
  float GetAscent(const std::string& text, float font_size);
  float GetAscent(int unicode, float font_size);
  io::FontProgram* GetFontProgram() const { return font_program_.get(); }
  std::shared_ptr<io::FontProgram> GetFontProgramPtr() const {
    return font_program_;
  }
  bool IsEmbedded() const { return embedded_; }
  bool IsSubset() const { return subset_; }
  void SetSubset(bool subset) { subset_ = subset; }
  /// Splits text into lines not wider than max_width.
  std::vector<std::string> SplitString(const std::string& text, float font_size,
                                       float max_width);
  bool IsBuiltWith(const std::string& font_program,
                   const std::string& encoding) const;
  void Flush() override;
  /// Whether the font was created from an existing PDF dictionary (read mode).
  bool IsNewFont() const { return new_font_; }
  /// Width of a glyph from the /Widths or /W arrays when reading (glyph space).
  virtual float GetGlyphWidthForCode(int code) const { return 0; }
  /// Font matrix (Type3 only differs from 0.001 scaling).
  virtual std::array<double, 6> GetFontMatrix() const {
    return {0.001, 0, 0, 0.001, 0, 0};
  }
  /// Unicode text of each code in the string with the code's width; used by
  /// text extraction.
  struct DecodedCode {
    std::string text;
    float width;    // glyph space (1/1000 em, before Tz/Tc/Tw)
    bool is_space;  // single byte 32 (word spacing applies)
    Bytes code;     // raw character code bytes
  };
  /// Average glyph width (glyph space), used when the space glyph is missing.
  virtual int GetAverageWidth() const {
    return font_program_ ? font_program_->GetAvgWidth() : 0;
  }
  virtual std::vector<DecodedCode> DecodeCodes(const PdfString& content) = 0;

 protected:
  PdfFont() = default;
  explicit PdfFont(PdfDictionaryPtr font_dictionary);
  virtual void FlushFontData() = 0;
  static std::string UpdateSubsetPrefix(const std::string& font_name,
                                        bool is_subset, bool is_embedded);
  PdfStreamPtr GetPdfFontStream(const Bytes& font_stream_bytes,
                                const std::vector<int>& font_stream_lengths);
  PdfDictionaryPtr GetFontDescriptor(const std::string& font_name);
  /// Reads Ascent/Descent/CapHeight/... of an existing font descriptor into the
  /// font program metrics.
  void ReadFontDescriptorMetrics(const PdfDictionaryPtr& descriptor);
  std::map<int, io::Glyph> doc_glyphs_;

  std::shared_ptr<io::FontProgram> font_program_;
  std::map<int, io::Glyph> notdef_glyphs_;
  bool new_font_ = true;
  bool embedded_ = false;
  bool subset_ = true;
  bool flushed_ = false;
};

/// Port of PdfSimpleFont + PdfType1Font + PdfTrueTypeFont (one byte per glyph).
class PdfSimpleFont : public PdfFont {
 public:
  enum class Kind { kType1, kTrueType, kType3 };

  /// Creates a new simple font for writing.
  PdfSimpleFont(std::shared_ptr<io::FontProgram> font_program,
                const std::string& encoding, bool embedded, Kind kind);
  /// Wraps an existing font dictionary (reading).
  explicit PdfSimpleFont(PdfDictionaryPtr font_dictionary);

  const io::Glyph* GetGlyph(int unicode) override;
  bool ContainsGlyph(int unicode) override;
  io::GlyphLine CreateGlyphLine(const std::string& content) override;
  Bytes ConvertToBytes(const std::string& text) override;
  Bytes ConvertToBytes(const io::GlyphLine& glyph_line) override;
  Bytes ConvertToBytes(const io::Glyph& glyph) override;
  io::GlyphLine DecodeIntoGlyphLine(const PdfString& character_codes) override;
  float GetContentWidth(const PdfString& content) override;
  std::vector<DecodedCode> DecodeCodes(const PdfString& content) override;
  float GetGlyphWidthForCode(int code) const override;
  std::array<double, 6> GetFontMatrix() const override { return font_matrix_; }
  io::FontEncoding* GetFontEncoding() const { return font_encoding_.get(); }
  int GetAverageWidth() const override;
  void SetForceWidthsOutput(bool v) { force_widths_output_ = v; }

 protected:
  void FlushFontData() override;

 private:
  void InitFromDictionary();
  std::shared_ptr<io::FontEncoding> font_encoding_;
  std::shared_ptr<io::CMapToUnicode> to_unicode_;
  std::array<bool, 256> used_glyphs_{};
  std::array<float, 256> widths_{};
  std::array<bool, 256> has_width_{};
  float missing_width_ = 0;
  std::array<double, 6> font_matrix_{0.001, 0, 0, 0.001, 0, 0};
  Kind kind_ = Kind::kType1;
  bool force_widths_output_ = false;
};

/// Port of PdfType0Font: composite fonts (Identity-H with embedded
/// TrueType/OpenType when writing).
class PdfType0Font : public PdfFont {
 public:
  PdfType0Font(std::shared_ptr<io::TrueTypeFont> ttf, const std::string& cmap,
               bool embedded);
  explicit PdfType0Font(PdfDictionaryPtr font_dictionary);

  const io::Glyph* GetGlyph(int unicode) override;
  bool ContainsGlyph(int unicode) override;
  Bytes ConvertToBytes(const std::string& text) override;
  Bytes ConvertToBytes(const io::GlyphLine& glyph_line) override;
  Bytes ConvertToBytes(const io::Glyph& glyph) override;
  io::GlyphLine DecodeIntoGlyphLine(const PdfString& character_codes) override;
  float GetContentWidth(const PdfString& content) override;
  std::vector<DecodedCode> DecodeCodes(const PdfString& content) override;
  float GetGlyphWidthForCode(int code) const override;
  int GetAverageWidth() const override;
  void WriteText(const io::GlyphLine& text, int from, int to,
                 PdfOutputStream& stream) override;
  void WriteText(const std::string& text, PdfOutputStream& stream) override;

 protected:
  void FlushFontData() override;

 private:
  PdfStreamPtr GetToUnicode();
  std::map<int, io::Glyph> used_glyphs_;  // gid -> glyph
  std::shared_ptr<io::CMapEncoding> cmap_encoding_;
  std::shared_ptr<io::CMapToUnicode> to_unicode_;
  std::map<int, float> cid_widths_;
  float default_width_ = 1000;
  bool vertical_ = false;
};

/// Embedding strategy (port of PdfFontFactory.EmbeddingStrategy).
enum class EmbeddingStrategy {
  kForceEmbedded,
  kForceNotEmbedded,
  kPreferEmbedded,
  kPreferNotEmbedded
};

/// Port of PdfFontFactory.
class PdfFontFactory {
 public:
  /// Helvetica with WinAnsi encoding.
  static std::shared_ptr<PdfFont> CreateFont();
  /// Standard font name or path to a TTF/OTF/AFM file.
  static std::shared_ptr<PdfFont> CreateFont(
      const std::string& font_program, const std::string& encoding = "",
      EmbeddingStrategy strategy = EmbeddingStrategy::kPreferEmbedded);
  static std::shared_ptr<PdfFont> CreateFont(
      const Bytes& font_program, const std::string& encoding = "",
      EmbeddingStrategy strategy = EmbeddingStrategy::kPreferEmbedded);
  static std::shared_ptr<PdfFont> CreateFont(
      std::shared_ptr<io::FontProgram> font_program,
      const std::string& encoding = "",
      EmbeddingStrategy strategy = EmbeddingStrategy::kPreferEmbedded);
  /// Wraps an existing font dictionary (from a read document).
  static std::shared_ptr<PdfFont> CreateFont(
      const PdfDictionaryPtr& font_dictionary);
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_FONT_PDF_FONT_H_
