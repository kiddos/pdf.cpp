#include "pdfcpp/kernel/crypto/pdf_encryption.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <chrono>
#include <cstring>
#include <random>

#include "pdfcpp/kernel/pdf/properties.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ CryptoUtil
static Bytes Digest(const EVP_MD* md, const Bytes& data) {
  Bytes out(EVP_MAX_MD_SIZE);
  unsigned int len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, md, nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  EVP_DigestFinal_ex(ctx, out.data(), &len);
  EVP_MD_CTX_free(ctx);
  out.resize(len);
  return out;
}

Bytes CryptoUtil::Md5(const Bytes& data) { return Digest(EVP_md5(), data); }
Bytes CryptoUtil::Sha256(const Bytes& data) {
  return Digest(EVP_sha256(), data);
}
Bytes CryptoUtil::Sha384(const Bytes& data) {
  return Digest(EVP_sha384(), data);
}
Bytes CryptoUtil::Sha512(const Bytes& data) {
  return Digest(EVP_sha512(), data);
}

Bytes CryptoUtil::Rc4(const Bytes& key, const Bytes& data) {
  // Plain RC4 implementation (OpenSSL 3 moves RC4 to the legacy provider).
  uint8_t s[256];
  for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);
  int j = 0;
  for (int i = 0; i < 256; ++i) {
    j = (j + s[i] + key[i % key.size()]) & 0xff;
    std::swap(s[i], s[j]);
  }
  Bytes out(data.size());
  int x = 0, y = 0;
  for (size_t k = 0; k < data.size(); ++k) {
    x = (x + 1) & 0xff;
    y = (y + s[x]) & 0xff;
    std::swap(s[x], s[y]);
    out[k] = data[k] ^ s[(s[x] + s[y]) & 0xff];
  }
  return out;
}

static const EVP_CIPHER* AesCbc(size_t key_len) {
  return key_len == 32
             ? EVP_aes_256_cbc()
             : (key_len == 24 ? EVP_aes_192_cbc() : EVP_aes_128_cbc());
}
static const EVP_CIPHER* AesEcb(size_t key_len) {
  return key_len == 32
             ? EVP_aes_256_ecb()
             : (key_len == 24 ? EVP_aes_192_ecb() : EVP_aes_128_ecb());
}

static Bytes RunCipher(const EVP_CIPHER* cipher, const Bytes& key,
                       const uint8_t* iv, const Bytes& data, bool padding,
                       bool encrypt) {
  // OpenSSL reads exactly the cipher's key length from key.data(): never hand
  // it a shorter buffer.
  if (key.size() != 16 && key.size() != 24 && key.size() != 32)
    throw PdfException("Invalid AES key length.");
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_CipherInit_ex(ctx, cipher, nullptr, key.data(), iv, encrypt ? 1 : 0);
  EVP_CIPHER_CTX_set_padding(ctx, padding ? 1 : 0);
  Bytes out(data.size() + 32);
  int len1 = 0, len2 = 0;
  EVP_CipherUpdate(ctx, out.data(), &len1, data.data(),
                   static_cast<int>(data.size()));
  if (EVP_CipherFinal_ex(ctx, out.data() + len1, &len2) != 1) len2 = 0;
  EVP_CIPHER_CTX_free(ctx);
  out.resize(len1 + len2);
  return out;
}

Bytes CryptoUtil::AesCbcEncrypt(const Bytes& key, const Bytes& iv,
                                const Bytes& data, bool padding) {
  return RunCipher(AesCbc(key.size()), key, iv.data(), data, padding, true);
}
Bytes CryptoUtil::AesCbcDecrypt(const Bytes& key, const Bytes& iv,
                                const Bytes& data, bool padding) {
  return RunCipher(AesCbc(key.size()), key, iv.data(), data, padding, false);
}
Bytes CryptoUtil::AesEcbEncrypt(const Bytes& key, const Bytes& data) {
  return RunCipher(AesEcb(key.size()), key, nullptr, data, false, true);
}
Bytes CryptoUtil::AesEcbDecrypt(const Bytes& key, const Bytes& data) {
  return RunCipher(AesEcb(key.size()), key, nullptr, data, false, false);
}

