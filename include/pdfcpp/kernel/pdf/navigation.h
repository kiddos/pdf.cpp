#ifndef PDFCPP_KERNEL_PDF_NAVIGATION_H_
#define PDFCPP_KERNEL_PDF_NAVIGATION_H_

#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfDestination / PdfExplicitDestination / PdfStringDestination.
class PdfDestination : public PdfObjectWrapper<PdfObject> {
 public:
  explicit PdfDestination(PdfObjectPtr pdf_object)
      : PdfObjectWrapper<PdfObject>(std::move(pdf_object)) {}
  static std::shared_ptr<PdfDestination> CreateXyz(PdfPage* page, float left,
                                                   float top, float zoom);
  static std::shared_ptr<PdfDestination> CreateFit(PdfPage* page);
  static std::shared_ptr<PdfDestination> CreateFitH(PdfPage* page, float top);
  static std::shared_ptr<PdfDestination> CreateFitR(PdfPage* page,
                                                    const Rectangle& rect);
  /// Named destination (port of PdfStringDestination).
  static std::shared_ptr<PdfDestination> CreateNamed(const std::string& name);
};

/// Port of PdfAction.
class PdfAction : public PdfObjectWrapper<PdfDictionary> {
 public:
  PdfAction()
      : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) {
    Put(PdfName::kType, PdfName::kAction);
  }
  explicit PdfAction(PdfDictionaryPtr dict)
      : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
  static std::shared_ptr<PdfAction> CreateUri(const std::string& uri,
                                              bool is_map = false);
  static std::shared_ptr<PdfAction> CreateGoTo(
      const std::shared_ptr<PdfDestination>& destination);
  static std::shared_ptr<PdfAction> CreateGoTo(
      const std::string& named_destination);
  static std::shared_ptr<PdfAction> CreateGoToR(const std::string& filename,
                                                int page_num,
                                                bool new_window = false);
  static std::shared_ptr<PdfAction> CreateJavaScript(
      const std::string& java_script);
  static std::shared_ptr<PdfAction> CreateNamed(const PdfNamePtr& named_action);
  PdfAction& Next(const std::shared_ptr<PdfAction>& next_action);
  PdfAction& Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    GetPdfObject()->Put(key, value);
    return *this;
  }
};

/// Port of PdfAnnotation (+ PdfLinkAnnotation, PdfTextAnnotation,
/// PdfSquareAnnotation, PdfFreeTextAnnotation subsets).
class PdfAnnotation : public PdfObjectWrapper<PdfDictionary> {
 public:
  static constexpr int kInvisible = 1, kHidden = 2, kPrint = 4, kNoZoom = 8,
                       kNoRotate = 16, kNoView = 32, kReadOnly = 64,
                       kLocked = 128, kToggleNoView = 256,
                       kLockedContents = 512;
  static constexpr int kStyleSolid = 'S', kStyleDashed = 'D',
                       kStyleBeveled = 'B', kStyleInset = 'I',
                       kStyleUnderline = 'U';

  PdfAnnotation(const PdfNamePtr& subtype, const Rectangle& rect);
  explicit PdfAnnotation(PdfDictionaryPtr dict)
      : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
  /// Link annotation.
  static std::shared_ptr<PdfAnnotation> CreateLink(const Rectangle& rect);
  /// Sticky note.
  static std::shared_ptr<PdfAnnotation> CreateText(const Rectangle& rect);
  static std::shared_ptr<PdfAnnotation> CreateSquare(const Rectangle& rect);
  static std::shared_ptr<PdfAnnotation> CreateFreeText(
      const Rectangle& rect, const std::string& contents);
  static std::shared_ptr<PdfAnnotation> CreateHighlight(
      const Rectangle& rect, const std::vector<float>& quad_points);

