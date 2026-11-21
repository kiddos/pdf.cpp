#include "pdfcpp/io/font/TrueTypeFont.h"

#include <algorithm>
#include <cstring>

#include "pdfcpp/io/exceptions/IoException.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/io/util/TextUtil.h"

namespace pdfcpp {
namespace io {

TrueTypeFont::TrueTypeFont(const Bytes& ttf, int ttcIndex) : data_(ttf) { parse(ttcIndex); }

std::shared_ptr<TrueTypeFont> TrueTypeFont::createFromFile(const std::string& path, int ttcIndex) {
    auto font = std::make_shared<TrueTypeFont>(StreamUtil::readFile(path), ttcIndex);
    font->setPath(path);
    return font;
}

int TrueTypeFont::u8(size_t pos) const {
    if (pos >= data_.size()) throw IoException(IoExceptionMessageConstant::UNEXPECTED_END_OF_FILE);
    return data_[pos];
}
int TrueTypeFont::u16(size_t pos) const { return (u8(pos) << 8) | u8(pos + 1); }
int TrueTypeFont::s16(size_t pos) const { return static_cast<int16_t>(u16(pos)); }
uint32_t TrueTypeFont::u32(size_t pos) const {
    return (static_cast<uint32_t>(u16(pos)) << 16) | static_cast<uint32_t>(u16(pos + 2));
}

const TrueTypeFont::Table* TrueTypeFont::table(const std::string& tag) const {
    auto it = tables_.find(tag);
    return it == tables_.end() ? nullptr : &it->second;
}

void TrueTypeFont::parse(int ttcIndex) {
    if (data_.size() < 12) throw IoException(format(IoExceptionMessageConstant::NOT_A_TRUETYPE_OR_OPENTYPE_FONT, path_));
    if (std::memcmp(data_.data(), "ttcf", 4) == 0) {
        int numFonts = static_cast<int>(u32(8));
        if (ttcIndex < 0) ttcIndex = 0;
        if (ttcIndex >= numFonts) throw IoException(IoExceptionMessageConstant::TTC_INDEX_DOESNT_EXIST_IN_THIS_TTC_FILE);
        directoryOffset_ = u32(12 + 4 * ttcIndex);
    }
    uint32_t version = u32(directoryOffset_);
    if (version != 0x00010000 && version != 0x4F54544F /*OTTO*/ && version != 0x74727565 /*true*/)
        throw IoException(format(IoExceptionMessageConstant::NOT_A_TRUETYPE_OR_OPENTYPE_FONT, path_));
    int numTables = u16(directoryOffset_ + 4);
    for (int k = 0; k < numTables; ++k) {
        size_t rec = directoryOffset_ + 12 + 16 * static_cast<size_t>(k);
        if (rec + 16 > data_.size()) throw IoException(IoExceptionMessageConstant::UNEXPECTED_END_OF_FILE);
        std::string tag(reinterpret_cast<const char*>(&data_[rec]), 4);
        tables_[tag] = Table{u32(rec + 8), u32(rec + 12)};
    }
    cff_ = table("CFF ") != nullptr;

    const Table* head = table("head");
    if (!head) throw IoException(format(IoExceptionMessageConstant::TABLE_DOES_NOT_EXISTS_IN, "head", path_));
    unitsPerEm_ = u16(head->offset + 18);
    if (unitsPerEm_ == 0) unitsPerEm_ = 1000;
    const double norm = 1000.0 / unitsPerEm_;
    auto n = [norm](int v) { return static_cast<int>(v * norm); };
    fontMetrics_.setUnitsPerEm(unitsPerEm_);
    fontMetrics_.setBbox(n(s16(head->offset + 36)), n(s16(head->offset + 38)), n(s16(head->offset + 40)), n(s16(head->offset + 42)));
    int macStyle = u16(head->offset + 44);
    locaShortFormat_ = s16(head->offset + 50) == 0;

    const Table* hhea = table("hhea");
    if (!hhea) throw IoException(format(IoExceptionMessageConstant::TABLE_DOES_NOT_EXISTS_IN, "hhea", path_));
    fontMetrics_.setAscender(n(s16(hhea->offset + 4)));
    fontMetrics_.setDescender(n(s16(hhea->offset + 6)));
    fontMetrics_.setLineGap(n(s16(hhea->offset + 8)));
    fontMetrics_.setAdvanceWidthMax(n(u16(hhea->offset + 10)));
    int numberOfHMetrics = u16(hhea->offset + 34);

    int numGlyphs = 0;
    if (const Table* maxp = table("maxp")) numGlyphs = u16(maxp->offset + 4);
    fontMetrics_.setNumberOfGlyphs(numGlyphs);

    if (const Table* os2 = table("OS/2")) {
        size_t o = os2->offset;
        int os2Version = u16(o);
        fontNames_.setFontWeight(u16(o + 4));
        fontNames_.setAllowEmbedding((u16(o + 8) & 0x000E) != 2);
        fontMetrics_.setSubscriptSize(n(s16(o + 12)));
        fontMetrics_.setSubscriptOffset(-n(s16(o + 16)));
        fontMetrics_.setSuperscriptSize(n(s16(o + 20)));
        fontMetrics_.setSuperscriptOffset(n(s16(o + 24)));
        fontMetrics_.setStrikeoutSize(n(s16(o + 26)));
        fontMetrics_.setStrikeoutPosition(n(s16(o + 28)));
        int familyClass = s16(o + 30);
        panose_[0] = static_cast<uint8_t>(familyClass >> 8);
        panose_[1] = static_cast<uint8_t>(familyClass);
        for (int i = 0; i < 10; ++i) panose_[2 + i] = static_cast<uint8_t>(u8(o + 32 + i));
        fontMetrics_.setTypoAscender(n(s16(o + 68)));
        fontMetrics_.setTypoDescender(n(s16(o + 70)));
        fontMetrics_.setWinAscender(n(u16(o + 74)));
        fontMetrics_.setWinDescender(-n(u16(o + 76)));
        if (os2Version >= 2 && os2->length >= 90) {
            fontMetrics_.setXHeight(n(s16(o + 86)));
            fontMetrics_.setCapHeight(n(s16(o + 88)));
        } else {
            fontMetrics_.setCapHeight(static_cast<int>(0.7 * 1000));
        }
    } else {
        fontMetrics_.setTypoAscender(fontMetrics_.getAscender());
        fontMetrics_.setTypoDescender(fontMetrics_.getDescender());
        fontMetrics_.setWinAscender(fontMetrics_.getAscender());
        fontMetrics_.setWinDescender(-fontMetrics_.getDescender());
        fontMetrics_.setCapHeight(700);
    }
    if (const Table* post = table("post")) {
        int mantissa = s16(post->offset + 4);
        int fraction = u16(post->offset + 6);
        fontMetrics_.setItalicAngle(static_cast<float>(mantissa + fraction / 16384.0));
        int underlinePosition = n(s16(post->offset + 8));
        int underlineThickness = n(s16(post->offset + 10));
        fontMetrics_.setUnderlinePosition((underlinePosition - underlineThickness) / 2);
        fontMetrics_.setUnderlineThickness(underlineThickness);
        fontMetrics_.setIsFixedPitch(u32(post->offset + 12) != 0);
    }
    fontNames_.setMacStyle(macStyle);
    readNames();

    // horizontal metrics
    glyphWidthsByIndex_.assign(std::max(numGlyphs, numberOfHMetrics), 0);
    if (const Table* hmtx = table("hmtx")) {
        int lastWidth = 0;
        for (int k = 0; k < static_cast<int>(glyphWidthsByIndex_.size()); ++k) {
            if (k < numberOfHMetrics) {
                if (hmtx->offset + 4 * k + 2 > data_.size()) break;
                lastWidth = u16(hmtx->offset + 4 * k);
            }
            glyphWidthsByIndex_[k] = static_cast<int>(lastWidth * 1000.0 / unitsPerEm_);
        }
    }
    fontMetrics_.setGlyphWidths(glyphWidthsByIndex_);
    readCmaps();
    readKerning();
    std::vector<std::array<int, 4>> bboxes = readBboxes();

    avgWidth_ = 0;
    for (const auto& kv : activeCmap_) {
        int charCode = kv.first;
        int index = kv.second.first;
        if (index >= static_cast<int>(glyphWidthsByIndex_.size())) continue;
        Glyph glyph = index < static_cast<int>(bboxes.size()) ? Glyph(index, glyphWidthsByIndex_[index], charCode, bboxes[index])
                                                               : Glyph(index, glyphWidthsByIndex_[index], charCode);
        unicodeToGlyph_[charCode] = glyph;
        if (!codeToGlyph_.count(index)) codeToGlyph_[index] = glyph;
        avgWidth_ += glyph.getWidth();
    }
    fixSpaceIssue();
    for (int index = 0; index < static_cast<int>(glyphWidthsByIndex_.size()); index++) {
        if (codeToGlyph_.count(index)) continue;
        codeToGlyph_[index] = Glyph(index, glyphWidthsByIndex_[index], -1);
        avgWidth_ += glyphWidthsByIndex_[index];
    }
    if (!codeToGlyph_.empty()) avgWidth_ /= static_cast<int>(codeToGlyph_.size());
}

void TrueTypeFont::readNames() {
    const Table* name = table("name");
    if (!name) {
        fontNames_.setFontName(path_);
        return;
    }
    size_t o = name->offset;
    int count = u16(o + 2);
    size_t storage = o + u16(o + 4);
    auto& all = fontNames_.getAllNames();
    for (int k = 0; k < count; ++k) {
        size_t rec = o + 6 + 12 * k;
        int platformID = u16(rec), encodingID = u16(rec + 2), languageID = u16(rec + 4), nameID = u16(rec + 6);
        int length = u16(rec + 8), offset = u16(rec + 10);
        if (storage + offset + length > data_.size()) continue;
        Bytes raw(data_.begin() + storage + offset, data_.begin() + storage + offset + length);
        std::string value;
        if (platformID == 0 || platformID == 3 || (platformID == 2 && encodingID == 1))
            value = PdfEncodings::convertToString(raw, PdfEncodings::UNICODE_BIG_UNMARKED);
        else
            value = PdfEncodings::convertToString(raw, "");
        all[nameID].push_back({std::to_string(platformID), std::to_string(encodingID), std::to_string(languageID), value});
    }
    auto pick = [&](int id) -> std::string {
        auto it = all.find(id);
        if (it == all.end() || it->second.empty()) return std::string();
        for (const auto& r : it->second)
            if (r[0] == "3" && (r[2] == "1033" || r[2] == "0")) return r[3];
        return it->second[0][3];
    };
    std::string fontName = pick(6);
    if (fontName.empty()) fontName = pick(4);
    // remove spaces (PostScript names cannot contain spaces)
    std::string ps;
    for (char c : fontName)
        if (c != ' ') ps += c;
    fontNames_.setFontName(ps);
    fontNames_.setFullName(pick(4));
    std::string family = pick(16);
    fontNames_.setFamilyName(family.empty() ? pick(1) : family);
    std::string style = pick(17);
    fontNames_.setStyle(style.empty() ? pick(2) : style);
}

bool TrueTypeFont::isCmapPresent(int platformID, int encodingID) const { return cmapsPresent_.count({platformID, encodingID}) > 0; }

std::map<int, std::pair<int, int>> TrueTypeFont::readCmapSubtable(size_t offset) {
    std::map<int, std::pair<int, int>> h;
    int format = u16(offset);
    auto width = [&](int gid) { return gid < static_cast<int>(glyphWidthsByIndex_.size()) ? glyphWidthsByIndex_[gid] : 0; };
    switch (format) {
        case 0: {
            for (int k = 0; k < 256; ++k) {
                int gid = u8(offset + 6 + k);
                h[k] = {gid, width(gid)};
            }
            break;
        }
        case 4: {
            int segCount = u16(offset + 6) / 2;
            size_t endCodes = offset + 14;
            size_t startCodes = endCodes + 2 * segCount + 2;
            size_t idDeltas = startCodes + 2 * segCount;
            size_t idRangeOffsets = idDeltas + 2 * segCount;
            for (int k = 0; k < segCount; ++k) {
                int end = u16(endCodes + 2 * k);
                int start = u16(startCodes + 2 * k);
                int delta = s16(idDeltas + 2 * k);
                int ro = u16(idRangeOffsets + 2 * k);
                for (int j = start; j <= end && j != 0xFFFF; ++j) {
                    int glyph;
                    if (ro == 0) {
                        glyph = (j + delta) & 0xFFFF;
                    } else {
                        size_t addr = idRangeOffsets + 2 * k + ro + 2 * (j - start);
                        if (addr + 1 >= data_.size()) continue;
                        glyph = u16(addr);
                        if (glyph != 0) glyph = (glyph + delta) & 0xFFFF;
                    }
                    if (glyph == 0) continue;
                    h[j] = {glyph, width(glyph)};
                }
            }
            break;
        }
        case 6: {
            int start = u16(offset + 6);
            int count = u16(offset + 8);
            for (int k = 0; k < count; ++k) {
                int gid = u16(offset + 10 + 2 * k);
                h[start + k] = {gid, width(gid)};
            }
            break;
        }
        case 12: {
            uint32_t numGroups = u32(offset + 12);
            // Well-formed fonts have sorted, disjoint groups within the Unicode range, so the total number of mapped
            // codes is at most 0x110000. Bound the work on malformed (overlapping) groups.
            uint64_t budget = 2 * 0x110000ULL;
            for (uint32_t k = 0; k < numGroups && budget > 0; ++k) {
                size_t g = offset + 16 + 12 * static_cast<size_t>(k);
                uint32_t startChar = u32(g), endChar = u32(g + 4), startGlyph = u32(g + 8);
                if (endChar < startChar || startChar > 0x10FFFF) continue;
                endChar = std::min<uint32_t>(endChar, 0x10FFFF);  // also keeps `c <= endChar` from wrapping around
                if (endChar - startChar + 1ULL > budget) endChar = static_cast<uint32_t>(startChar + budget - 1);
                budget -= endChar - startChar + 1ULL;
                for (uint32_t c = startChar; c <= endChar; ++c) {
                    int gid = static_cast<int>(startGlyph + (c - startChar));
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

void TrueTypeFont::readCmaps() {
    const Table* cmap = table("cmap");
    if (!cmap) return;
    size_t o = cmap->offset;
    int numTables = u16(o + 2);
    size_t map10 = 0, map31 = 0, map30 = 0, map310 = 0, map0x = 0;
    for (int k = 0; k < numTables; ++k) {
        size_t rec = o + 4 + 8 * k;
        int platId = u16(rec), platSpecId = u16(rec + 2);
        size_t off = o + u32(rec + 4);
        cmapsPresent_.insert({platId, platSpecId});
        if (platId == 3 && platSpecId == 0) map30 = off;
        else if (platId == 3 && platSpecId == 1) map31 = off;
        else if (platId == 3 && platSpecId == 10) map310 = off;
        else if (platId == 1 && platSpecId == 0) map10 = off;
        else if (platId == 0) map0x = off;
    }
    if (map310) {
        activeCmap_ = readCmapSubtable(map310);
    } else if (map31) {
        activeCmap_ = readCmapSubtable(map31);
    } else if (map0x) {
        activeCmap_ = readCmapSubtable(map0x);
    } else if (map30) {
        isFontSpecific_ = true;
        auto raw = readCmapSubtable(map30);
        for (const auto& kv : raw) {
            activeCmap_[kv.first] = kv.second;
            if ((kv.first & 0xFF00) == 0xF000) activeCmap_[kv.first & 0xFF] = kv.second;
        }
    } else if (map10) {
        isFontSpecific_ = true;
        activeCmap_ = readCmapSubtable(map10);
    }
    if (map30 && !map31 && !map310) isFontSpecific_ = true;
}

void TrueTypeFont::readKerning() {
    const Table* kern = table("kern");
    if (!kern) return;
    size_t o = kern->offset;
    int nTables = u16(o + 2);
    size_t checkpoint = o + 4;
    for (int k = 0; k < nTables; ++k) {
        size_t sub = checkpoint;
        int length = u16(sub + 2);
        int coverage = u16(sub + 4);
        checkpoint += length;
        if ((coverage & 0xfff7) == 0x0001) {
            int nPairs = u16(sub + 6);
            for (int j = 0; j < nPairs; ++j) {
                size_t p = sub + 14 + 6 * j;
                int64_t pair = static_cast<int64_t>(u32(p));  // (left << 16) | right, unsigned: left GIDs may be >= 32768
                int value = s16(p + 4);
                kerning_[pair] = static_cast<int>(value * 1000.0 / unitsPerEm_);
            }
        }
    }
}

int TrueTypeFont::getKerning(const Glyph& first, const Glyph& second) const {
    auto it = kerning_.find((static_cast<int64_t>(first.getCode()) << 16) + second.getCode());
    return it == kerning_.end() ? 0 : it->second;
}

std::vector<std::array<int, 4>> TrueTypeFont::readBboxes() {
    std::vector<std::array<int, 4>> result;
    const Table* loca = table("loca");
    const Table* glyf = table("glyf");
    if (!loca || !glyf) return result;
    int entries = locaShortFormat_ ? static_cast<int>(loca->length / 2) : static_cast<int>(loca->length / 4);
    std::vector<uint32_t> locaTable(entries);
    for (int k = 0; k < entries; ++k)
        locaTable[k] = locaShortFormat_ ? static_cast<uint32_t>(u16(loca->offset + 2 * k)) * 2 : u32(loca->offset + 4 * k);
    double norm = 1000.0 / unitsPerEm_;
    result.resize(std::max(0, entries - 1));
    for (int glyph = 0; glyph + 1 < entries; ++glyph) {
        if (locaTable[glyph + 1] != locaTable[glyph]) {
            size_t p = glyf->offset + locaTable[glyph];
            if (p + 10 > data_.size()) continue;
            result[glyph] = {static_cast<int>(s16(p + 2) * norm), static_cast<int>(s16(p + 4) * norm),
                             static_cast<int>(s16(p + 6) * norm), static_cast<int>(s16(p + 8) * norm)};
        }
    }
    return result;
}

Bytes TrueTypeFont::getFontStreamBytes() const {
    if (directoryOffset_ == 0) return data_;
    // extract a single font from a collection
    std::set<int> all;
    for (int i = 0; i < fontMetrics_.getNumberOfGlyphs(); ++i) all.insert(i);
    return getSubset(all, false);
}

int TrueTypeFont::getPdfFontFlags() const {
    int flags = 0;
    if (fontMetrics_.isFixedPitch()) flags |= 1;
    flags |= isFontSpecific() ? 4 : 32;
    if (fontNames_.isItalic()) flags |= 64;
    if (fontNames_.isBold() || fontNames_.getFontWeight() > 500) flags |= 262144;
    return flags;
}

namespace {
void put16(Bytes& b, size_t pos, int v) {
    b[pos] = static_cast<uint8_t>(v >> 8);
    b[pos + 1] = static_cast<uint8_t>(v);
}
void put32(Bytes& b, size_t pos, uint32_t v) {
    b[pos] = static_cast<uint8_t>(v >> 24);
    b[pos + 1] = static_cast<uint8_t>(v >> 16);
    b[pos + 2] = static_cast<uint8_t>(v >> 8);
    b[pos + 3] = static_cast<uint8_t>(v);
}
uint32_t checksum(const Bytes& b) {
    uint32_t sum = 0;
    for (size_t i = 0; i < b.size(); i += 4) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4; ++k) v = (v << 8) | (i + k < b.size() ? b[i + k] : 0);
        sum += v;
    }
    return sum;
}
}  // namespace

Bytes TrueTypeFont::getSubset(const std::set<int>& glyphsIn, bool subsetTables) const {
    const Table* loca = table("loca");
    const Table* glyf = table("glyf");
    std::map<std::string, Bytes> out;
    auto tableBytes = [&](const Table& t) {
        return Bytes(data_.begin() + t.offset, data_.begin() + std::min<size_t>(data_.size(), t.offset + t.length));
    };
    if (loca && glyf && !cff_) {
        int entries = locaShortFormat_ ? static_cast<int>(loca->length / 2) : static_cast<int>(loca->length / 4);
        std::vector<uint32_t> locaTable(entries);
        for (int k = 0; k < entries; ++k)
            locaTable[k] = locaShortFormat_ ? static_cast<uint32_t>(u16(loca->offset + 2 * k)) * 2 : u32(loca->offset + 4 * k);
        std::set<int> glyphs(glyphsIn);
        glyphs.insert(0);
        // add composite glyph components
        std::vector<int> stack(glyphs.begin(), glyphs.end());
        while (!stack.empty()) {
            int g = stack.back();
            stack.pop_back();
            if (g < 0 || g + 1 >= entries) continue;
            uint32_t start = locaTable[g];
            if (start == locaTable[g + 1]) continue;
            size_t p = glyf->offset + start;
            if (s16(p) >= 0) continue;
            p += 10;
            while (true) {
                int flags = u16(p);
                int cgid = u16(p + 2);
                if (!glyphs.count(cgid)) {
                    glyphs.insert(cgid);
                    stack.push_back(cgid);
                }
                if ((flags & 32) == 0) break;  // MORE_COMPONENTS
                int skip = 4;
                skip += (flags & 1) ? 4 : 2;  // ARG_1_AND_2_ARE_WORDS
                if (flags & 8) skip += 2;       // WE_HAVE_A_SCALE
                else if (flags & 64) skip += 4; // X_AND_Y_SCALE
                else if (flags & 128) skip += 8; // TWO_BY_TWO
                p += skip;
            }
        }
        Bytes newGlyf;
        Bytes newLoca(4 * entries, 0);
        for (int g = 0; g + 1 < entries; ++g) {
            put32(newLoca, 4 * g, static_cast<uint32_t>(newGlyf.size()));
            if (glyphs.count(g)) {
                uint32_t s = locaTable[g], e = locaTable[g + 1];
                if (e > s && glyf->offset + e <= data_.size()) {
                    newGlyf.insert(newGlyf.end(), data_.begin() + glyf->offset + s, data_.begin() + glyf->offset + e);
                    while (newGlyf.size() % 4) newGlyf.push_back(0);
                }
            }
        }
        put32(newLoca, 4 * (entries - 1), static_cast<uint32_t>(newGlyf.size()));
        out["glyf"] = newGlyf;
        out["loca"] = newLoca;
    }
    static const char* SUBSET_TABLES[] = {"cvt ", "fpgm", "head", "hhea", "hmtx", "maxp", "prep", "cmap", "OS/2", "name", "post"};
    if (subsetTables) {
        for (const char* t : SUBSET_TABLES)
            if (const Table* tb = table(t)) out[t] = tableBytes(*tb);
    } else {
        for (const auto& kv : tables_)
            if (!out.count(kv.first)) out[kv.first] = tableBytes(kv.second);
    }
    if (out.count("head") && out.count("loca")) {
        put16(out["head"], 50, 1);  // long loca
        put32(out["head"], 8, 0);   // checkSumAdjustment
    }
    // assemble
    int numTables = static_cast<int>(out.size());
    int searchRange = 1, entrySelector = 0;
    while (searchRange * 2 <= numTables) {
        searchRange *= 2;
        ++entrySelector;
    }
    searchRange *= 16;
    Bytes font(12 + 16 * numTables, 0);
    put32(font, 0, cff_ ? 0x4F54544F : 0x00010000);
    put16(font, 4, numTables);
    put16(font, 6, searchRange);
    put16(font, 8, entrySelector);
    put16(font, 10, numTables * 16 - searchRange);
    size_t dir = 12;
    for (auto& kv : out) {
        Bytes& tb = kv.second;
        uint32_t offset = static_cast<uint32_t>(font.size());
        std::memcpy(&font[dir], kv.first.data(), 4);
        put32(font, dir + 4, checksum(tb));
        put32(font, dir + 8, offset);
        put32(font, dir + 12, static_cast<uint32_t>(tb.size()));
        dir += 16;
        font.insert(font.end(), tb.begin(), tb.end());
        while (font.size() % 4) font.push_back(0);
    }
    return font;
}

// ============================================================ FontProgramFactory
std::shared_ptr<FontProgram> FontProgramFactory::createFont(const std::string& fontProgram) {
    if (StandardFonts::isStandardFont(fontProgram)) return Type1Font::createStandardFont(fontProgram);
    std::string lower = fontProgram;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    int ttcIndex = -1;
    std::string path = fontProgram;
    auto comma = lower.find(".ttc,");
    if (comma != std::string::npos) {
        ttcIndex = std::atoi(fontProgram.c_str() + comma + 5);
        path = fontProgram.substr(0, comma + 4);
    }
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".afm") == 0) {
        Bytes afm = StreamUtil::readFile(fontProgram);
        Bytes pfb;
        std::string pfbPath = fontProgram.substr(0, fontProgram.size() - 4) + ".pfb";
        try {
            pfb = StreamUtil::readFile(pfbPath);
        } catch (...) {
        }
        return Type1Font::createFromAfm(afm, pfb, fontProgram);
    }
    auto font = TrueTypeFont::createFromFile(path, ttcIndex);
    font->setPath(fontProgram);
    return font;
}

std::shared_ptr<FontProgram> FontProgramFactory::createFont(const Bytes& fontProgram) {
    static const char kAfmMagic[] = "StartFontMetrics";
    const size_t magicLen = sizeof(kAfmMagic) - 1;
    if (fontProgram.size() >= magicLen && std::memcmp(fontProgram.data(), kAfmMagic, magicLen) == 0)
        return Type1Font::createFromAfm(fontProgram);
    return std::make_shared<TrueTypeFont>(fontProgram);
}

}  // namespace io
}  // namespace pdfcpp
