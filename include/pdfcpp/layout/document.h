#ifndef PDFCPP_LAYOUT_DOCUMENT_H_
#define PDFCPP_LAYOUT_DOCUMENT_H_

#include <map>
#include <memory>

#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/layout/renderers.h"

namespace pdfcpp {
namespace layout {

class RootElementBase;

/// Port of RootRenderer / DocumentRenderer / CanvasRenderer: places top-level
/// renderers into areas.
class RootRenderer : public AbstractRenderer {
 public:
  explicit RootRenderer(RootElementBase* root);
  void AddChild(IRendererPtr renderer) override;
  LayoutResult Layout(LayoutContext&) override {
    throw std::logic_error("RootRenderer cannot be laid out");
  }
  IRendererPtr GetNextRenderer() override { return nullptr; }
  /// Forces a new area (page).
  virtual void NewArea(
      const std::optional<kernel::PageSize>& page_size = std::nullopt) = 0;
  LayoutArea& GetCurrentArea() { return current_area_; }
  kernel::PdfCanvas& GetPageCanvas(int page_number) {
    return GetCanvas(page_number);
  }
  virtual void Flush() {}
  virtual void Close() { Flush(); }

 protected:
  virtual kernel::PdfCanvas& GetCanvas(int page_number) = 0;
  virtual void AddFixedChild(IRendererPtr renderer);
  void DrawRenderer(const IRendererPtr& renderer);
  RootElementBase* root_;
  LayoutArea current_area_;
  bool has_area_ = false;
};

/// Port of DocumentRenderer.
class DocumentRenderer : public RootRenderer {
 public:
  explicit DocumentRenderer(class Document* document);
  void NewArea(
      const std::optional<kernel::PageSize>& page_size = std::nullopt) override;
  void Flush() override;

 protected:
  kernel::PdfCanvas& GetCanvas(int page_number) override;
  void AddFixedChild(IRendererPtr renderer) override;

 private:
  class Document* document_;
  std::map<int, std::unique_ptr<kernel::PdfCanvas>> canvases_;
  int current_page_number_ = 0;
};

/// Port of RootElement: shared base of Document and Canvas.
class RootElementBase : public ElementPropertyContainer<RootElementBase> {
 public:
  std::any GetDefaultPropertyAny(int property) const override;
  kernel::PdfDocument* GetPdfDocument() const { return pdf_document_; }
  std::shared_ptr<kernel::PdfFont> GetDefaultFont() const;
  virtual RootRenderer& GetRenderer() = 0;

  /// Adds an element and lays it out immediately.
  RootElementBase& Add(IElementPtr element);
  /// Draws a single line of text aligned at (x, y) (port of showTextAligned).
  RootElementBase& ShowTextAligned(
      const std::string& text, float x, float y, TextAlignment text_align,
      VerticalAlignment vert_align = VerticalAlignment::kBottom,
      float rad_angle = 0);
  RootElementBase& ShowTextAligned(std::shared_ptr<Paragraph> p, float x,
                                   float y, int page_number,
                                   TextAlignment text_align,
                                   VerticalAlignment vert_align,
                                   float rad_angle);

 protected:
  kernel::PdfDocument* pdf_document_ = nullptr;
  mutable std::shared_ptr<kernel::PdfFont> default_font_;
};

/// Port of com.itextpdf.layout.Document.
class Document : public RootElementBase {
 public:
  explicit Document(kernel::PdfDocument& pdf_doc, bool immediate_flush = true);
  Document(kernel::PdfDocument& pdf_doc, const kernel::PageSize& page_size,
           bool immediate_flush = true);
  ~Document() override;

  Document& Add(IElementPtr element) {
    RootElementBase::Add(std::move(element));
    return *this;
  }
  template <typename E>
  Document& Add(std::shared_ptr<E> element) {
    return Add(std::static_pointer_cast<IElement>(element));
  }
  Document& Add(IElement& element) { return Add(element.Shared()); }
  void Close();
  RootRenderer& GetRenderer() override;
  void SetRenderer(std::unique_ptr<RootRenderer> renderer) {
    renderer_ = std::move(renderer);
  }
  void Flush() { GetRenderer().Flush(); }
  void Relayout() {}
  float GetLeftMargin() const { return left_margin_; }
  void SetLeftMargin(float v) { left_margin_ = v; }
  float GetRightMargin() const { return right_margin_; }
  void SetRightMargin(float v) { right_margin_ = v; }
  float GetTopMargin() const { return top_margin_; }
  void SetTopMargin(float v) { top_margin_ = v; }
  float GetBottomMargin() const { return bottom_margin_; }
  void SetBottomMargin(float v) { bottom_margin_ = v; }
  void SetMargins(float top_margin, float right_margin, float bottom_margin,
                  float left_margin) {
    top_margin_ = top_margin;
    right_margin_ = right_margin;
    bottom_margin_ = bottom_margin;
    left_margin_ = left_margin;
  }
  /// Page area available for content on the given page (page box minus
  /// margins).
  Rectangle GetPageEffectiveArea(const kernel::PageSize& page_size) const;
  const kernel::PageSize& GetPageSize() const { return page_size_; }
  bool IsImmediateFlush() const { return immediate_flush_; }

 private:
  kernel::PageSize page_size_;
  float left_margin_ = 36, right_margin_ = 36, top_margin_ = 36,
        bottom_margin_ = 36;
  bool immediate_flush_;
  std::unique_ptr<RootRenderer> renderer_;
  bool closed_ = false;
};

/// Port of com.itextpdf.layout.Canvas: lays out elements inside a rectangle of
/// a PdfCanvas.
class Canvas : public RootElementBase {
 public:
  Canvas(kernel::PdfCanvas& pdf_canvas, const Rectangle& root_area);
  Canvas(kernel::PdfPage* page, const Rectangle& root_area);
  ~Canvas() override;
  Canvas& Add(IElementPtr element) {
    RootElementBase::Add(std::move(element));
    return *this;
  }
  template <typename E>
  Canvas& Add(std::shared_ptr<E> element) {
    return Add(std::static_pointer_cast<IElement>(element));
  }
  Canvas& Add(IElement& element) { return Add(element.Shared()); }
  RootRenderer& GetRenderer() override;
  kernel::PdfCanvas& GetPdfCanvas() { return *pdf_canvas_; }
  const Rectangle& GetRootArea() const { return root_area_; }
  void Close() {}

 private:
  std::unique_ptr<kernel::PdfCanvas> owned_canvas_;
  kernel::PdfCanvas* pdf_canvas_;
  Rectangle root_area_;
  std::unique_ptr<RootRenderer> renderer_;
};

}  // namespace layout
}  // namespace pdfcpp

#endif  // PDFCPP_LAYOUT_DOCUMENT_H_
