#include "pdfcpp/kernel/font/pdf_font.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <sstream>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/io/util/text_util.h"

namespace pdfcpp {
namespace kernel {

using io::Glyph;
using io::GlyphLine;
using io::TextUtil;

static std::u32string ToU32(const std::string& utf8) {
  std::u32string s;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(utf8))
    s += static_cast<char32_t>(cp);
  return s;
}

static std::string U32ToUtf8(const std::u32string& s) {
  std::string out;
  for (char32_t c : s) TextUtil::AppendUtf8(out, c);
  return out;
}

// ============================================================ PdfFont
PdfFont::PdfFont(PdfDictionaryPtr font_dictionary)
    : PdfObjectWrapper<PdfDictionary>(std::move(font_dictionary)) {
  new_font_ = false;
}

bool PdfFont::ContainsGlyph(int unicode) {
  const Glyph* glyph = GetGlyph(unicode);
  if (!glyph) return false;
  if (glyph->GetCode() == 0 && !GetFontProgram()->IsFontSpecific())
    return false;  // .notdef
  return true;
}

GlyphLine PdfFont::CreateGlyphLine(const std::string& content) {
  std::vector<Glyph> glyphs;
  std::u32string text = ToU32(content);
  for (size_t i = 0; i < text.size(); ++i) {
    const Glyph* glyph = GetGlyph(static_cast<int>(text[i]));
    if (glyph) glyphs.push_back(*glyph);
  }
  return GlyphLine(glyphs);
}

int PdfFont::AppendGlyphs(const std::u32string& text, int from, int to,
                          std::vector<Glyph>& glyphs) {
  int processed = 0;
  for (int i = from; i <= to; ++i) {
    const Glyph* glyph = GetGlyph(static_cast<int>(text[i]));
    if (glyph && (ContainsGlyph(static_cast<int>(text[i])) ||
                  TextUtil::IsWhitespaceOrNonPrintable(text[i]))) {
      glyphs.push_back(*glyph);
      ++processed;
    } else {
      break;
    }
  }
  return processed;
}

int PdfFont::AppendAnyGlyph(const std::u32string& text, int from,
                            std::vector<Glyph>& glyphs) {
  const Glyph* glyph = GetGlyph(static_cast<int>(text[from]));
  if (glyph) glyphs.push_back(*glyph);
  return 1;
}

std::string PdfFont::Decode(const PdfString& content) {
  std::string out;
  for (const auto& c : DecodeCodes(content)) out += c.text;
  return out;
}

float PdfFont::GetContentWidth(const PdfString& content) {
  float width = 0;
  for (const auto& c : DecodeCodes(content)) width += c.width;
  return width;
}

void PdfFont::WriteText(const GlyphLine& text, int from, int to,
                        PdfOutputStream& stream) {
  std::vector<Glyph> glyphs;
  for (int i = from; i <= to && i < text.size(); ++i)
    glyphs.push_back(text.Get(i));
  Bytes bytes = ConvertToBytes(GlyphLine(glyphs));
  io::StreamUtil::WriteEscapedString(stream, bytes);
}

void PdfFont::WriteText(const std::string& text, PdfOutputStream& stream) {
  io::StreamUtil::WriteEscapedString(stream, ConvertToBytes(text));
}

int PdfFont::GetWidth(int unicode) {
  const Glyph* glyph = GetGlyph(unicode);
  return glyph ? glyph->GetWidth() : 0;
}

int PdfFont::GetWidth(const std::string& text) {
  int total = 0;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(text)) {
    const Glyph* glyph = GetGlyph(static_cast<int>(cp));
    if (glyph) total += glyph->GetWidth();
  }
  return total;
}

float PdfFont::GetDescent(const std::string& text, float font_size) {
  int min = 0;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(text)) {
    const Glyph* glyph = GetGlyph(static_cast<int>(cp));
    const auto* bbox = glyph ? glyph->GetBbox() : nullptr;
    if (bbox)
      min = std::min(min, (*bbox)[1]);
    else if (font_program_)
      min = std::min(min, font_program_->GetFontMetrics().GetTypoDescender());
  }
  return min * font_size / io::FontProgram::kUnitsNormalization;
}

float PdfFont::GetDescent(int unicode, float font_size) {
  const Glyph* glyph = GetGlyph(unicode);
  const auto* bbox = glyph ? glyph->GetBbox() : nullptr;
  int min =
      bbox ? std::min(0, (*bbox)[1])
           : (font_program_ ? font_program_->GetFontMetrics().GetTypoDescender()
                            : 0);
  return min * font_size / io::FontProgram::kUnitsNormalization;
}

float PdfFont::GetAscent(const std::string& text, float font_size) {
  int max = 0;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(text)) {
    const Glyph* glyph = GetGlyph(static_cast<int>(cp));
    const auto* bbox = glyph ? glyph->GetBbox() : nullptr;
    if (bbox)
      max = std::max(max, (*bbox)[3]);
    else if (font_program_)
      max = std::max(max, font_program_->GetFontMetrics().GetTypoAscender());
  }
  return max * font_size / io::FontProgram::kUnitsNormalization;
}

float PdfFont::GetAscent(int unicode, float font_size) {
  const Glyph* glyph = GetGlyph(unicode);
  const auto* bbox = glyph ? glyph->GetBbox() : nullptr;
  int max =
      bbox ? std::max(0, (*bbox)[3])
           : (font_program_ ? font_program_->GetFontMetrics().GetTypoAscender()
                            : 0);
  return max * font_size / io::FontProgram::kUnitsNormalization;
}

std::vector<std::string> PdfFont::SplitString(const std::string& text,
                                              float font_size,
                                              float max_width) {
  std::vector<std::string> result_string;
  auto cps = TextUtil::Utf8ToCodePoints(text);
  size_t last_white_space = 0;
  size_t start_pos = 0;
  float token_length = 0;
  for (size_t i = 0; i < cps.size(); i++) {
    uint32_t ch = cps[i];
    if (ch == ' ') last_white_space = i;
    float current_char_width = GetWidth(static_cast<int>(ch), font_size);
    if (token_length + current_char_width >= max_width || ch == '\n') {
      if (start_pos < last_white_space) {
        result_string.push_back(
            TextUtil::CodePointsToUtf8(std::vector<uint32_t>(
                cps.begin() + start_pos, cps.begin() + last_white_space)));
        start_pos = last_white_space + 1;
        token_length = 0;
        i = last_white_space;
        continue;
      } else if (start_pos != i) {
        result_string.push_back(TextUtil::CodePointsToUtf8(
            std::vector<uint32_t>(cps.begin() + start_pos, cps.begin() + i)));
        start_pos = i;
        token_length = current_char_width;
      } else {
        result_string.push_back(
            TextUtil::CodePointsToUtf8(std::vector<uint32_t>(
                cps.begin() + start_pos, cps.begin() + start_pos + 1)));
        start_pos = i + 1;
        token_length = 0;
      }
    } else {
      token_length += current_char_width;
    }
  }
  if (start_pos < cps.size())
    result_string.push_back(TextUtil::CodePointsToUtf8(
        std::vector<uint32_t>(cps.begin() + start_pos, cps.end())));
  return result_string;
}

