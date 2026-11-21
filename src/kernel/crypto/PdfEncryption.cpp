#include "pdfcpp/kernel/crypto/PdfEncryption.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <chrono>
#include <cstring>
#include <random>

#include "pdfcpp/kernel/pdf/Properties.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ CryptoUtil
static Bytes digest(const EVP_MD* md, const Bytes& data) {
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

Bytes CryptoUtil::md5(const Bytes& data) { return digest(EVP_md5(), data); }
Bytes CryptoUtil::sha256(const Bytes& data) { return digest(EVP_sha256(), data); }
Bytes CryptoUtil::sha384(const Bytes& data) { return digest(EVP_sha384(), data); }
Bytes CryptoUtil::sha512(const Bytes& data) { return digest(EVP_sha512(), data); }

Bytes CryptoUtil::rc4(const Bytes& key, const Bytes& data) {
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

static const EVP_CIPHER* aesCbc(size_t keyLen) {
    return keyLen == 32 ? EVP_aes_256_cbc() : (keyLen == 24 ? EVP_aes_192_cbc() : EVP_aes_128_cbc());
}
static const EVP_CIPHER* aesEcb(size_t keyLen) {
    return keyLen == 32 ? EVP_aes_256_ecb() : (keyLen == 24 ? EVP_aes_192_ecb() : EVP_aes_128_ecb());
}

static Bytes runCipher(const EVP_CIPHER* cipher, const Bytes& key, const uint8_t* iv, const Bytes& data, bool padding,
                       bool encrypt) {
    // OpenSSL reads exactly the cipher's key length from key.data(): never hand it a shorter buffer.
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) throw PdfException("Invalid AES key length.");
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_CipherInit_ex(ctx, cipher, nullptr, key.data(), iv, encrypt ? 1 : 0);
    EVP_CIPHER_CTX_set_padding(ctx, padding ? 1 : 0);
    Bytes out(data.size() + 32);
    int len1 = 0, len2 = 0;
    EVP_CipherUpdate(ctx, out.data(), &len1, data.data(), static_cast<int>(data.size()));
    if (EVP_CipherFinal_ex(ctx, out.data() + len1, &len2) != 1) len2 = 0;
    EVP_CIPHER_CTX_free(ctx);
    out.resize(len1 + len2);
    return out;
}

Bytes CryptoUtil::aesCbcEncrypt(const Bytes& key, const Bytes& iv, const Bytes& data, bool padding) {
    return runCipher(aesCbc(key.size()), key, iv.data(), data, padding, true);
}
Bytes CryptoUtil::aesCbcDecrypt(const Bytes& key, const Bytes& iv, const Bytes& data, bool padding) {
    return runCipher(aesCbc(key.size()), key, iv.data(), data, padding, false);
}
Bytes CryptoUtil::aesEcbEncrypt(const Bytes& key, const Bytes& data) {
    return runCipher(aesEcb(key.size()), key, nullptr, data, false, true);
}
Bytes CryptoUtil::aesEcbDecrypt(const Bytes& key, const Bytes& data) {
    return runCipher(aesEcb(key.size()), key, nullptr, data, false, false);
}

Bytes CryptoUtil::randomBytes(int n) {
    Bytes b(n);
    if (RAND_bytes(b.data(), n) != 1) {
        std::random_device rd;
        for (auto& x : b) x = static_cast<uint8_t>(rd());
    }
    return b;
}

// ============================================================ PdfEncryption
static const uint8_t PAD[32] = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E,
                                0x56, 0xFF, 0xFA, 0x01, 0x08, 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68,
                                0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
static const uint8_t SALT[4] = {0x73, 0x41, 0x6c, 0x54};

static Bytes concat(std::initializer_list<const Bytes*> parts) {
    Bytes r;
    for (const Bytes* p : parts) r.insert(r.end(), p->begin(), p->end());
    return r;
}

Bytes PdfEncryption::padPassword(const Bytes& password) {
    Bytes userPad(32);
    size_t n = std::min<size_t>(password.size(), 32);
    std::memcpy(userPad.data(), password.data(), n);
    std::memcpy(userPad.data() + n, PAD, 32 - n);
    return userPad;
}

Bytes PdfEncryption::generateNewDocumentId() {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    Bytes seed = CryptoUtil::randomBytes(16);
    for (int i = 0; i < 8; ++i) seed.push_back(static_cast<uint8_t>(now >> (8 * i)));
    return CryptoUtil::md5(seed);
}

PdfObjectPtr PdfEncryption::createInfoId(const Bytes& id1, const Bytes& id2, bool) {
    auto s1 = std::make_shared<PdfString>(id1);
    auto s2 = std::make_shared<PdfString>(id2);
    s1->setHexWriting(true);
    s2->setHexWriting(true);
    auto arr = std::make_shared<PdfArray>();
    arr->add(s1);
    arr->add(s2);
    return arr;
}

PdfEncryption::PdfEncryption(const Bytes& userPassword, const Bytes& ownerPassword, int permissions, int encryptionType,
                             const Bytes& documentId, const PdfVersion&)
    : documentId_(documentId) {
    dictionary_ = std::make_shared<PdfDictionary>();
    encryptionAlgorithm_ = encryptionType & EncryptionConstants::ENCRYPTION_MASK;
    encryptMetadata_ = (encryptionType & EncryptionConstants::DO_NOT_ENCRYPT_METADATA) == 0;
    embeddedFilesOnly_ = (encryptionType & EncryptionConstants::EMBEDDED_FILES_ONLY) == EncryptionConstants::EMBEDDED_FILES_ONLY;
    cryptoMode_ = encryptionType;
    Bytes owner = ownerPassword.empty() ? CryptoUtil::randomBytes(16) : ownerPassword;
    switch (encryptionAlgorithm_) {
        case EncryptionConstants::STANDARD_ENCRYPTION_40:
            embeddedFilesOnly_ = false;  // as in Java: 40-bit RC4 has no crypt filters
            setupByUserPassword40or128(userPassword, owner, permissions, 40, 2, true);
            break;
        case EncryptionConstants::STANDARD_ENCRYPTION_128:
            // Java writes R3/V2 when metadata is encrypted, which cannot express "embedded files only" (strings and
            // streams would then be written in clear while the dictionary claims they are encrypted). Use R4/V4
            // crypt filters whenever they are needed.
            setupByUserPassword40or128(userPassword, owner, permissions, 128, encryptMetadata_ && !embeddedFilesOnly_ ? 3 : 4,
                                       encryptMetadata_);
            break;
        case EncryptionConstants::ENCRYPTION_AES_128:
            aes_ = true;
            setupByUserPassword40or128(userPassword, owner, permissions, 128, 4, encryptMetadata_);
            break;
        case EncryptionConstants::ENCRYPTION_AES_256:
        default:
            aes_ = true;
            setupAes256(userPassword, owner, permissions, encryptMetadata_);
            break;
    }
    dictionary_->put(PdfName::Filter, PdfName::Standard);
}

void PdfEncryption::putCryptFilters(PdfDictionary& d, const PdfDictionaryPtr& stdcf) const {
    if (embeddedFilesOnly_) {
        // Strings and streams stay in clear (Identity); only embedded files use the standard filter.
        stdcf->put(PdfName::AuthEvent, PdfName::EFOpen);
        d.put(PdfName::EFF, PdfName::StdCF);
        d.put(PdfName::StrF, PdfName::Identity);
        d.put(PdfName::StmF, PdfName::Identity);
    } else {
        stdcf->put(PdfName::AuthEvent, PdfName::DocOpen);
        d.put(PdfName::StrF, PdfName::StdCF);
        d.put(PdfName::StmF, PdfName::StdCF);
    }
    auto cf = std::make_shared<PdfDictionary>();
    cf->put(PdfName::StdCF, stdcf);
    d.put(PdfName::CF, cf);
}

void PdfEncryption::setupByUserPassword40or128(const Bytes& userPassword, const Bytes& ownerPassword, int permissions,
                                               int keyLengthBits, int revision, bool encryptMetadata) {
    keyLength_ = keyLengthBits;
    revision_ = revision;
    permissions |= static_cast<int>(revision >= 3 ? 0xfffff0c0 : 0xffffffc0);
    permissions &= static_cast<int>(0xfffffffc);
    pValue_ = permissions;
    permissions_ = permissions;
    encryptMetadata_ = encryptMetadata;
    Bytes userPad = padPassword(userPassword);
    Bytes ownerPad = padPassword(ownerPassword);
    ownerKey_ = computeOwnerKey(userPad, ownerPad);
    mkey_ = computeGlobalKey(userPad, false);
    userKey_ = computeU(mkey_);

    auto& d = *dictionary_;
    d.put(PdfName::O, std::make_shared<PdfString>(ownerKey_));
    std::static_pointer_cast<PdfString>(d.get(PdfName::O))->setHexWriting(true);
    d.put(PdfName::U, std::make_shared<PdfString>(userKey_));
    std::static_pointer_cast<PdfString>(d.get(PdfName::U))->setHexWriting(true);
    d.put(PdfName::P, makeNumber(pValue_));
    d.put(PdfName::R, makeNumber(revision));
    if (revision == 2) {
        d.put(PdfName::V, makeNumber(1));
    } else if (revision == 3) {
        d.put(PdfName::V, makeNumber(2));
        d.put(PdfName::Length, makeNumber(keyLengthBits));
    } else {
        d.put(PdfName::V, makeNumber(4));
        d.put(PdfName::Length, makeNumber(keyLengthBits));
        auto stdcf = std::make_shared<PdfDictionary>();
        stdcf->put(PdfName::Length, makeNumber(16));
        stdcf->put(PdfName::CFM, aes_ ? PdfName::AESV2 : PdfName::V2);
        putCryptFilters(d, stdcf);
        if (!encryptMetadata) d.put(PdfName::EncryptMetadata, PdfBoolean::FALSE_VALUE);
    }
}

Bytes PdfEncryption::computeOwnerKey(const Bytes& userPad, const Bytes& ownerPad) const {
    Bytes digest = CryptoUtil::md5(ownerPad);
    int keyBytes = keyLength_ / 8;
    if (revision_ >= 3) {
        for (int k = 0; k < 50; ++k) digest = CryptoUtil::md5(Bytes(digest.begin(), digest.begin() + keyBytes));
    }
    Bytes key(digest.begin(), digest.begin() + keyBytes);
    Bytes ownerKey = CryptoUtil::rc4(key, userPad);
    if (revision_ >= 3) {
        for (int i = 1; i <= 19; ++i) {
            Bytes k2 = key;
            for (auto& b : k2) b ^= static_cast<uint8_t>(i);
            ownerKey = CryptoUtil::rc4(k2, ownerKey);
        }
    }
    return ownerKey;
}

Bytes PdfEncryption::computeGlobalKey(const Bytes& userPad, bool) const {
    Bytes data = userPad;
    data.insert(data.end(), ownerKey_.begin(), ownerKey_.end());
    for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(static_cast<uint32_t>(pValue_) >> (8 * i)));
    data.insert(data.end(), documentId_.begin(), documentId_.end());
    if (revision_ >= 4 && !encryptMetadata_) {
        for (int i = 0; i < 4; ++i) data.push_back(0xff);
    }
    Bytes digest = CryptoUtil::md5(data);
    int keyBytes = keyLength_ / 8;
    if (revision_ >= 3) {
        for (int k = 0; k < 50; ++k) digest = CryptoUtil::md5(Bytes(digest.begin(), digest.begin() + keyBytes));
    }
    return Bytes(digest.begin(), digest.begin() + keyBytes);
}

