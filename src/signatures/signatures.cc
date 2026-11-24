#include "pdfcpp/signatures/signatures.h"

#include <openssl/bio.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstring>

#include "pdfcpp/forms/forms.h"
#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace signatures {

using namespace kernel;

static std::string OpensslError() {
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

std::vector<Certificate> Certificate::FromPem(const Bytes& pem) {
  std::vector<Certificate> certs;
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  while (X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr))
    certs.push_back(Certificate(x));
  BIO_free(bio);
  ERR_clear_error();
  return certs;
}

Certificate Certificate::FromDer(const Bytes& der) {
  const unsigned char* p = der.data();
  X509* x = d2i_X509(nullptr, &p, static_cast<long>(der.size()));
  if (!x) throw PdfException("Cannot parse certificate: " + OpensslError());
  return Certificate(x);
}

static std::string NameToString(X509_NAME* name) {
  BIO* bio = BIO_new(BIO_s_mem());
  X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
  char* data = nullptr;
  long len = BIO_get_mem_data(bio, &data);
  std::string s(data, static_cast<size_t>(len));
  BIO_free(bio);
  return s;
}

std::string Certificate::GetSubjectName() const {
  return x509_ ? NameToString(X509_get_subject_name(static_cast<X509*>(x509_)))
               : "";
}
std::string Certificate::GetIssuerName() const {
  return x509_ ? NameToString(X509_get_issuer_name(static_cast<X509*>(x509_)))
               : "";
}

std::string Certificate::GetSubjectCommonName() const {
  if (!x509_) return std::string();
  X509_NAME* name = X509_get_subject_name(static_cast<X509*>(x509_));
  int idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
  if (idx < 0) return GetSubjectName();
  ASN1_STRING* data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, idx));
  unsigned char* utf8 = nullptr;
  int len = ASN1_STRING_to_UTF8(&utf8, data);
  std::string cn =
      len > 0 ? std::string(reinterpret_cast<char*>(utf8), len) : std::string();
  OPENSSL_free(utf8);
  return cn;
}

Bytes Certificate::GetEncoded() const {
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

PrivateKey PrivateKey::FromPem(const Bytes& pem, const std::string& password) {
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  EVP_PKEY* key = PEM_read_bio_PrivateKey(
      bio, nullptr, nullptr,
      password.empty() ? nullptr : const_cast<char*>(password.c_str()));
  BIO_free(bio);
  if (!key) throw PdfException("Cannot read private key: " + OpensslError());
  return PrivateKey(key);
}

PrivateKey PrivateKey::GenerateRsa(int bits) {
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  EVP_PKEY* key = nullptr;
  if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 ||
      EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 ||
      EVP_PKEY_keygen(ctx, &key) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    throw PdfException("Cannot generate RSA key: " + OpensslError());
  }
  EVP_PKEY_CTX_free(ctx);
  return PrivateKey(key);
}

Certificate PrivateKey::CreateSelfSignedCertificate(
    const std::string& common_name, int days) const {
  X509* x = X509_new();
  X509_set_version(x, 2);
  ASN1_INTEGER_set(X509_get_serialNumber(x), static_cast<long>(time(nullptr)));
  X509_gmtime_adj(X509_getm_notBefore(x), -3600);
  X509_gmtime_adj(X509_getm_notAfter(x), 60L * 60 * 24 * days);
  X509_set_pubkey(x, static_cast<EVP_PKEY*>(pkey_));
  X509_NAME* name = X509_get_subject_name(x);
  X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_UTF8,
      reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0);
  X509_set_issuer_name(x, name);
  if (!X509_sign(x, static_cast<EVP_PKEY*>(pkey_), EVP_sha256())) {
    X509_free(x);
    throw PdfException("Cannot sign certificate: " + OpensslError());
  }
  return Certificate::FromDer([&] {
    int len = i2d_X509(x, nullptr);
    Bytes der(len);
    unsigned char* p = der.data();
    i2d_X509(x, &p);
    X509_free(x);
    return der;
  }());
}

