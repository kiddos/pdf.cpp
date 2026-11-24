#ifndef PDFCPP_KERNEL_PDF_PDF_XREF_TABLE_H_
#define PDFCPP_KERNEL_PDF_PDF_XREF_TABLE_H_

#include <map>
#include <memory>
#include <vector>

#include "pdfcpp/kernel/pdf/pdf_object.h"

namespace pdfcpp {
namespace kernel {

class PdfDocument;

/// Port of com.itextpdf.kernel.pdf.PdfXrefTable. Owns all PdfIndirectReference
/// instances of a document.
class PdfXrefTable {
 public:
  static constexpr int kMaxGeneration = 65535;

  explicit PdfXrefTable(int capacity = 32);
  ~PdfXrefTable();

  /// Adds a reference (takes shared ownership); returns the raw pointer.
  PdfIndirectReference* Add(PdfIndirectReferencePtr reference);
  int size() const { return count_ + 1; }
  int GetCountOfIndirectObjects() const;
  PdfIndirectReference* Get(int index) const;
  PdfIndirectReferencePtr GetPtr(int index) const;
  PdfIndirectReference* CreateNextIndirectReference(PdfDocument* document);
  PdfIndirectReference* CreateNewIndirectReference(PdfDocument* document) {
    return CreateNextIndirectReference(document);
  }
  void FreeReference(PdfIndirectReference* reference);
  int GetCapacity() const { return static_cast<int>(xref_.size()); }
  void SetCapacity(int capacity);
  void WriteXrefTableAndTrailer(PdfDocument* document,
                                const PdfObjectPtr& file_id,
                                const PdfObjectPtr& crypto);

  void MarkReadingCompleted() { reading_completed_ = true; }
  void UnmarkReadingCompleted() { reading_completed_ = false; }
  bool IsReadingCompleted() const { return reading_completed_; }
  void InitFreeReferencesList(PdfDocument* pdf_document);
  void clear();
  void ClearAllReferences();
  /// Breaks reference cycles (refersTo links) so memory can be reclaimed.
  void ReleaseAll();

 private:
  std::vector<int> CreateSections(PdfDocument* document,
                                  bool drop_objects_from_object_stream) const;
  static int GetOffsetSize(int64_t startxref);
  void AppendNewRefToFreeList(PdfIndirectReference* reference);
  PdfIndirectReference* RemoveFreeRefFromList(int free_ref_obj_nr);
  void EnsureCount(int count);
  void ExtendXref(int capacity);

  std::vector<PdfIndirectReferencePtr> xref_;
  int count_ = 0;
  bool reading_completed_ = false;
  std::map<int, PdfIndirectReference*> free_references_linked_list_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_XREF_TABLE_H_