Bytes CryptoUtil::RandomBytes(int n) {
  Bytes b(n);
  if (RAND_bytes(b.data(), n) != 1) {
    std::random_device rd;
    for (auto& x : b) x = static_cast<uint8_t>(rd());
  }
  return b;
}

// ============================================================ PdfEncryption
static const uint8_t kPad[32] = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E,
    0x56, 0xFF, 0xFA, 0x01, 0x08, 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68,
    0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
static const uint8_t kSalt[4] = {0x73, 0x41, 0x6c, 0x54};

static Bytes Concat(std::initializer_list<const Bytes*> parts) {
  Bytes r;
  for (const Bytes* p : parts) r.insert(r.end(), p->begin(), p->end());
  return r;
}

Bytes PdfEncryption::PadPassword(const Bytes& password) {
  Bytes user_pad(32);
  size_t n = std::min<size_t>(password.size(), 32);
  std::memcpy(user_pad.data(), password.data(), n);
  std::memcpy(user_pad.data() + n, kPad, 32 - n);
  return user_pad;
}

Bytes PdfEncryption::GenerateNewDocumentId() {
  auto now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  Bytes seed = CryptoUtil::RandomBytes(16);
  for (int i = 0; i < 8; ++i)
    seed.push_back(static_cast<uint8_t>(now >> (8 * i)));
  return CryptoUtil::Md5(seed);
}

PdfObjectPtr PdfEncryption::CreateInfoId(const Bytes& id1, const Bytes& id2,
                                         bool) {
  auto s1 = std::make_shared<PdfString>(id1);
  auto s2 = std::make_shared<PdfString>(id2);
  s1->SetHexWriting(true);
  s2->SetHexWriting(true);
  auto arr = std::make_shared<PdfArray>();
  arr->Add(s1);
  arr->Add(s2);
  return arr;
}

PdfEncryption::PdfEncryption(const Bytes& user_password,
                             const Bytes& owner_password, int permissions,
                             int encryption_type, const Bytes& document_id,
                             const PdfVersion&)
    : document_id_(document_id) {
  dictionary_ = std::make_shared<PdfDictionary>();
  encryption_algorithm_ =
      encryption_type & encryption_constants::kEncryptionMask;
  encrypt_metadata_ =
      (encryption_type & encryption_constants::kDoNotEncryptMetadata) == 0;
  embedded_files_only_ =
      (encryption_type & encryption_constants::kEmbeddedFilesOnly) ==
      encryption_constants::kEmbeddedFilesOnly;
  crypto_mode_ = encryption_type;
  Bytes owner =
      owner_password.empty() ? CryptoUtil::RandomBytes(16) : owner_password;
  switch (encryption_algorithm_) {
    case encryption_constants::kStandardEncryption40:
      embedded_files_only_ =
          false;  // as in Java: 40-bit RC4 has no crypt filters
      SetupByUserPassword40or128(user_password, owner, permissions, 40, 2,
                                 true);
      break;
    case encryption_constants::kStandardEncryption128:
      // Java writes R3/V2 when metadata is encrypted, which cannot express
      // "embedded files only" (strings and streams would then be written in
      // clear while the dictionary claims they are encrypted). Use R4/V4 crypt
      // filters whenever they are needed.
      SetupByUserPassword40or128(
          user_password, owner, permissions, 128,
          encrypt_metadata_ && !embedded_files_only_ ? 3 : 4,
          encrypt_metadata_);
      break;
    case encryption_constants::kEncryptionAes128:
      aes_ = true;
      SetupByUserPassword40or128(user_password, owner, permissions, 128, 4,
                                 encrypt_metadata_);
      break;
    case encryption_constants::kEncryptionAes256:
    default:
      aes_ = true;
      SetupAes256(user_password, owner, permissions, encrypt_metadata_);
      break;
  }
  dictionary_->Put(PdfName::kFilter, PdfName::kStandard);
}

