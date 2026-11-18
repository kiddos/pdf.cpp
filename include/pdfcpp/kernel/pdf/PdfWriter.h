#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>

#include "pdfcpp/kernel/pdf/PdfOutputStream.h"
#include "pdfcpp/kernel/pdf/Properties.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;

/// Object stream used in full compression mode (port of PdfObjectStream).
class PdfObjectStream : public PdfStream {
public:
    static constexpr int MAX_OBJ_STREAM_SIZE = 200;
    static std::shared_ptr<PdfObjectStream> create(PdfDocument* doc);
    void addObject(PdfObject& object);
    int getSize() const { return size_->intValue(); }
    PdfOutputStream* getIndexStream() const { return indexStream_.get(); }
    io::ByteArrayOutputStream* getIndexBytes() const { return indexBaos_.get(); }

    PdfObjectStream();  // use create()

private:
    PdfNumberPtr size_ = makeNumber(0);
    std::shared_ptr<io::ByteArrayOutputStream> indexBaos_;
    std::unique_ptr<PdfOutputStream> indexStream_;
};

/// Port of com.itextpdf.kernel.pdf.PdfWriter.
class PdfWriter : public PdfOutputStream {
public:
    explicit PdfWriter(const std::string& filename, const WriterProperties& properties = WriterProperties());
    explicit PdfWriter(std::shared_ptr<io::OutputStream> os, const WriterProperties& properties = WriterProperties());
    /// Writes into the given stream which is not owned (must outlive the writer).
    explicit PdfWriter(io::OutputStream& os, const WriterProperties& properties = WriterProperties());
    ~PdfWriter() override;

    bool isFullCompression() const { return properties_.isFullCompression.value_or(false); }
    int getCompressionLevel() const { return properties_.compressionLevel; }
    PdfWriter& setCompressionLevel(int compressionLevel) {
        properties_.setCompressionLevel(compressionLevel);
        return *this;
    }
    std::optional<PdfVersion> getPdfVersion() const { return properties_.pdfVersion; }
    WriterProperties& getProperties() { return properties_; }
    PdfWriter& setSmartMode(bool smartMode) {
        properties_.smartMode = smartMode;
        return *this;
    }

    // Internal API used by PdfDocument
    void initCryptoIfSpecified(const PdfVersion& version);
    void flushObject(PdfObject& pdfObject, bool canBeInObjStm);
    PdfObjectPtr copyObject(PdfObject* obj, PdfDocument* documentTo, bool allowDuplicating, ICopyFilter& copyFilter);
    void writeToBody(PdfObject& pdfObj);
    void writeHeader();
    void flushWaitingObjects(const std::set<PdfIndirectReference*>& forbiddenToFlush);
    void flushModifiedWaitingObjects(const std::set<PdfIndirectReference*>& forbiddenToFlush);
    void finish();
    PdfObjectStream* getObjectStream();
    void flushCopiedObjects(int64_t docId);
    std::unique_ptr<PdfEncryption>& cryptoHolder() { return ownedCrypto_; }

private:
    void markArrayContentToFlush(PdfArray& array);
    void markDictionaryContentToFlush(PdfDictionary& dictionary);
    void markObjectToFlush(PdfObject* pdfObject);

    WriterProperties properties_;
    std::shared_ptr<PdfObjectStream> objectStream_;
    std::map<PdfIndirectReference*, PdfIndirectReference*> copiedObjects_;
    std::map<std::string, PdfIndirectReference*> smartModeCache_;
    std::unique_ptr<PdfEncryption> ownedCrypto_;
    bool finished_ = false;
};

}  // namespace kernel
}  // namespace pdfcpp
