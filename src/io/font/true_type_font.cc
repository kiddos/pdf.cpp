#include "pdfcpp/io/font/true_type_font.h"

#include <algorithm>
#include <cstring>

#include "pdfcpp/io/exceptions/io_exception.h"
#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/io/util/text_util.h"

namespace pdfcpp {
namespace io {

TrueTypeFont::TrueTypeFont(const Bytes& ttf, int ttc_index) : data_(ttf) {
  Parse(ttc_index);
}

std::shared_ptr<TrueTypeFont> TrueTypeFont::CreateFromFile(
    const std::string& path, int ttc_index) {
  auto font =
      std::make_shared<TrueTypeFont>(StreamUtil::ReadFile(path), ttc_index);
  font->SetPath(path);
  return font;
}

int TrueTypeFont::U8(size_t pos) const {
  if (pos >= data_.size())
    throw IoException(io_exception_message_constant::kUnexpectedEndOfFile);
  return data_[pos];
}
int TrueTypeFont::U16(size_t pos) const { return (U8(pos) << 8) | U8(pos + 1); }
int TrueTypeFont::S16(size_t pos) const {
  return static_cast<int16_t>(U16(pos));
}
uint32_t TrueTypeFont::U32(size_t pos) const {
  return (static_cast<uint32_t>(U16(pos)) << 16) |
         static_cast<uint32_t>(U16(pos + 2));
}

const TrueTypeFont::Table* TrueTypeFont::GetTable(
    const std::string& tag) const {
  auto it = tables_.find(tag);
  return it == tables_.end() ? nullptr : &it->second;
}

void TrueTypeFont::Parse(int ttc_index) {
  if (data_.size() < 12)
    throw IoException(Format(
        io_exception_message_constant::kNotATruetypeOrOpentypeFont, path_));
  if (std::memcmp(data_.data(), "ttcf", 4) == 0) {
    int num_fonts = static_cast<int>(U32(8));
    if (ttc_index < 0) ttc_index = 0;
    if (ttc_index >= num_fonts)
      throw IoException(
          io_exception_message_constant::kTtcIndexDoesntExistInThisTtcFile);
    directory_offset_ = U32(12 + 4 * ttc_index);
  }
  uint32_t version = U32(directory_offset_);
  if (version != 0x00010000 && version != 0x4F54544F /*OTTO*/ &&
      version != 0x74727565 /*true*/)
    throw IoException(Format(
        io_exception_message_constant::kNotATruetypeOrOpentypeFont, path_));
  int num_tables = U16(directory_offset_ + 4);
  for (int k = 0; k < num_tables; ++k) {
    size_t rec = directory_offset_ + 12 + 16 * static_cast<size_t>(k);
    if (rec + 16 > data_.size())
      throw IoException(io_exception_message_constant::kUnexpectedEndOfFile);
    std::string tag(reinterpret_cast<const char*>(&data_[rec]), 4);
    tables_[tag] = Table{U32(rec + 8), U32(rec + 12)};
  }
  cff_ = GetTable("CFF ") != nullptr;

  const Table* head = GetTable("head");
  if (!head)
    throw IoException(Format(
        io_exception_message_constant::kTableDoesNotExistsIn, "head", path_));
  units_per_em_ = U16(head->offset + 18);
  if (units_per_em_ == 0) units_per_em_ = 1000;
  const double norm = 1000.0 / units_per_em_;
  auto n = [norm](int v) { return static_cast<int>(v * norm); };
  font_metrics_.SetUnitsPerEm(units_per_em_);
  font_metrics_.SetBbox(n(S16(head->offset + 36)), n(S16(head->offset + 38)),
                        n(S16(head->offset + 40)), n(S16(head->offset + 42)));
  int mac_style = U16(head->offset + 44);
  loca_short_format_ = S16(head->offset + 50) == 0;

  const Table* hhea = GetTable("hhea");
  if (!hhea)
    throw IoException(Format(
        io_exception_message_constant::kTableDoesNotExistsIn, "hhea", path_));
  font_metrics_.SetAscender(n(S16(hhea->offset + 4)));
  font_metrics_.SetDescender(n(S16(hhea->offset + 6)));
  font_metrics_.SetLineGap(n(S16(hhea->offset + 8)));
  font_metrics_.SetAdvanceWidthMax(n(U16(hhea->offset + 10)));
  int number_of_h_metrics = U16(hhea->offset + 34);

  int num_glyphs = 0;
  if (const Table* maxp = GetTable("maxp")) num_glyphs = U16(maxp->offset + 4);
  font_metrics_.SetNumberOfGlyphs(num_glyphs);

  if (const Table* os2 = GetTable("OS/2")) {
    size_t o = os2->offset;
    int os2_version = U16(o);
    font_names_.SetFontWeight(U16(o + 4));
    font_names_.SetAllowEmbedding((U16(o + 8) & 0x000E) != 2);
    font_metrics_.SetSubscriptSize(n(S16(o + 12)));
    font_metrics_.SetSubscriptOffset(-n(S16(o + 16)));
    font_metrics_.SetSuperscriptSize(n(S16(o + 20)));
    font_metrics_.SetSuperscriptOffset(n(S16(o + 24)));
    font_metrics_.SetStrikeoutSize(n(S16(o + 26)));
    font_metrics_.SetStrikeoutPosition(n(S16(o + 28)));
    int family_class = S16(o + 30);
    panose_[0] = static_cast<uint8_t>(family_class >> 8);
    panose_[1] = static_cast<uint8_t>(family_class);
    for (int i = 0; i < 10; ++i)
      panose_[2 + i] = static_cast<uint8_t>(U8(o + 32 + i));
    font_metrics_.SetTypoAscender(n(S16(o + 68)));
    font_metrics_.SetTypoDescender(n(S16(o + 70)));
    font_metrics_.SetWinAscender(n(U16(o + 74)));
    font_metrics_.SetWinDescender(-n(U16(o + 76)));
    if (os2_version >= 2 && os2->length >= 90) {
      font_metrics_.SetXHeight(n(S16(o + 86)));
      font_metrics_.SetCapHeight(n(S16(o + 88)));
    } else {
      font_metrics_.SetCapHeight(static_cast<int>(0.7 * 1000));
    }
  } else {
    font_metrics_.SetTypoAscender(font_metrics_.GetAscender());
    font_metrics_.SetTypoDescender(font_metrics_.GetDescender());
    font_metrics_.SetWinAscender(font_metrics_.GetAscender());
    font_metrics_.SetWinDescender(-font_metrics_.GetDescender());
    font_metrics_.SetCapHeight(700);
  }
  if (const Table* post = GetTable("post")) {
    int mantissa = S16(post->offset + 4);
    int fraction = U16(post->offset + 6);
    font_metrics_.SetItalicAngle(
        static_cast<float>(mantissa + fraction / 16384.0));
    int underline_position = n(S16(post->offset + 8));
    int underline_thickness = n(S16(post->offset + 10));
    font_metrics_.SetUnderlinePosition(
        (underline_position - underline_thickness) / 2);
    font_metrics_.SetUnderlineThickness(underline_thickness);
    font_metrics_.SetIsFixedPitch(U32(post->offset + 12) != 0);
  }
  font_names_.SetMacStyle(mac_style);
  ReadNames();

  // horizontal metrics
  glyph_widths_by_index_.assign(std::max(num_glyphs, number_of_h_metrics), 0);
  if (const Table* hmtx = GetTable("hmtx")) {
    int last_width = 0;
    for (int k = 0; k < static_cast<int>(glyph_widths_by_index_.size()); ++k) {
      if (k < number_of_h_metrics) {
        if (hmtx->offset + 4 * k + 2 > data_.size()) break;
        last_width = U16(hmtx->offset + 4 * k);
      }
      glyph_widths_by_index_[k] =
          static_cast<int>(last_width * 1000.0 / units_per_em_);
    }
  }
  font_metrics_.SetGlyphWidths(glyph_widths_by_index_);
  ReadCmaps();
  ReadKerning();
  std::vector<std::array<int, 4>> bboxes = ReadBboxes();

  avg_width_ = 0;
  for (const auto& kv : active_cmap_) {
    int char_code = kv.first;
    int index = kv.second.first;
    if (index >= static_cast<int>(glyph_widths_by_index_.size())) continue;
    Glyph glyph = index < static_cast<int>(bboxes.size())
                      ? Glyph(index, glyph_widths_by_index_[index], char_code,
                              bboxes[index])
                      : Glyph(index, glyph_widths_by_index_[index], char_code);
    unicode_to_glyph_[char_code] = glyph;
    if (!code_to_glyph_.count(index)) code_to_glyph_[index] = glyph;
    avg_width_ += glyph.GetWidth();
  }
  FixSpaceIssue();
  for (int index = 0; index < static_cast<int>(glyph_widths_by_index_.size());
       index++) {
    if (code_to_glyph_.count(index)) continue;
    code_to_glyph_[index] = Glyph(index, glyph_widths_by_index_[index], -1);
    avg_width_ += glyph_widths_by_index_[index];
  }
  if (!code_to_glyph_.empty())
    avg_width_ /= static_cast<int>(code_to_glyph_.size());
}

void TrueTypeFont::ReadNames() {
  const Table* name = GetTable("name");
  if (!name) {
    font_names_.SetFontName(path_);
    return;
  }
  size_t o = name->offset;
  int count = U16(o + 2);
  size_t storage = o + U16(o + 4);
  auto& all = font_names_.GetAllNames();
  for (int k = 0; k < count; ++k) {
    size_t rec = o + 6 + 12 * k;
    int platform_id = U16(rec), encoding_id = U16(rec + 2),
        language_id = U16(rec + 4), name_id = U16(rec + 6);
    int length = U16(rec + 8), offset = U16(rec + 10);
    if (storage + offset + length > data_.size()) continue;
    Bytes raw(data_.begin() + storage + offset,
              data_.begin() + storage + offset + length);
    std::string value;
    if (platform_id == 0 || platform_id == 3 ||
        (platform_id == 2 && encoding_id == 1))
      value =
          PdfEncodings::ConvertToString(raw, PdfEncodings::kUnicodeBigUnmarked);
    else
      value = PdfEncodings::ConvertToString(raw, "");
    all[name_id].push_back({std::to_string(platform_id),
                            std::to_string(encoding_id),
                            std::to_string(language_id), value});
  }
  auto pick = [&](int id) -> std::string {
    auto it = all.find(id);
    if (it == all.end() || it->second.empty()) return std::string();
    for (const auto& r : it->second)
      if (r[0] == "3" && (r[2] == "1033" || r[2] == "0")) return r[3];
    return it->second[0][3];
  };
  std::string font_name = pick(6);
  if (font_name.empty()) font_name = pick(4);
  // remove spaces (PostScript names cannot contain spaces)
  std::string ps;
  for (char c : font_name)
    if (c != ' ') ps += c;
  font_names_.SetFontName(ps);
  font_names_.SetFullName(pick(4));
  std::string family = pick(16);
  font_names_.SetFamilyName(family.empty() ? pick(1) : family);
  std::string style = pick(17);
  font_names_.SetStyle(style.empty() ? pick(2) : style);
}

bool TrueTypeFont::IsCmapPresent(int platform_id, int encoding_id) const {
  return cmaps_present_.count({platform_id, encoding_id}) > 0;
}

std::map<int, std::pair<int, int>> TrueTypeFont::ReadCmapSubtable(
    size_t offset) {
  std::map<int, std::pair<int, int>> h;
  int format = U16(offset);
  auto width = [&](int gid) {
    return gid < static_cast<int>(glyph_widths_by_index_.size())
               ? glyph_widths_by_index_[gid]
               : 0;
  };
  switch (format) {
    case 0: {
      for (int k = 0; k < 256; ++k) {
        int gid = U8(offset + 6 + k);
        h[k] = {gid, width(gid)};
      }
      break;
    }
    case 4: {
      int seg_count = U16(offset + 6) / 2;
      size_t end_codes = offset + 14;
      size_t start_codes = end_codes + 2 * seg_count + 2;
      size_t id_deltas = start_codes + 2 * seg_count;
      size_t id_range_offsets = id_deltas + 2 * seg_count;
      for (int k = 0; k < seg_count; ++k) {
        int end = U16(end_codes + 2 * k);
        int start = U16(start_codes + 2 * k);
        int delta = S16(id_deltas + 2 * k);
        int ro = U16(id_range_offsets + 2 * k);
        for (int j = start; j <= end && j != 0xFFFF; ++j) {
          int glyph;
          if (ro == 0) {
            glyph = (j + delta) & 0xFFFF;
          } else {
            size_t addr = id_range_offsets + 2 * k + ro + 2 * (j - start);
            if (addr + 1 >= data_.size()) continue;
            glyph = U16(addr);
            if (glyph != 0) glyph = (glyph + delta) & 0xFFFF;
          }
          if (glyph == 0) continue;
          h[j] = {glyph, width(glyph)};
        }
      }
      break;
    }
    case 6: {
      int start = U16(offset + 6);
      int count = U16(offset + 8);
      for (int k = 0; k < count; ++k) {
        int gid = U16(offset + 10 + 2 * k);
        h[start + k] = {gid, width(gid)};
      }
      break;
    }
    case 12: {
      uint32_t num_groups = U32(offset + 12);
      // Well-formed fonts have sorted, disjoint groups within the Unicode
      // range, so the total number of mapped codes is at most 0x110000. Bound
      // the work on malformed (overlapping) groups.
      uint64_t budget = 2 * 0x110000ULL;
      for (uint32_t k = 0; k < num_groups && budget > 0; ++k) {
        size_t g = offset + 16 + 12 * static_cast<size_t>(k);
        uint32_t start_char = U32(g), end_char = U32(g + 4),
                 start_glyph = U32(g + 8);
        if (end_char < start_char || start_char > 0x10FFFF) continue;
        end_char = std::min<uint32_t>(
            end_char,
            0x10FFFF);  // also keeps `c <= endChar` from wrapping around
        if (end_char - start_char + 1ULL > budget)
          end_char = static_cast<uint32_t>(start_char + budget - 1);
        budget -= end_char - start_char + 1ULL;
        for (uint32_t c = start_char; c <= end_char; ++c) {
          int gid = static_cast<int>(start_glyph + (c - start_char));
          h[static_cast<int>(c)] = {gid, width(gid)};
        }
      }
      break;
    }
    default:
      break;
  }
  return h;
}

void TrueTypeFont::ReadCmaps() {
  const Table* cmap = GetTable("cmap");
  if (!cmap) return;
  size_t o = cmap->offset;
  int num_tables = U16(o + 2);
  size_t map10 = 0, map31 = 0, map30 = 0, map310 = 0, map0x = 0;
  for (int k = 0; k < num_tables; ++k) {
    size_t rec = o + 4 + 8 * k;
    int plat_id = U16(rec), plat_spec_id = U16(rec + 2);
    size_t off = o + U32(rec + 4);
    cmaps_present_.insert({plat_id, plat_spec_id});
    if (plat_id == 3 && plat_spec_id == 0)
      map30 = off;
    else if (plat_id == 3 && plat_spec_id == 1)
      map31 = off;
    else if (plat_id == 3 && plat_spec_id == 10)
      map310 = off;
    else if (plat_id == 1 && plat_spec_id == 0)
      map10 = off;
    else if (plat_id == 0)
      map0x = off;
  }
  if (map310) {
    active_cmap_ = ReadCmapSubtable(map310);
  } else if (map31) {
    active_cmap_ = ReadCmapSubtable(map31);
  } else if (map0x) {
    active_cmap_ = ReadCmapSubtable(map0x);
  } else if (map30) {
    is_font_specific_ = true;
    auto raw = ReadCmapSubtable(map30);
    for (const auto& kv : raw) {
      active_cmap_[kv.first] = kv.second;
      if ((kv.first & 0xFF00) == 0xF000)
        active_cmap_[kv.first & 0xFF] = kv.second;
    }
  } else if (map10) {
    is_font_specific_ = true;
    active_cmap_ = ReadCmapSubtable(map10);
  }
  if (map30 && !map31 && !map310) is_font_specific_ = true;
}

void TrueTypeFont::ReadKerning() {
  const Table* kern = GetTable("kern");
  if (!kern) return;
  size_t o = kern->offset;
  int n_tables = U16(o + 2);
  size_t checkpoint = o + 4;
  for (int k = 0; k < n_tables; ++k) {
    size_t sub = checkpoint;
    int length = U16(sub + 2);
    int coverage = U16(sub + 4);
    checkpoint += length;
    if ((coverage & 0xfff7) == 0x0001) {
      int n_pairs = U16(sub + 6);
      for (int j = 0; j < n_pairs; ++j) {
        size_t p = sub + 14 + 6 * j;
        int64_t pair = static_cast<int64_t>(U32(
            p));  // (left << 16) | right, unsigned: left GIDs may be >= 32768
        int value = S16(p + 4);
        kerning_[pair] = static_cast<int>(value * 1000.0 / units_per_em_);
      }
    }
  }
}

int TrueTypeFont::GetKerning(const Glyph& first, const Glyph& second) const {
  auto it = kerning_.find((static_cast<int64_t>(first.GetCode()) << 16) +
                          second.GetCode());
  return it == kerning_.end() ? 0 : it->second;
}

std::vector<std::array<int, 4>> TrueTypeFont::ReadBboxes() {
  std::vector<std::array<int, 4>> result;
  const Table* loca = GetTable("loca");
  const Table* glyf = GetTable("glyf");
  if (!loca || !glyf) return result;
  int entries = loca_short_format_ ? static_cast<int>(loca->length / 2)
                                   : static_cast<int>(loca->length / 4);
  std::vector<uint32_t> loca_table(entries);
  for (int k = 0; k < entries; ++k)
    loca_table[k] = loca_short_format_
                        ? static_cast<uint32_t>(U16(loca->offset + 2 * k)) * 2
                        : U32(loca->offset + 4 * k);
  double norm = 1000.0 / units_per_em_;
  result.resize(std::max(0, entries - 1));
  for (int glyph = 0; glyph + 1 < entries; ++glyph) {
    if (loca_table[glyph + 1] != loca_table[glyph]) {
      size_t p = glyf->offset + loca_table[glyph];
      if (p + 10 > data_.size()) continue;
      result[glyph] = {static_cast<int>(S16(p + 2) * norm),
                       static_cast<int>(S16(p + 4) * norm),
                       static_cast<int>(S16(p + 6) * norm),
                       static_cast<int>(S16(p + 8) * norm)};
    }
  }
  return result;
}

Bytes TrueTypeFont::GetFontStreamBytes() const {
  if (directory_offset_ == 0) return data_;
  // extract a single font from a collection
  std::set<int> all;
  for (int i = 0; i < font_metrics_.GetNumberOfGlyphs(); ++i) all.insert(i);
  return GetSubset(all, false);
}

int TrueTypeFont::GetPdfFontFlags() const {
  int flags = 0;
  if (font_metrics_.IsFixedPitch()) flags |= 1;
  flags |= IsFontSpecific() ? 4 : 32;
  if (font_names_.IsItalic()) flags |= 64;
  if (font_names_.IsBold() || font_names_.GetFontWeight() > 500)
    flags |= 262144;
  return flags;
}

namespace {
void Put16(Bytes& b, size_t pos, int v) {
  b[pos] = static_cast<uint8_t>(v >> 8);
  b[pos + 1] = static_cast<uint8_t>(v);
}
void Put32(Bytes& b, size_t pos, uint32_t v) {
  b[pos] = static_cast<uint8_t>(v >> 24);
  b[pos + 1] = static_cast<uint8_t>(v >> 16);
  b[pos + 2] = static_cast<uint8_t>(v >> 8);
  b[pos + 3] = static_cast<uint8_t>(v);
}
uint32_t Checksum(const Bytes& b) {
  uint32_t sum = 0;
  for (size_t i = 0; i < b.size(); i += 4) {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; ++k)
      v = (v << 8) | (i + k < b.size() ? b[i + k] : 0);
    sum += v;
  }
  return sum;
}
}  // namespace

Bytes TrueTypeFont::GetSubset(const std::set<int>& glyphs_in,
                              bool subset_tables) const {
  const Table* loca = GetTable("loca");
  const Table* glyf = GetTable("glyf");
  std::map<std::string, Bytes> out;
  auto table_bytes = [&](const Table& t) {
    return Bytes(
        data_.begin() + t.offset,
        data_.begin() + std::min<size_t>(data_.size(), t.offset + t.length));
  };
  if (loca && glyf && !cff_) {
    int entries = loca_short_format_ ? static_cast<int>(loca->length / 2)
                                     : static_cast<int>(loca->length / 4);
    std::vector<uint32_t> loca_table(entries);
    for (int k = 0; k < entries; ++k)
      loca_table[k] = loca_short_format_
                          ? static_cast<uint32_t>(U16(loca->offset + 2 * k)) * 2
                          : U32(loca->offset + 4 * k);
    std::set<int> glyphs(glyphs_in);
    glyphs.insert(0);
    // add composite glyph components
    std::vector<int> stack(glyphs.begin(), glyphs.end());
    while (!stack.empty()) {
      int g = stack.back();
      stack.pop_back();
      if (g < 0 || g + 1 >= entries) continue;
      uint32_t start = loca_table[g];
      if (start == loca_table[g + 1]) continue;
      size_t p = glyf->offset + start;
      if (S16(p) >= 0) continue;
      p += 10;
      while (true) {
        int flags = U16(p);
        int cgid = U16(p + 2);
        if (!glyphs.count(cgid)) {
          glyphs.insert(cgid);
          stack.push_back(cgid);
        }
        if ((flags & 32) == 0) break;  // MORE_COMPONENTS
        int skip = 4;
        skip += (flags & 1) ? 4 : 2;  // ARG_1_AND_2_ARE_WORDS
        if (flags & 8)
          skip += 2;  // WE_HAVE_A_SCALE
        else if (flags & 64)
          skip += 4;  // X_AND_Y_SCALE
        else if (flags & 128)
          skip += 8;  // TWO_BY_TWO
        p += skip;
      }
    }
    Bytes new_glyf;
    Bytes new_loca(4 * entries, 0);
    for (int g = 0; g + 1 < entries; ++g) {
      Put32(new_loca, 4 * g, static_cast<uint32_t>(new_glyf.size()));
      if (glyphs.count(g)) {
        uint32_t s = loca_table[g], e = loca_table[g + 1];
        if (e > s && glyf->offset + e <= data_.size()) {
          new_glyf.insert(new_glyf.end(), data_.begin() + glyf->offset + s,
                          data_.begin() + glyf->offset + e);
          while (new_glyf.size() % 4) new_glyf.push_back(0);
        }
      }
    }
    Put32(new_loca, 4 * (entries - 1), static_cast<uint32_t>(new_glyf.size()));
    out["glyf"] = new_glyf;
    out["loca"] = new_loca;
  }
  static const char* const kSubsetTables[] = {"cvt ", "fpgm", "head", "hhea",
                                              "hmtx", "maxp", "prep", "cmap",
                                              "OS/2", "name", "post"};
  if (subset_tables) {
    for (const char* t : kSubsetTables)
      if (const Table* tb = GetTable(t)) out[t] = table_bytes(*tb);
  } else {
    for (const auto& kv : tables_)
      if (!out.count(kv.first)) out[kv.first] = table_bytes(kv.second);
  }
  if (out.count("head") && out.count("loca")) {
    Put16(out["head"], 50, 1);  // long loca
    Put32(out["head"], 8, 0);   // checkSumAdjustment
  }
  // assemble
  int num_tables = static_cast<int>(out.size());
  int search_range = 1, entry_selector = 0;
  while (search_range * 2 <= num_tables) {
    search_range *= 2;
    ++entry_selector;
  }
  search_range *= 16;
  Bytes font(12 + 16 * num_tables, 0);
  Put32(font, 0, cff_ ? 0x4F54544F : 0x00010000);
  Put16(font, 4, num_tables);
  Put16(font, 6, search_range);
  Put16(font, 8, entry_selector);
  Put16(font, 10, num_tables * 16 - search_range);
  size_t dir = 12;
  for (auto& kv : out) {
    Bytes& tb = kv.second;
    uint32_t offset = static_cast<uint32_t>(font.size());
    std::memcpy(&font[dir], kv.first.data(), 4);
    Put32(font, dir + 4, Checksum(tb));
    Put32(font, dir + 8, offset);
    Put32(font, dir + 12, static_cast<uint32_t>(tb.size()));
    dir += 16;
    font.insert(font.end(), tb.begin(), tb.end());
    while (font.size() % 4) font.push_back(0);
  }
  return font;
}

// ============================================================
// FontProgramFactory
std::shared_ptr<FontProgram> FontProgramFactory::CreateFont(
    const std::string& font_program) {
  if (standard_fonts::IsStandardFont(font_program))
    return Type1Font::CreateStandardFont(font_program);
  std::string lower = font_program;
  for (auto& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  int ttc_index = -1;
  std::string path = font_program;
  auto comma = lower.find(".ttc,");
  if (comma != std::string::npos) {
    ttc_index = std::atoi(font_program.c_str() + comma + 5);
    path = font_program.substr(0, comma + 4);
  }
  if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".afm") == 0) {
    Bytes afm = StreamUtil::ReadFile(font_program);
    Bytes pfb;
    std::string pfb_path =
        font_program.substr(0, font_program.size() - 4) + ".pfb";
    try {
      pfb = StreamUtil::ReadFile(pfb_path);
    } catch (...) {
    }
    return Type1Font::CreateFromAfm(afm, pfb, font_program);
  }
  auto font = TrueTypeFont::CreateFromFile(path, ttc_index);
  font->SetPath(font_program);
  return font;
}

std::shared_ptr<FontProgram> FontProgramFactory::CreateFont(
    const Bytes& font_program) {
  static const char kAfmMagic[] = "StartFontMetrics";
  const size_t magic_len = sizeof(kAfmMagic) - 1;
  if (font_program.size() >= magic_len &&
      std::memcmp(font_program.data(), kAfmMagic, magic_len) == 0)
    return Type1Font::CreateFromAfm(font_program);
  return std::make_shared<TrueTypeFont>(font_program);
}

}  // namespace io
}  // namespace pdfcpp
