#ifndef PDFCPP_SIGNATURES_SIGNATURES_H_
#define PDFCPP_SIGNATURES_SIGNATURES_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/source/output_stream.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"

namespace pdfcpp {
namespace signatures {

using kernel::Rectangle;

/// An X.509 certificate (OpenSSL backed).
class Certificate {
 public:
  Certificate();
  ~Certificate();
  Certificate(const Certificate&);
  Certificate& operator=(const Certificate&);
  /// Loads all certificates from PEM data.
  static std::vector<Certificate> FromPem(const Bytes& pem);
  static Certificate FromDer(const Bytes& der);
  std::string GetSubjectName() const;
  std::string GetIssuerName() const;
  /// Common name (CN) of the subject.
  std::string GetSubjectCommonName() const;
  Bytes GetEncoded() const;
  void* Handle() const { return x509_; }

 private:
  explicit Certificate(void* x509);
  void* x509_ = nullptr;
  friend class Pkcs12;
};

/// A private key (OpenSSL backed).
class PrivateKey {
 public:
  PrivateKey();
  ~PrivateKey();
  PrivateKey(const PrivateKey&);
  PrivateKey& operator=(const PrivateKey&);
  static PrivateKey FromPem(const Bytes& pem, const std::string& password = "");
  /// Generates an RSA key (useful for tests and self-signed certificates).
  static PrivateKey GenerateRsa(int bits = 2048);
  /// Creates a self-signed certificate for this key.
  Certificate CreateSelfSignedCertificate(const std::string& common_name,
                                          int days = 365) const;
  void* Handle() const { return pkey_; }

 private:
  explicit PrivateKey(void* pkey);
  void* pkey_ = nullptr;
  friend class Pkcs12;
};

/// Loads a key and certificate chain from a PKCS#12 (.p12/.pfx) file.
class Pkcs12 {
 public:
  static Pkcs12 Load(const Bytes& data, const std::string& password);
  PrivateKey key;
  std::vector<Certificate> chain;
};

/// Port of PdfSignatureAppearance properties used for the signature
/// dictionary/widget.
struct SignerProperties {
  std::string field_name;
  std::string reason;
  std::string location;
  std::string contact;
  std::string signer_name;
  int page_number = 1;
  Rectangle page_rect{0, 0, 0, 0};  // empty rectangle = invisible signature
  std::string
      layer2_text;  // text of a visible signature (default: signer/date/reason)
};

/// Port of com.itextpdf.signatures.PdfSigner (detached CMS signatures,
/// adbe.pkcs7.detached / ETSI.CAdES.detached).
class PdfSigner {
 public:
  enum class CryptoStandard { kCms, kCades };

  PdfSigner(kernel::PdfReader& reader, io::OutputStream& output_stream,
            const kernel::StampingProperties& properties =
                kernel::StampingProperties().UseAppendMode());
  PdfSigner& SetSignerProperties(const SignerProperties& props) {
    props_ = props;
    return *this;
  }
  SignerProperties& GetSignerProperties() { return props_; }
  PdfSigner& SetFieldName(const std::string& name) {
    props_.field_name = name;
    return *this;
  }
  /// Signs the document. digest_algorithm: "SHA256", "SHA384", "SHA512" or
  /// "SHA1".
  void SignDetached(const PrivateKey& key,
                    const std::vector<Certificate>& chain,
                    const std::string& digest_algorithm = "SHA256",
                    CryptoStandard standard = CryptoStandard::kCms,
                    int estimated_size = 0);
  /// Builds a detached CMS signature over `data` (port of PdfPKCS7 signing).
  static Bytes CreateCmsSignature(const Bytes& data, const PrivateKey& key,
                                  const std::vector<Certificate>& chain,
                                  const std::string& digest_algorithm);

 private:
  kernel::PdfReader& reader_;
  io::OutputStream& out_;
  kernel::StampingProperties properties_;
  SignerProperties props_;
};

/// Result of a signature check.
struct SignatureVerificationResult {
  bool integrity_and_authenticity =
      false;  // digest matches and CMS signature is valid
  bool signature_covers_whole_document = false;
  std::string signer_name;
  std::string sub_filter;
  std::string sign_date;
  std::string reason;
  std::string location;
  std::string error;
};

/// Port of SignatureUtil (signature discovery and integrity verification).
class SignatureUtil {
 public:
  explicit SignatureUtil(kernel::PdfDocument& document);
  /// Names of signed signature fields.
  std::vector<std::string> GetSignatureNames();
  std::vector<std::string> GetBlankSignatureNames();
  kernel::PdfDictionaryPtr GetSignatureDictionary(const std::string& name);
  bool SignatureCoversWholeDocument(const std::string& name);
  SignatureVerificationResult VerifySignature(const std::string& name);
  int GetTotalRevisions();

 private:
  void Populate();
  kernel::PdfDocument& document_;
  std::vector<std::pair<std::string, kernel::PdfDictionaryPtr>> signed_;
  std::vector<std::string> blank_;
  bool populated_ = false;
};

}  // namespace signatures
}  // namespace pdfcpp

#endif  // PDFCPP_SIGNATURES_SIGNATURES_H_