Pkcs12 Pkcs12::Load(const Bytes& data, const std::string& password) {
  BIO* bio = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
  PKCS12* p12 = d2i_PKCS12_bio(bio, nullptr);
  BIO_free(bio);
  if (!p12) throw PdfException("Cannot parse PKCS#12 data: " + OpensslError());
  EVP_PKEY* pkey = nullptr;
  X509* cert = nullptr;
  STACK_OF(X509)* ca = nullptr;
  if (!PKCS12_parse(p12, password.c_str(), &pkey, &cert, &ca)) {
    PKCS12_free(p12);
    throw PdfException("Cannot open PKCS#12 (wrong password?): " +
                       OpensslError());
  }
  PKCS12_free(p12);
  Pkcs12 result;
  result.key = PrivateKey(pkey);
  if (cert) result.chain.push_back(Certificate(cert));
  if (ca) {
    while (sk_X509_num(ca) > 0)
      result.chain.push_back(Certificate(sk_X509_shift(ca)));
    sk_X509_free(ca);
  }
  return result;
}

// ============================================================ PdfSigner
PdfSigner::PdfSigner(PdfReader& reader, io::OutputStream& output_stream,
                     const StampingProperties& properties)
    : reader_(reader), out_(output_stream), properties_(properties) {}

static const EVP_MD* DigestByName(const std::string& name) {
  std::string n;
  for (char c : name)
    if (c != '-')
      n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (n == "SHA1") return EVP_sha1();
  if (n == "SHA384") return EVP_sha384();
  if (n == "SHA512") return EVP_sha512();
  return EVP_sha256();
}

Bytes PdfSigner::CreateCmsSignature(const Bytes& data, const PrivateKey& key,
                                    const std::vector<Certificate>& chain,
                                    const std::string& digest_algorithm) {
  if (chain.empty()) throw PdfException("The certificate chain is empty.");
  BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
  STACK_OF(X509)* extra = sk_X509_new_null();
  for (size_t i = 1; i < chain.size(); ++i)
    sk_X509_push(extra, static_cast<X509*>(chain[i].Handle()));
  const unsigned int flags =
      CMS_DETACHED | CMS_BINARY | CMS_PARTIAL | CMS_NOSMIMECAP;
  CMS_ContentInfo* cms = CMS_sign(nullptr, nullptr, extra, nullptr, flags);
  bool ok = cms != nullptr;
  if (ok)
    ok = CMS_add1_signer(cms, static_cast<X509*>(chain[0].Handle()),
                         static_cast<EVP_PKEY*>(key.Handle()),
                         DigestByName(digest_algorithm),
                         CMS_BINARY | CMS_NOSMIMECAP) != nullptr;
  if (ok) ok = CMS_final(cms, in, nullptr, CMS_DETACHED | CMS_BINARY) == 1;
  Bytes der;
  if (ok) {
    int len = i2d_CMS_ContentInfo(cms, nullptr);
    der.resize(len);
    unsigned char* p = der.data();
    i2d_CMS_ContentInfo(cms, &p);
  }
  std::string err = ok ? std::string() : OpensslError();
  if (cms) CMS_ContentInfo_free(cms);
  sk_X509_free(extra);
  BIO_free(in);
  if (!ok) throw PdfException("Cannot create the CMS signature: " + err);
  return der;
}