Bytes PdfEncryption::computeU(const Bytes& globalKey) const {
    if (revision_ == 2) return CryptoUtil::rc4(globalKey, Bytes(PAD, PAD + 32));
    Bytes data(PAD, PAD + 32);
    data.insert(data.end(), documentId_.begin(), documentId_.end());
    Bytes digest = CryptoUtil::md5(data);
    Bytes u = CryptoUtil::rc4(globalKey, digest);
    for (int i = 1; i <= 19; ++i) {
        Bytes k2 = globalKey;
        for (auto& b : k2) b ^= static_cast<uint8_t>(i);
        u = CryptoUtil::rc4(k2, u);
    }
    u.resize(32, 0);
    return u;
}

Bytes PdfEncryption::hashR6(const Bytes& password, const Bytes& salt, const Bytes& userKey) const {
    Bytes pwd(password.begin(), password.begin() + std::min<size_t>(password.size(), 127));
    Bytes k = CryptoUtil::sha256(concat({&pwd, &salt, &userKey}));
    if (revision_ == 5) return k;
    for (int i = 0;; ++i) {
        Bytes k1seq = concat({&pwd, &k, &userKey});
        Bytes k1;
        k1.reserve(k1seq.size() * 64);
        for (int r = 0; r < 64; ++r) k1.insert(k1.end(), k1seq.begin(), k1seq.end());
        Bytes aesKey(k.begin(), k.begin() + 16);
        Bytes iv(k.begin() + 16, k.begin() + 32);
        Bytes e = CryptoUtil::aesCbcEncrypt(aesKey, iv, k1, false);
        int sum = 0;
        for (int j = 0; j < 16; ++j) sum += e[j];
        switch (sum % 3) {
            case 0: k = CryptoUtil::sha256(e); break;
            case 1: k = CryptoUtil::sha384(e); break;
            default: k = CryptoUtil::sha512(e); break;
        }
        if (i >= 63 && static_cast<int>(e.back()) <= i - 31) break;
    }
    k.resize(32);
    return k;
}

