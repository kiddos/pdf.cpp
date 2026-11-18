#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/kernel/geom/Geom.h"
#include "pdfcpp/kernel/pdf/PdfObject.h"
#include "pdfcpp/kernel/pdf/PdfVersion.h"
#include "pdfcpp/kernel/pdf/Properties.h"

namespace pdfcpp {
namespace kernel {

class PdfReader;
class PdfWriter;
class PdfXrefTable;
class PdfDocument;
class PdfPage;
class PdfFont;
class PdfImageXObject;
class PdfFormXObject;
class PdfExtGState;
class PdfColorSpace;
class PdfPattern;
class PdfShading;
class PdfAnnotation;
class PdfOutline;

/// Port of PdfObjectWrapper<T>: high-level wrapper around a low-level PDF object.
template <typename T>
class PdfObjectWrapper {
public:
    virtual ~PdfObjectWrapper() = default;
    const std::shared_ptr<T>& getPdfObject() const { return pdfObject_; }
    PdfObjectWrapper& makeIndirect(PdfDocument* document, PdfIndirectReference* reference = nullptr) {
        pdfObject_->makeIndirect(document, reference);
        return *this;
    }
    PdfObjectWrapper& setModified() {
        pdfObject_->setModified();
        return *this;
    }
    virtual void flush() { pdfObject_->flush(); }
    bool isFlushed() const { return pdfObject_->isFlushed(); }

protected:
    PdfObjectWrapper() = default;
    explicit PdfObjectWrapper(std::shared_ptr<T> pdfObject) : pdfObject_(std::move(pdfObject)) {}
    void setPdfObject(std::shared_ptr<T> pdfObject) { pdfObject_ = std::move(pdfObject); }
    void setForbidRelease() {
        if (pdfObject_) pdfObject_->setState(PdfObject::FORBID_RELEASE);
    }
    void unsetForbidRelease() {
        if (pdfObject_) pdfObject_->clearState(PdfObject::FORBID_RELEASE);
    }
    void ensureUnderlyingObjectHasIndirectReference() {
        if (pdfObject_->getIndirectReference() == nullptr) throw PdfException("Object must be indirect to work with this wrapper.");
    }
    std::shared_ptr<T> pdfObject_;
};

/// Port of PdfResources: the /Resources dictionary of a page or form.
class PdfResources : public PdfObjectWrapper<PdfDictionary> {
public:
    explicit PdfResources(PdfDictionaryPtr pdfObject);
    PdfResources();

    PdfNamePtr addFont(PdfDocument* pdfDocument, PdfFont* font);
    PdfNamePtr addImage(PdfImageXObject& image);
    PdfNamePtr addImage(const PdfStreamPtr& image);
    PdfNamePtr addForm(PdfFormXObject& form);
    PdfNamePtr addForm(const PdfStreamPtr& form);
    PdfNamePtr addForm(PdfFormXObject& form, const PdfNamePtr& name);
    PdfNamePtr addExtGState(PdfExtGState& extGState);
    PdfNamePtr addExtGState(const PdfDictionaryPtr& extGState);
    PdfNamePtr addProperties(const PdfDictionaryPtr& properties);
    PdfObjectPtr getProperties(const PdfNamePtr& name) const { return getResourceObject(PdfName::Properties, name); }
    PdfNamePtr addColorSpace(const PdfObjectPtr& colorSpace);
    PdfNamePtr addPattern(const PdfDictionaryPtr& pattern);
    PdfNamePtr addShading(const PdfDictionaryPtr& shading);
    PdfStreamPtr getImage(const PdfNamePtr& name) const;
    PdfStreamPtr getForm(const PdfNamePtr& name) const;

