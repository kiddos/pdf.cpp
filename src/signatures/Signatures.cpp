#include "pdfcpp/signatures/Signatures.h"

#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstring>

#include "pdfcpp/forms/Forms.h"
#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace signatures {

using namespace kernel;

static std::string opensslError() {
    unsigned long e = ERR_get_error();
    if (!e) return "unknown OpenSSL error";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof buf);
    return buf;
}

// ============================================================ Certificate
Certificate::Certificate() = default;
Certificate::Certificate(void* x509) : x509_(x509) {}
Certificate::~Certificate() {
    if (x509_) X509_free(static_cast<X509*>(x509_));
}
Certificate::Certificate(const Certificate& o) : x509_(o.x509_) {
    if (x509_) X509_up_ref(static_cast<X509*>(x509_));
}
Certificate& Certificate::operator=(const Certificate& o) {
    if (this != &o) {
        if (x509_) X509_free(static_cast<X509*>(x509_));
        x509_ = o.x509_;
        if (x509_) X509_up_ref(static_cast<X509*>(x509_));
    }
    return *this;
}

std::vector<Certificate> Certificate::fromPem(const Bytes& pem) {
    std::vector<Certificate> certs;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    while (X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) certs.push_back(Certificate(x));
    BIO_free(bio);
    ERR_clear_error();
    return certs;
}

Certificate Certificate::fromDer(const Bytes& der) {
    const unsigned char* p = der.data();
    X509* x = d2i_X509(nullptr, &p, static_cast<long>(der.size()));
    if (!x) throw PdfException("Cannot parse certificate: " + opensslError());
    return Certificate(x);
}

static std::string nameToString(X509_NAME* name) {
    BIO* bio = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string s(data, static_cast<size_t>(len));
    BIO_free(bio);
    return s;
}

std::string Certificate::getSubjectName() const { return x509_ ? nameToString(X509_get_subject_name(static_cast<X509*>(x509_))) : ""; }
std::string Certificate::getIssuerName() const { return x509_ ? nameToString(X509_get_issuer_name(static_cast<X509*>(x509_))) : ""; }

std::string Certificate::getSubjectCommonName() const {
    if (!x509_) return std::string();
    X509_NAME* name = X509_get_subject_name(static_cast<X509*>(x509_));
    int idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (idx < 0) return getSubjectName();
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, idx));
    unsigned char* utf8 = nullptr;
    int len = ASN1_STRING_to_UTF8(&utf8, data);
    std::string cn = len > 0 ? std::string(reinterpret_cast<char*>(utf8), len) : std::string();
    OPENSSL_free(utf8);
    return cn;
}

Bytes Certificate::getEncoded() const {
    if (!x509_) return Bytes();
    int len = i2d_X509(static_cast<X509*>(x509_), nullptr);
    Bytes out(len);
    unsigned char* p = out.data();
    i2d_X509(static_cast<X509*>(x509_), &p);
    return out;
}

// ============================================================ PrivateKey
PrivateKey::PrivateKey() = default;
PrivateKey::PrivateKey(void* pkey) : pkey_(pkey) {}
PrivateKey::~PrivateKey() {
    if (pkey_) EVP_PKEY_free(static_cast<EVP_PKEY*>(pkey_));
}
PrivateKey::PrivateKey(const PrivateKey& o) : pkey_(o.pkey_) {
    if (pkey_) EVP_PKEY_up_ref(static_cast<EVP_PKEY*>(pkey_));
}
PrivateKey& PrivateKey::operator=(const PrivateKey& o) {
    if (this != &o) {
        if (pkey_) EVP_PKEY_free(static_cast<EVP_PKEY*>(pkey_));
        pkey_ = o.pkey_;
        if (pkey_) EVP_PKEY_up_ref(static_cast<EVP_PKEY*>(pkey_));
    }
    return *this;
}

PrivateKey PrivateKey::fromPem(const Bytes& pem, const std::string& password) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr,
                                            password.empty() ? nullptr : const_cast<char*>(password.c_str()));
    BIO_free(bio);
    if (!key) throw PdfException("Cannot read private key: " + opensslError());
    return PrivateKey(key);
}