void PdfEncryption::setupAes256(const Bytes& userPassword, const Bytes& ownerPassword, int permissions,
                                bool encryptMetadata) {
    revision_ = 6;
    keyLength_ = 256;
    permissions |= static_cast<int>(0xfffff0c0);
    permissions &= static_cast<int>(0xfffffffc);
    pValue_ = permissions;
    permissions_ = permissions;
    encryptMetadata_ = encryptMetadata;
    mkey_ = CryptoUtil::randomBytes(32);
    Bytes uvs = CryptoUtil::randomBytes(8), uks = CryptoUtil::randomBytes(8);
    Bytes empty;
    Bytes uhash = hashR6(userPassword, uvs, empty);
    userKey_ = concat({&uhash, &uvs, &uks});
    Bytes ukey = hashR6(userPassword, uks, empty);
    Bytes zeroIv(16, 0);
    Bytes ue = CryptoUtil::aesCbcEncrypt(ukey, zeroIv, mkey_, false);
    Bytes ovs = CryptoUtil::randomBytes(8), oks = CryptoUtil::randomBytes(8);
    Bytes u48(userKey_.begin(), userKey_.begin() + 48);
    Bytes ohash = hashR6(ownerPassword, ovs, u48);
    ownerKey_ = concat({&ohash, &ovs, &oks});
    Bytes okey = hashR6(ownerPassword, oks, u48);
    Bytes oe = CryptoUtil::aesCbcEncrypt(okey, zeroIv, mkey_, false);
    Bytes perms(16);
    for (int i = 0; i < 4; ++i) perms[i] = static_cast<uint8_t>(static_cast<uint32_t>(pValue_) >> (8 * i));
    perms[4] = perms[5] = perms[6] = perms[7] = 0xff;
    perms[8] = encryptMetadata ? 'T' : 'F';
    perms[9] = 'a';
    perms[10] = 'd';
    perms[11] = 'b';
    Bytes rnd = CryptoUtil::randomBytes(4);
    std::memcpy(perms.data() + 12, rnd.data(), 4);
    Bytes permsEnc = CryptoUtil::aesEcbEncrypt(mkey_, perms);

    auto& d = *dictionary_;
    auto hexString = [](const Bytes& b) {
        auto s = std::make_shared<PdfString>(b);
        s->setHexWriting(true);
        return s;
    };
    d.put(PdfName::O, hexString(ownerKey_));
    d.put(PdfName::U, hexString(userKey_));
    d.put(PdfName::OE, hexString(oe));
    d.put(PdfName::UE, hexString(ue));
    d.put(PdfName::Perms, hexString(permsEnc));
    d.put(PdfName::P, makeNumber(pValue_));
    d.put(PdfName::R, makeNumber(6));
    d.put(PdfName::V, makeNumber(5));
    d.put(PdfName::Length, makeNumber(256));
    auto stdcf = std::make_shared<PdfDictionary>();
    stdcf->put(PdfName::Length, makeNumber(32));
    stdcf->put(PdfName::CFM, PdfName::AESV3);
    putCryptFilters(d, stdcf);
    if (!encryptMetadata) d.put(PdfName::EncryptMetadata, PdfBoolean::FALSE_VALUE);
}