bool PdfFont::IsBuiltWith(const std::string& font_program,
                          const std::string& encoding) const {
  return font_program_ && font_program_->IsBuiltWith(font_program) &&
         encoding.empty();
}

void PdfFont::Flush() {
  if (flushed_) return;
  if (new_font_) {
    FlushFontData();
  }
  flushed_ = true;
  if (GetPdfObject()->GetIndirectReference()) GetPdfObject()->Flush();
}

std::string PdfFont::UpdateSubsetPrefix(const std::string& font_name,
                                        bool is_subset, bool is_embedded) {
  if (!is_subset || !is_embedded) return font_name;
  static std::mt19937 rng(std::random_device{}());
  std::string prefix;
  for (int k = 0; k < 6; ++k) prefix += static_cast<char>('A' + rng() % 26);
  return prefix + "+" + font_name;
}

PdfStreamPtr PdfFont::GetPdfFontStream(
    const Bytes& font_stream_bytes,
    const std::vector<int>& font_stream_lengths) {
  auto font_stream = std::make_shared<PdfStream>(font_stream_bytes);
  font_stream->MakeIndirect(
      GetPdfObject()->GetIndirectReference()->GetDocument());
  for (size_t k = 0; k < font_stream_lengths.size(); ++k)
    font_stream->Put(
        std::make_shared<PdfName>("Length" + std::to_string(k + 1)),
        MakeNumber(font_stream_lengths[k]));
  return font_stream;
}

PdfDictionaryPtr PdfFont::GetFontDescriptor(const std::string& font_name) {
  const io::FontMetrics& font_metrics = font_program_->GetFontMetrics();
  const io::FontNames& font_names = font_program_->GetFontNames();
  auto font_descriptor = std::make_shared<PdfDictionary>();
  font_descriptor->MakeIndirect(
      GetPdfObject()->GetIndirectReference()->GetDocument());
  font_descriptor->Put(PdfName::kType, PdfName::kFontDescriptor);
  font_descriptor->Put(PdfName::kFontName,
                       std::make_shared<PdfName>(font_name));
  font_descriptor->Put(PdfName::kAscent,
                       MakeNumber(font_metrics.GetTypoAscender()));
  font_descriptor->Put(PdfName::kCapHeight,
                       MakeNumber(font_metrics.GetCapHeight()));
  font_descriptor->Put(PdfName::kDescent,
                       MakeNumber(font_metrics.GetTypoDescender()));
  const auto& bbox = font_metrics.GetBbox();
  font_descriptor->Put(PdfName::kFontBBox,
                       std::make_shared<PdfArray>(std::vector<int>{
                           bbox[0], bbox[1], bbox[2], bbox[3]}));
  font_descriptor->Put(
      PdfName::kItalicAngle,
      MakeNumber(static_cast<double>(font_metrics.GetItalicAngle())));
  font_descriptor->Put(PdfName::kStemV, MakeNumber(font_metrics.GetStemV()));
  if (font_metrics.GetXHeight() > 0)
    font_descriptor->Put(PdfName::kXHeight,
                         MakeNumber(font_metrics.GetXHeight()));
  if (font_metrics.GetStemH() > 0)
    font_descriptor->Put(PdfName::kStemH, MakeNumber(font_metrics.GetStemH()));
  if (font_names.GetFontWeight() > 0)
    font_descriptor->Put(PdfName::kFontWeight,
                         MakeNumber(font_names.GetFontWeight()));
  if (!font_names.GetFamilyName().empty())
    font_descriptor->Put(PdfName::kFontFamily, std::make_shared<PdfString>(
                                                   font_names.GetFamilyName()));
  return font_descriptor;
}

void PdfFont::ReadFontDescriptorMetrics(const PdfDictionaryPtr& descriptor) {
  if (!descriptor || !font_program_) return;
  auto& m = font_program_->GetFontMetrics();
  if (auto v = descriptor->GetAsNumber(PdfName::kAscent))
    m.SetTypoAscender(v->IntValue());
  if (auto v = descriptor->GetAsNumber(PdfName::kDescent))
    m.SetTypoDescender(v->IntValue());
  if (auto v = descriptor->GetAsNumber(PdfName::kCapHeight))
    m.SetCapHeight(v->IntValue());
  if (auto v = descriptor->GetAsNumber(PdfName::kXHeight))
    m.SetXHeight(v->IntValue());
  if (auto v = descriptor->GetAsNumber(PdfName::kItalicAngle))
    m.SetItalicAngle(v->FloatValue());
  if (auto v = descriptor->GetAsNumber(PdfName::kStemV))
    m.SetStemV(v->IntValue());
  if (auto bbox = descriptor->GetAsArray(PdfName::kFontBBox)) {
    auto a = bbox->ToIntArray();
    if (a.size() == 4) m.SetBbox(a[0], a[1], a[2], a[3]);
  }
}

// ============================================================ PdfSimpleFont
PdfSimpleFont::PdfSimpleFont(std::shared_ptr<io::FontProgram> font_program,
                             const std::string& encoding_in, bool embedded,
                             Kind kind)
    : kind_(kind) {
  SetPdfObject(std::make_shared<PdfDictionary>());
  font_program_ = std::move(font_program);
  std::string encoding = encoding_in;
  if (encoding.empty() && font_program_->IsFontSpecific() &&
      kind == Kind::kType1)
    encoding = io::FontEncoding::kFontSpecific;
  std::string lower = encoding;
  for (auto& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower == "fontspecific")
    font_encoding_ = io::FontEncoding::CreateFontSpecificEncoding();
  else
    font_encoding_ = io::FontEncoding::CreateFontEncoding(encoding);
  auto* type1 = dynamic_cast<io::Type1Font*>(font_program_.get());
  embedded_ = embedded && !(type1 && type1->IsBuiltInFont());
  if (kind == Kind::kTrueType) embedded_ = embedded;
  subset_ = true;
  new_font_ = true;
}

