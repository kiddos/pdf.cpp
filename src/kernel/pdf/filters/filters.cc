#include "pdfcpp/kernel/pdf/filters/filters.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>

#include "pdfcpp/io/source/pdf_tokenizer.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ Flate
Bytes FlateDecodeFilter::FlateDecode(const Bytes& in, bool strict) {
  Bytes out;
  if (in.empty()) return out;
  z_stream zs{};
  if (inflateInit(&zs) != Z_OK) return out;
  zs.next_in = const_cast<Bytef*>(in.data());
  zs.avail_in = static_cast<uInt>(in.size());
  uint8_t buf[32768];
  int ret;
  do {
    zs.next_out = buf;
    zs.avail_out = sizeof buf;
    ret = inflate(&zs, Z_NO_FLUSH);
    size_t have = sizeof buf - zs.avail_out;
    out.insert(out.end(), buf, buf + have);
    if (ret == Z_STREAM_END) break;
    if (ret != Z_OK) {
      if (ret == Z_BUF_ERROR && zs.avail_in == 0) break;  // truncated stream
      inflateEnd(&zs);
      if (strict) return Bytes();
      return out;
    }
  } while (true);
  inflateEnd(&zs);
  if (strict && ret != Z_STREAM_END && zs.avail_in == 0) {
    // truncated but usable
    return out;
  }
  return out;
}

Bytes FlateDecodeFilter::DecodePredictor(const Bytes& in,
                                         const PdfDictionary* decode_params) {
  if (decode_params == nullptr) return in;
  auto predictor_obj = decode_params->GetAsNumber(PdfName::kPredictor);
  if (!predictor_obj) return in;
  int predictor = predictor_obj->IntValue();
  if (predictor < 10 && predictor != 2) return in;
  int width = 1;
  if (auto o = decode_params->GetAsNumber(PdfName::kColumns))
    width = o->IntValue();
  int colors = 1;
  if (auto o = decode_params->GetAsNumber(PdfName::kColors))
    colors = o->IntValue();
  int bpc = 8;
  if (auto o = decode_params->GetAsNumber(PdfName::kBitsPerComponent))
    bpc = o->IntValue();
  // Validate before multiplying: negative factors (whose product may still be
  // positive) or rows wider than the data itself are malformed parameters;
  // leave the data undecoded as for an unknown predictor.
  if (width <= 0 || colors <= 0 || bpc <= 0 || bpc > 16) return in;
  const int64_t row_bits = static_cast<int64_t>(colors) * width * bpc;
  if (row_bits > (static_cast<int64_t>(in.size()) + 1) * 8) return in;
  int bytes_per_pixel = colors * bpc / 8;
  if (bytes_per_pixel < 1) bytes_per_pixel = 1;
  int bytes_per_row = static_cast<int>((row_bits + 7) / 8);
  if (bytes_per_row <= 0) return in;

  if (predictor == 2) {
    if (bpc == 8) {
      Bytes out = in;
      int num_rows = static_cast<int>(out.size()) / bytes_per_row;
      for (int row = 0; row < num_rows; row++) {
        int row_start = row * bytes_per_row;
        for (int col = bytes_per_pixel; col < bytes_per_row; col++)
          out[row_start + col] = static_cast<uint8_t>(
              out[row_start + col] + out[row_start + col - bytes_per_pixel]);
      }
      return out;
    }
    return in;
  }

  Bytes out;
  out.reserve(in.size());
  Bytes curr(bytes_per_row), prior(bytes_per_row, 0);
  size_t pos = 0;
  while (pos < in.size()) {
    int filter = in[pos++];
    if (pos + bytes_per_row > in.size()) {
      // partial last row
      size_t remaining = in.size() - pos;
      std::fill(curr.begin(), curr.end(), 0);
      std::memcpy(curr.data(), in.data() + pos, remaining);
      pos = in.size();
    } else {
      std::memcpy(curr.data(), in.data() + pos, bytes_per_row);
      pos += bytes_per_row;
    }
    switch (filter) {
      case 0:
        break;
      case 1:
        for (int i = bytes_per_pixel; i < bytes_per_row; i++)
          curr[i] = static_cast<uint8_t>(curr[i] + curr[i - bytes_per_pixel]);
        break;
      case 2:
        for (int i = 0; i < bytes_per_row; i++)
          curr[i] = static_cast<uint8_t>(curr[i] + prior[i]);
        break;
      case 3:
        for (int i = 0; i < bytes_per_pixel; i++)
          curr[i] = static_cast<uint8_t>(curr[i] + prior[i] / 2);
        for (int i = bytes_per_pixel; i < bytes_per_row; i++)
          curr[i] = static_cast<uint8_t>(
              curr[i] + ((curr[i - bytes_per_pixel] + prior[i]) / 2));
        break;
      case 4:
        for (int i = 0; i < bytes_per_pixel; i++)
          curr[i] = static_cast<uint8_t>(curr[i] + prior[i]);
        for (int i = bytes_per_pixel; i < bytes_per_row; i++) {
          int a = curr[i - bytes_per_pixel];
          int b = prior[i];
          int c = prior[i - bytes_per_pixel];
          int p = a + b - c;
          int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
          int ret = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
          curr[i] = static_cast<uint8_t>(curr[i] + ret);
        }
        break;
      default:
        throw PdfException("PNG filter unknown.");
    }
    out.insert(out.end(), curr.begin(), curr.end());
    std::swap(curr, prior);
  }
  return out;
}

