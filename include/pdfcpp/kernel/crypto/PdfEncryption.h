#pragma once

#include <memory>
#include <optional>

#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/kernel/pdf/PdfObject.h"
#include "pdfcpp/kernel/pdf/PdfVersion.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;

/// Low-level crypto primitives (OpenSSL backed).
class CryptoUtil {
public:
    static Bytes md5(const Bytes& data);
    static Bytes sha256(const Bytes& data);
    static Bytes sha384(const Bytes& data);
    static Bytes sha512(const Bytes& data);
    static Bytes rc4(const Bytes& key, const Bytes& data);
    /// AES-CBC; when `padding` is false data must be a multiple of 16 bytes.
    static Bytes aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& data, bool padding);
    static Bytes aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& data, bool padding);
    static Bytes aesEcbEncrypt(const Bytes& key, const Bytes& data);
    static Bytes aesEcbDecrypt(const Bytes& key, const Bytes& data);
    static Bytes randomBytes(int n);
};

/// Port of com.itextpdf.kernel.pdf.PdfEncryption with the standard security handler
/// (RC4 40/128-bit, AES-128, AES-256 revision 5/6).
class PdfEncryption {
public:
    /// Writer-side constructor (standard handler).
    PdfEncryption(const Bytes& userPassword, const Bytes& ownerPassword, int permissions, int encryptionType,
                  const Bytes& documentId, const PdfVersion& version);
    /// Reader-side constructor (standard handler).
    PdfEncryption(const PdfDictionaryPtr& pdfDict, const Bytes& password, const Bytes& documentId);

    static Bytes generateNewDocumentId();
    static PdfObjectPtr createInfoId(const Bytes& id1, const Bytes& id2, bool preserveEncryption = false);

    int getCryptoMode() const { return cryptoMode_; }
    bool isMetadataEncrypted() const { return encryptMetadata_; }
    bool isEmbeddedFilesOnly() const { return embeddedFilesOnly_; }
    std::optional<int64_t> getPermissions() const { return permissions_; }
    bool isOpenedWithFullPermission() const { return openedWithFullPermissions_; }
    int getEncryptionAlgorithm() const { return encryptionAlgorithm_; }
    const PdfDictionaryPtr& getPdfObject() const { return dictionary_; }

    void setHashKeyForNextObject(int objNumber, int objGeneration);
    Bytes encryptByteArray(const Bytes& b);
    Bytes decryptByteArray(const Bytes& b);
    /// Returns a stream that encrypts everything written into `os` (call finish()).
    std::shared_ptr<io::OutputStream> getEncryptionStream(std::shared_ptr<io::OutputStream> os);
    Bytes computeUserPassword(const Bytes& ownerPassword) const;

private:
    void setupByUserPassword40or128(const Bytes& userPassword, const Bytes& ownerPassword, int permissions,
                                    int keyLengthBits, int revision, bool encryptMetadata);
    void setupAes256(const Bytes& userPassword, const Bytes& ownerPassword, int permissions, bool encryptMetadata);
    void readStandard(const Bytes& password);
    void readAes256(const Bytes& password);
    Bytes computeGlobalKey(const Bytes& paddedUser, bool) const;
    Bytes computeOwnerKey(const Bytes& userPad, const Bytes& ownerPad) const;
    Bytes computeU(const Bytes& globalKey) const;
    Bytes hashR6(const Bytes& password, const Bytes& salt, const Bytes& userKey) const;
    static Bytes padPassword(const Bytes& password);

    PdfDictionaryPtr dictionary_;
    int cryptoMode_ = 0;
    int encryptionAlgorithm_ = 0;
    int revision_ = 0;
    int keyLength_ = 40;  // in bits
    bool encryptMetadata_ = true;
    bool embeddedFilesOnly_ = false;
    bool aes_ = false;
    bool openedWithFullPermissions_ = true;
    std::optional<int64_t> permissions_;
    int32_t pValue_ = 0;
    Bytes documentId_;
    Bytes ownerKey_;  // O
    Bytes userKey_;   // U
    Bytes mkey_;      // global key
    Bytes key_;       // per-object key
};

}  // namespace kernel
}  // namespace pdfcpp