PdfSimpleFont::PdfSimpleFont(PdfDictionaryPtr font_dictionary)
    : PdfFont(std::move(font_dictionary)) {
  InitFromDictionary();
}

static std::shared_ptr<io::FontEncoding> ReadEncoding(
    const PdfObjectPtr& encoding_obj, std::shared_ptr<io::FontEncoding> base) {
  if (!encoding_obj) return base;
  if (encoding_obj->IsName()) {
    return io::FontEncoding::CreateFontEncoding(
        static_cast<PdfName&>(*encoding_obj).GetValue());
  }
  if (encoding_obj->IsDictionary()) {
    auto dict = std::static_pointer_cast<PdfDictionary>(encoding_obj);
    std::shared_ptr<io::FontEncoding> enc;
    auto base_name = dict->GetAsName(PdfName::kBaseEncoding);
    if (base_name)
      enc = io::FontEncoding::CreateFontEncoding(base_name->GetValue());
    else
      enc = base;
    if (!enc) enc = io::FontEncoding::CreateEmptyFontEncoding();
    // make a private copy since differences modify it
    auto copy = std::make_shared<io::FontEncoding>(*enc);
    auto differences = dict->GetAsArray(PdfName::kDifferences);
    if (differences) {
      copy->InitDifferences();
      int current_number = 0;
      for (int k = 0; k < differences->size(); ++k) {
        PdfObjectPtr obj = differences->Get(k);
        if (obj->IsNumber()) {
          current_number = std::static_pointer_cast<PdfNumber>(obj)->IntValue();
        } else if (obj->IsName() && current_number >= 0 &&
                   current_number < 256) {
          std::string glyph_name =
              std::static_pointer_cast<PdfName>(obj)->GetValue();
          int unicode = io::AdobeGlyphList::NameToUnicode(glyph_name);
          if (unicode != -1)
            copy->SetCodeToUnicode(current_number, unicode);
          else
            copy->SetCodeToUnicode(current_number, -1);
          copy->SetDifference(current_number, glyph_name);
          ++current_number;
        }
      }
    }
    return copy;
  }
  return base;
}

void PdfSimpleFont::InitFromDictionary() {
  auto& dict = *GetPdfObject();
  auto subtype = dict.GetAsName(PdfName::kSubtype);
  if (NameEquals(subtype, PdfName::kTrueType))
    kind_ = Kind::kTrueType;
  else if (NameEquals(subtype, PdfName::kType3))
    kind_ = Kind::kType3;
  else
    kind_ = Kind::kType1;
  auto base_font = dict.GetAsName(PdfName::kBaseFont);
  std::string font_name = base_font ? base_font->GetValue() : std::string();
  std::string stripped = font_name;
  if (stripped.size() > 7 && stripped[6] == '+') stripped = stripped.substr(7);
  std::shared_ptr<io::Type1Font> standard;
  // map common aliases (Arial -> Helvetica etc.) only for exact standard names
  if (kind_ != Kind::kType3 && io::standard_fonts::IsStandardFont(stripped)) {
    try {
      standard = io::Type1Font::CreateStandardFont(stripped);
    } catch (...) {
    }
  }
  auto descriptor = dict.GetAsDictionary(PdfName::kFontDescriptor);
  bool symbolic = false;
  if (descriptor) {
    auto flags = descriptor->GetAsNumber(PdfName::kFlags);
    if (flags)
      symbolic = (flags->IntValue() & 4) != 0 && (flags->IntValue() & 32) == 0;
    if (auto mw = descriptor->GetAsNumber(PdfName::kMissingWidth))
      missing_width_ = mw->FloatValue();
  }
  std::shared_ptr<io::FontEncoding> base;
  if (standard) {
    font_program_ = standard;
    if (standard->IsFontSpecific()) {
      base = io::FontEncoding::CreateFontSpecificEncoding();
      // map codes via built-in AFM codes
      auto enc = io::FontEncoding::CreateEmptyFontEncoding();
      for (const auto& kv : standard->GetCodeToGlyph())
        if (kv.first >= 0 && kv.first < 256 && kv.second.HasValidUnicode())
          enc->SetCodeToUnicode(kv.first, kv.second.GetUnicode());
      base = enc;
    } else {
      auto enc = io::FontEncoding::CreateEmptyFontEncoding();
      enc->FillStandardEncoding();
      base = enc;
    }
  } else {
    auto t1 = std::make_shared<io::Type1Font>(font_name);
    font_program_ = t1;
    if (kind_ == Kind::kTrueType && symbolic) {
      base = io::FontEncoding::CreateFontSpecificEncoding();
    } else if (kind_ == Kind::kTrueType) {
      base = io::FontEncoding::CreateFontEncoding(io::PdfEncodings::kWinansi);
    } else {
      auto enc = io::FontEncoding::CreateEmptyFontEncoding();
      enc->FillStandardEncoding();
      base = enc;
    }
  }
  font_encoding_ = ReadEncoding(dict.Get(PdfName::kEncoding), base);
  if (!font_encoding_) font_encoding_ = base;
  if (auto tu = dict.GetAsStream(PdfName::kToUnicode)) {
    try {
      to_unicode_ = io::CMapToUnicode::Parse(tu->GetBytes());
    } catch (...) {
    }
  }
  if (!standard) ReadFontDescriptorMetrics(descriptor);
  // widths
  auto widths = dict.GetAsArray(PdfName::kWidths);
  auto first_char_n = dict.GetAsNumber(PdfName::kFirstChar);
  int first_char = first_char_n ? first_char_n->IntValue() : 0;
  if (widths) {
    for (int i = 0; i < widths->size(); ++i) {
      int code = first_char + i;
      if (code < 0 || code > 255) continue;
      auto w = widths->GetAsNumber(i);
      if (w) {
        widths_[code] = w->FloatValue();
        has_width_[code] = true;
      }
    }
  }
  if (kind_ == Kind::kType3) {
    if (auto fm = dict.GetAsArray(PdfName::kFontMatrix)) {
      auto d = fm->ToDoubleArray();
      if (d.size() == 6) std::copy(d.begin(), d.end(), font_matrix_.begin());
    }
  }
  if (standard && !widths) {
    for (int code = 0; code < 256; ++code) {
      int uni = font_encoding_->GetUnicode(code);
      const Glyph* g = nullptr;
      if (standard->IsFontSpecific())
        g = standard->GetGlyphByCode(code);
      else if (uni >= 0)
        g = standard->GetGlyph(uni);
      if (g) {
        widths_[code] = static_cast<float>(g->GetWidth());
        has_width_[code] = true;
      }
    }
  }
}

