#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"

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
    static std::vector<Certificate> fromPem(const Bytes& pem);
    static Certificate fromDer(const Bytes& der);
    std::string getSubjectName() const;
    std::string getIssuerName() const;
    /// Common name (CN) of the subject.
    std::string getSubjectCommonName() const;
    Bytes getEncoded() const;
    void* handle() const { return x509_; }

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
    static PrivateKey fromPem(const Bytes& pem, const std::string& password = "");
    /// Generates an RSA key (useful for tests and self-signed certificates).
    static PrivateKey generateRsa(int bits = 2048);
    /// Creates a self-signed certificate for this key.
    Certificate createSelfSignedCertificate(const std::string& commonName, int days = 365) const;
    void* handle() const { return pkey_; }

private:
    explicit PrivateKey(void* pkey);
    void* pkey_ = nullptr;
    friend class Pkcs12;
};

/// Loads a key and certificate chain from a PKCS#12 (.p12/.pfx) file.
class Pkcs12 {
public:
    static Pkcs12 load(const Bytes& data, const std::string& password);
    PrivateKey key;
    std::vector<Certificate> chain;
};

/// Port of PdfSignatureAppearance properties used for the signature dictionary/widget.
struct SignerProperties {
    std::string fieldName;
    std::string reason;
    std::string location;
    std::string contact;
    std::string signerName;
    int pageNumber = 1;
    Rectangle pageRect{0, 0, 0, 0};  // empty rectangle = invisible signature
    std::string layer2Text;          // text of a visible signature (default: signer/date/reason)
};

/// Port of com.itextpdf.signatures.PdfSigner (detached CMS signatures, adbe.pkcs7.detached / ETSI.CAdES.detached).
class PdfSigner {
public:
    enum class CryptoStandard { CMS, CADES };

    PdfSigner(kernel::PdfReader& reader, io::OutputStream& outputStream, const kernel::StampingProperties& properties =
                                                                              kernel::StampingProperties().useAppendMode());
    PdfSigner& setSignerProperties(const SignerProperties& props) {
        props_ = props;
        return *this;
    }
    SignerProperties& getSignerProperties() { return props_; }
    PdfSigner& setFieldName(const std::string& name) {
        props_.fieldName = name;
        return *this;
    }
    /// Signs the document. digestAlgorithm: "SHA256", "SHA384", "SHA512" or "SHA1".
    void signDetached(const PrivateKey& key, const std::vector<Certificate>& chain, const std::string& digestAlgorithm = "SHA256",
                      CryptoStandard standard = CryptoStandard::CMS, int estimatedSize = 0);
    /// Builds a detached CMS signature over `data` (port of PdfPKCS7 signing).
    static Bytes createCmsSignature(const Bytes& data, const PrivateKey& key, const std::vector<Certificate>& chain,
                                    const std::string& digestAlgorithm);

private:
    kernel::PdfReader& reader_;
    io::OutputStream& out_;
    kernel::StampingProperties properties_;
    SignerProperties props_;
};

/// Result of a signature check.
struct SignatureVerificationResult {
    bool integrityAndAuthenticity = false;  // digest matches and CMS signature is valid
    bool signatureCoversWholeDocument = false;
    std::string signerName;
    std::string subFilter;
    std::string signDate;
    std::string reason;
    std::string location;
    std::string error;
};

/// Port of SignatureUtil (signature discovery and integrity verification).
class SignatureUtil {
public:
    explicit SignatureUtil(kernel::PdfDocument& document);
    /// Names of signed signature fields.
    std::vector<std::string> getSignatureNames();
    std::vector<std::string> getBlankSignatureNames();
    kernel::PdfDictionaryPtr getSignatureDictionary(const std::string& name);
    bool signatureCoversWholeDocument(const std::string& name);
    SignatureVerificationResult verifySignature(const std::string& name);
    int getTotalRevisions();

private:
    void populate();
    kernel::PdfDocument& document_;
    std::vector<std::pair<std::string, kernel::PdfDictionaryPtr>> signed_;
    std::vector<std::string> blank_;
    bool populated_ = false;
};

}  // namespace signatures
}  // namespace pdfcpp