PdfEncryption::PdfEncryption(const PdfDictionaryPtr& pdfDict, const Bytes& password, const Bytes& documentId)
    : dictionary_(pdfDict), documentId_(documentId) {
    auto& d = *pdfDict;
    auto rNum = d.getAsNumber(PdfName::R);
    auto vNum = d.getAsNumber(PdfName::V);
    if (!rNum) throw PdfException(format(KernelExceptionMessageConstant::UNKNOWN_ENCRYPTION_TYPE_R, "null"));
    revision_ = rNum->intValue();
    int v = vNum ? vNum->intValue() : 0;
    auto pNum = d.getAsNumber(PdfName::P);
    pValue_ = pNum ? static_cast<int32_t>(pNum->longValue()) : 0;
    permissions_ = pValue_;
    auto o = d.getAsString(PdfName::O);
    auto u = d.getAsString(PdfName::U);
    if (o) ownerKey_ = o->getValueBytes();
    if (u) userKey_ = u->getValueBytes();
    auto em = d.getAsBoolean(PdfName::EncryptMetadata);
    encryptMetadata_ = !em || em->getValue();
    switch (revision_) {
        case 2:
            keyLength_ = 40;
            encryptionAlgorithm_ = EncryptionConstants::STANDARD_ENCRYPTION_40;
            break;
        case 3: {
            auto len = d.getAsNumber(PdfName::Length);
            keyLength_ = len ? len->intValue() : 40;
            if (keyLength_ > 128 || keyLength_ < 40 || keyLength_ % 8 != 0)
                throw PdfException("Illegal length value.");
            encryptionAlgorithm_ = EncryptionConstants::STANDARD_ENCRYPTION_128;
            break;
        }
        case 4: {
            auto cf = d.getAsDictionary(PdfName::CF);
            if (!cf) throw PdfException("/CF not found (encryption)");
            auto stdcf = cf->getAsDictionary(PdfName::StdCF);
            if (!stdcf) throw PdfException("/StdCF not found (encryption)");
            auto cfm = stdcf->getAsName(PdfName::CFM);
            keyLength_ = 128;
            if (nameEquals(cfm, PdfName::V2)) {
                encryptionAlgorithm_ = EncryptionConstants::STANDARD_ENCRYPTION_128;
            } else if (nameEquals(cfm, PdfName::AESV2)) {
                encryptionAlgorithm_ = EncryptionConstants::ENCRYPTION_AES_128;
                aes_ = true;
            } else {
                throw PdfException("No compatible encryption found.");
            }
            auto ef = d.getAsName(PdfName::EFF);
            auto stmf = d.getAsName(PdfName::StmF);
            if (ef && nameEquals(stmf, PdfName::Identity)) embeddedFilesOnly_ = true;
            break;
        }
        case 5:
        case 6:
            keyLength_ = 256;
            encryptionAlgorithm_ = EncryptionConstants::ENCRYPTION_AES_256;
            aes_ = true;
            if (d.getAsName(PdfName::EFF) && nameEquals(d.getAsName(PdfName::StmF), PdfName::Identity))
                embeddedFilesOnly_ = true;
            break;
        default:
            throw PdfException(format(KernelExceptionMessageConstant::UNKNOWN_ENCRYPTION_TYPE_R, revision_));
    }
    (void)v;
    cryptoMode_ = encryptionAlgorithm_ | (encryptMetadata_ ? 0 : EncryptionConstants::DO_NOT_ENCRYPT_METADATA);
    if (revision_ >= 5) readAes256(password);
    else readStandard(password);
}