float PdfSimpleFont::GetGlyphWidthForCode(int code) const {
  if (code >= 0 && code < 256 && has_width_[code]) {
    if (kind_ == Kind::kType3)
      return static_cast<float>(widths_[code] * font_matrix_[0] * 1000);
    return widths_[code];
  }
  return missing_width_;
}

int PdfSimpleFont::GetAverageWidth() const {
  if (new_font_) return font_program_->GetAvgWidth();
  float total = 0;
  int count = 0;
  for (int i = 0; i < 256; ++i)
    if (has_width_[i] && widths_[i] > 0) {
      total += GetGlyphWidthForCode(i);
      ++count;
    }
  return count ? static_cast<int>(total / count) : 0;
}

const Glyph* PdfSimpleFont::GetGlyph(int unicode) {
  if (!new_font_) {
    auto it = doc_glyphs_.find(unicode);
    if (it != doc_glyphs_.end()) return &it->second;
    int code = -1;
    if (to_unicode_) {
      auto rev = to_unicode_->CreateReverseMapping();
      auto r = rev.find(unicode);
      if (r != rev.end()) code = r->second;
    }
    if (code < 0) {
      for (int c = 0; c < 256; ++c)
        if (font_encoding_->GetUnicode(c) == unicode) {
          code = c;
          break;
        }
    }
    if (code < 0 && font_encoding_->IsFontSpecific() && unicode < 256)
      code = unicode;
    if (code < 0 || code > 255) return nullptr;
    auto ins = doc_glyphs_.emplace(
        unicode,
        Glyph(code, static_cast<int>(GetGlyphWidthForCode(code)), unicode));
    return &ins.first->second;
  }
  if (!font_encoding_->CanEncode(unicode)) return nullptr;
  const Glyph* glyph = nullptr;
  if (font_encoding_->IsFontSpecific()) {
    glyph = kind_ == Kind::kTrueType ? font_program_->GetGlyph(unicode)
                                     : font_program_->GetGlyphByCode(unicode);
  } else {
    glyph =
        font_program_->GetGlyph(font_encoding_->GetUnicodeDifference(unicode));
    if (!glyph) {
      auto it = notdef_glyphs_.find(unicode);
      if (it != notdef_glyphs_.end()) return &it->second;
      const Glyph* notdef = font_program_->GetGlyphByCode(0);
      if (notdef) {
        auto ins = notdef_glyphs_.emplace(unicode, Glyph(*notdef, unicode));
        return &ins.first->second;
      }
    }
  }
  return glyph;
}

bool PdfSimpleFont::ContainsGlyph(int unicode) {
  if (font_encoding_->CanEncode(unicode)) {
    if (font_encoding_->IsFontSpecific()) {
      return (kind_ == Kind::kTrueType
                  ? font_program_->GetGlyph(unicode)
                  : font_program_->GetGlyphByCode(unicode)) != nullptr;
    }
    return font_program_->GetGlyph(
               font_encoding_->GetUnicodeDifference(unicode)) != nullptr;
  }
  return false;
}

GlyphLine PdfSimpleFont::CreateGlyphLine(const std::string& content) {
  std::vector<Glyph> glyphs;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(content)) {
    if (font_encoding_->IsFontSpecific()) {
      const Glyph* g = GetGlyph(static_cast<int>(cp));
      if (g) glyphs.push_back(*g);
    } else {
      int unicode = static_cast<int>(cp);
      if (font_encoding_->CanEncode(unicode)) {
        const Glyph* g = GetGlyph(unicode);
        if (g) glyphs.push_back(*g);
      }
    }
  }
  return GlyphLine(glyphs);
}

Bytes PdfSimpleFont::ConvertToBytes(const std::string& text) {
  Bytes bytes;
  if (font_encoding_->IsFontSpecific()) {
    for (uint32_t cp : TextUtil::Utf8ToCodePoints(text)) {
      const Glyph* g = GetGlyph(static_cast<int>(cp));
      if (!g) continue;
      int code =
          kind_ == Kind::kTrueType ? static_cast<int>(cp & 0xff) : g->GetCode();
      if (code < 0 || code > 255) continue;
      bytes.push_back(static_cast<uint8_t>(code));
    }
  } else {
    bytes = font_encoding_->ConvertToBytes(text);
  }
  for (uint8_t b : bytes) used_glyphs_[b] = true;
  return bytes;
}

Bytes PdfSimpleFont::ConvertToBytes(const Glyph& glyph) {
  int code;
  if (font_encoding_->IsFontSpecific())
    code = kind_ == Kind::kTrueType ? (glyph.GetUnicode() & 0xff)
                                    : glyph.GetCode();
  else
    code = font_encoding_->ConvertToByte(glyph.GetUnicode());
  if (code < 0 || code > 255) return Bytes();
  used_glyphs_[code] = true;
  return Bytes{static_cast<uint8_t>(code)};
}

Bytes PdfSimpleFont::ConvertToBytes(const GlyphLine& glyph_line) {
  Bytes bytes;
  for (int i = glyph_line.GetStart(); i < glyph_line.GetEnd(); ++i) {
    Bytes b = ConvertToBytes(glyph_line.Get(i));
    bytes.insert(bytes.end(), b.begin(), b.end());
  }
  return bytes;
}

std::vector<PdfFont::DecodedCode> PdfSimpleFont::DecodeCodes(
    const PdfString& content) {
  std::vector<DecodedCode> out;
  Bytes bytes = content.GetValueBytes();
  for (uint8_t b : bytes) {
    DecodedCode dc;
    dc.is_space = b == 32;
    dc.code = Bytes{b};
    std::optional<std::u32string> tu =
        to_unicode_ ? to_unicode_->Lookup(&b, 1) : std::nullopt;
    if (tu) {
      dc.text = U32ToUtf8(*tu);
    } else {
      int uni = font_encoding_->GetUnicode(b);
      if (uni > 0)
        dc.text = TextUtil::CodePointToUtf8(static_cast<uint32_t>(uni));
      else if (font_encoding_->IsFontSpecific() || kind_ == Kind::kTrueType ||
               kind_ == Kind::kType3)
        dc.text = TextUtil::CodePointToUtf8(b);
    }
    if (!new_font_) {
      dc.width = GetGlyphWidthForCode(b);
    } else {
      const Glyph* g = nullptr;
      if (font_encoding_->IsFontSpecific())
        g = kind_ == Kind::kTrueType ? font_program_->GetGlyph(b)
                                     : font_program_->GetGlyphByCode(b);
      else
        g = font_program_->GetGlyph(font_encoding_->GetUnicodeDifference(
            font_encoding_->GetUnicode(b)));
      dc.width = g ? static_cast<float>(g->GetWidth()) : 0.f;
    }
    out.push_back(std::move(dc));
  }
  return out;
}