void PdfEncryption::PutCryptFilters(PdfDictionary& d,
                                    const PdfDictionaryPtr& stdcf) const {
  if (embedded_files_only_) {
    // Strings and streams stay in clear (Identity); only embedded files use the
    // standard filter.
    stdcf->Put(PdfName::kAuthEvent, PdfName::kEFOpen);
    d.Put(PdfName::kEFF, PdfName::kStdCF);
    d.Put(PdfName::kStrF, PdfName::kIdentity);
    d.Put(PdfName::kStmF, PdfName::kIdentity);
  } else {
    stdcf->Put(PdfName::kAuthEvent, PdfName::kDocOpen);
    d.Put(PdfName::kStrF, PdfName::kStdCF);
    d.Put(PdfName::kStmF, PdfName::kStdCF);
  }
  auto cf = std::make_shared<PdfDictionary>();
  cf->Put(PdfName::kStdCF, stdcf);
  d.Put(PdfName::kCF, cf);
}

void PdfEncryption::SetupByUserPassword40or128(
    const Bytes& user_password, const Bytes& owner_password, int permissions,
    int key_length_bits, int revision, bool encrypt_metadata) {
  key_length_ = key_length_bits;
  revision_ = revision;
  permissions |= static_cast<int>(revision >= 3 ? 0xfffff0c0 : 0xffffffc0);
  permissions &= static_cast<int>(0xfffffffc);
  p_value_ = permissions;
  permissions_ = permissions;
  encrypt_metadata_ = encrypt_metadata;
  Bytes user_pad = PadPassword(user_password);
  Bytes owner_pad = PadPassword(owner_password);
  owner_key_ = ComputeOwnerKey(user_pad, owner_pad);
  mkey_ = ComputeGlobalKey(user_pad, false);
  user_key_ = ComputeU(mkey_);

  auto& d = *dictionary_;
  d.Put(PdfName::kO, std::make_shared<PdfString>(owner_key_));
  std::static_pointer_cast<PdfString>(d.Get(PdfName::kO))->SetHexWriting(true);
  d.Put(PdfName::kU, std::make_shared<PdfString>(user_key_));
  std::static_pointer_cast<PdfString>(d.Get(PdfName::kU))->SetHexWriting(true);
  d.Put(PdfName::kP, MakeNumber(p_value_));
  d.Put(PdfName::kR, MakeNumber(revision));
  if (revision == 2) {
    d.Put(PdfName::kV, MakeNumber(1));
  } else if (revision == 3) {
    d.Put(PdfName::kV, MakeNumber(2));
    d.Put(PdfName::kLength, MakeNumber(key_length_bits));
  } else {
    d.Put(PdfName::kV, MakeNumber(4));
    d.Put(PdfName::kLength, MakeNumber(key_length_bits));
    auto stdcf = std::make_shared<PdfDictionary>();
    stdcf->Put(PdfName::kLength, MakeNumber(16));
    stdcf->Put(PdfName::kCFM, aes_ ? PdfName::kAESV2 : PdfName::kV2);
    PutCryptFilters(d, stdcf);
    if (!encrypt_metadata)
      d.Put(PdfName::kEncryptMetadata, PdfBoolean::kFalseValue);
  }
}

