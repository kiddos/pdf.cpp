#ifndef PDFCPP_KERNEL_CRYPTO_PDF_ENCRYPTION_H_
#define PDFCPP_KERNEL_CRYPTO_PDF_ENCRYPTION_H_

#include <memory>
#include <optional>

#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/kernel/pdf/pdf_object.h"
#include "pdfcpp/kernel/pdf/pdf_version.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;

/// Low-level crypto primitives (OpenSSL backed).
class CryptoUtil {
 public:
  static Bytes Md5(const Bytes& data);
  static Bytes Sha256(const Bytes& data);
  static Bytes Sha384(const Bytes& data);
  static Bytes Sha512(const Bytes& data);
  static Bytes Rc4(const Bytes& key, const Bytes& data);
  /// AES-CBC; when `padding` is false data must be a multiple of 16 bytes.
  static Bytes AesCbcEncrypt(const Bytes& key, const Bytes& iv,
                             const Bytes& data, bool padding);
  static Bytes AesCbcDecrypt(const Bytes& key, const Bytes& iv,
                             const Bytes& data, bool padding);
  static Bytes AesEcbEncrypt(const Bytes& key, const Bytes& data);
  static Bytes AesEcbDecrypt(const Bytes& key, const Bytes& data);
  static Bytes RandomBytes(int n);
};

/// Port of com.itextpdf.kernel.pdf.PdfEncryption with the standard security
/// handler (RC4 40/128-bit, AES-128, AES-256 revision 5/6).
class PdfEncryption {
 public:
  /// Writer-side constructor (standard handler).
  PdfEncryption(const Bytes& user_password, const Bytes& owner_password,
                int permissions, int encryption_type, const Bytes& document_id,
                const PdfVersion& version);
  /// Reader-side constructor (standard handler).
  PdfEncryption(const PdfDictionaryPtr& pdf_dict, const Bytes& password,
                const Bytes& document_id);

  static Bytes GenerateNewDocumentId();
  static PdfObjectPtr CreateInfoId(const Bytes& id1, const Bytes& id2,
                                   bool preserve_encryption = false);

  int GetCryptoMode() const { return crypto_mode_; }
  bool IsMetadataEncrypted() const { return encrypt_metadata_; }
  bool IsEmbeddedFilesOnly() const { return embedded_files_only_; }
  std::optional<int64_t> GetPermissions() const { return permissions_; }
  bool IsOpenedWithFullPermission() const {
    return opened_with_full_permissions_;
  }
  int GetEncryptionAlgorithm() const { return encryption_algorithm_; }
  const PdfDictionaryPtr& GetPdfObject() const { return dictionary_; }

  void SetHashKeyForNextObject(int obj_number, int obj_generation);
  Bytes EncryptByteArray(const Bytes& b);
  Bytes DecryptByteArray(const Bytes& b);
  /// Returns a stream that encrypts everything written into `os` (call
  /// finish()).
  std::shared_ptr<io::OutputStream> GetEncryptionStream(
      std::shared_ptr<io::OutputStream> os);
  Bytes ComputeUserPassword(const Bytes& owner_password) const;

 private:
  void PutCryptFilters(PdfDictionary& d, const PdfDictionaryPtr& stdcf) const;
  void SetupByUserPassword40or128(const Bytes& user_password,
                                  const Bytes& owner_password, int permissions,
                                  int key_length_bits, int revision,
                                  bool encrypt_metadata);
  void SetupAes256(const Bytes& user_password, const Bytes& owner_password,
                   int permissions, bool encrypt_metadata);
  void ReadStandard(const Bytes& password);
  void ReadAes256(const Bytes& password);
  Bytes ComputeGlobalKey(const Bytes& padded_user, bool) const;
  Bytes ComputeOwnerKey(const Bytes& user_pad, const Bytes& owner_pad) const;
  Bytes ComputeU(const Bytes& global_key) const;
  Bytes HashR6(const Bytes& password, const Bytes& salt,
               const Bytes& user_key) const;
  static Bytes PadPassword(const Bytes& password);

  PdfDictionaryPtr dictionary_;
  int crypto_mode_ = 0;
  int encryption_algorithm_ = 0;
  int revision_ = 0;
  int key_length_ = 40;  // in bits
  bool encrypt_metadata_ = true;
  bool embedded_files_only_ = false;
  bool aes_ = false;
  bool opened_with_full_permissions_ = true;
  std::optional<int64_t> permissions_;
  int32_t p_value_ = 0;
  Bytes document_id_;
  Bytes owner_key_;  // O
  Bytes user_key_;   // U
  Bytes mkey_;       // global key
  Bytes key_;        // per-object key
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_CRYPTO_PDF_ENCRYPTION_H_
