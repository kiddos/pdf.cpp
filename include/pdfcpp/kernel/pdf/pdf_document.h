#ifndef PDFCPP_KERNEL_PDF_PDF_DOCUMENT_H_
#define PDFCPP_KERNEL_PDF_PDF_DOCUMENT_H_

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/kernel/geom/geom.h"
#include "pdfcpp/kernel/pdf/pdf_object.h"
#include "pdfcpp/kernel/pdf/pdf_version.h"
#include "pdfcpp/kernel/pdf/properties.h"

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

/// Port of PdfObjectWrapper<T>: high-level wrapper around a low-level PDF
/// object.
template <typename T>
class PdfObjectWrapper {
 public:
  virtual ~PdfObjectWrapper() = default;
  const std::shared_ptr<T>& GetPdfObject() const { return pdf_object_; }
  PdfObjectWrapper& MakeIndirect(PdfDocument* document,
                                 PdfIndirectReference* reference = nullptr) {
    pdf_object_->MakeIndirect(document, reference);
    return *this;
  }
  PdfObjectWrapper& SetModified() {
    pdf_object_->SetModified();
    return *this;
  }
  virtual void Flush() { pdf_object_->Flush(); }
  bool IsFlushed() const { return pdf_object_->IsFlushed(); }

 protected:
  PdfObjectWrapper() = default;
  explicit PdfObjectWrapper(std::shared_ptr<T> pdf_object)
      : pdf_object_(std::move(pdf_object)) {}
  void SetPdfObject(std::shared_ptr<T> pdf_object) {
    pdf_object_ = std::move(pdf_object);
  }
  void SetForbidRelease() {
    if (pdf_object_) pdf_object_->SetState(PdfObject::kForbidRelease);
  }
  void UnsetForbidRelease() {
    if (pdf_object_) pdf_object_->ClearState(PdfObject::kForbidRelease);
  }
  void EnsureUnderlyingObjectHasIndirectReference() {
    if (pdf_object_->GetIndirectReference() == nullptr)
      throw PdfException("Object must be indirect to work with this wrapper.");
  }
  std::shared_ptr<T> pdf_object_;
};

/// Port of PdfResources: the /Resources dictionary of a page or form.
class PdfResources : public PdfObjectWrapper<PdfDictionary> {
 public:
  explicit PdfResources(PdfDictionaryPtr pdf_object);
  PdfResources();

  PdfNamePtr AddFont(PdfDocument* pdf_document, PdfFont* font);
  PdfNamePtr AddImage(PdfImageXObject& image);
  PdfNamePtr AddImage(const PdfStreamPtr& image);
  PdfNamePtr AddForm(PdfFormXObject& form);
  PdfNamePtr AddForm(const PdfStreamPtr& form);
  PdfNamePtr AddForm(PdfFormXObject& form, const PdfNamePtr& name);
  PdfNamePtr AddExtGState(PdfExtGState& ext_g_state);
  PdfNamePtr AddExtGState(const PdfDictionaryPtr& ext_g_state);
  PdfNamePtr AddProperties(const PdfDictionaryPtr& properties);
  PdfObjectPtr GetProperties(const PdfNamePtr& name) const {
    return GetResourceObject(PdfName::kProperties, name);
  }
  PdfNamePtr AddColorSpace(const PdfObjectPtr& color_space);
  PdfNamePtr AddPattern(const PdfDictionaryPtr& pattern);
  PdfNamePtr AddShading(const PdfDictionaryPtr& shading);
  PdfStreamPtr GetImage(const PdfNamePtr& name) const;
  PdfStreamPtr GetForm(const PdfNamePtr& name) const;