Bytes PdfEncryption::ComputeOwnerKey(const Bytes& user_pad,
                                     const Bytes& owner_pad) const {
  Bytes digest = CryptoUtil::Md5(owner_pad);
  int key_bytes = key_length_ / 8;
  if (revision_ >= 3) {
    for (int k = 0; k < 50; ++k)
      digest =
          CryptoUtil::Md5(Bytes(digest.begin(), digest.begin() + key_bytes));
  }
  Bytes key(digest.begin(), digest.begin() + key_bytes);
  Bytes owner_key = CryptoUtil::Rc4(key, user_pad);
  if (revision_ >= 3) {
    for (int i = 1; i <= 19; ++i) {
      Bytes k2 = key;
      for (auto& b : k2) b ^= static_cast<uint8_t>(i);
      owner_key = CryptoUtil::Rc4(k2, owner_key);
    }
  }
  return owner_key;
}

Bytes PdfEncryption::ComputeGlobalKey(const Bytes& user_pad, bool) const {
  Bytes data = user_pad;
  data.insert(data.end(), owner_key_.begin(), owner_key_.end());
  for (int i = 0; i < 4; ++i)
    data.push_back(
        static_cast<uint8_t>(static_cast<uint32_t>(p_value_) >> (8 * i)));
  data.insert(data.end(), document_id_.begin(), document_id_.end());
  if (revision_ >= 4 && !encrypt_metadata_) {
    for (int i = 0; i < 4; ++i) data.push_back(0xff);
  }
  Bytes digest = CryptoUtil::Md5(data);
  int key_bytes = key_length_ / 8;
  if (revision_ >= 3) {
    for (int k = 0; k < 50; ++k)
      digest =
          CryptoUtil::Md5(Bytes(digest.begin(), digest.begin() + key_bytes));
  }
  return Bytes(digest.begin(), digest.begin() + key_bytes);
}

Bytes PdfEncryption::ComputeU(const Bytes& global_key) const {
  if (revision_ == 2)
    return CryptoUtil::Rc4(global_key, Bytes(kPad, kPad + 32));
  Bytes data(kPad, kPad + 32);
  data.insert(data.end(), document_id_.begin(), document_id_.end());
  Bytes digest = CryptoUtil::Md5(data);
  Bytes u = CryptoUtil::Rc4(global_key, digest);
  for (int i = 1; i <= 19; ++i) {
    Bytes k2 = global_key;
    for (auto& b : k2) b ^= static_cast<uint8_t>(i);
    u = CryptoUtil::Rc4(k2, u);
  }
  u.resize(32, 0);
  return u;
}

Bytes PdfEncryption::HashR6(const Bytes& password, const Bytes& salt,
                            const Bytes& user_key) const {
  Bytes pwd(password.begin(),
            password.begin() + std::min<size_t>(password.size(), 127));
  Bytes k = CryptoUtil::Sha256(Concat({&pwd, &salt, &user_key}));
  if (revision_ == 5) return k;
  for (int i = 0;; ++i) {
    Bytes k1seq = Concat({&pwd, &k, &user_key});
    Bytes k1;
    k1.reserve(k1seq.size() * 64);
    for (int r = 0; r < 64; ++r)
      k1.insert(k1.end(), k1seq.begin(), k1seq.end());
    Bytes aes_key(k.begin(), k.begin() + 16);
    Bytes iv(k.begin() + 16, k.begin() + 32);
    Bytes e = CryptoUtil::AesCbcEncrypt(aes_key, iv, k1, false);
    int sum = 0;
    for (int j = 0; j < 16; ++j) sum += e[j];
    switch (sum % 3) {
      case 0:
        k = CryptoUtil::Sha256(e);
        break;
      case 1:
        k = CryptoUtil::Sha384(e);
        break;
      default:
        k = CryptoUtil::Sha512(e);
        break;
    }
    if (i >= 63 && static_cast<int>(e.back()) <= i - 31) break;
  }
  k.resize(32);
  return k;
}