    PdfNamePtr getResourceName(const PdfObject* resource) const;
    std::vector<PdfNamePtr> getResourceNames() const;
    std::vector<PdfNamePtr> getResourceNames(const PdfNamePtr& resType) const;
    PdfArrayPtr getProcSet() const { return getPdfObject()->getAsArray(PdfName::ProcSet); }
    void setProcSet(const PdfArrayPtr& array) { getPdfObject()->put(PdfName::ProcSet, array); }
    PdfDictionaryPtr getResource(const PdfNamePtr& resType) const { return getPdfObject()->getAsDictionary(resType); }
    PdfObjectPtr getResourceObject(const PdfNamePtr& resType, const PdfNamePtr& resName) const;
    bool isModified() const { return isModified_; }
    void setModifiedFlag(bool modified) { isModified_ = modified; }
    bool isReadOnly() const { return readOnly_; }
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }

    /// Adds a resource object under resType, generating a new name with the given prefix.
    PdfNamePtr addResource(const PdfObjectPtr& resource, const PdfNamePtr& resType, const std::string& prefix);

private:
    void addResource(const PdfObjectPtr& resource, const PdfNamePtr& resType, const PdfNamePtr& resName);
    void buildResources(const PdfDictionary& dictionary);
    std::map<std::string, std::map<const PdfObject*, PdfNamePtr>> resourceToName_;
    std::map<std::string, int> counters_;
    bool readOnly_ = false;
    bool isModified_ = false;
};

/// Port of PdfDocumentInfo (the /Info dictionary).
class PdfDocumentInfo {
public:
    PdfDocumentInfo(PdfDictionaryPtr pdfObject, PdfDocument* pdfDocument);
    PdfDocumentInfo& setTitle(const std::string& title) { return put(PdfName::Title, std::make_shared<PdfString>(title, io_unicode())); }
    PdfDocumentInfo& setAuthor(const std::string& author) { return put(PdfName::Author, std::make_shared<PdfString>(author, io_unicode())); }
    PdfDocumentInfo& setSubject(const std::string& subject) { return put(PdfName::Subject, std::make_shared<PdfString>(subject, io_unicode())); }
    PdfDocumentInfo& setKeywords(const std::string& keywords) { return put(PdfName::Keywords, std::make_shared<PdfString>(keywords, io_unicode())); }
    PdfDocumentInfo& setCreator(const std::string& creator) { return put(PdfName::Creator, std::make_shared<PdfString>(creator, io_unicode())); }
    PdfDocumentInfo& setProducer(const std::string& producer) { return put(PdfName::Producer, std::make_shared<PdfString>(producer, io_unicode())); }
    PdfDocumentInfo& setTrapped(const PdfNamePtr& trapped) { return put(PdfName::Trapped, trapped); }
    std::string getTitle() const { return getStringValue(PdfName::Title); }
    std::string getAuthor() const { return getStringValue(PdfName::Author); }
    std::string getSubject() const { return getStringValue(PdfName::Subject); }
    std::string getKeywords() const { return getStringValue(PdfName::Keywords); }
    std::string getCreator() const { return getStringValue(PdfName::Creator); }
    std::string getProducer() const { return getStringValue(PdfName::Producer); }
    PdfNamePtr getTrapped() const { return infoDictionary_->getAsName(PdfName::Trapped); }
    PdfDocumentInfo& addCreationDate();
    PdfDocumentInfo& removeCreationDate();
    PdfDocumentInfo& addModDate();
    void setMoreInfo(const std::map<std::string, std::string>& moreInfo);
    void setMoreInfo(const std::string& key, const std::string& value);
    std::string getMoreInfo(const std::string& key) const;
    const PdfDictionaryPtr& getPdfObject() const { return infoDictionary_; }
    PdfDocumentInfo& put(const PdfNamePtr& key, const PdfObjectPtr& value);

private:
    static const char* io_unicode() { return "UnicodeBig"; }
    std::string getStringValue(const PdfNamePtr& name) const;
    PdfDictionaryPtr infoDictionary_;
    PdfDocument* document_;
};

/// Port of PdfDate helpers.
class PdfDate {
public:
    /// Returns the current date as a PDF date string "D:YYYYMMDDHHmmSS+HH'mm'".
    static std::string getCurrentDateString();
};

/// Port of PdfPagesTree (simplified): keeps a flat list of pages and generates a balanced tree on write.
class PdfPagesTree {
public:
    explicit PdfPagesTree(PdfDocument* document, PdfDictionaryPtr root);
    ~PdfPagesTree();
    PdfPage* getPage(int pageNum);
    PdfPage* getPage(const PdfDictionary* pageDictionary);
    int getNumberOfPages() const { return static_cast<int>(pageRefs_.size()); }
    int getPageNumber(const PdfPage* page) const;
    int getPageNumber(const PdfDictionary* pageDictionary) const;
    void addPage(std::shared_ptr<PdfPage> pdfPage);
    void addPage(int index, std::shared_ptr<PdfPage> pdfPage);
    std::shared_ptr<PdfPage> removePage(int pageNum);
    /// Builds (or returns the unchanged) /Pages root.
    PdfObjectPtr generateTree();
    void clearPageRefs();
    bool isModified() const { return modified_; }

private:
    void loadPages(const PdfDictionaryPtr& node, std::set<const PdfObject*>& visited, int depth);
    PdfDocument* document_;
    PdfDictionaryPtr originalRoot_;
    std::vector<PdfDictionaryPtr> pageRefs_;
    std::vector<std::shared_ptr<PdfPage>> pages_;
    bool modified_ = false;
};

/// Port of PdfCatalog.
class PdfCatalog : public PdfObjectWrapper<PdfDictionary> {
public:
    explicit PdfCatalog(PdfDictionaryPtr pdfObject, PdfDocument* document);
    explicit PdfCatalog(PdfDocument* pdfDocument);
    PdfDocument* getDocument() const { return document_; }
    PdfPagesTree& getPageTree() { return *pageTree_; }
    PdfNamePtr getPageMode() const { return getPdfObject()->getAsName(PdfName::PageMode); }
    PdfCatalog& setPageMode(const PdfNamePtr& pageMode) { return put(PdfName::PageMode, pageMode); }
    PdfNamePtr getPageLayout() const { return getPdfObject()->getAsName(PdfName::PageLayout); }
    PdfCatalog& setPageLayout(const PdfNamePtr& pageLayout) { return put(PdfName::PageLayout, pageLayout); }
    PdfStringPtr getLang() const { return getPdfObject()->getAsString(PdfName::Lang); }
    void setLang(const PdfStringPtr& lang) { put(PdfName::Lang, lang); }
    PdfCatalog& put(const PdfNamePtr& key, const PdfObjectPtr& value);
    PdfCatalog& remove(const PdfNamePtr& key);
    void flush() override;

private:
    PdfDocument* document_;
    std::unique_ptr<PdfPagesTree> pageTree_;
};

/// Port of com.itextpdf.kernel.pdf.PdfPage.
class PdfPage : public PdfObjectWrapper<PdfDictionary> {
public:
    PdfPage(PdfDictionaryPtr pdfObject, PdfDocument* document);
    PdfPage(PdfDocument* pdfDocument, const PageSize& pageSize);
    ~PdfPage() override;