  PdfNamePtr GetSubtype() const {
    return GetPdfObject()->GetAsName(PdfName::kSubtype);
  }
  PdfStringPtr GetContents() const {
    return GetPdfObject()->GetAsString(PdfName::kContents);
  }
  PdfAnnotation& SetContents(const std::string& contents) {
    return Put(PdfName::kContents,
               std::make_shared<PdfString>(contents, "UnicodeBig"));
  }
  int GetFlags() const {
    auto f = GetPdfObject()->GetAsNumber(PdfName::kF);
    return f ? f->IntValue() : 0;
  }
  PdfAnnotation& SetFlags(int flags) {
    return Put(PdfName::kF, MakeNumber(flags));
  }
  PdfAnnotation& SetFlag(int flag) { return SetFlags(GetFlags() | flag); }
  PdfAnnotation& ResetFlag(int flag) { return SetFlags(GetFlags() & ~flag); }
  bool HasFlag(int flag) const { return (GetFlags() & flag) != 0; }
  PdfAnnotation& SetColor(const ColorPtr& color) {
    return Put(PdfName::kC, std::make_shared<PdfArray>(color->GetColorValue()));
  }
  PdfAnnotation& SetTitle(const std::string& title) {
    return Put(PdfName::kT, std::make_shared<PdfString>(title, "UnicodeBig"));
  }
  PdfAnnotation& SetBorder(float h_radius, float v_radius, float width) {
    return Put(PdfName::kBorder, std::make_shared<PdfArray>(std::vector<float>{
                                     h_radius, v_radius, width}));
  }
  PdfAnnotation& SetAction(const std::shared_ptr<PdfAction>& action) {
    return Put(PdfName::kA, action->GetPdfObject());
  }
  PdfAnnotation& SetDestination(
      const std::shared_ptr<PdfDestination>& destination) {
    return Put(PdfName::kDest, destination->GetPdfObject());
  }
  PdfAnnotation& SetHighlightMode(const PdfNamePtr& mode) {
    return Put(PdfName::kH, mode);
  }
  PdfAnnotation& SetOpen(bool open) {
    return Put(PdfName::kOpen, PdfBoolean::ValueOf(open));
  }
  PdfAnnotation& SetNormalAppearance(const PdfStreamPtr& appearance);
  PdfAnnotation& SetRectangle(const Rectangle& rect) {
    return Put(PdfName::kRect, std::make_shared<PdfArray>(rect));
  }
  std::optional<Rectangle> GetRectangle() const {
    return GetPdfObject()->GetAsRectangle(PdfName::kRect);
  }
  PdfAnnotation& Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    GetPdfObject()->Put(key, value);
    GetPdfObject()->SetModified();
    return *this;
  }
};

/// Port of PdfOutline (bookmarks).
class PdfOutline {
 public:
  static constexpr int kFlagItalic = 1, kFlagBold = 2;
  /// Root outline of a document (creates /Outlines if needed).
  static std::shared_ptr<PdfOutline> GetOutlines(PdfDocument& document,
                                                 bool update_outlines = false);

  PdfOutline(const std::string& title, PdfDictionaryPtr content,
             PdfDocument* document);
  const std::string& GetTitle() const { return title_; }
  void SetTitle(const std::string& title);
  void SetColor(const ColorPtr& color) {
    content_->Put(PdfName::kC,
                  std::make_shared<PdfArray>(color->GetColorValue()));
  }
  void SetStyle(int style) { content_->Put(PdfName::kF, MakeNumber(style)); }
  const PdfDictionaryPtr& GetContent() const { return content_; }
  const std::vector<std::shared_ptr<PdfOutline>>& GetAllChildren() const {
    return children_;
  }
  PdfOutline* GetParent() const { return parent_; }
  void AddDestination(const std::shared_ptr<PdfDestination>& destination) {
    content_->Put(PdfName::kDest, destination->GetPdfObject());
  }
  void AddAction(const std::shared_ptr<PdfAction>& action) {
    content_->Put(PdfName::kA, action->GetPdfObject());
  }
  void SetOpen(bool open);
  bool IsOpen() const { return open_; }
  std::shared_ptr<PdfOutline> AddOutline(const std::string& title,
                                         int position = -1);
  void RemoveOutline();

 private:
  void RebuildLinks();
  /// Number of descendants visible when this item is open (PDF 1.7, 12.3.3
  /// /Count semantics).
  int VisibleDescendants() const;
  void InitFromDictionary(int depth);
  std::string title_;
  PdfDictionaryPtr content_;
  PdfDocument* document_;
  PdfOutline* parent_ = nullptr;
  std::vector<std::shared_ptr<PdfOutline>> children_;
  bool open_ = true;
};

/// Port of PdfMerger.
class PdfMerger {
 public:
  explicit PdfMerger(PdfDocument& destination, bool merge_tags = true,
                     bool merge_outlines = true)
      : destination_(destination) {}
  PdfMerger& SetCloseSourceDocuments(bool v) {
    close_source_documents_ = v;
    return *this;
  }
  PdfMerger& Merge(PdfDocument& from, int from_page, int to_page);
  PdfMerger& Merge(PdfDocument& from, const std::vector<int>& pages);
  void Close() { destination_.Close(); }

 private:
  PdfDocument& destination_;
  bool close_source_documents_ = false;
};

/// Port of PdfSplitter: splits a document into documents of N pages each.
class PdfSplitter {
 public:
  explicit PdfSplitter(PdfDocument& pdf_document)
      : pdf_document_(pdf_document) {}
  /// Returns the bytes of each part (the Java version calls a factory for the
  /// next document).
  std::vector<Bytes> SplitByPageCount(int page_count);
  /// Extracts the given 1-based page ranges [from, to] into a new document.
  Bytes ExtractPageRange(int from_page, int to_page);

 private:
  PdfDocument& pdf_document_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_NAVIGATION_H_