void PdfEncryption::SetupAes256(const Bytes& user_password,
                                const Bytes& owner_password, int permissions,
                                bool encrypt_metadata) {
  revision_ = 6;
  key_length_ = 256;
  permissions |= static_cast<int>(0xfffff0c0);
  permissions &= static_cast<int>(0xfffffffc);
  p_value_ = permissions;
  permissions_ = permissions;
  encrypt_metadata_ = encrypt_metadata;
  mkey_ = CryptoUtil::RandomBytes(32);
  Bytes uvs = CryptoUtil::RandomBytes(8), uks = CryptoUtil::RandomBytes(8);
  Bytes empty;
  Bytes uhash = HashR6(user_password, uvs, empty);
  user_key_ = Concat({&uhash, &uvs, &uks});
  Bytes ukey = HashR6(user_password, uks, empty);
  Bytes zero_iv(16, 0);
  Bytes ue = CryptoUtil::AesCbcEncrypt(ukey, zero_iv, mkey_, false);
  Bytes ovs = CryptoUtil::RandomBytes(8), oks = CryptoUtil::RandomBytes(8);
  Bytes u48(user_key_.begin(), user_key_.begin() + 48);
  Bytes ohash = HashR6(owner_password, ovs, u48);
  owner_key_ = Concat({&ohash, &ovs, &oks});
  Bytes okey = HashR6(owner_password, oks, u48);
  Bytes oe = CryptoUtil::AesCbcEncrypt(okey, zero_iv, mkey_, false);
  Bytes perms(16);
  for (int i = 0; i < 4; ++i)
    perms[i] = static_cast<uint8_t>(static_cast<uint32_t>(p_value_) >> (8 * i));
  perms[4] = perms[5] = perms[6] = perms[7] = 0xff;
  perms[8] = encrypt_metadata ? 'T' : 'F';
  perms[9] = 'a';
  perms[10] = 'd';
  perms[11] = 'b';
  Bytes rnd = CryptoUtil::RandomBytes(4);
  std::memcpy(perms.data() + 12, rnd.data(), 4);
  Bytes perms_enc = CryptoUtil::AesEcbEncrypt(mkey_, perms);

  auto& d = *dictionary_;
  auto hex_string = [](const Bytes& b) {
    auto s = std::make_shared<PdfString>(b);
    s->SetHexWriting(true);
    return s;
  };
  d.Put(PdfName::kO, hex_string(owner_key_));
  d.Put(PdfName::kU, hex_string(user_key_));
  d.Put(PdfName::kOE, hex_string(oe));
  d.Put(PdfName::kUE, hex_string(ue));
  d.Put(PdfName::kPerms, hex_string(perms_enc));
  d.Put(PdfName::kP, MakeNumber(p_value_));
  d.Put(PdfName::kR, MakeNumber(6));
  d.Put(PdfName::kV, MakeNumber(5));
  d.Put(PdfName::kLength, MakeNumber(256));
  auto stdcf = std::make_shared<PdfDictionary>();
  stdcf->Put(PdfName::kLength, MakeNumber(32));
  stdcf->Put(PdfName::kCFM, PdfName::kAESV3);
  PutCryptFilters(d, stdcf);
  if (!encrypt_metadata)
    d.Put(PdfName::kEncryptMetadata, PdfBoolean::kFalseValue);
}