    Rectangle getPageSize() const { return getMediaBox(); }
    Rectangle getPageSizeWithRotation() const;
    int getRotation() const;
    PdfPage& setRotation(int degAngle);
    PdfStreamPtr getContentStream(int index) const;
    int getContentStreamCount() const;
    PdfStreamPtr getFirstContentStream() const;
    PdfStreamPtr getLastContentStream() const;
    PdfStreamPtr newContentStreamBefore();
    PdfStreamPtr newContentStreamAfter();
    PdfResources* getResources();
    PdfPage& setResources(std::shared_ptr<PdfResources> pdfResources);
    PdfDocument* getDocument() const;
    void flush() override { flush(false); }
    void flush(bool flushResourcesContentStreams);
    Rectangle getMediaBox() const;
    PdfPage& setMediaBox(const Rectangle& rectangle);
    Rectangle getCropBox() const;
    PdfPage& setCropBox(const Rectangle& rectangle);
    Rectangle getBleedBox() const;
    PdfPage& setBleedBox(const Rectangle& rectangle);
    Rectangle getArtBox() const;
    PdfPage& setArtBox(const Rectangle& rectangle);
    Rectangle getTrimBox() const;
    PdfPage& setTrimBox(const Rectangle& rectangle);
    /// Concatenated decoded bytes of all content streams.
    Bytes getContentBytes() const;
    Bytes getStreamBytes(int index) const;
    int getNextMcid() { return mcid_++; }
    bool isIgnorePageRotationForContent() const { return ignorePageRotationForContent_; }
    PdfPage& setIgnorePageRotationForContent(bool v) {
        ignorePageRotationForContent_ = v;
        return *this;
    }
    PdfArrayPtr getAnnots(bool create);
    int getAnnotsSize() const;
    PdfPage& addAnnotation(const PdfDictionaryPtr& annotation);
    std::vector<PdfDictionaryPtr> getAnnotations() const;
    PdfPage& put(const PdfNamePtr& key, const PdfObjectPtr& value);
    PdfPage& remove(const PdfNamePtr& key);
    /// Copies this page into another document (resources are deep-copied).
    std::shared_ptr<PdfPage> copyTo(PdfDocument* toDocument);