void PdfSigner::SignDetached(const PrivateKey& key,
                             const std::vector<Certificate>& chain,
                             const std::string& digest_algorithm,
                             CryptoStandard standard, int estimated_size) {
  if (chain.empty()) throw PdfException("The certificate chain is empty.");
  if (estimated_size <= 0) {
    estimated_size = 8192;
    for (const auto& c : chain)
      estimated_size += static_cast<int>(c.GetEncoded().size()) + 10;
  }
  io::ByteArrayOutputStream temp;
  auto byte_range_literal = std::make_shared<PdfLiteral>(80);
  auto contents_literal = std::make_shared<PdfLiteral>(estimated_size * 2 + 2);
  {
    PdfWriter writer(temp);
    PdfDocument document(reader_, writer, properties_);
    auto acro_form = forms::PdfAcroForm::GetAcroForm(document, true);
    acro_form->GetPdfObject()->Put(PdfName::kSigFlags, MakeNumber(3));
    acro_form->GetPdfObject()->SetModified();
    std::string field_name = props_.field_name;
    if (field_name.empty()) {
      auto existing = acro_form->GetAllFormFields();
      int i = 1;
      do {
        field_name = "Signature" + std::to_string(i++);
      } while (existing.count(field_name));
    }
    PdfPage* page = document.GetPage(
        std::max(1, std::min(props_.page_number, document.GetNumberOfPages())));
    // signature dictionary
    auto sig_dict = std::make_shared<PdfDictionary>();
    sig_dict->Put(PdfName::kType, PdfName::kSig);
    sig_dict->Put(PdfName::kFilter, PdfName::kAdobe_PPKLite);
    sig_dict->Put(PdfName::kSubFilter, standard == CryptoStandard::kCades
                                           ? PdfName::kETSI_CAdES_DETACHED
                                           : PdfName::kAdbe_pkcs7_detached);
    sig_dict->Put(PdfName::kM,
                  std::make_shared<PdfString>(PdfDate::GetCurrentDateString()));
    std::string signer_name = props_.signer_name.empty()
                                  ? chain[0].GetSubjectCommonName()
                                  : props_.signer_name;
    if (!signer_name.empty())
      sig_dict->Put(PdfName::kName,
                    std::make_shared<PdfString>(signer_name,
                                                io::PdfEncodings::kUnicodeBig));
    if (!props_.reason.empty())
      sig_dict->Put(PdfName::kReason,
                    std::make_shared<PdfString>(props_.reason,
                                                io::PdfEncodings::kUnicodeBig));
    if (!props_.location.empty())
      sig_dict->Put(PdfName::kLocation,
                    std::make_shared<PdfString>(props_.location,
                                                io::PdfEncodings::kUnicodeBig));
    if (!props_.contact.empty())
      sig_dict->Put(PdfName::kContactInfo,
                    std::make_shared<PdfString>(props_.contact,
                                                io::PdfEncodings::kUnicodeBig));
    sig_dict->Put(PdfName::kByteRange, byte_range_literal);
    sig_dict->Put(PdfName::kContents, contents_literal);
    sig_dict->MakeIndirect(&document);
    // signature field (merged with its widget)
    auto field = std::make_shared<PdfDictionary>();
    field->Put(PdfName::kFT, PdfName::kSig);
    field->Put(PdfName::kT, std::make_shared<PdfString>(
                                field_name, io::PdfEncodings::kUnicodeBig));
    field->Put(PdfName::kType, PdfName::kAnnot);
    field->Put(PdfName::kSubtype, PdfName::kWidget);
    field->Put(PdfName::kF, MakeNumber(4 | 128));  // print, locked
    field->Put(PdfName::kRect, std::make_shared<PdfArray>(props_.page_rect));
    field->Put(PdfName::kV, sig_dict);
    field->MakeIndirect(&document);
    // appearance (empty for invisible signatures, as required by PDF/A)
    Rectangle r = props_.page_rect;
    PdfFormXObject appearance(Rectangle(0, 0, r.GetWidth(), r.GetHeight()));
    if (r.GetWidth() > 0 && r.GetHeight() > 0) {
      PdfCanvas canvas(appearance, &document);
      std::string text = props_.layer2_text;
      if (text.empty()) {
        text = "Digitally signed by " + signer_name +
               "\nDate: " + PdfDate::GetCurrentDateString().substr(2, 8);
        if (!props_.reason.empty()) text += "\nReason: " + props_.reason;
        if (!props_.location.empty()) text += "\nLocation: " + props_.location;
      }
      auto font = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica,
                                             io::PdfEncodings::kWinansi);
      float size = std::max(4.f, std::min(10.f, r.GetHeight() / 5));
      float y = r.GetHeight() - size - 2;
      canvas.BeginText().SetFontAndSize(font, size);
      size_t start = 0;
      while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(
            start, nl == std::string::npos ? std::string::npos : nl - start);
        canvas.SetTextMatrix(2, y).ShowText(line);
        y -= size * 1.2f;
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
      canvas.EndText();
    }
    auto ap = std::make_shared<PdfDictionary>();
    appearance.GetPdfObject()->MakeIndirect(&document);
    ap->Put(PdfName::kN, appearance.GetPdfObject());
    field->Put(PdfName::kAP, ap);
    page->AddAnnotation(field);
    auto fields = acro_form->GetPdfObject()->GetAsArray(PdfName::kFields);
    if (!fields) {
      fields = std::make_shared<PdfArray>();
      acro_form->GetPdfObject()->Put(PdfName::kFields, fields);
    }
    fields->Add(field);
    if (fields->GetIndirectReference()) fields->SetModified();
    // write the signature dictionary directly (never inside an object stream)
    // to know the literal offsets
    sig_dict->Flush(false);
    document.Close();
  }
  Bytes pdf = temp.ToByteArray();
  int64_t contents_start = contents_literal->GetPosition();
  int64_t contents_end = contents_start + contents_literal->GetBytesCount();
  int64_t byte_range_pos = byte_range_literal->GetPosition();
  if (contents_start <= 0 || byte_range_pos <= 0)
    throw PdfException("Cannot locate the signature placeholders.");
  std::string br =
      "[0 " + std::to_string(contents_start) + " " +
      std::to_string(contents_end) + " " +
      std::to_string(static_cast<int64_t>(pdf.size()) - contents_end) + "]";
  if (br.size() > 80) throw PdfException("ByteRange placeholder too small.");
  br.resize(80, ' ');
  std::memcpy(pdf.data() + byte_range_pos, br.data(), br.size());
  Bytes data(pdf.begin(), pdf.begin() + contents_start);
  data.insert(data.end(), pdf.begin() + contents_end, pdf.end());
  Bytes cms = CreateCmsSignature(data, key, chain, digest_algorithm);
  if (static_cast<int>(cms.size()) > estimated_size)
    throw PdfException("Not enough space for the signature contents.");
  static const char* hex = "0123456789ABCDEF";
  std::string contents = "<";
  for (uint8_t b : cms) {
    contents += hex[b >> 4];
    contents += hex[b & 0xf];
  }
  contents.resize(static_cast<size_t>(contents_end - contents_start - 1), '0');
  contents += ">";
  std::memcpy(pdf.data() + contents_start, contents.data(), contents.size());
  out_.Write(pdf);
}