PdfEncryption::PdfEncryption(const PdfDictionaryPtr& pdf_dict,
                             const Bytes& password, const Bytes& document_id)
    : dictionary_(pdf_dict), document_id_(document_id) {
  auto& d = *pdf_dict;
  auto r_num = d.GetAsNumber(PdfName::kR);
  auto v_num = d.GetAsNumber(PdfName::kV);
  if (!r_num)
    throw PdfException(Format(
        kernel_exception_message_constant::kUnknownEncryptionTypeR, "null"));
  revision_ = r_num->IntValue();
  int v = v_num ? v_num->IntValue() : 0;
  auto p_num = d.GetAsNumber(PdfName::kP);
  p_value_ = p_num ? static_cast<int32_t>(p_num->LongValue()) : 0;
  permissions_ = p_value_;
  auto o = d.GetAsString(PdfName::kO);
  auto u = d.GetAsString(PdfName::kU);
  if (o) owner_key_ = o->GetValueBytes();
  if (u) user_key_ = u->GetValueBytes();
  auto em = d.GetAsBoolean(PdfName::kEncryptMetadata);
  encrypt_metadata_ = !em || em->GetValue();
  switch (revision_) {
    case 2:
      key_length_ = 40;
      encryption_algorithm_ = encryption_constants::kStandardEncryption40;
      break;
    case 3: {
      auto len = d.GetAsNumber(PdfName::kLength);
      key_length_ = len ? len->IntValue() : 40;
      if (key_length_ > 128 || key_length_ < 40 || key_length_ % 8 != 0)
        throw PdfException("Illegal length value.");
      encryption_algorithm_ = encryption_constants::kStandardEncryption128;
      break;
    }
    case 4: {
      auto cf = d.GetAsDictionary(PdfName::kCF);
      if (!cf) throw PdfException("/CF not found (encryption)");
      auto stdcf = cf->GetAsDictionary(PdfName::kStdCF);
      if (!stdcf) throw PdfException("/StdCF not found (encryption)");
      auto cfm = stdcf->GetAsName(PdfName::kCFM);
      key_length_ = 128;
      if (NameEquals(cfm, PdfName::kV2)) {
        encryption_algorithm_ = encryption_constants::kStandardEncryption128;
      } else if (NameEquals(cfm, PdfName::kAESV2)) {
        encryption_algorithm_ = encryption_constants::kEncryptionAes128;
        aes_ = true;
      } else {
        throw PdfException("No compatible encryption found.");
      }
      auto ef = d.GetAsName(PdfName::kEFF);
      auto stmf = d.GetAsName(PdfName::kStmF);
      if (ef && NameEquals(stmf, PdfName::kIdentity))
        embedded_files_only_ = true;
      break;
    }
    case 5:
    case 6:
      key_length_ = 256;
      encryption_algorithm_ = encryption_constants::kEncryptionAes256;
      aes_ = true;
      if (d.GetAsName(PdfName::kEFF) &&
          NameEquals(d.GetAsName(PdfName::kStmF), PdfName::kIdentity))
        embedded_files_only_ = true;
      break;
    default:
      throw PdfException(
          Format(kernel_exception_message_constant::kUnknownEncryptionTypeR,
                 revision_));
  }
  (void)v;
  crypto_mode_ =
      encryption_algorithm_ |
      (encrypt_metadata_ ? 0 : encryption_constants::kDoNotEncryptMetadata);
  if (revision_ >= 5)
    ReadAes256(password);
  else
    ReadStandard(password);
}

void PdfEncryption::ReadStandard(const Bytes& password) {
  // try as owner password
  Bytes owner_pad = PadPassword(password);
  Bytes digest = CryptoUtil::Md5(owner_pad);
  int key_bytes = key_length_ / 8;
  if (revision_ >= 3)
    for (int k = 0; k < 50; ++k)
      digest =
          CryptoUtil::Md5(Bytes(digest.begin(), digest.begin() + key_bytes));
  Bytes okey(digest.begin(), digest.begin() + key_bytes);
  Bytes user_pad;
  if (revision_ == 2) {
    user_pad = CryptoUtil::Rc4(okey, owner_key_);
  } else {
    user_pad = owner_key_;
    for (int i = 19; i >= 0; --i) {
      Bytes k2 = okey;
      for (auto& b : k2) b ^= static_cast<uint8_t>(i);
      user_pad = CryptoUtil::Rc4(k2, user_pad);
    }
  }
  Bytes candidate = ComputeGlobalKey(user_pad, false);
  Bytes u = ComputeU(candidate);
  size_t cmp_len = revision_ >= 3 ? 16 : 32;
  auto matches = [&](const Bytes& computed) {
    return user_key_.size() >= cmp_len &&
           std::equal(computed.begin(), computed.begin() + cmp_len,
                      user_key_.begin());
  };
  if (matches(u)) {
    mkey_ = candidate;
    opened_with_full_permissions_ = true;
    return;
  }
  // try as user password
  candidate = ComputeGlobalKey(PadPassword(password), false);
  u = ComputeU(candidate);
  if (!matches(u))
    throw BadPasswordException(
        kernel_exception_message_constant::kBadUserPassword);
  mkey_ = candidate;
  opened_with_full_permissions_ = false;
}

