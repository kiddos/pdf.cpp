#ifndef PDFCPP_KERNEL_PDF_FILTERS_FILTERS_H_
#define PDFCPP_KERNEL_PDF_FILTERS_FILTERS_H_

#include "pdfcpp/kernel/pdf/pdf_object.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"

namespace pdfcpp {
namespace kernel {

/// Port of FlateDecodeFilter (+ predictor decoding).
class FlateDecodeFilter : public IFilterHandler {
 public:
  /// Inflates data. In non-strict mode returns what could be decoded from a
  /// corrupt stream.
  static Bytes FlateDecode(const Bytes& in, bool strict);
  static Bytes DecodePredictor(const Bytes& in,
                               const PdfDictionary* decode_params);
  Bytes Decode(const Bytes& b, const PdfName& filter_name,
               const PdfDictionary* decode_params,
               const PdfDictionary& stream_dictionary) override;
};

/// Port of ASCIIHexDecodeFilter.
class ASCIIHexDecodeFilter : public IFilterHandler {
 public:
  static Bytes ASCIIHexDecode(const Bytes& in);
  Bytes Decode(const Bytes& b, const PdfName&, const PdfDictionary*,
               const PdfDictionary&) override {
    return ASCIIHexDecode(b);
  }
};

/// Port of ASCII85DecodeFilter.
class ASCII85DecodeFilter : public IFilterHandler {
 public:
  static Bytes ASCII85Decode(const Bytes& in);
  Bytes Decode(const Bytes& b, const PdfName&, const PdfDictionary*,
               const PdfDictionary&) override {
    return ASCII85Decode(b);
  }
};

/// Port of LZWDecodeFilter.
class LZWDecodeFilter : public IFilterHandler {
 public:
  static Bytes LZWDecode(const Bytes& in);
  Bytes Decode(const Bytes& b, const PdfName&,
               const PdfDictionary* decode_params,
               const PdfDictionary&) override;
};

/// Port of RunLengthDecodeFilter.
class RunLengthDecodeFilter : public IFilterHandler {
 public:
  Bytes Decode(const Bytes& b, const PdfName&, const PdfDictionary*,
               const PdfDictionary&) override;
};

/// Port of DoNothingFilter: image codecs left encoded (DCTDecode, JPXDecode,
/// JBIG2Decode, ...).
class DoNothingFilter : public IFilterHandler {
 public:
  Bytes Decode(const Bytes& b, const PdfName&, const PdfDictionary*,
               const PdfDictionary&) override {
    return b;
  }
};

/// Port of FlateDecodeStrictFilter-like Crypt filter (identity).
class CryptFilter : public IFilterHandler {
 public:
  Bytes Decode(const Bytes& b, const PdfName&, const PdfDictionary*,
               const PdfDictionary&) override {
    return b;
  }
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_FILTERS_FILTERS_H_
