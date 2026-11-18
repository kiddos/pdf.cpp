#pragma once

#include <memory>
#include <optional>
#include <string>

#include "pdfcpp/kernel/pdf/PdfObject.h"
#include "pdfcpp/kernel/pdf/PdfVersion.h"

namespace pdfcpp {
namespace kernel {

/// Port of EncryptionConstants.
namespace EncryptionConstants {
inline constexpr int STANDARD_ENCRYPTION_40 = 0;
inline constexpr int STANDARD_ENCRYPTION_128 = 1;
inline constexpr int ENCRYPTION_AES_128 = 2;
inline constexpr int ENCRYPTION_AES_256 = 3;
inline constexpr int ENCRYPTION_AES_GCM = 4;
inline constexpr int ENCRYPTION_MASK = 7;
inline constexpr int DO_NOT_ENCRYPT_METADATA = 8;
inline constexpr int EMBEDDED_FILES_ONLY = 24;
inline constexpr int ALLOW_PRINTING = 4 + 2048;
inline constexpr int ALLOW_MODIFY_CONTENTS = 8;
inline constexpr int ALLOW_COPY = 16;
inline constexpr int ALLOW_MODIFY_ANNOTATIONS = 32;
inline constexpr int ALLOW_FILL_IN = 256;
inline constexpr int ALLOW_SCREENREADERS = 512;
inline constexpr int ALLOW_ASSEMBLY = 1024;
inline constexpr int ALLOW_DEGRADED_PRINTING = 4;
}  // namespace EncryptionConstants

/// Port of EncryptionProperties (standard security handler only).
class EncryptionProperties {
public:
    EncryptionProperties& setStandardEncryption(const Bytes& userPassword, const Bytes& ownerPassword, int permissions,
                                                int encryptionAlgorithm) {
        userPassword_ = userPassword;
        ownerPassword_ = ownerPassword;
        standardEncryptPermissions_ = permissions;
        encryptionAlgorithm_ = encryptionAlgorithm;
        standard_ = true;
        return *this;
    }
    bool isStandardEncryptionUsed() const { return standard_; }

    Bytes userPassword_;
    Bytes ownerPassword_;
    int standardEncryptPermissions_ = 0;
    int encryptionAlgorithm_ = 0;
    bool standard_ = false;
};

/// Port of WriterProperties.
class WriterProperties {
public:
    WriterProperties() = default;
    WriterProperties& setPdfVersion(const PdfVersion& version) {
        pdfVersion = version;
        return *this;
    }
    WriterProperties& useSmartMode() {
        smartMode = true;
        return *this;
    }
    WriterProperties& addXmpMetadata() {
        addXmpMetadata_ = true;
        return *this;
    }
    WriterProperties& setCompressionLevel(int level) {
        compressionLevel = level;
        return *this;
    }
    WriterProperties& setFullCompressionMode(bool fullCompressionMode) {
        isFullCompression = fullCompressionMode;
        return *this;
    }
    WriterProperties& setStandardEncryption(const Bytes& userPassword, const Bytes& ownerPassword, int permissions,
                                            int encryptionAlgorithm) {
        encryptionProperties.setStandardEncryption(userPassword, ownerPassword, permissions, encryptionAlgorithm);
        return *this;
    }
    WriterProperties& setInitialDocumentId(PdfStringPtr id) {
        initialDocumentId = std::move(id);
        return *this;
    }
    WriterProperties& setModifiedDocumentId(PdfStringPtr id) {
        modifiedDocumentId = std::move(id);
        return *this;
    }
    bool isStandardEncryptionUsed() const { return encryptionProperties.isStandardEncryptionUsed(); }

    int compressionLevel = -1;  // CompressionConstants::DEFAULT_COMPRESSION
    std::optional<bool> isFullCompression;
    bool smartMode = false;
    bool addXmpMetadata_ = false;
    std::optional<PdfVersion> pdfVersion;
    EncryptionProperties encryptionProperties;
    PdfStringPtr initialDocumentId;
    PdfStringPtr modifiedDocumentId;
};

/// Port of ReaderProperties.
class ReaderProperties {
public:
    ReaderProperties& setPassword(const Bytes& pwd) {
        password = pwd;
        return *this;
    }
    ReaderProperties& setPassword(const std::string& pwd) { return setPassword(toBytes(pwd)); }
    Bytes password;
};

/// Port of DocumentProperties / StampingProperties.
class StampingProperties {
public:
    StampingProperties& useAppendMode() {
        appendMode = true;
        return *this;
    }
    StampingProperties& preserveEncryption() {
        preserveEncryption_ = true;
        return *this;
    }
    bool appendMode = false;
    bool preserveEncryption_ = false;
};

using DocumentProperties = StampingProperties;

}  // namespace kernel
}  // namespace pdfcpp