void PdfEncryption::ReadAes256(const Bytes& password) {
  auto& d = *dictionary_;
  auto oe = d.GetAsString(PdfName::kOE);
  auto ue = d.GetAsString(PdfName::kUE);
  Bytes oe_b = oe ? oe->GetValueBytes() : Bytes(32, 0);
  Bytes ue_b = ue ? ue->GetValueBytes() : Bytes(32, 0);
  if (owner_key_.size() < 48 || user_key_.size() < 48)
    throw PdfException("Bad /O or /U for AES-256 encryption");
  // /OE and /UE are the 32-byte encrypted file key; anything shorter would
  // yield a short AES-256 key.
  if (oe_b.size() < 32 || ue_b.size() < 32)
    throw PdfException("Bad /OE or /UE for AES-256 encryption");
  oe_b.resize(32);
  ue_b.resize(32);
  Bytes u48(user_key_.begin(), user_key_.begin() + 48);
  Bytes empty;
  Bytes zero_iv(16, 0);
  Bytes ovs(owner_key_.begin() + 32, owner_key_.begin() + 40),
      oks(owner_key_.begin() + 40, owner_key_.begin() + 48);
  Bytes uvs(user_key_.begin() + 32, user_key_.begin() + 40),
      uks(user_key_.begin() + 40, user_key_.begin() + 48);
  Bytes h = HashR6(password, ovs, u48);
  if (std::equal(h.begin(), h.end(), owner_key_.begin())) {
    Bytes k = HashR6(password, oks, u48);
    mkey_ = CryptoUtil::AesCbcDecrypt(k, zero_iv, oe_b, false);
    opened_with_full_permissions_ = true;
  } else {
    h = HashR6(password, uvs, empty);
    if (!std::equal(h.begin(), h.end(), user_key_.begin()))
      throw BadPasswordException(
          kernel_exception_message_constant::kBadUserPassword);
    Bytes k = HashR6(password, uks, empty);
    mkey_ = CryptoUtil::AesCbcDecrypt(k, zero_iv, ue_b, false);
    opened_with_full_permissions_ = false;
  }
  if (mkey_.size() != 32)
    throw PdfException("Bad /OE or /UE for AES-256 encryption");
  auto perms_str = d.GetAsString(PdfName::kPerms);
  if (perms_str) {
    Bytes perms = CryptoUtil::AesEcbDecrypt(mkey_, perms_str->GetValueBytes());
    if (perms.size() >= 12 && perms[9] == 'a' && perms[10] == 'd' &&
        perms[11] == 'b') {
      int32_t p =
          static_cast<int32_t>(perms[0] | (perms[1] << 8) | (perms[2] << 16) |
                               (static_cast<uint32_t>(perms[3]) << 24));
      permissions_ = p;
      encrypt_metadata_ = perms[8] == 'T';
    }
  }
}