GlyphLine PdfSimpleFont::DecodeIntoGlyphLine(const PdfString& character_codes) {
  std::vector<Glyph> glyphs;
  Bytes bytes = character_codes.GetValueBytes();
  auto decoded = DecodeCodes(character_codes);
  for (size_t i = 0; i < bytes.size(); ++i) {
    std::u32string chars = ToU32(decoded[i].text);
    glyphs.emplace_back(bytes[i], static_cast<int>(decoded[i].width), chars);
  }
  return GlyphLine(glyphs);
}

float PdfSimpleFont::GetContentWidth(const PdfString& content) {
  return PdfFont::GetContentWidth(content);
}

void PdfSimpleFont::FlushFontData() {
  auto& dict = *GetPdfObject();
  PdfDocument* doc = dict.GetIndirectReference()
                         ? dict.GetIndirectReference()->GetDocument()
                         : nullptr;
  auto* type1 = dynamic_cast<io::Type1Font*>(font_program_.get());
  auto* ttf = dynamic_cast<io::TrueTypeFont*>(font_program_.get());
  bool built_in = type1 && type1->IsBuiltInFont();
  std::string font_name = font_program_->GetFontNames().GetFontName();
  if (!built_in)
    font_name = UpdateSubsetPrefix(
        font_name, subset_ && embedded_ && ttf != nullptr, embedded_);

  dict.Put(PdfName::kType, PdfName::kFont);
  dict.Put(PdfName::kSubtype,
           kind_ == Kind::kTrueType ? PdfName::kTrueType : PdfName::kType1);
  dict.Put(PdfName::kBaseFont, std::make_shared<PdfName>(font_name));

  int first_char = 0, last_char = 255;
  for (first_char = 0; first_char <= 255; ++first_char)
    if (used_glyphs_[first_char]) break;
  for (last_char = 255; last_char >= first_char; --last_char)
    if (used_glyphs_[last_char]) break;
  if (first_char > 255) {
    first_char = 255;
    last_char = 255;
  }
  if (!font_encoding_->IsFontSpecific() || font_encoding_->HasDifferences()) {
    if (font_encoding_->HasDifferences() && !font_encoding_->IsFontSpecific()) {
      auto enc = std::make_shared<PdfDictionary>();
      enc->Put(PdfName::kType, PdfName::kEncoding);
      auto diff = std::make_shared<PdfArray>();
      bool gap = true;
      for (int k = first_char; k <= last_char; ++k) {
        if (used_glyphs_[k]) {
          if (gap) {
            diff->Add(MakeNumber(k));
            gap = false;
          }
          std::string name = font_encoding_->GetDifference(k);
          diff->Add(std::make_shared<PdfName>(
              name.empty() ? std::string(io::FontEncoding::kNotdef) : name));
        } else {
          gap = true;
        }
      }
      enc->Put(PdfName::kDifferences, diff);
      dict.Put(PdfName::kEncoding, enc);
    } else if (!font_encoding_->IsFontSpecific()) {
      const std::string& be = font_encoding_->GetBaseEncoding();
      if (be == io::PdfEncodings::kWinansi)
        dict.Put(PdfName::kEncoding, PdfName::kWinAnsiEncoding);
      else if (be == io::PdfEncodings::kMacroman)
        dict.Put(PdfName::kEncoding, PdfName::kMacRomanEncoding);
    }
  }
  if (force_widths_output_ || !built_in || font_encoding_->HasDifferences()) {
    dict.Put(PdfName::kFirstChar, MakeNumber(first_char));
    dict.Put(PdfName::kLastChar, MakeNumber(last_char));
    auto wd = std::make_shared<PdfArray>();
    for (int k = first_char; k <= last_char; ++k) {
      int width = 0;
      if (used_glyphs_[k]) {
        const Glyph* g = nullptr;
        if (font_encoding_->IsFontSpecific())
          g = ttf ? font_program_->GetGlyph(k)
                  : font_program_->GetGlyphByCode(k);
        else
          g = font_program_->GetGlyph(font_encoding_->GetUnicodeDifference(
              font_encoding_->GetUnicode(k)));
        if (g) width = g->GetWidth();
      }
      wd->Add(MakeNumber(width));
    }
    dict.Put(PdfName::kWidths, wd);
  }
  if (!built_in && doc) {
    auto fd = GetFontDescriptor(font_name);
    int flags = font_program_->GetPdfFontFlags();
    flags &= ~(4 | 32);
    flags |= font_encoding_->IsFontSpecific() ? 4 : 32;
    fd->Put(PdfName::kFlags, MakeNumber(flags));
    if (embedded_) {
      if (ttf) {
        std::set<int> gids;
        for (int k = first_char; k <= last_char; ++k) {
          if (!used_glyphs_[k]) continue;
          const Glyph* g = font_encoding_->IsFontSpecific()
                               ? font_program_->GetGlyph(k)
                               : font_program_->GetGlyph(
                                     font_encoding_->GetUnicodeDifference(
                                         font_encoding_->GetUnicode(k)));
          if (g) gids.insert(g->GetCode());
        }
        Bytes font_bytes = (subset_ && !ttf->IsCff())
                               ? ttf->GetSubset(gids, true)
                               : ttf->GetFontStreamBytes();
        auto stream =
            GetPdfFontStream(font_bytes, {static_cast<int>(font_bytes.size())});
        if (ttf->IsCff()) {
          stream->Remove(std::make_shared<PdfName>("Length1"));
          stream->Put(PdfName::kSubtype, PdfName::Of("OpenType"));
          fd->Put(PdfName::kFontFile3, stream);
        } else {
          fd->Put(PdfName::kFontFile2, stream);
        }
      } else if (type1) {
        std::array<int, 3> lengths{};
        Bytes font_bytes = type1->GetFontStreamBytes(lengths);
        if (!font_bytes.empty()) {
          auto stream = GetPdfFontStream(font_bytes,
                                         {lengths[0], lengths[1], lengths[2]});
          fd->Put(PdfName::kFontFile, stream);
        }
      }
    }
    dict.Put(PdfName::kFontDescriptor, fd);
  }
}