  PdfNamePtr GetResourceName(const PdfObject* resource) const;
  std::vector<PdfNamePtr> GetResourceNames() const;
  std::vector<PdfNamePtr> GetResourceNames(const PdfNamePtr& res_type) const;
  PdfArrayPtr GetProcSet() const {
    return GetPdfObject()->GetAsArray(PdfName::kProcSet);
  }
  void SetProcSet(const PdfArrayPtr& array) {
    GetPdfObject()->Put(PdfName::kProcSet, array);
  }
  PdfDictionaryPtr GetResource(const PdfNamePtr& res_type) const {
    return GetPdfObject()->GetAsDictionary(res_type);
  }
  PdfObjectPtr GetResourceObject(const PdfNamePtr& res_type,
                                 const PdfNamePtr& res_name) const;
  bool IsModified() const { return is_modified_; }
  void SetModifiedFlag(bool modified) { is_modified_ = modified; }
  bool IsReadOnly() const { return read_only_; }
  void SetReadOnly(bool read_only) { read_only_ = read_only; }

  /// Adds a resource object under res_type, generating a new name with the
  /// given prefix.
  PdfNamePtr AddResource(const PdfObjectPtr& resource,
                         const PdfNamePtr& res_type, const std::string& prefix);

 private:
  void AddResource(const PdfObjectPtr& resource, const PdfNamePtr& res_type,
                   const PdfNamePtr& res_name);
  void BuildResources(const PdfDictionary& dictionary);
  std::map<std::string, std::map<const PdfObject*, PdfNamePtr>>
      resource_to_name_;
  std::map<std::string, int> counters_;
  bool read_only_ = false;
  bool is_modified_ = false;
};

/// Port of PdfDocumentInfo (the /Info dictionary).
class PdfDocumentInfo {
 public:
  PdfDocumentInfo(PdfDictionaryPtr pdf_object, PdfDocument* pdf_document);
  PdfDocumentInfo& SetTitle(const std::string& title) {
    return Put(PdfName::kTitle,
               std::make_shared<PdfString>(title, IoUnicode()));
  }
  PdfDocumentInfo& SetAuthor(const std::string& author) {
    return Put(PdfName::kAuthor,
               std::make_shared<PdfString>(author, IoUnicode()));
  }
  PdfDocumentInfo& SetSubject(const std::string& subject) {
    return Put(PdfName::kSubject,
               std::make_shared<PdfString>(subject, IoUnicode()));
  }
  PdfDocumentInfo& SetKeywords(const std::string& keywords) {
    return Put(PdfName::kKeywords,
               std::make_shared<PdfString>(keywords, IoUnicode()));
  }
  PdfDocumentInfo& SetCreator(const std::string& creator) {
    return Put(PdfName::kCreator,
               std::make_shared<PdfString>(creator, IoUnicode()));
  }
  PdfDocumentInfo& SetProducer(const std::string& producer) {
    return Put(PdfName::kProducer,
               std::make_shared<PdfString>(producer, IoUnicode()));
  }
  PdfDocumentInfo& SetTrapped(const PdfNamePtr& trapped) {
    return Put(PdfName::kTrapped, trapped);
  }
  std::string GetTitle() const { return GetStringValue(PdfName::kTitle); }
  std::string GetAuthor() const { return GetStringValue(PdfName::kAuthor); }
  std::string GetSubject() const { return GetStringValue(PdfName::kSubject); }
  std::string GetKeywords() const { return GetStringValue(PdfName::kKeywords); }
  std::string GetCreator() const { return GetStringValue(PdfName::kCreator); }
  std::string GetProducer() const { return GetStringValue(PdfName::kProducer); }
  PdfNamePtr GetTrapped() const {
    return info_dictionary_->GetAsName(PdfName::kTrapped);
  }
  PdfDocumentInfo& AddCreationDate();
  PdfDocumentInfo& RemoveCreationDate();
  PdfDocumentInfo& AddModDate();
  void SetMoreInfo(const std::map<std::string, std::string>& more_info);
  void SetMoreInfo(const std::string& key, const std::string& value);
  std::string GetMoreInfo(const std::string& key) const;
  const PdfDictionaryPtr& GetPdfObject() const { return info_dictionary_; }
  PdfDocumentInfo& Put(const PdfNamePtr& key, const PdfObjectPtr& value);

 private:
  static const char* IoUnicode() { return "UnicodeBig"; }
  std::string GetStringValue(const PdfNamePtr& name) const;
  PdfDictionaryPtr info_dictionary_;
  PdfDocument* document_;
};

