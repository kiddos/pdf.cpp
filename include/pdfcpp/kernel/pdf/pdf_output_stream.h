#ifndef PDFCPP_KERNEL_PDF_PDF_OUTPUT_STREAM_H_
#define PDFCPP_KERNEL_PDF_PDF_OUTPUT_STREAM_H_

#include <memory>

#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/kernel/pdf/pdf_object.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;
class PdfObjectStream;

/// Strategy for compressing streams (port of IStreamCompressionStrategy).
class IStreamCompressionStrategy {
 public:
  virtual ~IStreamCompressionStrategy() = default;
  virtual PdfNamePtr GetFilterName() const = 0;
  virtual PdfObjectPtr GetDecodeParams() const = 0;
  virtual std::shared_ptr<io::OutputStream> CreateNewOutputStream(
      std::shared_ptr<io::OutputStream> original, PdfStream& stream) = 0;
};

/// Default Flate compression (port of FlateCompressionStrategy).
class FlateCompressionStrategy : public IStreamCompressionStrategy {
 public:
  PdfNamePtr GetFilterName() const override { return PdfName::kFlateDecode; }
  PdfObjectPtr GetDecodeParams() const override { return nullptr; }
  std::shared_ptr<io::OutputStream> CreateNewOutputStream(
      std::shared_ptr<io::OutputStream> original, PdfStream& stream) override;
};

/// Port of com.itextpdf.kernel.pdf.PdfOutputStream: serializes PDF objects.
class PdfOutputStream : public io::HighPrecisionOutputStream {
 public:
  explicit PdfOutputStream(std::shared_ptr<io::OutputStream> output_stream);

  /// Serializes an object (indirect objects are written as references).
  PdfOutputStream& Write(PdfObject& pdf_object);
  PdfOutputStream& Write(const PdfObjectPtr& pdf_object) {
    return Write(*pdf_object);
  }
  using io::HighPrecisionOutputStream::Write;

  /// Writes `bytes` as a big-endian integer of `size` bytes.
  void WriteBigEndian(int64_t bytes, int size);

  PdfDocument* GetDocument() const { return document_; }
  void SetDocument(PdfDocument* document) { document_ = document; }
  PdfEncryption* GetCrypto() const { return crypto_; }
  void SetCrypto(PdfEncryption* crypto) { crypto_ = crypto; }
  void SetCompressionStrategy(std::shared_ptr<IStreamCompressionStrategy> s) {
    compression_strategy_ = std::move(s);
  }

 protected:
  IStreamCompressionStrategy& GetCompressionStrategy();
  bool CheckEncryption(PdfStream& pdf_stream);
  bool ContainsFlateFilter(PdfStream& pdf_stream);
  void UpdateCompressionFilter(PdfStream& pdf_stream);
  Bytes DecodeFlateBytes(PdfStream& stream, const Bytes& bytes);

  PdfDocument* document_ = nullptr;
  PdfEncryption* crypto_ = nullptr;

 private:
  void WriteArray(PdfArray& pdf_array);
  void WriteDictionary(PdfDictionary& pdf_dictionary);
  void WriteReference(PdfIndirectReference& indirect_reference);
  void WritePdfString(PdfString& pdf_string);
  void WriteName(PdfName& name);
  void WriteNumber(PdfNumber& pdf_number);
  void WriteStream(PdfStream& pdf_stream);
  std::shared_ptr<IStreamCompressionStrategy> compression_strategy_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_OUTPUT_STREAM_H_
