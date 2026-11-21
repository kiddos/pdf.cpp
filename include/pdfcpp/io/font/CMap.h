#pragma once

#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Port of CMapToUnicode / CMapCidUni / CMapParser (subset): parses a CMap program
/// (codespacerange, bfchar/bfrange, cidchar/cidrange).
class CMapParser {
public:
    struct Range {
        Bytes low;
        Bytes high;
    };
    struct Result {
        std::vector<Range> codeSpaceRanges;
        /// code (big-endian integer of the byte sequence) + code length -> unicode (UTF-32 sequence)
        std::map<std::pair<int, uint32_t>, std::u32string> bfMap;
        /// code -> CID
        std::map<std::pair<int, uint32_t>, int> cidMap;
        std::string useCMap;
        int wmode = 0;
    };
    static Result parse(const Bytes& cmap);
};

/// Port of CMapToUnicode: maps character codes to unicode text.
class CMapToUnicode {
public:
    static std::shared_ptr<CMapToUnicode> getIdentity();
    static std::shared_ptr<CMapToUnicode> parse(const Bytes& cmap);
    CMapToUnicode() = default;
    /// Looks up the unicode text for the code formed by `length` bytes; empty optional if absent. Returned by value
    /// because identity mappings are synthesised on the fly and have no stable storage.
    std::optional<std::u32string> lookup(const uint8_t* code, int length) const;
    bool hasByteMappings() const { return has1Byte_; }
    bool hasTwoByteMappings() const { return has2Byte_; }
    bool isIdentity() const { return identity_; }
    /// Returns code -> unicode map with codes as ints (for widths / reverse lookups).
    std::map<int, int> createDirectMapping() const;
    std::map<int, int> createReverseMapping() const;
    void addChar(int code, int codeLength, const std::u32string& unicode);
    const std::vector<CMapParser::Range>& getCodeSpaceRanges() const { return codeSpaceRanges_; }
    void setCodeSpaceRanges(std::vector<CMapParser::Range> r) { codeSpaceRanges_ = std::move(r); }

private:
    std::map<std::pair<int, uint32_t>, std::u32string> map_;
    std::vector<CMapParser::Range> codeSpaceRanges_;
    bool has1Byte_ = false;
    bool has2Byte_ = false;
    bool identity_ = false;
};

/// Port of CMapEncoding: converts byte sequences into CIDs (Identity-H/V or embedded CMaps).
class CMapEncoding {
public:
    /// Identity encoding (2-byte codes, CID == code).
    static std::shared_ptr<CMapEncoding> identity(bool vertical = false);
    /// Encoding defined by an embedded CMap stream.
    static std::shared_ptr<CMapEncoding> fromStream(const Bytes& cmap);
    /// Predefined CMap name; only Identity-H/V and one/two-byte fallbacks are supported.
    static std::shared_ptr<CMapEncoding> fromName(const std::string& name);

    bool isDirect() const { return direct_; }
    const std::string& getCmapName() const { return name_; }
    /// Splits the byte string into codes, returning (code, length, cid) triples.
    struct Code {
        int code;
        int length;
        int cid;
    };
    std::vector<Code> decode(const Bytes& bytes) const;

private:
    bool direct_ = false;
    std::string name_;
    std::vector<CMapParser::Range> codeSpaceRanges_;
    std::map<std::pair<int, uint32_t>, int> cidMap_;
};

}  // namespace io
}  // namespace pdfcpp