    /// Resolves an inheritable page attribute (/Resources, /MediaBox, /CropBox, /Rotate) through /Parent.
    PdfObjectPtr getInheritableValue(const PdfNamePtr& pdfName, bool allowParentInheritance = true) const;

private:
    Rectangle boxOrDefault(const PdfNamePtr& name, const Rectangle& def) const;
    std::shared_ptr<PdfResources> resources_;
    int mcid_ = -1;
    bool ignorePageRotationForContent_ = false;
};

/// Port of com.itextpdf.kernel.pdf.PdfDocument.
class PdfDocument {
public:
    explicit PdfDocument(PdfReader& reader, const DocumentProperties& properties = DocumentProperties());
    explicit PdfDocument(PdfWriter& writer, const DocumentProperties& properties = DocumentProperties());
    PdfDocument(PdfReader& reader, PdfWriter& writer, const StampingProperties& properties = StampingProperties());
    /// Owning convenience constructors.
    explicit PdfDocument(std::unique_ptr<PdfReader> reader, const DocumentProperties& properties = DocumentProperties());
    explicit PdfDocument(std::unique_ptr<PdfWriter> writer, const DocumentProperties& properties = DocumentProperties());
    PdfDocument(std::unique_ptr<PdfReader> reader, std::unique_ptr<PdfWriter> writer,
                const StampingProperties& properties = StampingProperties());
    virtual ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    bool isClosing() const { return isClosing_; }
    PdfObjectPtr getPdfObject(int objNum);
    int getNumberOfPdfObjects() const;
    PdfPage* getPage(int pageNum);
    PdfPage* getPage(const PdfDictionary* pageDictionary);
    PdfPage* getFirstPage() { return getPage(1); }
    PdfPage* getLastPage() { return getPage(getNumberOfPages()); }
    PdfPage* addNewPage() { return addNewPage(getDefaultPageSize()); }
    PdfPage* addNewPage(const PageSize& pageSize);
    PdfPage* addNewPage(int index) { return addNewPage(index, getDefaultPageSize()); }
    PdfPage* addNewPage(int index, const PageSize& pageSize);
    PdfPage* addPage(std::shared_ptr<PdfPage> page);
    PdfPage* addPage(int index, std::shared_ptr<PdfPage> page);
    int getNumberOfPages() const;
    int getPageNumber(const PdfPage* page) const;
    int getPageNumber(const PdfDictionary* pageDictionary) const;
    bool movePage(PdfPage* page, int insertBefore);
    void movePage(int pageNumber, int insertBefore);
    bool removePage(PdfPage* page);
    void removePage(int pageNum);
    PdfDocumentInfo& getDocumentInfo();
    PdfStringPtr getOriginalDocumentId() const { return originalDocumentId_; }
    PdfStringPtr getModifiedDocumentId() const { return modifiedDocumentId_; }
    PageSize getDefaultPageSize() const { return defaultPageSize_; }
    void setDefaultPageSize(const PageSize& pageSize) { defaultPageSize_ = pageSize; }
    PdfWriter* getWriter() const { return writer_; }
    PdfReader* getReader() const { return reader_; }
    bool isAppendMode() const { return properties_.appendMode; }
    PdfIndirectReference* createNextIndirectReference();
    PdfVersion getPdfVersion() const { return pdfVersion_; }
    PdfCatalog& getCatalog() { return *catalog_; }
    void close();
    bool isClosed() const { return closed_; }
    std::vector<PdfPage*> copyPagesTo(int pageFrom, int pageTo, PdfDocument& toDocument, int insertBeforePage = -1);
    std::vector<PdfPage*> copyPagesTo(const std::vector<int>& pagesToCopy, PdfDocument& toDocument,
                                      int insertBeforePage = -1);
    bool isCloseReader() const { return closeReader_; }
    void setCloseReader(bool v) { closeReader_ = v; }
    bool isCloseWriter() const { return closeWriter_; }
    void setCloseWriter(bool v) { closeWriter_ = v; }
    bool isFlushUnusedObjects() const { return flushUnusedObjects_; }
    void setFlushUnusedObjects(bool v) { flushUnusedObjects_ = v; }
    std::vector<PdfIndirectReference*> listIndirectReferences() const;
    const PdfDictionaryPtr& getTrailer() const { return trailer_; }
    int64_t getDocumentId() const { return documentId_; }
    PdfXrefTable* getXref() const { return xref_.get(); }
    void flushObject(PdfObject* pdfObject, bool canBeInObjStm);
    bool doesStreamBelongToEmbeddedFile(const PdfStream&) const { return false; }