void PdfEncryption::readStandard(const Bytes& password) {
    // try as owner password
    Bytes ownerPad = padPassword(password);
    Bytes digest = CryptoUtil::md5(ownerPad);
    int keyBytes = keyLength_ / 8;
    if (revision_ >= 3)
        for (int k = 0; k < 50; ++k) digest = CryptoUtil::md5(Bytes(digest.begin(), digest.begin() + keyBytes));
    Bytes okey(digest.begin(), digest.begin() + keyBytes);
    Bytes userPad;
    if (revision_ == 2) {
        userPad = CryptoUtil::rc4(okey, ownerKey_);
    } else {
        userPad = ownerKey_;
        for (int i = 19; i >= 0; --i) {
            Bytes k2 = okey;
            for (auto& b : k2) b ^= static_cast<uint8_t>(i);
            userPad = CryptoUtil::rc4(k2, userPad);
        }
    }
    Bytes candidate = computeGlobalKey(userPad, false);
    Bytes u = computeU(candidate);
    size_t cmpLen = revision_ >= 3 ? 16 : 32;
    auto matches = [&](const Bytes& computed) {
        return userKey_.size() >= cmpLen && std::equal(computed.begin(), computed.begin() + cmpLen, userKey_.begin());
    };
    if (matches(u)) {
        mkey_ = candidate;
        openedWithFullPermissions_ = true;
        return;
    }
    // try as user password
    candidate = computeGlobalKey(padPassword(password), false);
    u = computeU(candidate);
    if (!matches(u)) throw BadPasswordException(KernelExceptionMessageConstant::BAD_USER_PASSWORD);
    mkey_ = candidate;
    openedWithFullPermissions_ = false;
}