// ============================================================ PdfType0Font
PdfType0Font::PdfType0Font(std::shared_ptr<io::TrueTypeFont> ttf,
                           const std::string& cmap, bool embedded) {
  if (cmap != io::PdfEncodings::kIdentityH &&
      cmap != io::PdfEncodings::kIdentityV)
    throw PdfException("Only Identity CMaps supported on writing: " + cmap);
  SetPdfObject(std::make_shared<PdfDictionary>());
  font_program_ = std::move(ttf);
  vertical_ = cmap == io::PdfEncodings::kIdentityV;
  cmap_encoding_ = io::CMapEncoding::Identity(vertical_);
  embedded_ = embedded;
  subset_ = true;
  new_font_ = true;
}

PdfType0Font::PdfType0Font(PdfDictionaryPtr font_dictionary)
    : PdfFont(std::move(font_dictionary)) {
  auto& dict = *GetPdfObject();
  PdfObjectPtr encoding = dict.Get(PdfName::kEncoding);
  if (encoding && encoding->IsName()) {
    cmap_encoding_ = io::CMapEncoding::FromName(
        std::static_pointer_cast<PdfName>(encoding)->GetValue());
  } else if (encoding && encoding->IsStream()) {
    cmap_encoding_ = io::CMapEncoding::FromStream(
        std::static_pointer_cast<PdfStream>(encoding)->GetBytes());
  } else {
    cmap_encoding_ = io::CMapEncoding::Identity();
  }
  auto base_font = dict.GetAsName(PdfName::kBaseFont);
  font_program_ = std::make_shared<io::Type1Font>(
      base_font ? base_font->GetValue() : std::string());
  if (auto tu = dict.GetAsStream(PdfName::kToUnicode)) {
    try {
      to_unicode_ = io::CMapToUnicode::Parse(tu->GetBytes());
    } catch (...) {
    }
  } else {
    auto tu_name = dict.GetAsName(PdfName::kToUnicode);
    if (tu_name && (tu_name->GetValue() == "Identity-H" ||
                    tu_name->GetValue() == "Identity-V"))
      to_unicode_ = io::CMapToUnicode::GetIdentity();
  }
  auto descendants = dict.GetAsArray(PdfName::kDescendantFonts);
  PdfDictionaryPtr cid_font =
      descendants ? descendants->GetAsDictionary(0) : nullptr;
  if (cid_font) {
    ReadFontDescriptorMetrics(
        cid_font->GetAsDictionary(PdfName::kFontDescriptor));
    if (auto dw = cid_font->GetAsNumber(PdfName::kDW))
      default_width_ = dw->FloatValue();
    if (auto w = cid_font->GetAsArray(PdfName::kW)) {
      int i = 0;
      while (i < w->size()) {
        auto first = w->GetAsNumber(i);
        if (!first) break;
        int c = first->IntValue();
        PdfObjectPtr next = w->Get(i + 1);
        if (next && next->IsArray()) {
          auto arr = std::static_pointer_cast<PdfArray>(next);
          for (int k = 0; k < arr->size(); ++k)
            if (auto n = arr->GetAsNumber(k))
              cid_widths_[c + k] = n->FloatValue();
          i += 2;
        } else {
          auto last = w->GetAsNumber(i + 1);
          auto width = w->GetAsNumber(i + 2);
          if (!last || !width) break;
          for (int k = c; k <= last->IntValue() && k - c < 65536; ++k)
            cid_widths_[k] = width->FloatValue();
          i += 3;
        }
      }
    }
    // Without /ToUnicode, try to recover text from the embedded TrueType cmap
    // (Identity CIDToGIDMap).
    if (!to_unicode_) {
      auto fd = cid_font->GetAsDictionary(PdfName::kFontDescriptor);
      auto ff2 = fd ? fd->GetAsStream(PdfName::kFontFile2) : nullptr;
      if (ff2) {
        try {
          io::TrueTypeFont ttf(ff2->GetBytes());
          auto m = std::make_shared<io::CMapToUnicode>();
          for (const auto& kv : ttf.GetActiveCmap())
            m->AddChar(kv.second.first, 2,
                       std::u32string(1, static_cast<char32_t>(kv.first)));
          to_unicode_ = m;
        } catch (...) {
        }
      }
    }
    if (!to_unicode_) {
      // Predefined Unicode CMaps (UniGB-UCS2-H, UniJIS-UTF16-H, ...): the code
      // is the UTF-16 value.
      const std::string& cmap_name = cmap_encoding_->GetCmapName();
      if (cmap_name.rfind("Uni", 0) == 0 &&
          (cmap_name.find("UCS2") != std::string::npos ||
           cmap_name.find("UTF16") != std::string::npos))
        to_unicode_ = io::CMapToUnicode::GetIdentity();
    }
    if (!to_unicode_) {
      auto info = cid_font->GetAsDictionary(PdfName::kCIDSystemInfo);
      auto ordering = info ? info->GetAsString(PdfName::kOrdering) : nullptr;
      if (ordering && ordering->GetValue() == "Identity" &&
          cmap_encoding_->IsDirect()) {
        // Unknown mapping; fall back to identity (may be wrong for subset
        // fonts)
        to_unicode_ = io::CMapToUnicode::GetIdentity();
      }
    }
  }
}

int PdfType0Font::GetAverageWidth() const {
  if (new_font_) return font_program_->GetAvgWidth();
  if (cid_widths_.empty()) return static_cast<int>(default_width_);
  float total = 0;
  for (const auto& kv : cid_widths_) total += kv.second;
  return static_cast<int>(total / cid_widths_.size());
}

const Glyph* PdfType0Font::GetGlyph(int unicode) {
  if (!new_font_) {
    auto it = doc_glyphs_.find(unicode);
    if (it != doc_glyphs_.end()) return &it->second;
    if (!to_unicode_ || to_unicode_->IsIdentity()) {
      auto ins = doc_glyphs_.emplace(
          unicode,
          Glyph(unicode, static_cast<int>(GetGlyphWidthForCode(unicode)),
                unicode));
      return &ins.first->second;
    }
    auto rev = to_unicode_->CreateReverseMapping();
    auto r = rev.find(unicode);
    if (r == rev.end()) return nullptr;
    auto ins = doc_glyphs_.emplace(
        unicode,
        Glyph(r->second, static_cast<int>(GetGlyphWidthForCode(r->second)),
              unicode));
    return &ins.first->second;
  }
  const Glyph* glyph = font_program_->GetGlyph(unicode);
  if (!glyph) {
    auto it = notdef_glyphs_.find(unicode);
    if (it != notdef_glyphs_.end()) return &it->second;
    const Glyph* notdef = font_program_->GetGlyphByCode(0);
    if (notdef) {
      auto ins = notdef_glyphs_.emplace(unicode, Glyph(*notdef, -1));
      return &ins.first->second;
    }
  }
  return glyph;
}