Bytes FlateDecodeFilter::Decode(const Bytes& b, const PdfName&,
                                const PdfDictionary* decode_params,
                                const PdfDictionary&) {
  Bytes res = FlateDecode(b, true);
  if (res.empty() && !b.empty()) res = FlateDecode(b, false);
  return DecodePredictor(res, decode_params);
}

// ============================================================ ASCIIHex
Bytes ASCIIHexDecodeFilter::ASCIIHexDecode(const Bytes& in) {
  Bytes out;
  bool first = true;
  int n1 = 0;
  for (uint8_t ch : in) {
    if (ch == '>') break;
    if (io::PdfTokenizer::IsWhitespace(ch)) continue;
    int n = io::ByteBuffer::GetHex(ch);
    if (n == -1) throw PdfException("Illegal character in ASCIIHexDecode.");
    if (first)
      n1 = n;
    else
      out.push_back(static_cast<uint8_t>((n1 << 4) + n));
    first = !first;
  }
  if (!first) out.push_back(static_cast<uint8_t>(n1 << 4));
  return out;
}

// ============================================================ ASCII85
Bytes ASCII85DecodeFilter::ASCII85Decode(const Bytes& in) {
  Bytes out;
  int state = 0;
  int chn[5];
  size_t start = 0;
  if (in.size() >= 2 && in[0] == '<' && in[1] == '~') start = 2;
  for (size_t k = start; k < in.size(); ++k) {
    int ch = in[k] & 0xff;
    if (ch == '~') break;
    if (io::PdfTokenizer::IsWhitespace(ch)) continue;
    if (ch == 'z' && state == 0) {
      out.insert(out.end(), 4, 0);
      continue;
    }
    if (ch < '!' || ch > 'u')
      throw PdfException("Illegal character in ASCII85Decode.");
    chn[state] = ch - '!';
    ++state;
    if (state == 5) {
      state = 0;
      int64_t r = 0;
      for (int j = 0; j < 5; ++j) r = r * 85 + chn[j];
      out.push_back(static_cast<uint8_t>(r >> 24));
      out.push_back(static_cast<uint8_t>(r >> 16));
      out.push_back(static_cast<uint8_t>(r >> 8));
      out.push_back(static_cast<uint8_t>(r));
    }
  }
  if (state == 2) {
    int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 +
                85LL * 85 * 85 + 85 * 85 + 85;
    out.push_back(static_cast<uint8_t>(r >> 24));
  } else if (state == 3) {
    int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 +
                chn[2] * 85LL * 85 + 85 * 85 + 85;
    out.push_back(static_cast<uint8_t>(r >> 24));
    out.push_back(static_cast<uint8_t>(r >> 16));
  } else if (state == 4) {
    int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 +
                chn[2] * 85LL * 85 + chn[3] * 85LL + 85;
    out.push_back(static_cast<uint8_t>(r >> 24));
    out.push_back(static_cast<uint8_t>(r >> 16));
    out.push_back(static_cast<uint8_t>(r >> 8));
  }
  return out;
}