// ============================================================ SignatureUtil
SignatureUtil::SignatureUtil(PdfDocument& document) : document_(document) {}

void SignatureUtil::Populate() {
  if (populated_) return;
  populated_ = true;
  auto acro_form = forms::PdfAcroForm::GetAcroForm(document_, false);
  if (!acro_form) return;
  std::vector<std::pair<int64_t, std::pair<std::string, PdfDictionaryPtr>>>
      sorted;
  for (auto& kv : acro_form->GetAllFormFields()) {
    if (!NameEquals(kv.second->GetFormType(), PdfName::kSig)) continue;
    auto v = kv.second->GetPdfObject()->GetAsDictionary(PdfName::kV);
    if (!v || !v->ContainsKey(PdfName::kContents) ||
        !v->GetAsArray(PdfName::kByteRange)) {
      blank_.push_back(kv.first);
      continue;
    }
    auto br = v->GetAsArray(PdfName::kByteRange)->ToLongArray();
    int64_t end = br.size() >= 4 ? br[2] + br[3] : 0;
    sorted.push_back({end, {kv.first, v}});
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (auto& s : sorted) signed_.push_back(s.second);
}

std::vector<std::string> SignatureUtil::GetSignatureNames() {
  Populate();
  std::vector<std::string> names;
  for (auto& s : signed_) names.push_back(s.first);
  return names;
}

std::vector<std::string> SignatureUtil::GetBlankSignatureNames() {
  Populate();
  return blank_;
}

int SignatureUtil::GetTotalRevisions() {
  Populate();
  return static_cast<int>(signed_.size());
}

PdfDictionaryPtr SignatureUtil::GetSignatureDictionary(
    const std::string& name) {
  Populate();
  for (auto& s : signed_)
    if (s.first == name) return s.second;
  return nullptr;
}

bool SignatureUtil::SignatureCoversWholeDocument(const std::string& name) {
  auto dict = GetSignatureDictionary(name);
  if (!dict || !document_.GetReader()) return false;
  auto br = dict->GetAsArray(PdfName::kByteRange)->ToLongArray();
  return br.size() >= 4 &&
         br[2] + br[3] == document_.GetReader()->GetFileLength();
}

SignatureVerificationResult SignatureUtil::VerifySignature(
    const std::string& name) {
  SignatureVerificationResult result;
  auto dict = GetSignatureDictionary(name);
  if (!dict) {
    result.error = "No signature named " + name;
    return result;
  }
  auto sub_filter = dict->GetAsName(PdfName::kSubFilter);
  result.sub_filter = sub_filter ? sub_filter->GetValue() : "";
  if (auto m = dict->GetAsString(PdfName::kM))
    result.sign_date = m->ToUnicodeString();
  if (auto r = dict->GetAsString(PdfName::kReason))
    result.reason = r->ToUnicodeString();
  if (auto l = dict->GetAsString(PdfName::kLocation))
    result.location = l->ToUnicodeString();
  result.signature_covers_whole_document = SignatureCoversWholeDocument(name);
  auto contents_str = dict->GetAsString(PdfName::kContents);
  if (!contents_str) {
    result.error = "Missing /Contents";
    return result;
  }
  // signature contents are never encrypted: decode the raw literal
  Bytes contents = io::PdfTokenizer::DecodeStringContent(
      contents_str->GetInternalContent(), contents_str->IsHexWriting());
  auto br_array = dict->GetAsArray(PdfName::kByteRange);
  if (!br_array) {
    result.error = "Missing /ByteRange";
    return result;
  }
  if (document_.GetReader() == nullptr) {
    result.error = "Document has no reader";
    return result;
  }
  auto br = br_array->ToLongArray();
  auto file = document_.GetReader()->GetSafeFile();
  const int64_t file_length = file->Length();
  Bytes data;
  for (size_t i = 0; i + 1 < br.size(); i += 2) {
    // Forged ranges must not trigger huge allocations or reads outside the
    // file.
    if (br[i] < 0 || br[i + 1] < 0 || br[i] > file_length ||
        br[i + 1] > file_length - br[i]) {
      result.error = "Invalid /ByteRange";
      return result;
    }
    Bytes part(static_cast<size_t>(br[i + 1]));
    file->Seek(br[i]);
    file->ReadFully(part);
    data.insert(data.end(), part.begin(), part.end());
  }
  const unsigned char* p = contents.data();
  CMS_ContentInfo* cms =
      d2i_CMS_ContentInfo(nullptr, &p, static_cast<long>(contents.size()));
  if (!cms) {
    result.error = "Cannot parse the CMS signature: " + OpensslError();
    return result;
  }
  BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
  int ok = CMS_verify(cms, nullptr, nullptr, in, nullptr,
                      CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY);
  result.integrity_and_authenticity = ok == 1;
  if (!ok) result.error = OpensslError();
  STACK_OF(X509)* signers = CMS_get0_signers(cms);
  if (signers && sk_X509_num(signers) > 0) {
    X509* x = sk_X509_value(signers, 0);
    X509_up_ref(x);
    Certificate c = Certificate::FromDer([&] {
      int len = i2d_X509(x, nullptr);
      Bytes der(len);
      unsigned char* q = der.data();
      i2d_X509(x, &q);
      X509_free(x);
      return der;
    }());
    result.signer_name = c.GetSubjectCommonName();
  }
  if (signers) sk_X509_free(signers);
  BIO_free(in);
  CMS_ContentInfo_free(cms);
  ERR_clear_error();
  return result;
}

}  // namespace signatures
}  // namespace pdfcpp