    /// Fonts used by this document (flushed on close).
    PdfFont* getDefaultFont();
    PdfFont* addFont(std::shared_ptr<PdfFont> font);
    PdfFont* getFont(const PdfDictionaryPtr& dictionary);

    void setXmpMetadata(const Bytes& xmpMetadata) { xmpMetadata_ = xmpMetadata; }
    Bytes getXmpMetadataBytes() const;

protected:
    void open(std::optional<PdfVersion> newPdfVersion);
    void checkClosingStatus() const;
    void flushFonts();

private:
    void init(PdfReader* reader, PdfWriter* writer, const StampingProperties& properties);
    void readDocumentIds();

    std::unique_ptr<PdfReader> ownedReader_;
    std::unique_ptr<PdfWriter> ownedWriter_;
    PdfReader* reader_ = nullptr;
    PdfWriter* writer_ = nullptr;
    StampingProperties properties_;
    std::unique_ptr<PdfXrefTable> xref_;
    PdfDictionaryPtr trailer_;
    std::unique_ptr<PdfCatalog> catalog_;
    std::unique_ptr<PdfDocumentInfo> info_;
    PdfVersion pdfVersion_ = PdfVersion::PDF_1_7;
    PageSize defaultPageSize_ = PageSize::DEFAULT;
    PdfStringPtr originalDocumentId_;
    PdfStringPtr modifiedDocumentId_;
    bool closed_ = false;
    bool isClosing_ = false;
    bool closeReader_ = true;
    bool closeWriter_ = true;
    bool flushUnusedObjects_ = false;
    int64_t documentId_;
    std::map<const PdfObject*, std::shared_ptr<PdfFont>> documentFonts_;
    std::shared_ptr<PdfFont> defaultFont_;
    Bytes xmpMetadata_;
};

}  // namespace kernel
}  // namespace pdfcpp