/// Port of PdfDate helpers.
class PdfDate {
 public:
  /// Returns the current date as a PDF date string "D:YYYYMMDDHHmmSS+HH'mm'".
  static std::string GetCurrentDateString();
};

/// Port of PdfPagesTree (simplified): keeps a flat list of pages and generates
/// a balanced tree on write.
class PdfPagesTree {
 public:
  explicit PdfPagesTree(PdfDocument* document, PdfDictionaryPtr root);
  ~PdfPagesTree();
  PdfPage* GetPage(int page_num);
  PdfPage* GetPage(const PdfDictionary* page_dictionary);
  int GetNumberOfPages() const { return static_cast<int>(page_refs_.size()); }
  int GetPageNumber(const PdfPage* page) const;
  int GetPageNumber(const PdfDictionary* page_dictionary) const;
  void AddPage(std::shared_ptr<PdfPage> pdf_page);
  void AddPage(int index, std::shared_ptr<PdfPage> pdf_page);
  std::shared_ptr<PdfPage> RemovePage(int page_num);
  /// Builds (or returns the unchanged) /Pages root.
  PdfObjectPtr GenerateTree();
  void ClearPageRefs();
  bool IsModified() const { return modified_; }

 private:
  void LoadPages(const PdfDictionaryPtr& node,
                 std::set<const PdfObject*>& visited, int depth);
  PdfDocument* document_;
  PdfDictionaryPtr original_root_;
  std::vector<PdfDictionaryPtr> page_refs_;
  std::vector<std::shared_ptr<PdfPage>> pages_;
  bool modified_ = false;
};

/// Port of PdfCatalog.
class PdfCatalog : public PdfObjectWrapper<PdfDictionary> {
 public:
  explicit PdfCatalog(PdfDictionaryPtr pdf_object, PdfDocument* document);
  explicit PdfCatalog(PdfDocument* pdf_document);
  PdfDocument* GetDocument() const { return document_; }
  PdfPagesTree& GetPageTree() { return *page_tree_; }
  PdfNamePtr GetPageMode() const {
    return GetPdfObject()->GetAsName(PdfName::kPageMode);
  }
  PdfCatalog& SetPageMode(const PdfNamePtr& page_mode) {
    return Put(PdfName::kPageMode, page_mode);
  }
  PdfNamePtr GetPageLayout() const {
    return GetPdfObject()->GetAsName(PdfName::kPageLayout);
  }
  PdfCatalog& SetPageLayout(const PdfNamePtr& page_layout) {
    return Put(PdfName::kPageLayout, page_layout);
  }
  PdfStringPtr GetLang() const {
    return GetPdfObject()->GetAsString(PdfName::kLang);
  }
  void SetLang(const PdfStringPtr& lang) { Put(PdfName::kLang, lang); }
  PdfCatalog& Put(const PdfNamePtr& key, const PdfObjectPtr& value);
  PdfCatalog& Remove(const PdfNamePtr& key);
  void Flush() override;

 private:
  PdfDocument* document_;
  std::unique_ptr<PdfPagesTree> page_tree_;
};

/// Port of com.itextpdf.kernel.pdf.PdfPage.
class PdfPage : public PdfObjectWrapper<PdfDictionary> {
 public:
  PdfPage(PdfDictionaryPtr pdf_object, PdfDocument* document);
  PdfPage(PdfDocument* pdf_document, const PageSize& page_size);
  ~PdfPage() override;

