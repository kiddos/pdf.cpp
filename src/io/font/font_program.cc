#include "pdfcpp/io/font/font_program.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/text_util.h"

namespace pdfcpp {
namespace io {

namespace detail {
const std::map<std::string, std::pair<const uint8_t*, size_t>>&
EmbeddedResources();
}

const Bytes* ResourceUtil::GetResource(const std::string& name) {
  static std::map<std::string, Bytes> cache;
  auto it = cache.find(name);
  if (it != cache.end()) return &it->second;
  const auto& res = detail::EmbeddedResources();
  auto r = res.find(name);
  if (r == res.end()) return nullptr;
  auto ins = cache.emplace(
      name, Bytes(r->second.first, r->second.first + r->second.second));
  return &ins.first->second;
}

// ============================================================ Glyph /
// GlyphLine
std::string Glyph::GetUnicodeString() const {
  std::string s;
  for (char32_t c : chars_) TextUtil::AppendUtf8(s, c);
  return s;
}

std::string GlyphLine::ToUnicodeString(int start, int end) const {
  std::string s;
  for (int i = start; i < end && i < size(); ++i) {
    const Glyph& g = glyphs_[i];
    if (!g.GetChars().empty())
      s += g.GetUnicodeString();
    else if (g.HasValidUnicode())
      TextUtil::AppendUtf8(s, static_cast<uint32_t>(g.GetUnicode()));
  }
  return s;
}

// ============================================================ AdobeGlyphList
namespace {
struct GlyphListData {
  std::unordered_map<std::string, int> names2unicode;
  std::unordered_map<int, std::string> unicode2names;
  GlyphListData() {
    const Bytes* data = ResourceUtil::GetResource("font/AdobeGlyphList.txt");
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
const GlyphListData& GlyphList() {
  static GlyphListData d;
  return d;
}
}  // namespace

int AdobeGlyphList::NameToUnicode(const std::string& name) {
  const auto& d = GlyphList();
  auto it = d.names2unicode.find(name);
  if (it != d.names2unicode.end()) return it->second;
  if (name.size() == 7 &&
      (name.compare(0, 3, "uni") == 0 || name.compare(0, 3, "UNI") == 0)) {
    char* end = nullptr;
    long v = std::strtol(name.c_str() + 3, &end, 16);
    if (end && *end == '\0') return static_cast<int>(v);
  }
  return -1;
}

std::string AdobeGlyphList::UnicodeToName(int unicode) {
  const auto& d = GlyphList();
  auto it = d.unicode2names.find(unicode);
  return it == d.unicode2names.end() ? std::string() : it->second;
}

// ============================================================ FontEncoding
std::string FontEncoding::NormalizeEncoding(const std::string& enc) {
  std::string tmp = enc;
  for (auto& c : tmp)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (tmp.empty() || tmp == "winansi" || tmp == "winansiencoding")
    return PdfEncodings::kWinansi;
  if (tmp == "macroman" || tmp == "macromanencoding")
    return PdfEncodings::kMacroman;
  if (tmp == "zapfdingbatsencoding") return PdfEncodings::kZapfdingbats;
  return enc;
}

std::shared_ptr<FontEncoding> FontEncoding::CreateFontEncoding(
    const std::string& base_encoding) {
  std::shared_ptr<FontEncoding> encoding(new FontEncoding());
  encoding->base_encoding_ = NormalizeEncoding(base_encoding);
  if (!encoding->base_encoding_.empty() && encoding->base_encoding_[0] == '#')
    encoding->FillCustomEncoding();
  else
    encoding->FillNamedEncoding();
  return encoding;
}

std::shared_ptr<FontEncoding> FontEncoding::CreateEmptyFontEncoding() {
  std::shared_ptr<FontEncoding> encoding(new FontEncoding());
  encoding->base_encoding_.clear();
  encoding->font_specific_ = false;
  encoding->InitDifferences();
  for (int ch = 0; ch < 256; ch++) encoding->unicode_differences_[ch] = ch;
  return encoding;
}

std::shared_ptr<FontEncoding> FontEncoding::CreateFontSpecificEncoding() {
  std::shared_ptr<FontEncoding> encoding(new FontEncoding());
  encoding->font_specific_ = true;
  FillFontEncoding(*encoding);
  return encoding;
}

void FontEncoding::FillFontEncoding(FontEncoding& encoding) {
  for (int ch = 0; ch < 256; ch++) {
    encoding.unicode_to_code_[ch] = ch;
    encoding.code_to_unicode_[ch] = ch;
    encoding.unicode_differences_[ch] = ch;
  }
}

bool FontEncoding::AddSymbol(int code, int unicode) {
  if (code < 0 || code > 255) return false;
  std::string glyph_name = AdobeGlyphList::UnicodeToName(unicode);
  if (glyph_name.empty()) return false;
  unicode_to_code_[unicode] = code;
  code_to_unicode_[code] = unicode;
  if (!has_differences_) InitDifferences();
  differences_[code] = glyph_name;
  unicode_differences_[unicode] = unicode;
  return true;
}

int FontEncoding::GetUnicodeDifference(int index) const {
  auto it = unicode_differences_.find(index);
  return it == unicode_differences_.end() ? 0 : it->second;
}

Bytes FontEncoding::ConvertToBytes(const std::string& text) const {
  Bytes bytes;
  for (uint32_t ch : TextUtil::Utf8ToCodePoints(text)) {
    auto it = unicode_to_code_.find(static_cast<int>(ch));
    if (it != unicode_to_code_.end())
      bytes.push_back(static_cast<uint8_t>(it->second));
  }
  return bytes;
}

int FontEncoding::ConvertToByte(int unicode) const {
  auto it = unicode_to_code_.find(unicode);
  return it == unicode_to_code_.end() ? 0 : it->second;
}

bool FontEncoding::CanEncode(int unicode) const {
  return unicode_to_code_.count(unicode) > 0 || unicode < 32 ||
         TextUtil::IsNewLine(unicode);
}

void FontEncoding::FillCustomEncoding() {
  InitDifferences();
  std::string spec = base_encoding_.substr(1);
  for (auto& c : spec)
    if (c == ',' || c == '\t' || c == '\n' || c == '\r' || c == '\f') c = ' ';
  std::istringstream tok(spec);
  std::string first;
  tok >> first;
  if (first == "full") {
    std::string order, name, uni_hex;
    while (tok >> order >> name >> uni_hex) {
      int uni = static_cast<int>(std::strtol(uni_hex.c_str(), nullptr, 16));
      int uni_name = AdobeGlyphList::NameToUnicode(name);
      int order_k = order[0] == '\'' ? static_cast<unsigned char>(order[1])
                                     : std::atoi(order.c_str());
      order_k %= 256;
      if (order_k < 0)
        continue;  // Java would throw ArrayIndexOutOfBounds; ignore the
                   // malformed entry
      unicode_to_code_[uni] = order_k;
      code_to_unicode_[order_k] = uni;
      differences_[order_k] = name;
      unicode_differences_[uni] = uni_name;
    }
  } else {
    int k = std::atoi(first.c_str());
    std::string hex;
    while (k >= 0 && k < 256 && tok >> hex) {
      int uni =
          static_cast<int>(std::strtol(hex.c_str(), nullptr, 16) % 0x10000);
      std::string name = AdobeGlyphList::UnicodeToName(uni);
      if (name.empty()) name = "uni" + hex;
      unicode_to_code_[uni] = k;
      code_to_unicode_[k] = uni;
      differences_[k] = name;
      unicode_differences_[uni] = uni;
      k++;
    }
  }
  for (int k = 0; k < 256; k++)
    if (differences_[k].empty()) differences_[k] = kNotdef;
}

void FontEncoding::FillNamedEncoding() {
  bool std_encoding = base_encoding_ == PdfEncodings::kWinansi ||
                      base_encoding_ == PdfEncodings::kMacroman;
  if (!std_encoding && !has_differences_) InitDifferences();
  for (int ch = 0; ch < 256; ++ch) {
    int uni = PdfEncodings::ByteToUnicode(ch, base_encoding_);
    std::string name =
        uni >= 0 ? AdobeGlyphList::UnicodeToName(uni) : std::string();
    if (name.empty()) {
      name = kNotdef;
    } else {
      unicode_to_code_[uni] = ch;
      code_to_unicode_[ch] = uni;
      unicode_differences_[uni] = uni;
    }
    if (has_differences_) differences_[ch] = name;
  }
}

void FontEncoding::FillStandardEncoding() {
  for (int ch = 0; ch < 256; ++ch) {
    int uni = PdfEncodings::StandardEncoding(ch);
    std::string name = AdobeGlyphList::UnicodeToName(uni);
    if (name.empty()) {
      name = kNotdef;
    } else {
      unicode_to_code_[uni] = ch;
      code_to_unicode_[ch] = uni;
      unicode_differences_[uni] = uni;
    }
    if (has_differences_) differences_[ch] = name;
  }
}

// ============================================================ FontProgram
int FontProgram::GetWidth(int unicode) const {
  const Glyph* glyph = GetGlyph(unicode);
  return glyph ? glyph->GetWidth() : 0;
}

const std::array<int, 4>* FontProgram::GetCharBBox(int unicode) const {
  const Glyph* glyph = GetGlyph(unicode);
  return glyph ? glyph->GetBbox() : nullptr;
}

const Glyph* FontProgram::GetGlyph(int unicode) const {
  auto it = unicode_to_glyph_.find(unicode);
  return it == unicode_to_glyph_.end() ? nullptr : &it->second;
}

const Glyph* FontProgram::GetGlyphByCode(int char_code) const {
  auto it = code_to_glyph_.find(char_code);
  return it == code_to_glyph_.end() ? nullptr : &it->second;
}

int FontProgram::GetKerning(int first, int second) const {
  const Glyph* a = GetGlyph(first);
  const Glyph* b = GetGlyph(second);
  if (!a || !b) return 0;
  return GetKerning(*a, *b);
}

void FontProgram::FixSpaceIssue() {
  auto it = unicode_to_glyph_.find(32);
  if (it != unicode_to_glyph_.end())
    code_to_glyph_[it->second.GetCode()] = it->second;
}

// ============================================================ StandardFonts
bool standard_fonts::IsStandardFont(const std::string& font_name) {
  for (const char* n :
       {kCourier, kCourierBold, kCourierBoldoblique, kCourierOblique,
        kHelvetica, kHelveticaBold, kHelveticaBoldoblique, kHelveticaOblique,
        kSymbol, kTimesBold, kTimesBolditalic, kTimesItalic, kTimesRoman,
        kZapfdingbats})
    if (font_name == n) return true;
  return false;
}

// ============================================================ Type1Font
static int FontWeightFromType1(const std::string& weight) {
  std::string w;
  for (char c : weight)
    w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (w == "ultralight") return 100;
  if (w == "thin" || w == "extralight") return 200;
  if (w == "light") return 300;
  if (w == "book" || w == "regular" || w == "normal") return 400;
  if (w == "medium") return 500;
  if (w == "demibold" || w == "semibold") return 600;
  if (w == "bold") return 700;
  if (w == "extrabold" || w == "ultrabold") return 800;
  if (w == "heavy" || w == "black" || w == "ultra" || w == "ultrablack")
    return 900;
  if (w == "fat" || w == "extrablack") return 900;
  return 400;
}

std::shared_ptr<Type1Font> Type1Font::CreateStandardFont(
    const std::string& name) {
  if (!standard_fonts::IsStandardFont(name))
    throw IoException(name + " is not a standard type1 font.");
  const Bytes* afm = ResourceUtil::GetResource("font/afm/" + name + ".afm");
  if (!afm)
    throw IoException(
        Format(io_exception_message_constant::kNotFoundAsFileOrResource,
               name + ".afm"));
  std::shared_ptr<Type1Font> font(new Type1Font());
  font->built_in_ = true;
  font->afm_path_ = name;
  font->Process(*afm);
  return font;
}

std::shared_ptr<Type1Font> Type1Font::CreateFromAfm(
    const Bytes& afm, const Bytes& pfb, const std::string& afm_path) {
  std::shared_ptr<Type1Font> font(new Type1Font());
  font->pfb_ = pfb;
  font->afm_path_ = afm_path;
  font->Process(afm);
  return font;
}

Type1Font::Type1Font(const std::string& base_font) {
  font_names_.SetFontName(base_font);
}

void Type1Font::InitializeGlyphs(const FontEncoding& font_encoding) {
  for (int i = 0; i < 256; i++) {
    int unicode = font_encoding.GetUnicode(i);
    auto it = unicode_to_glyph_.find(unicode);
    if (it == unicode_to_glyph_.end()) continue;
    Glyph glyph(i, it->second.GetWidth(), unicode, it->second.GetChars(),
                false);
    code_to_glyph_[i] = glyph;
    unicode_to_glyph_[glyph.GetUnicode()] = glyph;
  }
}

int Type1Font::GetPdfFontFlags() const {
  int flags = 0;
  if (font_metrics_.IsFixedPitch()) flags |= 1;
  flags |= IsFontSpecific() ? 4 : 32;
  if (font_metrics_.GetItalicAngle() < 0) flags |= 64;
  const std::string& n = font_names_.GetFontName();
  if (n.find("Caps") != std::string::npos ||
      (n.size() >= 2 && n.compare(n.size() - 2, 2, "SC") == 0))
    flags |= 131072;
  if (font_names_.IsBold() || font_names_.GetFontWeight() > 500)
    flags |= 262144;
  return flags;
}

int Type1Font::GetKerning(const Glyph& first, const Glyph& second) const {
  if (first.HasValidUnicode() && second.HasValidUnicode()) {
    int64_t record =
        (static_cast<int64_t>(first.GetUnicode()) << 32) + second.GetUnicode();
    auto it = kern_pairs_.find(record);
    return it == kern_pairs_.end() ? 0 : it->second;
  }
  return 0;
}

bool Type1Font::SetKerning(int first, int second, int kern) {
  kern_pairs_[(static_cast<int64_t>(first) << 32) + second] = kern;
  return true;
}

const Glyph* Type1Font::GetGlyphByName(const std::string& name) const {
  int unicode = AdobeGlyphList::NameToUnicode(name);
  return unicode != -1 ? GetGlyph(unicode) : nullptr;
}

Bytes Type1Font::GetFontStreamBytes(std::array<int, 3>& lengths) const {
  if (built_in_ || pfb_.size() < 18) return Bytes();
  static const int kPfbTypes[] = {1, 2, 1};
  Bytes out;
  size_t ptr = 0;
  for (int k = 0; k < 3; ++k) {
    if (ptr + 6 > pfb_.size() || pfb_[ptr] != 0x80 ||
        pfb_[ptr + 1] != kPfbTypes[k])
      return Bytes();
    size_t size = pfb_[ptr + 2] | (pfb_[ptr + 3] << 8) | (pfb_[ptr + 4] << 16) |
                  (static_cast<size_t>(pfb_[ptr + 5]) << 24);
    ptr += 6;
    if (ptr + size > pfb_.size()) return Bytes();
    lengths[k] = static_cast<int>(size);
    out.insert(out.end(), pfb_.begin() + ptr, pfb_.begin() + ptr + size);
    ptr += size;
  }
  return out;
}

static std::vector<std::string> SplitTokens(const std::string& s,
                                            const char* delims) {
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

static std::string RestOfLine(const std::string& line,
                              const std::string& ident) {
  std::string rest = line.substr(line.find(ident) + ident.size());
  size_t b = rest.find_first_not_of(" \t");
  if (b == std::string::npos) return std::string();
  rest = rest.substr(b);
  while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' '))
    rest.pop_back();
  return rest;
}

void Type1Font::Process(const Bytes& afm) {
  std::string text(afm.begin(), afm.end());
  std::istringstream in(text);
  std::string line;
  bool start_char_metrics = false;
  while (!start_char_metrics && std::getline(in, line)) {
    auto tok = SplitTokens(line, " ,\n\r\t\f");
    if (tok.empty()) continue;
    const std::string& ident = tok[0];
    auto num = [&](size_t i) {
      return i < tok.size() ? std::strtof(tok[i].c_str(), nullptr) : 0.f;
    };
    if (ident == "FontName")
      font_names_.SetFontName(RestOfLine(line, ident));
    else if (ident == "FullName")
      font_names_.SetFullName(RestOfLine(line, ident));
    else if (ident == "FamilyName")
      font_names_.SetFamilyName(RestOfLine(line, ident));
    else if (ident == "Weight")
      font_names_.SetFontWeight(FontWeightFromType1(RestOfLine(line, ident)));
    else if (ident == "ItalicAngle")
      font_metrics_.SetItalicAngle(num(1));
    else if (ident == "IsFixedPitch")
      font_metrics_.SetIsFixedPitch(tok.size() > 1 && tok[1] == "true");
    else if (ident == "CharacterSet")
      character_set_ = RestOfLine(line, ident);
    else if (ident == "FontBBox")
      font_metrics_.SetBbox(static_cast<int>(num(1)), static_cast<int>(num(2)),
                            static_cast<int>(num(3)), static_cast<int>(num(4)));
    else if (ident == "UnderlinePosition")
      font_metrics_.SetUnderlinePosition(static_cast<int>(num(1)));
    else if (ident == "UnderlineThickness")
      font_metrics_.SetUnderlineThickness(static_cast<int>(num(1)));
    else if (ident == "EncodingScheme")
      encoding_scheme_ = RestOfLine(line, ident);
    else if (ident == "CapHeight")
      font_metrics_.SetCapHeight(static_cast<int>(num(1)));
    else if (ident == "XHeight")
      font_metrics_.SetXHeight(static_cast<int>(num(1)));
    else if (ident == "Ascender")
      font_metrics_.SetTypoAscender(static_cast<int>(num(1)));
    else if (ident == "Descender")
      font_metrics_.SetTypoDescender(static_cast<int>(num(1)));
    else if (ident == "StdHW")
      font_metrics_.SetStemH(static_cast<int>(num(1)));
    else if (ident == "StdVW")
      font_metrics_.SetStemV(static_cast<int>(num(1)));
    else if (ident == "StartCharMetrics")
      start_char_metrics = true;
  }
  if (!start_char_metrics)
    throw IoException(Format(io_exception_message_constant::
                                 kStartCharMetricsIsMissingInTheMetricsFile,
                             afm_path_));
  avg_width_ = 0;
  int width_count = 0;
  bool end_char_metrics = false;
  while (std::getline(in, line)) {
    auto tok = SplitTokens(line, " \t\r");
    if (tok.empty()) continue;
    if (tok[0] == "EndCharMetrics") {
      end_char_metrics = true;
      break;
    }
    int c = -1, wx = 250;
    std::string n;
    std::array<int, 4> b{};
    bool has_b = false;
    for (const auto& part : SplitTokens(line, ";")) {
      auto t = SplitTokens(part, " \t\r");
      if (t.empty()) continue;
      if (t[0] == "C" && t.size() > 1)
        c = std::atoi(t[1].c_str());
      else if (t[0] == "WX" && t.size() > 1)
        wx = static_cast<int>(std::strtof(t[1].c_str(), nullptr));
      else if (t[0] == "N" && t.size() > 1)
        n = t[1];
      else if (t[0] == "B" && t.size() > 4) {
        b = {std::atoi(t[1].c_str()), std::atoi(t[2].c_str()),
             std::atoi(t[3].c_str()), std::atoi(t[4].c_str())};
        has_b = true;
      }
    }
    int unicode = AdobeGlyphList::NameToUnicode(n);
    Glyph glyph = has_b ? Glyph(c, wx, unicode, b) : Glyph(c, wx, unicode);
    if (c >= 0) code_to_glyph_[c] = glyph;
    if (unicode != -1) unicode_to_glyph_[unicode] = glyph;
    avg_width_ += wx;
    width_count++;
  }
  if (width_count != 0) avg_width_ /= width_count;
  if (!end_char_metrics)
    throw IoException(Format(
        io_exception_message_constant::kEndCharMetricsIsMissingInTheMetricsFile,
        afm_path_));
  if (!unicode_to_glyph_.count(0x00A0)) {
    auto sp = unicode_to_glyph_.find(0x0020);
    if (sp != unicode_to_glyph_.end()) {
      const auto* bb = sp->second.GetBbox();
      unicode_to_glyph_[0x00A0] =
          bb ? Glyph(sp->second.GetCode(), sp->second.GetWidth(), 0x00A0, *bb)
             : Glyph(sp->second.GetCode(), sp->second.GetWidth(), 0x00A0);
    }
  }
  bool start_kern_pairs = false;
  while (std::getline(in, line)) {
    auto tok = SplitTokens(line, " \t\r");
    if (tok.empty()) continue;
    if (tok[0] == "EndFontMetrics") break;
    if (tok[0] == "StartKernPairs") {
      start_kern_pairs = true;
      break;
    }
  }
  if (start_kern_pairs) {
    while (std::getline(in, line)) {
      auto tok = SplitTokens(line, " \t\r");
      if (tok.empty()) continue;
      if (tok[0] == "KPX" && tok.size() >= 4) {
        int first_uni = AdobeGlyphList::NameToUnicode(tok[1]);
        int second_uni = AdobeGlyphList::NameToUnicode(tok[2]);
        if (first_uni != -1 && second_uni != -1)
          kern_pairs_[(static_cast<int64_t>(first_uni) << 32) + second_uni] =
              static_cast<int>(std::strtof(tok[3].c_str(), nullptr));
      } else if (tok[0] == "EndKernPairs") {
        break;
      }
    }
  }
  is_font_specific_ = !(encoding_scheme_ == "AdobeStandardEncoding" ||
                        encoding_scheme_ == "StandardEncoding");
}

}  // namespace io
}  // namespace pdfcpp
