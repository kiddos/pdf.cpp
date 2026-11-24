#ifndef PDFCPP_KERNEL_PDF_PDF_READER_H_
#define PDFCPP_KERNEL_PDF_PDF_READER_H_

#include <map>
#include <memory>
#include <string>

#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/kernel/pdf/pdf_object.h"
#include "pdfcpp/kernel/pdf/pdf_version.h"
#include "pdfcpp/kernel/pdf/properties.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;

/// Decoder for one stream filter (port of IFilterHandler).
class IFilterHandler {
 public:
  virtual ~IFilterHandler() = default;
  virtual Bytes Decode(const Bytes& b, const PdfName& filter_name,
                       const PdfDictionary* decode_params,
                       const PdfDictionary& stream_dictionary) = 0;
};

using FilterHandlerMap = std::map<std::string, std::shared_ptr<IFilterHandler>>;

/// Port of FilterHandlers: the default filter decoders keyed by filter name.
class FilterHandlers {
 public:
  static const FilterHandlerMap& GetDefaultFilterHandlers();
};

/// Port of com.itextpdf.kernel.pdf.PdfReader.
class PdfReader {
 public:
  enum class StrictnessLevel { kConservative = 5000, kLenient = 3000 };
  static bool IsStricter(StrictnessLevel a, StrictnessLevel b) {
    return static_cast<int>(a) > static_cast<int>(b);
  }

  explicit PdfReader(const std::string& filename,
                     const ReaderProperties& properties = ReaderProperties());
  explicit PdfReader(io::RandomAccessSourcePtr byte_source,
                     const ReaderProperties& properties = ReaderProperties());
  /// Reads from an in-memory PDF.
  static std::unique_ptr<PdfReader> FromBytes(
      const Bytes& bytes,
      const ReaderProperties& properties = ReaderProperties());
  ~PdfReader();

  void Close();
  PdfReader& SetUnethicalReading(bool unethical_reading) {
    unethical_reading_ = unethical_reading;
    return *this;
  }
  PdfReader& SetMemorySavingMode(bool memory_saving_mode) {
    memory_saving_mode_ = memory_saving_mode;
    return *this;
  }
  StrictnessLevel GetStrictnessLevel() const { return strictness_level_; }
  PdfReader& SetStrictnessLevel(StrictnessLevel level) {
    strictness_level_ = level;
    return *this;
  }
  bool IsCloseStream() const { return tokens_->IsCloseStream(); }
  void SetCloseStream(bool close_stream) {
    tokens_->SetCloseStream(close_stream);
  }
  bool HasRebuiltXref() const { return rebuilt_xref_; }
  bool HasHybridXref() const { return hybrid_xref_; }
  bool HasXrefStm() const { return xref_stm_; }
  bool HasFixedXref() const { return fixed_xref_; }
  int64_t GetLastXref() const { return last_xref_; }

  Bytes ReadStreamBytes(PdfStream& stream, bool decode);
  Bytes ReadStreamBytesRaw(PdfStream& stream);
  static Bytes DecodeBytes(const Bytes& b,
                           const PdfDictionary& stream_dictionary);
  static Bytes DecodeBytes(const Bytes& b,
                           const PdfDictionary& stream_dictionary,
                           const FilterHandlerMap& filter_handlers);

  std::unique_ptr<io::RandomAccessFileOrArray> GetSafeFile() const {
    return tokens_->GetSafeFile();
  }
  int64_t GetFileLength() const { return tokens_->Length(); }
  bool IsOpenedWithFullPermission() const;
  int GetPermissions() const;
  int GetCryptoMode() const;
  Bytes GetOriginalFileId() const;
  Bytes GetModifiedFileId() const;
  bool IsEncrypted() const { return encrypted_; }
  const PdfDictionaryPtr& GetTrailer() const { return trailer_; }
  PdfVersion GetHeaderPdfVersion() const { return header_pdf_version_; }
  PdfEncryption* GetDecrypt() const { return decrypt_.get(); }
  io::PdfTokenizer& GetTokens() { return *tokens_; }

  // Internal (called by PdfDocument / PdfIndirectReference)
  void SetDocument(PdfDocument* document) { pdf_document_ = document; }
  PdfDocument* GetDocument() const { return pdf_document_; }
  void ReadPdf();
  PdfObjectPtr ReadObject(PdfIndirectReference* reference);
  const ReaderProperties& GetProperties() const { return properties_; }

 protected:
  void ReadObjectStream(PdfStream& object_stream);
  PdfObjectPtr ReadObject(bool read_as_direct, bool obj_stm = false);
  PdfObjectPtr ReadReference(bool read_as_direct);
  PdfNamePtr ReadPdfName(bool read_as_direct);
  PdfDictionaryPtr ReadDictionary(bool obj_stm);
  PdfArrayPtr ReadArray(bool obj_stm);
  void ReadXref();
  PdfDictionaryPtr ReadXrefSection();
  bool ReadXrefStream(int64_t ptr);
  void FixXref();
  void RebuildXref();
  PdfNumberPtr GetXrefPrev(const PdfObjectPtr& prev_object_to_check);

 private:
  PdfObjectPtr ReadObject(PdfIndirectReference* reference, bool fix_xref);
  void CheckPdfStreamLength(PdfStream& pdf_stream);
  PdfObjectPtr CreatePdfNullInstance(bool read_as_direct);
  bool IsCurrentObjectATrailer();
  void ReadDecryptObj();
  void InitTokenizer(io::RandomAccessSourcePtr byte_source);

  std::unique_ptr<io::PdfTokenizer> tokens_;
  std::unique_ptr<PdfEncryption> decrypt_;
  PdfVersion header_pdf_version_{1, 7};
  int64_t last_xref_ = 0;
  int64_t eof_pos_ = 0;
  PdfDictionaryPtr trailer_;
  PdfDocument* pdf_document_ = nullptr;
  ReaderProperties properties_;
  bool encrypted_ = false;
  bool rebuilt_xref_ = false;
  bool hybrid_xref_ = false;
  bool fixed_xref_ = false;
  bool xref_stm_ = false;
  bool unethical_reading_ = false;
  bool memory_saving_mode_ = false;
  StrictnessLevel strictness_level_ = StrictnessLevel::kLenient;
  PdfIndirectReference* current_indirect_reference_ = nullptr;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_READER_H_
