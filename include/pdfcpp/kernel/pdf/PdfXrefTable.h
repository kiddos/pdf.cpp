#pragma once

#include <map>
#include <memory>
#include <vector>

#include "pdfcpp/kernel/pdf/PdfObject.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;

/// Port of com.itextpdf.kernel.pdf.PdfXrefTable. Owns all PdfIndirectReference instances of a document.
class PdfXrefTable {
public:
    static constexpr int MAX_GENERATION = 65535;

    explicit PdfXrefTable(int capacity = 32);
    ~PdfXrefTable();

    /// Adds a reference (takes shared ownership); returns the raw pointer.
    PdfIndirectReference* add(PdfIndirectReferencePtr reference);
    int size() const { return count_ + 1; }
    int getCountOfIndirectObjects() const;
    PdfIndirectReference* get(int index) const;
    PdfIndirectReferencePtr getPtr(int index) const;
    PdfIndirectReference* createNextIndirectReference(PdfDocument* document);
    PdfIndirectReference* createNewIndirectReference(PdfDocument* document) { return createNextIndirectReference(document); }
    void freeReference(PdfIndirectReference* reference);
    int getCapacity() const { return static_cast<int>(xref_.size()); }
    void setCapacity(int capacity);
    void writeXrefTableAndTrailer(PdfDocument* document, const PdfObjectPtr& fileId, const PdfObjectPtr& crypto);

    void markReadingCompleted() { readingCompleted_ = true; }
    void unmarkReadingCompleted() { readingCompleted_ = false; }
    bool isReadingCompleted() const { return readingCompleted_; }
    void initFreeReferencesList(PdfDocument* pdfDocument);
    void clear();
    void clearAllReferences();
    /// Breaks reference cycles (refersTo links) so memory can be reclaimed.
    void releaseAll();

private:
    std::vector<int> createSections(PdfDocument* document, bool dropObjectsFromObjectStream) const;
    static int getOffsetSize(int64_t startxref);
    void appendNewRefToFreeList(PdfIndirectReference* reference);
    PdfIndirectReference* removeFreeRefFromList(int freeRefObjNr);
    void ensureCount(int count);
    void extendXref(int capacity);

    std::vector<PdfIndirectReferencePtr> xref_;
    int count_ = 0;
    bool readingCompleted_ = false;
    std::map<int, PdfIndirectReference*> freeReferencesLinkedList_;
};

}  // namespace kernel
}  // namespace pdfcpp