  Rectangle GetPageSize() const { return GetMediaBox(); }
  Rectangle GetPageSizeWithRotation() const;
  int GetRotation() const;
  PdfPage& SetRotation(int deg_angle);
  PdfStreamPtr GetContentStream(int index) const;
  int GetContentStreamCount() const;
  PdfStreamPtr GetFirstContentStream() const;
  PdfStreamPtr GetLastContentStream() const;
  PdfStreamPtr NewContentStreamBefore();
  PdfStreamPtr NewContentStreamAfter();
  PdfResources* GetResources();
  PdfPage& SetResources(std::shared_ptr<PdfResources> pdf_resources);
  PdfDocument* GetDocument() const;
  void Flush() override { Flush(false); }
  void Flush(bool flush_resources_content_streams);
  Rectangle GetMediaBox() const;
  PdfPage& SetMediaBox(const Rectangle& rectangle);
  Rectangle GetCropBox() const;
  PdfPage& SetCropBox(const Rectangle& rectangle);
  Rectangle GetBleedBox() const;
  PdfPage& SetBleedBox(const Rectangle& rectangle);
  Rectangle GetArtBox() const;
  PdfPage& SetArtBox(const Rectangle& rectangle);
  Rectangle GetTrimBox() const;
  PdfPage& SetTrimBox(const Rectangle& rectangle);
  /// Concatenated decoded bytes of all content streams.
  Bytes GetContentBytes() const;
  Bytes GetStreamBytes(int index) const;
  int GetNextMcid() { return mcid_++; }
  bool IsIgnorePageRotationForContent() const {
    return ignore_page_rotation_for_content_;
  }
  PdfPage& SetIgnorePageRotationForContent(bool v) {
    ignore_page_rotation_for_content_ = v;
    return *this;
  }
  PdfArrayPtr GetAnnots(bool create);
  int GetAnnotsSize() const;
  PdfPage& AddAnnotation(const PdfDictionaryPtr& annotation);
  std::vector<PdfDictionaryPtr> GetAnnotations() const;
  PdfPage& Put(const PdfNamePtr& key, const PdfObjectPtr& value);
  PdfPage& Remove(const PdfNamePtr& key);
  /// Copies this page into another document (resources are deep-copied).
  std::shared_ptr<PdfPage> CopyTo(PdfDocument* to_document);

  /// Resolves an inheritable page attribute (/Resources, /MediaBox, /CropBox,
  /// /Rotate) through /Parent.
  PdfObjectPtr GetInheritableValue(const PdfNamePtr& pdf_name,
                                   bool allow_parent_inheritance = true) const;

 private:
  Rectangle BoxOrDefault(const PdfNamePtr& name, const Rectangle& def) const;
  std::shared_ptr<PdfResources> resources_;
  int mcid_ = -1;
  bool ignore_page_rotation_for_content_ = false;
};

/// Port of com.itextpdf.kernel.pdf.PdfDocument.
class PdfDocument {
 public:
  explicit PdfDocument(PdfReader& reader, const DocumentProperties& properties =
                                              DocumentProperties());
  explicit PdfDocument(PdfWriter& writer, const DocumentProperties& properties =
                                              DocumentProperties());
  PdfDocument(PdfReader& reader, PdfWriter& writer,
              const StampingProperties& properties = StampingProperties());
  /// Owning convenience constructors.
  explicit PdfDocument(
      std::unique_ptr<PdfReader> reader,
      const DocumentProperties& properties = DocumentProperties());
  explicit PdfDocument(
      std::unique_ptr<PdfWriter> writer,
      const DocumentProperties& properties = DocumentProperties());
  PdfDocument(std::unique_ptr<PdfReader> reader,
              std::unique_ptr<PdfWriter> writer,
              const StampingProperties& properties = StampingProperties());
  virtual ~PdfDocument();
  PdfDocument(const PdfDocument&) = delete;
  PdfDocument& operator=(const PdfDocument&) = delete;

