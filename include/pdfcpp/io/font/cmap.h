#ifndef PDFCPP_IO_FONT_C_MAP_H_
#define PDFCPP_IO_FONT_C_MAP_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"

namespace pdfcpp {
namespace io {

/// Port of CMapToUnicode / CMapCidUni / CMapParser (subset): parses a CMap
/// program (codespacerange, bfchar/bfrange, cidchar/cidrange).
class CMapParser {
 public:
  struct Range {
    Bytes low;
    Bytes high;
  };
  struct Result {
    std::vector<Range> code_space_ranges;
    /// code (big-endian integer of the byte sequence) + code length -> unicode
    /// (UTF-32 sequence)
    std::map<std::pair<int, uint32_t>, std::u32string> bf_map;
    /// code -> CID
    std::map<std::pair<int, uint32_t>, int> cid_map;
    std::string use_c_map;
    int wmode = 0;
  };
  static Result Parse(const Bytes& cmap);
};

/// Port of CMapToUnicode: maps character codes to unicode text.
class CMapToUnicode {
 public:
  static std::shared_ptr<CMapToUnicode> GetIdentity();
  static std::shared_ptr<CMapToUnicode> Parse(const Bytes& cmap);
  CMapToUnicode() = default;
  /// Looks up the unicode text for the code formed by `length` bytes; empty
  /// optional if absent. Returned by value because identity mappings are
  /// synthesised on the fly and have no stable storage.
  std::optional<std::u32string> Lookup(const uint8_t* code, int length) const;
  bool HasByteMappings() const { return has1_byte_; }
  bool HasTwoByteMappings() const { return has2_byte_; }
  bool IsIdentity() const { return identity_; }
  /// Returns code -> unicode map with codes as ints (for widths / reverse
  /// lookups).
  std::map<int, int> CreateDirectMapping() const;
  std::map<int, int> CreateReverseMapping() const;
  void AddChar(int code, int code_length, const std::u32string& unicode);
  const std::vector<CMapParser::Range>& GetCodeSpaceRanges() const {
    return code_space_ranges_;
  }
  void SetCodeSpaceRanges(std::vector<CMapParser::Range> r) {
    code_space_ranges_ = std::move(r);
  }

 private:
  std::map<std::pair<int, uint32_t>, std::u32string> map_;
  std::vector<CMapParser::Range> code_space_ranges_;
  bool has1_byte_ = false;
  bool has2_byte_ = false;
  bool identity_ = false;
};

/// Port of CMapEncoding: converts byte sequences into CIDs (Identity-H/V or
/// embedded CMaps).
class CMapEncoding {
 public:
  /// Identity encoding (2-byte codes, CID == code).
  static std::shared_ptr<CMapEncoding> Identity(bool vertical = false);
  /// Encoding defined by an embedded CMap stream.
  static std::shared_ptr<CMapEncoding> FromStream(const Bytes& cmap);
  /// Predefined CMap name; only Identity-H/V and one/two-byte fallbacks are
  /// supported.
  static std::shared_ptr<CMapEncoding> FromName(const std::string& name);

  bool IsDirect() const { return direct_; }
  const std::string& GetCmapName() const { return name_; }
  /// Splits the byte string into codes, returning (code, length, cid) triples.
  struct Code {
    int code;
    int length;
    int cid;
  };
  std::vector<Code> Decode(const Bytes& bytes) const;

 private:
  bool direct_ = false;
  std::string name_;
  std::vector<CMapParser::Range> code_space_ranges_;
  std::map<std::pair<int, uint32_t>, int> cid_map_;
};

}  // namespace io
}  // namespace pdfcpp

#endif  // PDFCPP_IO_FONT_C_MAP_H_