PrivateKey PrivateKey::generateRsa(int bits) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* key = nullptr;
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 || EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw PdfException("Cannot generate RSA key: " + opensslError());
    }
    EVP_PKEY_CTX_free(ctx);
    return PrivateKey(key);
}

Certificate PrivateKey::createSelfSignedCertificate(const std::string& commonName, int days) const {
    X509* x = X509_new();
    X509_set_version(x, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x), static_cast<long>(time(nullptr)));
    X509_gmtime_adj(X509_getm_notBefore(x), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x), 60L * 60 * 24 * days);
    X509_set_pubkey(x, static_cast<EVP_PKEY*>(pkey_));
    X509_NAME* name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, reinterpret_cast<const unsigned char*>(commonName.c_str()), -1, -1, 0);
    X509_set_issuer_name(x, name);
    if (!X509_sign(x, static_cast<EVP_PKEY*>(pkey_), EVP_sha256())) {
        X509_free(x);
        throw PdfException("Cannot sign certificate: " + opensslError());
    }
    return Certificate::fromDer([&] {
        int len = i2d_X509(x, nullptr);
        Bytes der(len);
        unsigned char* p = der.data();
        i2d_X509(x, &p);
        X509_free(x);
        return der;
    }());
}

Pkcs12 Pkcs12::load(const Bytes& data, const std::string& password) {
    BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    PKCS12* p12 = d2i_PKCS12_bio(bio, nullptr);
    BIO_free(bio);
    if (!p12) throw PdfException("Cannot parse PKCS#12 data: " + opensslError());
    EVP_PKEY* pkey = nullptr;
    X509* cert = nullptr;
    STACK_OF(X509)* ca = nullptr;
    if (!PKCS12_parse(p12, password.c_str(), &pkey, &cert, &ca)) {
        PKCS12_free(p12);
        throw PdfException("Cannot open PKCS#12 (wrong password?): " + opensslError());
    }
    PKCS12_free(p12);
    Pkcs12 result;
    result.key = PrivateKey(pkey);
    if (cert) result.chain.push_back(Certificate(cert));
    if (ca) {
        while (sk_X509_num(ca) > 0) result.chain.push_back(Certificate(sk_X509_shift(ca)));
        sk_X509_free(ca);
    }
    return result;
}

// ============================================================ PdfSigner
PdfSigner::PdfSigner(PdfReader& reader, io::OutputStream& outputStream, const StampingProperties& properties)
    : reader_(reader), out_(outputStream), properties_(properties) {}

static const EVP_MD* digestByName(const std::string& name) {
    std::string n;
    for (char c : name)
        if (c != '-') n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (n == "SHA1") return EVP_sha1();
    if (n == "SHA384") return EVP_sha384();
    if (n == "SHA512") return EVP_sha512();
    return EVP_sha256();
}

Bytes PdfSigner::createCmsSignature(const Bytes& data, const PrivateKey& key, const std::vector<Certificate>& chain,
                                    const std::string& digestAlgorithm) {
    if (chain.empty()) throw PdfException("The certificate chain is empty.");
    BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    STACK_OF(X509)* extra = sk_X509_new_null();
    for (size_t i = 1; i < chain.size(); ++i) sk_X509_push(extra, static_cast<X509*>(chain[i].handle()));
    const unsigned int flags = CMS_DETACHED | CMS_BINARY | CMS_PARTIAL | CMS_NOSMIMECAP;
    CMS_ContentInfo* cms = CMS_sign(nullptr, nullptr, extra, nullptr, flags);
    bool ok = cms != nullptr;
    if (ok) ok = CMS_add1_signer(cms, static_cast<X509*>(chain[0].handle()), static_cast<EVP_PKEY*>(key.handle()),
                                 digestByName(digestAlgorithm), CMS_BINARY | CMS_NOSMIMECAP) != nullptr;
    if (ok) ok = CMS_final(cms, in, nullptr, CMS_DETACHED | CMS_BINARY) == 1;
    Bytes der;
    if (ok) {
        int len = i2d_CMS_ContentInfo(cms, nullptr);
        der.resize(len);
        unsigned char* p = der.data();
        i2d_CMS_ContentInfo(cms, &p);
    }
    std::string err = ok ? std::string() : opensslError();
    if (cms) CMS_ContentInfo_free(cms);
    sk_X509_free(extra);
    BIO_free(in);
    if (!ok) throw PdfException("Cannot create the CMS signature: " + err);
    return der;
}

