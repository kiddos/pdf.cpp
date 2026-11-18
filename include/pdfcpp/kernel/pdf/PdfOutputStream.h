#pragma once

#include <memory>

#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/kernel/pdf/PdfObject.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;
class PdfEncryption;
class PdfObjectStream;

/// Strategy for compressing streams (port of IStreamCompressionStrategy).
class IStreamCompressionStrategy {
public:
    virtual ~IStreamCompressionStrategy() = default;
    virtual PdfNamePtr getFilterName() const = 0;
    virtual PdfObjectPtr getDecodeParams() const = 0;
    virtual std::shared_ptr<io::OutputStream> createNewOutputStream(std::shared_ptr<io::OutputStream> original,
                                                                    PdfStream& stream) = 0;
};

/// Default Flate compression (port of FlateCompressionStrategy).
class FlateCompressionStrategy : public IStreamCompressionStrategy {
public:
    PdfNamePtr getFilterName() const override { return PdfName::FlateDecode; }
    PdfObjectPtr getDecodeParams() const override { return nullptr; }
    std::shared_ptr<io::OutputStream> createNewOutputStream(std::shared_ptr<io::OutputStream> original,
                                                            PdfStream& stream) override;
};

/// Port of com.itextpdf.kernel.pdf.PdfOutputStream: serializes PDF objects.
class PdfOutputStream : public io::HighPrecisionOutputStream {
public:
    explicit PdfOutputStream(std::shared_ptr<io::OutputStream> outputStream);

    /// Serializes an object (indirect objects are written as references).
    PdfOutputStream& write(PdfObject& pdfObject);
    PdfOutputStream& write(const PdfObjectPtr& pdfObject) { return write(*pdfObject); }
    using io::HighPrecisionOutputStream::write;

    /// Writes `bytes` as a big-endian integer of `size` bytes.
    void writeBigEndian(int64_t bytes, int size);

    PdfDocument* getDocument() const { return document_; }
    void setDocument(PdfDocument* document) { document_ = document; }
    PdfEncryption* getCrypto() const { return crypto_; }
    void setCrypto(PdfEncryption* crypto) { crypto_ = crypto; }
    void setCompressionStrategy(std::shared_ptr<IStreamCompressionStrategy> s) { compressionStrategy_ = std::move(s); }

protected:
    IStreamCompressionStrategy& getCompressionStrategy();
    bool checkEncryption(PdfStream& pdfStream);
    bool containsFlateFilter(PdfStream& pdfStream);
    void updateCompressionFilter(PdfStream& pdfStream);
    Bytes decodeFlateBytes(PdfStream& stream, const Bytes& bytes);

    PdfDocument* document_ = nullptr;
    PdfEncryption* crypto_ = nullptr;

private:
    void writeArray(PdfArray& pdfArray);
    void writeDictionary(PdfDictionary& pdfDictionary);
    void writeReference(PdfIndirectReference& indirectReference);
    void writePdfString(PdfString& pdfString);
    void writeName(PdfName& name);
    void writeNumber(PdfNumber& pdfNumber);
    void writeStream(PdfStream& pdfStream);
    std::shared_ptr<IStreamCompressionStrategy> compressionStrategy_;
};

}  // namespace kernel
}  // namespace pdfcpp