Bytes PdfEncryption::ComputeUserPassword(const Bytes& owner_password) const {
  if (revision_ >= 5) return Bytes();
  Bytes owner_pad = PadPassword(owner_password);
  Bytes digest = CryptoUtil::Md5(owner_pad);
  int key_bytes = key_length_ / 8;
  if (revision_ >= 3)
    for (int k = 0; k < 50; ++k)
      digest =
          CryptoUtil::Md5(Bytes(digest.begin(), digest.begin() + key_bytes));
  Bytes okey(digest.begin(), digest.begin() + key_bytes);
  Bytes user_pad = owner_key_;
  if (revision_ == 2) {
    user_pad = CryptoUtil::Rc4(okey, owner_key_);
  } else {
    for (int i = 19; i >= 0; --i) {
      Bytes k2 = okey;
      for (auto& b : k2) b ^= static_cast<uint8_t>(i);
      user_pad = CryptoUtil::Rc4(k2, user_pad);
    }
  }
  // strip padding
  if (user_pad.size() != 32)
    return user_pad;  // malformed /O: no padding to strip (and PAD is only 32
                      // bytes)
  for (size_t i = 0; i < 32; ++i) {
    if (std::equal(user_pad.begin() + i, user_pad.end(), kPad))
      return Bytes(user_pad.begin(), user_pad.begin() + i);
  }
  return user_pad;
}

void PdfEncryption::SetHashKeyForNextObject(int obj_number,
                                            int obj_generation) {
  if (revision_ >= 5) {
    key_ = mkey_;
    return;
  }
  Bytes extra = {static_cast<uint8_t>(obj_number),
                 static_cast<uint8_t>(obj_number >> 8),
                 static_cast<uint8_t>(obj_number >> 16),
                 static_cast<uint8_t>(obj_generation),
                 static_cast<uint8_t>(obj_generation >> 8)};
  Bytes data = mkey_;
  data.insert(data.end(), extra.begin(), extra.end());
  if (aes_) data.insert(data.end(), kSalt, kSalt + 4);
  Bytes digest = CryptoUtil::Md5(data);
  size_t key_len = std::min<size_t>(mkey_.size() + 5, 16);
  key_.assign(digest.begin(), digest.begin() + key_len);
}

Bytes PdfEncryption::EncryptByteArray(const Bytes& b) {
  if (aes_) {
    Bytes iv = CryptoUtil::RandomBytes(16);
    Bytes enc = CryptoUtil::AesCbcEncrypt(key_, iv, b, true);
    iv.insert(iv.end(), enc.begin(), enc.end());
    return iv;
  }
  return CryptoUtil::Rc4(key_, b);
}

Bytes PdfEncryption::DecryptByteArray(const Bytes& b) {
  if (aes_) {
    if (key_.empty()) return b;
    if (b.size() < 16) return Bytes();
    Bytes iv(b.begin(), b.begin() + 16);
    Bytes data(b.begin() + 16, b.end());
    if (data.empty()) return Bytes();
    return CryptoUtil::AesCbcDecrypt(key_, iv, data, data.size() % 16 == 0);
  }
  if (key_.empty()) return b;
  return CryptoUtil::Rc4(key_, b);
}

namespace {
/// Buffers everything and encrypts on finish().
class EncryptionOutputStream : public io::OutputStream, public io::IFinishable {
 public:
  EncryptionOutputStream(PdfEncryption* enc,
                         std::shared_ptr<io::OutputStream> out)
      : enc_(enc), out_(std::move(out)) {}
  void Write(int b) override { buf_.push_back(static_cast<uint8_t>(b)); }
  void Write(const uint8_t* b, int off, int len) override {
    buf_.insert(buf_.end(), b + off, b + off + len);
  }
  using io::OutputStream::Write;
  void Finish() override {
    if (done_) return;
    done_ = true;
    out_->Write(enc_->EncryptByteArray(buf_));
  }
  void Close() override {
    Finish();
    out_->Close();
  }

 private:
  PdfEncryption* enc_;
  std::shared_ptr<io::OutputStream> out_;
  Bytes buf_;
  bool done_ = false;
};
}  // namespace

std::shared_ptr<io::OutputStream> PdfEncryption::GetEncryptionStream(
    std::shared_ptr<io::OutputStream> os) {
  return std::make_shared<EncryptionOutputStream>(this, std::move(os));
}

}  // namespace kernel
}  // namespace pdfcpp