void PdfSigner::signDetached(const PrivateKey& key, const std::vector<Certificate>& chain, const std::string& digestAlgorithm,
                             CryptoStandard standard, int estimatedSize) {
    if (chain.empty()) throw PdfException("The certificate chain is empty.");
    if (estimatedSize <= 0) {
        estimatedSize = 8192;
        for (const auto& c : chain) estimatedSize += static_cast<int>(c.getEncoded().size()) + 10;
    }
    io::ByteArrayOutputStream temp;
    auto byteRangeLiteral = std::make_shared<PdfLiteral>(80);
    auto contentsLiteral = std::make_shared<PdfLiteral>(estimatedSize * 2 + 2);
    {
        PdfWriter writer(temp);
        PdfDocument document(reader_, writer, properties_);
        auto acroForm = forms::PdfAcroForm::getAcroForm(document, true);
        acroForm->getPdfObject()->put(PdfName::SigFlags, makeNumber(3));
        acroForm->getPdfObject()->setModified();
        std::string fieldName = props_.fieldName;
        if (fieldName.empty()) {
            auto existing = acroForm->getAllFormFields();
            int i = 1;
            do {
                fieldName = "Signature" + std::to_string(i++);
            } while (existing.count(fieldName));
        }
        PdfPage* page = document.getPage(std::max(1, std::min(props_.pageNumber, document.getNumberOfPages())));
        // signature dictionary
        auto sigDict = std::make_shared<PdfDictionary>();
        sigDict->put(PdfName::Type, PdfName::Sig);
        sigDict->put(PdfName::Filter, PdfName::Adobe_PPKLite);
        sigDict->put(PdfName::SubFilter, standard == CryptoStandard::CADES ? PdfName::ETSI_CAdES_DETACHED : PdfName::Adbe_pkcs7_detached);
        sigDict->put(PdfName::M, std::make_shared<PdfString>(PdfDate::getCurrentDateString()));
        std::string signerName = props_.signerName.empty() ? chain[0].getSubjectCommonName() : props_.signerName;
        if (!signerName.empty()) sigDict->put(PdfName::Name, std::make_shared<PdfString>(signerName, io::PdfEncodings::UNICODE_BIG));
        if (!props_.reason.empty()) sigDict->put(PdfName::Reason, std::make_shared<PdfString>(props_.reason, io::PdfEncodings::UNICODE_BIG));
        if (!props_.location.empty()) sigDict->put(PdfName::Location, std::make_shared<PdfString>(props_.location, io::PdfEncodings::UNICODE_BIG));
        if (!props_.contact.empty()) sigDict->put(PdfName::ContactInfo, std::make_shared<PdfString>(props_.contact, io::PdfEncodings::UNICODE_BIG));
        sigDict->put(PdfName::ByteRange, byteRangeLiteral);
        sigDict->put(PdfName::Contents, contentsLiteral);
        sigDict->makeIndirect(&document);
        // signature field (merged with its widget)
        auto field = std::make_shared<PdfDictionary>();
        field->put(PdfName::FT, PdfName::Sig);
        field->put(PdfName::T, std::make_shared<PdfString>(fieldName, io::PdfEncodings::UNICODE_BIG));
        field->put(PdfName::Type, PdfName::Annot);
        field->put(PdfName::Subtype, PdfName::Widget);
        field->put(PdfName::F, makeNumber(4 | 128));  // print, locked
        field->put(PdfName::Rect, std::make_shared<PdfArray>(props_.pageRect));
        field->put(PdfName::V, sigDict);
        field->makeIndirect(&document);
        // appearance (empty for invisible signatures, as required by PDF/A)
        Rectangle r = props_.pageRect;
        PdfFormXObject appearance(Rectangle(0, 0, r.getWidth(), r.getHeight()));
        if (r.getWidth() > 0 && r.getHeight() > 0) {
            PdfCanvas canvas(appearance, &document);
            std::string text = props_.layer2Text;
            if (text.empty()) {
                text = "Digitally signed by " + signerName + "\nDate: " + PdfDate::getCurrentDateString().substr(2, 8);
                if (!props_.reason.empty()) text += "\nReason: " + props_.reason;
                if (!props_.location.empty()) text += "\nLocation: " + props_.location;
            }
            auto font = PdfFontFactory::createFont(io::StandardFonts::HELVETICA, io::PdfEncodings::WINANSI);
            float size = std::max(4.f, std::min(10.f, r.getHeight() / 5));
            float y = r.getHeight() - size - 2;
            canvas.beginText().setFontAndSize(font, size);
            size_t start = 0;
            while (start <= text.size()) {
                size_t nl = text.find('\n', start);
                std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                canvas.setTextMatrix(2, y).showText(line);
                y -= size * 1.2f;
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            canvas.endText();
        }
        auto ap = std::make_shared<PdfDictionary>();
        appearance.getPdfObject()->makeIndirect(&document);
        ap->put(PdfName::N, appearance.getPdfObject());
        field->put(PdfName::AP, ap);
        page->addAnnotation(field);
        auto fields = acroForm->getPdfObject()->getAsArray(PdfName::Fields);
        if (!fields) {
            fields = std::make_shared<PdfArray>();
            acroForm->getPdfObject()->put(PdfName::Fields, fields);
        }
        fields->add(field);
        if (fields->getIndirectReference()) fields->setModified();
        // write the signature dictionary directly (never inside an object stream) to know the literal offsets
        sigDict->flush(false);
        document.close();
    }
    Bytes pdf = temp.toByteArray();
    int64_t contentsStart = contentsLiteral->getPosition();
    int64_t contentsEnd = contentsStart + contentsLiteral->getBytesCount();
    int64_t byteRangePos = byteRangeLiteral->getPosition();
    if (contentsStart <= 0 || byteRangePos <= 0) throw PdfException("Cannot locate the signature placeholders.");
    std::string br = "[0 " + std::to_string(contentsStart) + " " + std::to_string(contentsEnd) + " " +
                     std::to_string(static_cast<int64_t>(pdf.size()) - contentsEnd) + "]";
    if (br.size() > 80) throw PdfException("ByteRange placeholder too small.");
    br.resize(80, ' ');
    std::memcpy(pdf.data() + byteRangePos, br.data(), br.size());
    Bytes data(pdf.begin(), pdf.begin() + contentsStart);
    data.insert(data.end(), pdf.begin() + contentsEnd, pdf.end());
    Bytes cms = createCmsSignature(data, key, chain, digestAlgorithm);
    if (static_cast<int>(cms.size()) > estimatedSize) throw PdfException("Not enough space for the signature contents.");
    static const char* hex = "0123456789ABCDEF";
    std::string contents = "<";
    for (uint8_t b : cms) {
        contents += hex[b >> 4];
        contents += hex[b & 0xf];
    }
    contents.resize(static_cast<size_t>(contentsEnd - contentsStart - 1), '0');
    contents += ">";
    std::memcpy(pdf.data() + contentsStart, contents.data(), contents.size());
    out_.write(pdf);
}

// ============================================================ SignatureUtil
SignatureUtil::SignatureUtil(PdfDocument& document) : document_(document) {}

void SignatureUtil::populate() {
    if (populated_) return;
    populated_ = true;
    auto acroForm = forms::PdfAcroForm::getAcroForm(document_, false);
    if (!acroForm) return;
    std::vector<std::pair<int64_t, std::pair<std::string, PdfDictionaryPtr>>> sorted;
    for (auto& kv : acroForm->getAllFormFields()) {
        if (!nameEquals(kv.second->getFormType(), PdfName::Sig)) continue;
        auto v = kv.second->getPdfObject()->getAsDictionary(PdfName::V);
        if (!v || !v->containsKey(PdfName::Contents) || !v->getAsArray(PdfName::ByteRange)) {
            blank_.push_back(kv.first);
            continue;
        }
        auto br = v->getAsArray(PdfName::ByteRange)->toLongArray();
        int64_t end = br.size() >= 4 ? br[2] + br[3] : 0;
        sorted.push_back({end, {kv.first, v}});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& s : sorted) signed_.push_back(s.second);
}

std::vector<std::string> SignatureUtil::getSignatureNames() {
    populate();
    std::vector<std::string> names;
    for (auto& s : signed_) names.push_back(s.first);
    return names;
}

std::vector<std::string> SignatureUtil::getBlankSignatureNames() {
    populate();
    return blank_;
}

int SignatureUtil::getTotalRevisions() {
    populate();
    return static_cast<int>(signed_.size());
}

PdfDictionaryPtr SignatureUtil::getSignatureDictionary(const std::string& name) {
    populate();
    for (auto& s : signed_)
        if (s.first == name) return s.second;
    return nullptr;
}

bool SignatureUtil::signatureCoversWholeDocument(const std::string& name) {
    auto dict = getSignatureDictionary(name);
    if (!dict || !document_.getReader()) return false;
    auto br = dict->getAsArray(PdfName::ByteRange)->toLongArray();
    return br.size() >= 4 && br[2] + br[3] == document_.getReader()->getFileLength();
}

SignatureVerificationResult SignatureUtil::verifySignature(const std::string& name) {
    SignatureVerificationResult result;
    auto dict = getSignatureDictionary(name);
    if (!dict) {
        result.error = "No signature named " + name;
        return result;
    }
    auto subFilter = dict->getAsName(PdfName::SubFilter);
    result.subFilter = subFilter ? subFilter->getValue() : "";
    if (auto m = dict->getAsString(PdfName::M)) result.signDate = m->toUnicodeString();
    if (auto r = dict->getAsString(PdfName::Reason)) result.reason = r->toUnicodeString();
    if (auto l = dict->getAsString(PdfName::Location)) result.location = l->toUnicodeString();
    result.signatureCoversWholeDocument = signatureCoversWholeDocument(name);
    auto contentsStr = dict->getAsString(PdfName::Contents);
    if (!contentsStr) {
        result.error = "Missing /Contents";
        return result;
    }
    // signature contents are never encrypted: decode the raw literal
    Bytes contents = io::PdfTokenizer::decodeStringContent(contentsStr->getInternalContent(), contentsStr->isHexWriting());
    auto brArray = dict->getAsArray(PdfName::ByteRange);
    if (!brArray) {
        result.error = "Missing /ByteRange";
        return result;
    }
    if (document_.getReader() == nullptr) {
        result.error = "Document has no reader";
        return result;
    }
    auto br = brArray->toLongArray();
    auto file = document_.getReader()->getSafeFile();
    const int64_t fileLength = file->length();
    Bytes data;
    for (size_t i = 0; i + 1 < br.size(); i += 2) {
        // Forged ranges must not trigger huge allocations or reads outside the file.
        if (br[i] < 0 || br[i + 1] < 0 || br[i] > fileLength || br[i + 1] > fileLength - br[i]) {
            result.error = "Invalid /ByteRange";
            return result;
        }
        Bytes part(static_cast<size_t>(br[i + 1]));
        file->seek(br[i]);
        file->readFully(part);
        data.insert(data.end(), part.begin(), part.end());
    }
    const unsigned char* p = contents.data();
    CMS_ContentInfo* cms = d2i_CMS_ContentInfo(nullptr, &p, static_cast<long>(contents.size()));
    if (!cms) {
        result.error = "Cannot parse the CMS signature: " + opensslError();
        return result;
    }
    BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    int ok = CMS_verify(cms, nullptr, nullptr, in, nullptr, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY);
    result.integrityAndAuthenticity = ok == 1;
    if (!ok) result.error = opensslError();
    STACK_OF(X509)* signers = CMS_get0_signers(cms);
    if (signers && sk_X509_num(signers) > 0) {
        X509* x = sk_X509_value(signers, 0);
        X509_up_ref(x);
        Certificate c = Certificate::fromDer([&] {
            int len = i2d_X509(x, nullptr);
            Bytes der(len);
            unsigned char* q = der.data();
            i2d_X509(x, &q);
            X509_free(x);
            return der;
        }());
        result.signerName = c.getSubjectCommonName();
    }
    if (signers) sk_X509_free(signers);
    BIO_free(in);
    CMS_ContentInfo_free(cms);
    ERR_clear_error();
    return result;
}

}  // namespace signatures
}  // namespace pdfcpp