bool PdfType0Font::ContainsGlyph(int unicode) {
  const Glyph* glyph = font_program_->GetGlyph(unicode);
  return glyph && glyph->GetCode() > 0;
}

Bytes PdfType0Font::ConvertToBytes(const Glyph& glyph) {
  used_glyphs_[glyph.GetCode()] = glyph;
  return Bytes{static_cast<uint8_t>(glyph.GetCode() >> 8),
               static_cast<uint8_t>(glyph.GetCode())};
}

Bytes PdfType0Font::ConvertToBytes(const std::string& text) {
  Bytes out;
  for (uint32_t cp : TextUtil::Utf8ToCodePoints(text)) {
    const Glyph* g = GetGlyph(static_cast<int>(cp));
    if (!g) continue;
    Bytes b = ConvertToBytes(*g);
    out.insert(out.end(), b.begin(), b.end());
  }
  return out;
}

Bytes PdfType0Font::ConvertToBytes(const GlyphLine& glyph_line) {
  Bytes out;
  for (int i = glyph_line.GetStart(); i < glyph_line.GetEnd(); ++i) {
    Bytes b = ConvertToBytes(glyph_line.Get(i));
    out.insert(out.end(), b.begin(), b.end());
  }
  return out;
}

void PdfType0Font::WriteText(const GlyphLine& text, int from, int to,
                             PdfOutputStream& stream) {
  PdfFont::WriteText(text, from, to, stream);
}

void PdfType0Font::WriteText(const std::string& text, PdfOutputStream& stream) {
  io::StreamUtil::WriteEscapedString(stream, ConvertToBytes(text));
}

float PdfType0Font::GetGlyphWidthForCode(int cid) const {
  auto it = cid_widths_.find(cid);
  return it == cid_widths_.end() ? default_width_ : it->second;
}

std::vector<PdfFont::DecodedCode> PdfType0Font::DecodeCodes(
    const PdfString& content) {
  std::vector<DecodedCode> out;
  Bytes bytes = content.GetValueBytes();
  size_t pos = 0;
  for (const auto& code : cmap_encoding_->Decode(bytes)) {
    DecodedCode dc;
    dc.is_space = code.length == 1 && code.code == 32;
    dc.code.assign(bytes.begin() + pos, bytes.begin() + pos + code.length);
    std::optional<std::u32string> tu =
        to_unicode_ ? to_unicode_->Lookup(bytes.data() + pos, code.length)
                    : std::nullopt;
    if (tu) {
      dc.text = U32ToUtf8(*tu);
    } else if (new_font_) {
      const Glyph* g = font_program_->GetGlyphByCode(code.cid);
      if (g && !g->GetChars().empty()) dc.text = g->GetUnicodeString();
    }
    if (new_font_) {
      const Glyph* g = font_program_->GetGlyphByCode(code.cid);
      dc.width = g ? static_cast<float>(g->GetWidth()) : 0.f;
    } else {
      dc.width = GetGlyphWidthForCode(code.cid);
    }
    pos += code.length;
    out.push_back(std::move(dc));
  }
  return out;
}

GlyphLine PdfType0Font::DecodeIntoGlyphLine(const PdfString& character_codes) {
  std::vector<Glyph> glyphs;
  Bytes bytes = character_codes.GetValueBytes();
  auto codes = cmap_encoding_->Decode(bytes);
  auto decoded = DecodeCodes(character_codes);
  for (size_t i = 0; i < codes.size(); ++i)
    glyphs.emplace_back(codes[i].cid, static_cast<int>(decoded[i].width),
                        ToU32(decoded[i].text));
  return GlyphLine(glyphs);
}

float PdfType0Font::GetContentWidth(const PdfString& content) {
  return PdfFont::GetContentWidth(content);
}

static std::string Hex4(int v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04X", v & 0xFFFF);
  return buf;
}

PdfStreamPtr PdfType0Font::GetToUnicode() {
  std::ostringstream buf;
  buf << "/CIDInit /ProcSet findresource begin\n12 dict "
         "begin\nbegincmap\n/CIDSystemInfo\n<< /Registry (Adobe)\n"
         "/Ordering (UCS)\n/Supplement 0\n>> def\n/CMapName "
         "/Adobe-Identity-UCS def\n/CMapType 2 def\n"
         "1 begincodespacerange\n<0000><FFFF>\nendcodespacerange\n";
  std::vector<std::pair<int, std::u32string>> entries;
  for (const auto& kv : used_glyphs_)
    if (!kv.second.GetChars().empty())
      entries.emplace_back(kv.first, kv.second.GetChars());
  for (size_t i = 0; i < entries.size(); i += 100) {
    size_t n = std::min<size_t>(100, entries.size() - i);
    buf << n << " beginbfchar\n";
    for (size_t k = i; k < i + n; ++k) {
      buf << "<" << Hex4(entries[k].first) << "> <";
      for (char32_t c : entries[k].second)
        for (char16_t u : TextUtil::ConvertFromUtf32(c)) buf << Hex4(u);
      buf << ">\n";
    }
    buf << "endbfchar\n";
  }
  buf << "endcmap\nCMapName currentdict /CMap defineresource pop\nend end\n";
  auto stream = std::make_shared<PdfStream>(ToBytes(buf.str()));
  PdfIndirectReference* ref = GetPdfObject()->GetIndirectReference();
  stream->MakeIndirect(ref ? ref->GetDocument() : nullptr);
  return stream;
}

