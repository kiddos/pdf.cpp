#ifndef PDFCPP_KERNEL_PDF_PROPERTIES_H_
#define PDFCPP_KERNEL_PDF_PROPERTIES_H_

#include <memory>
#include <optional>
#include <string>

#include "pdfcpp/kernel/pdf/pdf_object.h"
#include "pdfcpp/kernel/pdf/pdf_version.h"

namespace pdfcpp {
namespace kernel {

/// Port of EncryptionConstants.
namespace encryption_constants {
inline constexpr int kStandardEncryption40 = 0;
inline constexpr int kStandardEncryption128 = 1;
inline constexpr int kEncryptionAes128 = 2;
inline constexpr int kEncryptionAes256 = 3;
inline constexpr int kEncryptionAesGcm = 4;
inline constexpr int kEncryptionMask = 7;
inline constexpr int kDoNotEncryptMetadata = 8;
inline constexpr int kEmbeddedFilesOnly = 24;
inline constexpr int kAllowPrinting = 4 + 2048;
inline constexpr int kAllowModifyContents = 8;
inline constexpr int kAllowCopy = 16;
inline constexpr int kAllowModifyAnnotations = 32;
inline constexpr int kAllowFillIn = 256;
inline constexpr int kAllowScreenreaders = 512;
inline constexpr int kAllowAssembly = 1024;
inline constexpr int kAllowDegradedPrinting = 4;
}  // namespace encryption_constants

/// Port of EncryptionProperties (standard security handler only).
class EncryptionProperties {
 public:
  EncryptionProperties& SetStandardEncryption(const Bytes& user_password,
                                              const Bytes& owner_password,
                                              int permissions,
                                              int encryption_algorithm) {
    user_password_ = user_password;
    owner_password_ = owner_password;
    standard_encrypt_permissions_ = permissions;
    encryption_algorithm_ = encryption_algorithm;
    standard_ = true;
    return *this;
  }
  bool IsStandardEncryptionUsed() const { return standard_; }

  Bytes user_password_;
  Bytes owner_password_;
  int standard_encrypt_permissions_ = 0;
  int encryption_algorithm_ = 0;
  bool standard_ = false;
};

/// Port of WriterProperties.
class WriterProperties {
 public:
  WriterProperties() = default;
  WriterProperties& SetPdfVersion(const PdfVersion& version) {
    pdf_version = version;
    return *this;
  }
  WriterProperties& UseSmartMode() {
    smart_mode = true;
    return *this;
  }
  WriterProperties& AddXmpMetadata() {
    add_xmp_metadata_ = true;
    return *this;
  }
  WriterProperties& SetCompressionLevel(int level) {
    compression_level = level;
    return *this;
  }
  WriterProperties& SetFullCompressionMode(bool full_compression_mode) {
    is_full_compression = full_compression_mode;
    return *this;
  }
  WriterProperties& SetStandardEncryption(const Bytes& user_password,
                                          const Bytes& owner_password,
                                          int permissions,
                                          int encryption_algorithm) {
    encryption_properties.SetStandardEncryption(
        user_password, owner_password, permissions, encryption_algorithm);
    return *this;
  }
  WriterProperties& SetInitialDocumentId(PdfStringPtr id) {
    initial_document_id = std::move(id);
    return *this;
  }
  WriterProperties& SetModifiedDocumentId(PdfStringPtr id) {
    modified_document_id = std::move(id);
    return *this;
  }
  bool IsStandardEncryptionUsed() const {
    return encryption_properties.IsStandardEncryptionUsed();
  }

  int compression_level = -1;  // compression_constants::kDefaultCompression
  std::optional<bool> is_full_compression;
  bool smart_mode = false;
  bool add_xmp_metadata_ = false;
  std::optional<PdfVersion> pdf_version;
  EncryptionProperties encryption_properties;
  PdfStringPtr initial_document_id;
  PdfStringPtr modified_document_id;
};

/// Port of ReaderProperties.
class ReaderProperties {
 public:
  ReaderProperties& SetPassword(const Bytes& pwd) {
    password = pwd;
    return *this;
  }
  ReaderProperties& SetPassword(const std::string& pwd) {
    return SetPassword(ToBytes(pwd));
  }
  Bytes password;
};

/// Port of DocumentProperties / StampingProperties.
class StampingProperties {
 public:
  StampingProperties& UseAppendMode() {
    append_mode = true;
    return *this;
  }
  StampingProperties& PreserveEncryption() {
    preserve_encryption_ = true;
    return *this;
  }
  bool append_mode = false;
  bool preserve_encryption_ = false;
};

using DocumentProperties = StampingProperties;

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PROPERTIES_H_