  bool IsClosing() const { return is_closing_; }
  PdfObjectPtr GetPdfObject(int obj_num);
  int GetNumberOfPdfObjects() const;
  PdfPage* GetPage(int page_num);
  PdfPage* GetPage(const PdfDictionary* page_dictionary);
  PdfPage* GetFirstPage() { return GetPage(1); }
  PdfPage* GetLastPage() { return GetPage(GetNumberOfPages()); }
  PdfPage* AddNewPage() { return AddNewPage(GetDefaultPageSize()); }
  PdfPage* AddNewPage(const PageSize& page_size);
  PdfPage* AddNewPage(int index) {
    return AddNewPage(index, GetDefaultPageSize());
  }
  PdfPage* AddNewPage(int index, const PageSize& page_size);
  PdfPage* AddPage(std::shared_ptr<PdfPage> page);
  PdfPage* AddPage(int index, std::shared_ptr<PdfPage> page);
  int GetNumberOfPages() const;
  int GetPageNumber(const PdfPage* page) const;
  int GetPageNumber(const PdfDictionary* page_dictionary) const;
  bool MovePage(PdfPage* page, int insert_before);
  void MovePage(int page_number, int insert_before);
  bool RemovePage(PdfPage* page);
  void RemovePage(int page_num);
  PdfDocumentInfo& GetDocumentInfo();
  PdfStringPtr GetOriginalDocumentId() const { return original_document_id_; }
  PdfStringPtr GetModifiedDocumentId() const { return modified_document_id_; }
  PageSize GetDefaultPageSize() const { return default_page_size_; }
  void SetDefaultPageSize(const PageSize& page_size) {
    default_page_size_ = page_size;
  }
  PdfWriter* GetWriter() const { return writer_; }
  PdfReader* GetReader() const { return reader_; }
  bool IsAppendMode() const { return properties_.append_mode; }
  PdfIndirectReference* CreateNextIndirectReference();
  PdfVersion GetPdfVersion() const { return pdf_version_; }
  PdfCatalog& GetCatalog() { return *catalog_; }
  void Close();
  bool IsClosed() const { return closed_; }
  std::vector<PdfPage*> CopyPagesTo(int page_from, int page_to,
                                    PdfDocument& to_document,
                                    int insert_before_page = -1);
  std::vector<PdfPage*> CopyPagesTo(const std::vector<int>& pages_to_copy,
                                    PdfDocument& to_document,
                                    int insert_before_page = -1);
  bool IsCloseReader() const { return close_reader_; }
  void SetCloseReader(bool v) { close_reader_ = v; }
  bool IsCloseWriter() const { return close_writer_; }
  void SetCloseWriter(bool v) { close_writer_ = v; }
  bool IsFlushUnusedObjects() const { return flush_unused_objects_; }
  void SetFlushUnusedObjects(bool v) { flush_unused_objects_ = v; }
  std::vector<PdfIndirectReference*> ListIndirectReferences() const;
  const PdfDictionaryPtr& GetTrailer() const { return trailer_; }
  int64_t GetDocumentId() const { return document_id_; }
  PdfXrefTable* GetXref() const { return xref_.get(); }
  void FlushObject(PdfObject* pdf_object, bool can_be_in_obj_stm);
  bool DoesStreamBelongToEmbeddedFile(const PdfStream&) const { return false; }

  /// Fonts used by this document (flushed on close).
  PdfFont* GetDefaultFont();
  PdfFont* AddFont(std::shared_ptr<PdfFont> font);
  PdfFont* GetFont(const PdfDictionaryPtr& dictionary);

  void SetXmpMetadata(const Bytes& xmp_metadata) {
    xmp_metadata_ = xmp_metadata;
  }
  Bytes GetXmpMetadataBytes() const;

 protected:
  void Open(std::optional<PdfVersion> new_pdf_version);
  void CheckClosingStatus() const;
  void FlushFonts();

 private:
  void Init(PdfReader* reader, PdfWriter* writer,
            const StampingProperties& properties);
  void ReadDocumentIds();

  std::unique_ptr<PdfReader> owned_reader_;
  std::unique_ptr<PdfWriter> owned_writer_;
  PdfReader* reader_ = nullptr;
  PdfWriter* writer_ = nullptr;
  StampingProperties properties_;
  std::unique_ptr<PdfXrefTable> xref_;
  PdfDictionaryPtr trailer_;
  std::unique_ptr<PdfCatalog> catalog_;
  std::unique_ptr<PdfDocumentInfo> info_;
  PdfVersion pdf_version_ = PdfVersion::kPdf17;
  PageSize default_page_size_ = PageSize::kDefault;
  PdfStringPtr original_document_id_;
  PdfStringPtr modified_document_id_;
  bool closed_ = false;
  bool is_closing_ = false;
  bool close_reader_ = true;
  bool close_writer_ = true;
  bool flush_unused_objects_ = false;
  int64_t document_id_;
  std::map<const PdfObject*, std::shared_ptr<PdfFont>> document_fonts_;
  std::shared_ptr<PdfFont> default_font_;
  Bytes xmp_metadata_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_DOCUMENT_H_