void PdfEncryption::readAes256(const Bytes& password) {
    auto& d = *dictionary_;
    auto oe = d.getAsString(PdfName::OE);
    auto ue = d.getAsString(PdfName::UE);
    Bytes oeB = oe ? oe->getValueBytes() : Bytes(32, 0);
    Bytes ueB = ue ? ue->getValueBytes() : Bytes(32, 0);
    if (ownerKey_.size() < 48 || userKey_.size() < 48) throw PdfException("Bad /O or /U for AES-256 encryption");
    // /OE and /UE are the 32-byte encrypted file key; anything shorter would yield a short AES-256 key.
    if (oeB.size() < 32 || ueB.size() < 32) throw PdfException("Bad /OE or /UE for AES-256 encryption");
    oeB.resize(32);
    ueB.resize(32);
    Bytes u48(userKey_.begin(), userKey_.begin() + 48);
    Bytes empty;
    Bytes zeroIv(16, 0);
    Bytes ovs(ownerKey_.begin() + 32, ownerKey_.begin() + 40), oks(ownerKey_.begin() + 40, ownerKey_.begin() + 48);
    Bytes uvs(userKey_.begin() + 32, userKey_.begin() + 40), uks(userKey_.begin() + 40, userKey_.begin() + 48);
    Bytes h = hashR6(password, ovs, u48);
    if (std::equal(h.begin(), h.end(), ownerKey_.begin())) {
        Bytes k = hashR6(password, oks, u48);
        mkey_ = CryptoUtil::aesCbcDecrypt(k, zeroIv, oeB, false);
        openedWithFullPermissions_ = true;
    } else {
        h = hashR6(password, uvs, empty);
        if (!std::equal(h.begin(), h.end(), userKey_.begin()))
            throw BadPasswordException(KernelExceptionMessageConstant::BAD_USER_PASSWORD);
        Bytes k = hashR6(password, uks, empty);
        mkey_ = CryptoUtil::aesCbcDecrypt(k, zeroIv, ueB, false);
        openedWithFullPermissions_ = false;
    }
    if (mkey_.size() != 32) throw PdfException("Bad /OE or /UE for AES-256 encryption");
    auto permsStr = d.getAsString(PdfName::Perms);
    if (permsStr) {
        Bytes perms = CryptoUtil::aesEcbDecrypt(mkey_, permsStr->getValueBytes());
        if (perms.size() >= 12 && perms[9] == 'a' && perms[10] == 'd' && perms[11] == 'b') {
            int32_t p = static_cast<int32_t>(perms[0] | (perms[1] << 8) | (perms[2] << 16) | (static_cast<uint32_t>(perms[3]) << 24));
            permissions_ = p;
            encryptMetadata_ = perms[8] == 'T';
        }
    }
}

