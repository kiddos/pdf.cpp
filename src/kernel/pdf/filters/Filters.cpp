#include "pdfcpp/kernel/pdf/filters/Filters.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>

#include "pdfcpp/io/source/PdfTokenizer.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ Flate
Bytes FlateDecodeFilter::flateDecode(const Bytes& in, bool strict) {
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

Bytes FlateDecodeFilter::decodePredictor(const Bytes& in, const PdfDictionary* decodeParams) {
    if (decodeParams == nullptr) return in;
    auto predictorObj = decodeParams->getAsNumber(PdfName::Predictor);
    if (!predictorObj) return in;
    int predictor = predictorObj->intValue();
    if (predictor < 10 && predictor != 2) return in;
    int width = 1;
    if (auto o = decodeParams->getAsNumber(PdfName::Columns)) width = o->intValue();
    int colors = 1;
    if (auto o = decodeParams->getAsNumber(PdfName::Colors)) colors = o->intValue();
    int bpc = 8;
    if (auto o = decodeParams->getAsNumber(PdfName::BitsPerComponent)) bpc = o->intValue();
    int bytesPerPixel = colors * bpc / 8;
    if (bytesPerPixel < 1) bytesPerPixel = 1;
    int bytesPerRow = (colors * width * bpc + 7) / 8;
    if (bytesPerRow <= 0) return in;

    if (predictor == 2) {
        if (bpc == 8) {
            Bytes out = in;
            int numRows = static_cast<int>(out.size()) / bytesPerRow;
            for (int row = 0; row < numRows; row++) {
                int rowStart = row * bytesPerRow;
                for (int col = bytesPerPixel; col < bytesPerRow; col++)
                    out[rowStart + col] = static_cast<uint8_t>(out[rowStart + col] + out[rowStart + col - bytesPerPixel]);
            }
            return out;
        }
        return in;
    }

    Bytes out;
    out.reserve(in.size());
    Bytes curr(bytesPerRow), prior(bytesPerRow, 0);
    size_t pos = 0;
    while (pos < in.size()) {
        int filter = in[pos++];
        if (pos + bytesPerRow > in.size()) {
            // partial last row
            size_t remaining = in.size() - pos;
            std::fill(curr.begin(), curr.end(), 0);
            std::memcpy(curr.data(), in.data() + pos, remaining);
            pos = in.size();
        } else {
            std::memcpy(curr.data(), in.data() + pos, bytesPerRow);
            pos += bytesPerRow;
        }
        switch (filter) {
            case 0:
                break;
            case 1:
                for (int i = bytesPerPixel; i < bytesPerRow; i++) curr[i] = static_cast<uint8_t>(curr[i] + curr[i - bytesPerPixel]);
                break;
            case 2:
                for (int i = 0; i < bytesPerRow; i++) curr[i] = static_cast<uint8_t>(curr[i] + prior[i]);
                break;
            case 3:
                for (int i = 0; i < bytesPerPixel; i++) curr[i] = static_cast<uint8_t>(curr[i] + prior[i] / 2);
                for (int i = bytesPerPixel; i < bytesPerRow; i++)
                    curr[i] = static_cast<uint8_t>(curr[i] + ((curr[i - bytesPerPixel] + prior[i]) / 2));
                break;
            case 4:
                for (int i = 0; i < bytesPerPixel; i++) curr[i] = static_cast<uint8_t>(curr[i] + prior[i]);
                for (int i = bytesPerPixel; i < bytesPerRow; i++) {
                    int a = curr[i - bytesPerPixel];
                    int b = prior[i];
                    int c = prior[i - bytesPerPixel];
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

Bytes FlateDecodeFilter::decode(const Bytes& b, const PdfName&, const PdfDictionary* decodeParams, const PdfDictionary&) {
    Bytes res = flateDecode(b, true);
    if (res.empty() && !b.empty()) res = flateDecode(b, false);
    return decodePredictor(res, decodeParams);
}

// ============================================================ ASCIIHex
Bytes ASCIIHexDecodeFilter::ASCIIHexDecode(const Bytes& in) {
    Bytes out;
    bool first = true;
    int n1 = 0;
    for (uint8_t ch : in) {
        if (ch == '>') break;
        if (io::PdfTokenizer::isWhitespace(ch)) continue;
        int n = io::ByteBuffer::getHex(ch);
        if (n == -1) throw PdfException("Illegal character in ASCIIHexDecode.");
        if (first) n1 = n;
        else out.push_back(static_cast<uint8_t>((n1 << 4) + n));
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
        if (io::PdfTokenizer::isWhitespace(ch)) continue;
        if (ch == 'z' && state == 0) {
            out.insert(out.end(), 4, 0);
            continue;
        }
        if (ch < '!' || ch > 'u') throw PdfException("Illegal character in ASCII85Decode.");
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
        int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 + 85LL * 85 * 85 + 85 * 85 + 85;
        out.push_back(static_cast<uint8_t>(r >> 24));
    } else if (state == 3) {
        int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 + chn[2] * 85LL * 85 + 85 * 85 + 85;
        out.push_back(static_cast<uint8_t>(r >> 24));
        out.push_back(static_cast<uint8_t>(r >> 16));
    } else if (state == 4) {
        int64_t r = chn[0] * 85LL * 85 * 85 * 85 + chn[1] * 85LL * 85 * 85 + chn[2] * 85LL * 85 + chn[3] * 85LL + 85;
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
    auto initTable = [&]() {
        table.clear();
        table.reserve(4096);
        for (int i = 0; i < 256; i++) table.push_back(Bytes{static_cast<uint8_t>(i)});
        table.emplace_back();  // 256 clear
        table.emplace_back();  // 257 EOD
    };
    initTable();
    int bitsToRead = 9;
    size_t bytePointer = 0;
    int nextData = 0, nextBits = 0;
    auto getNextCode = [&]() -> int {
        if (bytePointer >= in.size()) return 257;
        while (nextBits < bitsToRead) {
            if (bytePointer >= in.size()) return 257;
            nextData = (nextData << 8) | in[bytePointer++];
            nextBits += 8;
        }
        int code = (nextData >> (nextBits - bitsToRead)) & ((1 << bitsToRead) - 1);
        nextBits -= bitsToRead;
        return code;
    };
    int code, oldCode = 0;
    while ((code = getNextCode()) != 257) {
        if (code == 256) {
            initTable();
            bitsToRead = 9;
            code = getNextCode();
            if (code == 257) break;
            if (code >= static_cast<int>(table.size())) break;
            out.insert(out.end(), table[code].begin(), table[code].end());
            oldCode = code;
        } else {
            Bytes entry;
            if (code < static_cast<int>(table.size())) {
                entry = table[code];
                out.insert(out.end(), entry.begin(), entry.end());
                Bytes n = table[oldCode];
                n.push_back(entry[0]);
                table.push_back(n);
            } else {
                Bytes n = table[oldCode];
                n.push_back(table[oldCode][0]);
                out.insert(out.end(), n.begin(), n.end());
                table.push_back(n);
            }
            oldCode = code;
            int tableIndex = static_cast<int>(table.size());
            if (tableIndex == 511) bitsToRead = 10;
            else if (tableIndex == 1023) bitsToRead = 11;
            else if (tableIndex == 2047) bitsToRead = 12;
        }
    }
    return out;
}

Bytes LZWDecodeFilter::decode(const Bytes& b, const PdfName&, const PdfDictionary* decodeParams, const PdfDictionary&) {
    return FlateDecodeFilter::decodePredictor(LZWDecode(b), decodeParams);
}

// ============================================================ RunLength
Bytes RunLengthDecodeFilter::decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) {
    Bytes out;
    size_t i = 0;
    while (i < b.size()) {
        int dupCount = static_cast<int8_t>(b[i]);
        if (dupCount == -128) break;
        if (dupCount >= 0) {
            size_t bytesToCopy = dupCount + 1;
            size_t end = std::min(b.size(), i + 1 + bytesToCopy);
            out.insert(out.end(), b.begin() + i + 1, b.begin() + end);
            i += bytesToCopy + 1;
        } else {
            if (i + 1 >= b.size()) break;
            size_t count = 1 - dupCount;
            out.insert(out.end(), count, b[i + 1]);
            i += 2;
        }
    }
    return out;
}

// ============================================================ FilterHandlers
const FilterHandlerMap& FilterHandlers::getDefaultFilterHandlers() {
    static const FilterHandlerMap handlers = [] {
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
    return handlers;
}

}  // namespace kernel
}  // namespace pdfcpp
