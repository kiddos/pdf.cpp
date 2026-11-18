#pragma once

#include "pdfcpp/kernel/pdf/PdfObject.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"

namespace pdfcpp {
namespace kernel {

/// Port of FlateDecodeFilter (+ predictor decoding).
class FlateDecodeFilter : public IFilterHandler {
public:
    /// Inflates data. In non-strict mode returns what could be decoded from a corrupt stream.
    static Bytes flateDecode(const Bytes& in, bool strict);
    static Bytes decodePredictor(const Bytes& in, const PdfDictionary* decodeParams);
    Bytes decode(const Bytes& b, const PdfName& filterName, const PdfDictionary* decodeParams,
                 const PdfDictionary& streamDictionary) override;
};

/// Port of ASCIIHexDecodeFilter.
class ASCIIHexDecodeFilter : public IFilterHandler {
public:
    static Bytes ASCIIHexDecode(const Bytes& in);
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) override {
        return ASCIIHexDecode(b);
    }
};

/// Port of ASCII85DecodeFilter.
class ASCII85DecodeFilter : public IFilterHandler {
public:
    static Bytes ASCII85Decode(const Bytes& in);
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) override {
        return ASCII85Decode(b);
    }
};

/// Port of LZWDecodeFilter.
class LZWDecodeFilter : public IFilterHandler {
public:
    static Bytes LZWDecode(const Bytes& in);
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary* decodeParams, const PdfDictionary&) override;
};

/// Port of RunLengthDecodeFilter.
class RunLengthDecodeFilter : public IFilterHandler {
public:
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) override;
};

/// Port of DoNothingFilter: image codecs left encoded (DCTDecode, JPXDecode, JBIG2Decode, ...).
class DoNothingFilter : public IFilterHandler {
public:
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) override { return b; }
};

/// Port of FlateDecodeStrictFilter-like Crypt filter (identity).
class CryptFilter : public IFilterHandler {
public:
    Bytes decode(const Bytes& b, const PdfName&, const PdfDictionary*, const PdfDictionary&) override { return b; }
};

}  // namespace kernel
}  // namespace pdfcpp
