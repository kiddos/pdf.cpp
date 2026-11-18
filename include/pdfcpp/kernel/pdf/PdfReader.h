#pragma once

#include <map>
#include <memory>
#include <string>

#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/kernel/pdf/PdfObject.h"
#include "pdfcpp/kernel/pdf/PdfVersion.h"
#include "pdfcpp/kernel/pdf/Properties.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;

/// Decoder for one stream filter (port of IFilterHandler).
class IFilterHandler {
public:
    virtual ~IFilterHandler() = default;
    virtual Bytes decode(const Bytes& b, const PdfName& filterName, const PdfDictionary* decodeParams,
                         const PdfDictionary& streamDictionary) = 0;
};

using FilterHandlerMap = std::map<std::string, std::shared_ptr<IFilterHandler>>;

/// Port of FilterHandlers: the default filter decoders keyed by filter name.
class FilterHandlers {
public:
    static const FilterHandlerMap& getDefaultFilterHandlers();
};

/// Port of com.itextpdf.kernel.pdf.PdfReader.
class PdfReader {
public:
    enum class StrictnessLevel { CONSERVATIVE = 5000, LENIENT = 3000 };
    static bool isStricter(StrictnessLevel a, StrictnessLevel b) { return static_cast<int>(a) > static_cast<int>(b); }

    explicit PdfReader(const std::string& filename, const ReaderProperties& properties = ReaderProperties());
    explicit PdfReader(io::RandomAccessSourcePtr byteSource, const ReaderProperties& properties = ReaderProperties());
    /// Reads from an in-memory PDF.
    static std::unique_ptr<PdfReader> fromBytes(const Bytes& bytes, const ReaderProperties& properties = ReaderProperties());
    ~PdfReader();

    void close();
    PdfReader& setUnethicalReading(bool unethicalReading) {
        unethicalReading_ = unethicalReading;
        return *this;
    }
    PdfReader& setMemorySavingMode(bool memorySavingMode) {
        memorySavingMode_ = memorySavingMode;
        return *this;
    }
    StrictnessLevel getStrictnessLevel() const { return strictnessLevel_; }
    PdfReader& setStrictnessLevel(StrictnessLevel level) {
        strictnessLevel_ = level;
        return *this;
    }
    bool isCloseStream() const { return tokens_->isCloseStream(); }
    void setCloseStream(bool closeStream) { tokens_->setCloseStream(closeStream); }
    bool hasRebuiltXref() const { return rebuiltXref_; }
    bool hasHybridXref() const { return hybridXref_; }
    bool hasXrefStm() const { return xrefStm_; }
    bool hasFixedXref() const { return fixedXref_; }
    int64_t getLastXref() const { return lastXref_; }

    Bytes readStreamBytes(PdfStream& stream, bool decode);
    Bytes readStreamBytesRaw(PdfStream& stream);
    static Bytes decodeBytes(const Bytes& b, const PdfDictionary& streamDictionary);
    static Bytes decodeBytes(const Bytes& b, const PdfDictionary& streamDictionary, const FilterHandlerMap& filterHandlers);

    std::unique_ptr<io::RandomAccessFileOrArray> getSafeFile() const { return tokens_->getSafeFile(); }
    int64_t getFileLength() const { return tokens_->length(); }
    bool isOpenedWithFullPermission() const;
    int getPermissions() const;
    int getCryptoMode() const;
    Bytes getOriginalFileId() const;
    Bytes getModifiedFileId() const;
    bool isEncrypted() const { return encrypted_; }
    const PdfDictionaryPtr& getTrailer() const { return trailer_; }
    PdfVersion getHeaderPdfVersion() const { return headerPdfVersion_; }
    PdfEncryption* getDecrypt() const { return decrypt_.get(); }
    io::PdfTokenizer& getTokens() { return *tokens_; }

    // Internal (called by PdfDocument / PdfIndirectReference)
    void setDocument(PdfDocument* document) { pdfDocument_ = document; }
    PdfDocument* getDocument() const { return pdfDocument_; }
    void readPdf();
    PdfObjectPtr readObject(PdfIndirectReference* reference);
    const ReaderProperties& getProperties() const { return properties_; }

protected:
    void readObjectStream(PdfStream& objectStream);
    PdfObjectPtr readObject(bool readAsDirect, bool objStm = false);
    PdfObjectPtr readReference(bool readAsDirect);
    PdfNamePtr readPdfName(bool readAsDirect);
    PdfDictionaryPtr readDictionary(bool objStm);
    PdfArrayPtr readArray(bool objStm);
    void readXref();
    PdfDictionaryPtr readXrefSection();
    bool readXrefStream(int64_t ptr);
    void fixXref();
    void rebuildXref();
    PdfNumberPtr getXrefPrev(const PdfObjectPtr& prevObjectToCheck);

private:
    PdfObjectPtr readObject(PdfIndirectReference* reference, bool fixXref);
    void checkPdfStreamLength(PdfStream& pdfStream);
    PdfObjectPtr createPdfNullInstance(bool readAsDirect);
    bool isCurrentObjectATrailer();
    void readDecryptObj();
    void initTokenizer(io::RandomAccessSourcePtr byteSource);

    std::unique_ptr<io::PdfTokenizer> tokens_;
    std::unique_ptr<PdfEncryption> decrypt_;
    PdfVersion headerPdfVersion_{1, 7};
    int64_t lastXref_ = 0;
    int64_t eofPos_ = 0;
    PdfDictionaryPtr trailer_;
    PdfDocument* pdfDocument_ = nullptr;
    ReaderProperties properties_;
    bool encrypted_ = false;
    bool rebuiltXref_ = false;
    bool hybridXref_ = false;
    bool fixedXref_ = false;
    bool xrefStm_ = false;
    bool unethicalReading_ = false;
    bool memorySavingMode_ = false;
    StrictnessLevel strictnessLevel_ = StrictnessLevel::LENIENT;
    PdfIndirectReference* currentIndirectReference_ = nullptr;
};

}  // namespace kernel
}  // namespace pdfcpp