void PdfType0Font::FlushFontData() {
  auto& dict = *GetPdfObject();
  PdfDocument* doc = dict.GetIndirectReference()
                         ? dict.GetIndirectReference()->GetDocument()
                         : nullptr;
  auto* ttf = dynamic_cast<io::TrueTypeFont*>(font_program_.get());
  std::string font_name =
      UpdateSubsetPrefix(font_program_->GetFontNames().GetFontName(),
                         subset_ && !ttf->IsCff(), embedded_);

  auto fd = GetFontDescriptor(font_name);
  fd->Put(PdfName::kFlags, MakeNumber(font_program_->GetPdfFontFlags()));
  if (embedded_) {
    std::set<int> gids;
    for (const auto& kv : used_glyphs_) gids.insert(kv.first);
    Bytes font_bytes = (subset_ && !ttf->IsCff()) ? ttf->GetSubset(gids, true)
                                                  : ttf->GetFontStreamBytes();
    auto stream = std::make_shared<PdfStream>(font_bytes);
    stream->MakeIndirect(doc);
    if (ttf->IsCff()) {
      stream->Put(PdfName::kSubtype, PdfName::Of("OpenType"));
      fd->Put(PdfName::kFontFile3, stream);
    } else {
      stream->Put(PdfName::kLength1,
                  MakeNumber(static_cast<int>(font_bytes.size())));
      fd->Put(PdfName::kFontFile2, stream);
    }
  }
  auto cid_font = std::make_shared<PdfDictionary>();
  cid_font->MakeIndirect(doc);
  cid_font->Put(PdfName::kType, PdfName::kFont);
  cid_font->Put(PdfName::kSubtype,
                ttf->IsCff() ? PdfName::kCIDFontType0 : PdfName::kCIDFontType2);
  cid_font->Put(PdfName::kBaseFont, std::make_shared<PdfName>(font_name));
  cid_font->Put(PdfName::kFontDescriptor, fd);
  if (!ttf->IsCff()) cid_font->Put(PdfName::kCIDToGIDMap, PdfName::kIdentity);
  auto csi = std::make_shared<PdfDictionary>();
  csi->Put(PdfName::kRegistry, std::make_shared<PdfString>("Adobe"));
  csi->Put(PdfName::kOrdering, std::make_shared<PdfString>("Identity"));
  csi->Put(PdfName::kSupplement, MakeNumber(0));
  cid_font->Put(PdfName::kCIDSystemInfo, csi);
  cid_font->Put(PdfName::kDW, MakeNumber(1000));
  auto w = std::make_shared<PdfArray>();
  int last_gid = -10;
  PdfArrayPtr run;
  for (const auto& kv : used_glyphs_) {
    if (kv.second.GetWidth() == 1000) {
      last_gid = -10;
      continue;
    }
    if (kv.first != last_gid + 1 || !run) {
      run = std::make_shared<PdfArray>();
      w->Add(MakeNumber(kv.first));
      w->Add(run);
    }
    run->Add(MakeNumber(kv.second.GetWidth()));
    last_gid = kv.first;
  }
  if (!w->IsEmpty()) cid_font->Put(PdfName::kW, w);

  dict.Put(PdfName::kType, PdfName::kFont);
  dict.Put(PdfName::kSubtype, PdfName::kType0);
  dict.Put(PdfName::kBaseFont,
           std::make_shared<PdfName>(
               ttf->IsCff() ? font_name + "-" + cmap_encoding_->GetCmapName()
                            : font_name));
  dict.Put(PdfName::kEncoding,
           std::make_shared<PdfName>(cmap_encoding_->GetCmapName()));
  auto descendants = std::make_shared<PdfArray>();
  descendants->Add(cid_font);
  dict.Put(PdfName::kDescendantFonts, descendants);
  dict.Put(PdfName::kToUnicode, GetToUnicode());
  cid_font->Flush();
  fd->Flush();
}

// ============================================================ PdfFontFactory
std::shared_ptr<PdfFont> PdfFontFactory::CreateFont() {
  return CreateFont(io::standard_fonts::kHelvetica, "");
}

std::shared_ptr<PdfFont> PdfFontFactory::CreateFont(
    std::shared_ptr<io::FontProgram> font_program, const std::string& encoding,
    EmbeddingStrategy strategy) {
  bool embedded = strategy == EmbeddingStrategy::kForceEmbedded ||
                  strategy == EmbeddingStrategy::kPreferEmbedded;
  if (auto type1 = std::dynamic_pointer_cast<io::Type1Font>(font_program))
    return std::make_shared<PdfSimpleFont>(type1, encoding, embedded,
                                           PdfSimpleFont::Kind::kType1);
  auto ttf = std::dynamic_pointer_cast<io::TrueTypeFont>(font_program);
  if (!ttf)
    throw PdfException(kernel_exception_message_constant::kUnsupportedFont);
  const std::string licensing_message =
      std::string(kernel_exception_message_constant::
                      kCannotBeEmbeddedDueToLicensingRestrictions)
          .replace(0, 3,
                   ttf->GetFontNames().GetFontName() +
                       ttf->GetFontNames().GetStyle());
  if (encoding == io::PdfEncodings::kIdentityH ||
      encoding == io::PdfEncodings::kIdentityV) {
    // Port of createType0FontFromTrueTypeFontProgram: a Type0 font with a
    // TrueType program is always embedded, so licensing restrictions and
    // kForceNotEmbedded are errors rather than silently ignored.
    if (!ttf->GetFontNames().AllowEmbedding())
      throw PdfException(licensing_message);
    if (strategy == EmbeddingStrategy::kForceNotEmbedded)
      throw PdfException(
          kernel_exception_message_constant::
              kCannotCreateType0FontWithTrueTypeFontProgramWithoutEmbeddingIt);
    return std::make_shared<PdfType0Font>(ttf, encoding, true);
  }
  if (!ttf->GetFontNames().AllowEmbedding()) {
    if (strategy == EmbeddingStrategy::kForceEmbedded)
      throw PdfException(licensing_message);
    embedded = false;
  }
  return std::make_shared<PdfSimpleFont>(ttf, encoding, embedded,
                                         PdfSimpleFont::Kind::kTrueType);
}

std::shared_ptr<PdfFont> PdfFontFactory::CreateFont(
    const std::string& font_program, const std::string& encoding,
    EmbeddingStrategy strategy) {
  return CreateFont(io::FontProgramFactory::CreateFont(font_program), encoding,
                    strategy);
}

std::shared_ptr<PdfFont> PdfFontFactory::CreateFont(
    const Bytes& font_program, const std::string& encoding,
    EmbeddingStrategy strategy) {
  return CreateFont(io::FontProgramFactory::CreateFont(font_program), encoding,
                    strategy);
}

std::shared_ptr<PdfFont> PdfFontFactory::CreateFont(
    const PdfDictionaryPtr& font_dictionary) {
  if (!font_dictionary)
    throw PdfException("Cannot create font from null pdf dictionary.");
  auto subtype = font_dictionary->GetAsName(PdfName::kSubtype);
  if (NameEquals(subtype, PdfName::kType0))
    return std::make_shared<PdfType0Font>(font_dictionary);
  return std::make_shared<PdfSimpleFont>(font_dictionary);
}

}  // namespace kernel
}  // namespace pdfcpp