// ============================================================ LZW
Bytes LZWDecodeFilter::LZWDecode(const Bytes& in) {
  Bytes out;
  std::vector<Bytes> table;
  auto init_table = [&]() {
    table.clear();
    table.reserve(4096);
    for (int i = 0; i < 256; i++)
      table.push_back(Bytes{static_cast<uint8_t>(i)});
    table.emplace_back();  // 256 clear
    table.emplace_back();  // 257 EOD
  };
  init_table();
  int bits_to_read = 9;
  size_t byte_pointer = 0;
  int next_data = 0, next_bits = 0;
  auto get_next_code = [&]() -> int {
    if (byte_pointer >= in.size()) return 257;
    while (next_bits < bits_to_read) {
      if (byte_pointer >= in.size()) return 257;
      next_data = (next_data << 8) | in[byte_pointer++];
      next_bits += 8;
    }
    int code =
        (next_data >> (next_bits - bits_to_read)) & ((1 << bits_to_read) - 1);
    next_bits -= bits_to_read;
    return code;
  };
  int code, old_code = 0;
  while ((code = get_next_code()) != 257) {
    if (code == 256) {
      init_table();
      bits_to_read = 9;
      code = get_next_code();
      if (code == 257) break;
      if (code >= static_cast<int>(table.size())) break;
      out.insert(out.end(), table[code].begin(), table[code].end());
      old_code = code;
    } else {
      // A corrupt stream may reference codes beyond the next free slot, or
      // follow a clear code with another clear code (whose table entry is
      // empty); stop decoding rather than index out of bounds.
      const int table_size = static_cast<int>(table.size());
      if (code > table_size) break;
      const Bytes& prev = table[old_code];
      // The table is full at 4096 entries (12-bit codes): keep decoding but
      // stop adding entries until the encoder sends a clear code.
      const bool can_add = table_size < 4096 && !prev.empty();
      if (code < table_size) {
        Bytes entry = table[code];
        out.insert(out.end(), entry.begin(), entry.end());
        if (can_add && !entry.empty()) {
          Bytes n = prev;
          n.push_back(entry[0]);
          table.push_back(std::move(n));
        }
      } else {
        if (prev.empty() || table_size >= 4096) break;
        Bytes n = prev;
        n.push_back(prev[0]);
        out.insert(out.end(), n.begin(), n.end());
        table.push_back(std::move(n));
      }
      old_code = code;
      int table_index = static_cast<int>(table.size());
      if (table_index == 511)
        bits_to_read = 10;
      else if (table_index == 1023)
        bits_to_read = 11;
      else if (table_index == 2047)
        bits_to_read = 12;
    }
  }
  return out;
}

Bytes LZWDecodeFilter::Decode(const Bytes& b, const PdfName&,
                              const PdfDictionary* decode_params,
                              const PdfDictionary&) {
  return FlateDecodeFilter::DecodePredictor(LZWDecode(b), decode_params);
}

// ============================================================ RunLength
Bytes RunLengthDecodeFilter::Decode(const Bytes& b, const PdfName&,
                                    const PdfDictionary*,
                                    const PdfDictionary&) {
  Bytes out;
  size_t i = 0;
  while (i < b.size()) {
    int dup_count = static_cast<int8_t>(b[i]);
    if (dup_count == -128) break;
    if (dup_count >= 0) {
      size_t bytes_to_copy = dup_count + 1;
      size_t end = std::min(b.size(), i + 1 + bytes_to_copy);
      out.insert(out.end(), b.begin() + i + 1, b.begin() + end);
      i += bytes_to_copy + 1;
    } else {
      if (i + 1 >= b.size()) break;
      size_t count = 1 - dup_count;
      out.insert(out.end(), count, b[i + 1]);
      i += 2;
    }
  }
  return out;
}

// ============================================================ FilterHandlers
const FilterHandlerMap& FilterHandlers::GetDefaultFilterHandlers() {
  static const FilterHandlerMap kHandlers = [] {
    FilterHandlerMap map;
    auto flate = std::make_shared<FlateDecodeFilter>();
    auto hex = std::make_shared<ASCIIHexDecodeFilter>();
    auto a85 = std::make_shared<ASCII85DecodeFilter>();
    auto lzw = std::make_shared<LZWDecodeFilter>();
    auto rl = std::make_shared<RunLengthDecodeFilter>();
    auto nothing = std::make_shared<DoNothingFilter>();
    map["FlateDecode"] = flate;
    map["Fl"] = flate;
    map["ASCIIHexDecode"] = hex;
    map["AHx"] = hex;
    map["ASCII85Decode"] = a85;
    map["A85"] = a85;
    map["LZWDecode"] = lzw;
    map["LZW"] = lzw;
    map["RunLengthDecode"] = rl;
    map["RL"] = rl;
    map["CCITTFaxDecode"] = nothing;
    map["CCF"] = nothing;
    map["DCTDecode"] = nothing;
    map["DCT"] = nothing;
    map["JBIG2Decode"] = nothing;
    map["JPXDecode"] = nothing;
    map["Crypt"] = std::make_shared<CryptFilter>();
    return map;
  }();
  return kHandlers;
}

}  // namespace kernel
}  // namespace pdfcpp
