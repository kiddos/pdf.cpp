#ifndef PDFCPP_KERNEL_PDF_PDF_WRITER_H_
#define PDFCPP_KERNEL_PDF_PDF_WRITER_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include "pdfcpp/kernel/pdf/pdf_output_stream.h"
#include "pdfcpp/kernel/pdf/properties.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;

/// Object stream used in full compression mode (port of PdfObjectStream).
class PdfObjectStream : public PdfStream {
 public:
  static constexpr int kMaxObjStreamSize = 200;
  static std::shared_ptr<PdfObjectStream> Create(PdfDocument* doc);
  void AddObject(PdfObject& object);
  int GetSize() const { return size_->IntValue(); }
  PdfOutputStream* GetIndexStream() const { return index_stream_.get(); }
  io::ByteArrayOutputStream* GetIndexBytes() const { return index_baos_.get(); }

  PdfObjectStream();  // use create()

 private:
  PdfNumberPtr size_ = MakeNumber(0);
  std::shared_ptr<io::ByteArrayOutputStream> index_baos_;
  std::unique_ptr<PdfOutputStream> index_stream_;
};

/// Port of com.itextpdf.kernel.pdf.PdfWriter.
class PdfWriter : public PdfOutputStream {
 public:
  explicit PdfWriter(const std::string& filename,
                     const WriterProperties& properties = WriterProperties());
  explicit PdfWriter(std::shared_ptr<io::OutputStream> os,
                     const WriterProperties& properties = WriterProperties());
  /// Writes into the given stream which is not owned (must outlive the writer).
  explicit PdfWriter(io::OutputStream& os,
                     const WriterProperties& properties = WriterProperties());
  ~PdfWriter() override;

  bool IsFullCompression() const {
    return properties_.is_full_compression.value_or(false);
  }
  int GetCompressionLevel() const { return properties_.compression_level; }
  PdfWriter& SetCompressionLevel(int compression_level) {
    properties_.SetCompressionLevel(compression_level);
    return *this;
  }
  std::optional<PdfVersion> GetPdfVersion() const {
    return properties_.pdf_version;
  }
  WriterProperties& GetProperties() { return properties_; }
  PdfWriter& SetSmartMode(bool smart_mode) {
    properties_.smart_mode = smart_mode;
    return *this;
  }

  // Internal API used by PdfDocument
  void InitCryptoIfSpecified(const PdfVersion& version);
  void FlushObject(PdfObject& pdf_object, bool can_be_in_obj_stm);
  PdfObjectPtr CopyObject(PdfObject* obj, PdfDocument* document_to,
                          bool allow_duplicating, ICopyFilter& copy_filter);
  void WriteToBody(PdfObject& pdf_obj);
  void WriteHeader();
  void FlushWaitingObjects(
      const std::set<PdfIndirectReference*>& forbidden_to_flush);
  void FlushModifiedWaitingObjects(
      const std::set<PdfIndirectReference*>& forbidden_to_flush);
  void Finish();
  PdfObjectStream* GetObjectStream();
  void FlushCopiedObjects(int64_t doc_id);
  std::unique_ptr<PdfEncryption>& CryptoHolder() { return owned_crypto_; }

 private:
  void MarkArrayContentToFlush(PdfArray& array);
  void MarkDictionaryContentToFlush(PdfDictionary& dictionary);
  void MarkObjectToFlush(PdfObject* pdf_object);

  WriterProperties properties_;
  std::shared_ptr<PdfObjectStream> object_stream_;
  std::map<PdfIndirectReference*, PdfIndirectReference*> copied_objects_;
  std::map<std::string, PdfIndirectReference*> smart_mode_cache_;
  std::unique_ptr<PdfEncryption> owned_crypto_;
  bool finished_ = false;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_WRITER_H_