Bytes PdfEncryption::computeUserPassword(const Bytes& ownerPassword) const {
    if (revision_ >= 5) return Bytes();
    Bytes ownerPad = padPassword(ownerPassword);
    Bytes digest = CryptoUtil::md5(ownerPad);
    int keyBytes = keyLength_ / 8;
    if (revision_ >= 3)
        for (int k = 0; k < 50; ++k) digest = CryptoUtil::md5(Bytes(digest.begin(), digest.begin() + keyBytes));
    Bytes okey(digest.begin(), digest.begin() + keyBytes);
    Bytes userPad = ownerKey_;
    if (revision_ == 2) {
        userPad = CryptoUtil::rc4(okey, ownerKey_);
    } else {
        for (int i = 19; i >= 0; --i) {
            Bytes k2 = okey;
            for (auto& b : k2) b ^= static_cast<uint8_t>(i);
            userPad = CryptoUtil::rc4(k2, userPad);
        }
    }
    // strip padding
    if (userPad.size() != 32) return userPad;  // malformed /O: no padding to strip (and PAD is only 32 bytes)
    for (size_t i = 0; i < 32; ++i) {
        if (std::equal(userPad.begin() + i, userPad.end(), PAD)) return Bytes(userPad.begin(), userPad.begin() + i);
    }
    return userPad;
}

void PdfEncryption::setHashKeyForNextObject(int objNumber, int objGeneration) {
    if (revision_ >= 5) {
        key_ = mkey_;
        return;
    }
    Bytes extra = {static_cast<uint8_t>(objNumber), static_cast<uint8_t>(objNumber >> 8),
                   static_cast<uint8_t>(objNumber >> 16), static_cast<uint8_t>(objGeneration),
                   static_cast<uint8_t>(objGeneration >> 8)};
    Bytes data = mkey_;
    data.insert(data.end(), extra.begin(), extra.end());
    if (aes_) data.insert(data.end(), SALT, SALT + 4);
    Bytes digest = CryptoUtil::md5(data);
    size_t keyLen = std::min<size_t>(mkey_.size() + 5, 16);
    key_.assign(digest.begin(), digest.begin() + keyLen);
}

Bytes PdfEncryption::encryptByteArray(const Bytes& b) {
    if (aes_) {
        Bytes iv = CryptoUtil::randomBytes(16);
        Bytes enc = CryptoUtil::aesCbcEncrypt(key_, iv, b, true);
        iv.insert(iv.end(), enc.begin(), enc.end());
        return iv;
    }
    return CryptoUtil::rc4(key_, b);
}

Bytes PdfEncryption::decryptByteArray(const Bytes& b) {
    if (aes_) {
        if (key_.empty()) return b;
        if (b.size() < 16) return Bytes();
        Bytes iv(b.begin(), b.begin() + 16);
        Bytes data(b.begin() + 16, b.end());
        if (data.empty()) return Bytes();
        return CryptoUtil::aesCbcDecrypt(key_, iv, data, data.size() % 16 == 0);
    }
    if (key_.empty()) return b;
    return CryptoUtil::rc4(key_, b);
}

namespace {
/// Buffers everything and encrypts on finish().
class EncryptionOutputStream : public io::OutputStream, public io::IFinishable {
public:
    EncryptionOutputStream(PdfEncryption* enc, std::shared_ptr<io::OutputStream> out) : enc_(enc), out_(std::move(out)) {}
    void write(int b) override { buf_.push_back(static_cast<uint8_t>(b)); }
    void write(const uint8_t* b, int off, int len) override { buf_.insert(buf_.end(), b + off, b + off + len); }
    using io::OutputStream::write;
    void finish() override {
        if (done_) return;
        done_ = true;
        out_->write(enc_->encryptByteArray(buf_));
    }
    void close() override {
        finish();
        out_->close();
    }

private:
    PdfEncryption* enc_;
    std::shared_ptr<io::OutputStream> out_;
    Bytes buf_;
    bool done_ = false;
};
}  // namespace

std::shared_ptr<io::OutputStream> PdfEncryption::getEncryptionStream(std::shared_ptr<io::OutputStream> os) {
    return std::make_shared<EncryptionOutputStream>(this, std::move(os));
}

}  // namespace kernel
}  // namespace pdfcpp
