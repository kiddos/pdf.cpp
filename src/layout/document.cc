#include "pdfcpp/layout/document.h"

#include <cmath>

#include "pdfcpp/io/font/pdf_encodings.h"

namespace pdfcpp {
namespace layout {

// ============================================================ RootElementBase
std::any RootElementBase::GetDefaultPropertyAny(int property) const {
  switch (property) {
    case property::kFont:
      return GetDefaultFont();
    case property::kFontSize:
      return UnitValue::CreatePointValue(12);
    case property::kTextRenderingMode:
      return 0;
    case property::kTextRise:
      return 0.f;
    case property::kSpacingRatio:
      return 0.75f;
    case property::kFontKerning:
      return false;
    case property::kHorizontalScaling:
      return 1.f;
    default:
      return std::any();
  }
}

std::shared_ptr<kernel::PdfFont> RootElementBase::GetDefaultFont() const {
  if (!default_font_)
    default_font_ = kernel::PdfFontFactory::CreateFont(
        io::standard_fonts::kHelvetica, io::PdfEncodings::kWinansi);
  return default_font_;
}

RootElementBase& RootElementBase::Add(IElementPtr element) {
  IRendererPtr renderer = element->CreateRendererSubTree();
  GetRenderer().AddChild(renderer);
  return *this;
}

RootElementBase& RootElementBase::ShowTextAligned(const std::string& text,
                                                  float x, float y,
                                                  TextAlignment text_align,
                                                  VerticalAlignment vert_align,
                                                  float rad_angle) {
  auto p = std::make_shared<Paragraph>(text);
  return ShowTextAligned(p, x, y,
                         GetRenderer().GetCurrentArea().GetPageNumber(),
                         text_align, vert_align, rad_angle);
}

RootElementBase& RootElementBase::ShowTextAligned(
    std::shared_ptr<Paragraph> p, float x, float y, int page_number,
    TextAlignment text_align, VerticalAlignment vert_align, float rad_angle) {
  p->SetMargin(0).SetMultipliedLeading(1);
  auto renderer =
      std::dynamic_pointer_cast<ParagraphRenderer>(p->CreateRendererSubTree());
  renderer->SetParent(&GetRenderer());
  LayoutArea area(page_number, Rectangle(0, 0, 1e5f, 1e5f));
  LayoutContext ctx(area);
  LayoutResult result = renderer->Layout(ctx);
  if (result.GetStatus() != LayoutResult::kFull || renderer->GetLines().empty())
    return *this;
  const LineInfo& line = renderer->GetLines().front();
  float width = 0;
  for (const auto& l : renderer->GetLines()) width = std::max(width, l.width);
  float dx = text_align == TextAlignment::kCenter
                 ? -width / 2
                 : (text_align == TextAlignment::kRight ? -width : 0);
  float total_height = renderer->GetOccupiedArea().GetBBox().GetHeight();
  float baseline_from_top = line.baseline_offset;
  float dy;
  switch (vert_align) {
    case VerticalAlignment::kTop:
      dy = -baseline_from_top;
      break;
    case VerticalAlignment::kMiddle:
      dy = total_height / 2 - baseline_from_top;
      break;
    default:
      dy = 0;
      break;
  }
  // move so that the first baseline is at (x + dx, y + dy) before rotation
  auto first = renderer->GetFirstYLineRecursively();
  float cur_baseline = first ? *first
                             : renderer->GetOccupiedArea().GetBBox().GetTop() -
                                   baseline_from_top;
  renderer->Move(x + dx - renderer->GetOccupiedArea().GetBBox().GetX(),
                 y + dy - cur_baseline);
  kernel::PdfCanvas* canvas = &GetRenderer().GetPageCanvas(page_number);
  canvas->SaveState();
  if (rad_angle != 0)
    canvas->ConcatMatrix(
        kernel::AffineTransform::GetRotateInstance(rad_angle, x, y));
  DrawContext dc(pdf_document_, *canvas);
  renderer->Draw(dc);
  canvas->RestoreState();
  return *this;
}

// ============================================================ RootRenderer
RootRenderer::RootRenderer(RootElementBase* root)
    : AbstractRenderer(static_cast<IPropertyContainer*>(root)), root_(root) {}

void RootRenderer::DrawRenderer(const IRendererPtr& renderer) {
  int page = renderer->GetOccupiedArea().GetPageNumber();
  kernel::PdfCanvas& canvas = GetCanvas(page);
  DrawContext dc(root_->GetPdfDocument(), canvas);
  renderer->Draw(dc);
}

void RootRenderer::AddFixedChild(IRendererPtr renderer) {
  auto* ar = dynamic_cast<AbstractRenderer*>(renderer.get());
  float left = ar->GetPropertyAsFloat(property::kLeft).value_or(0);
  float bottom = ar->GetPropertyAsFloat(property::kBottom).value_or(0);
  float width = 1e5f;
  auto declared_width = ar->GetPropertyAsUnitValue(property::kWidth);
  if (declared_width)
    width = declared_width->Resolve(current_area_.GetBBox().GetWidth());
  int page = ar->GetPropertyAsInteger(property::kPageNumber)
                 .value_or(current_area_.GetPageNumber());
  // the element's own width property already constrains it; give generous room
  LayoutArea area(page,
                  Rectangle(left, bottom, std::max(width, 1.f) + 1e4f, 1e5f));
  area.GetBBox().SetWidth(width < 1e5f ? width : 1e5f);
  // The kWidth is already applied through the area. Deleting the renderer's own
  // property is not enough (the model element - where SetFixedPosition stores
  // it - would still be consulted and e.g. 50% applied a second time), so
  // shadow it with "the whole area".
  if (declared_width)
    ar->SetPropertyAny(property::kWidth, UnitValue::CreatePercentValue(100));
  LayoutContext ctx(area);
  renderer->SetPropertyAny(property::kForcedPlacement, true);
  LayoutResult result = renderer->Layout(ctx);
  if (result.GetStatus() == LayoutResult::kNothing) return;
  IRendererPtr to_draw = result.GetStatus() == LayoutResult::kPartial
                             ? result.GetSplitRenderer()
                             : renderer;
  float occupied_bottom = to_draw->GetOccupiedArea().GetBBox().GetY();
  to_draw->Move(0, bottom - occupied_bottom);
  to_draw->GetOccupiedArea() =
      LayoutArea(page, to_draw->GetOccupiedArea().GetBBox());
  DrawRenderer(to_draw);
}

void RootRenderer::AddChild(IRendererPtr renderer) {
  if (!renderer) return;
  renderer->SetParent(this);
  auto* ar = dynamic_cast<AbstractRenderer*>(renderer.get());
  if (ar && ar->IsFixedLayout()) {
    if (!has_area_) NewArea();
    AddFixedChild(renderer);
    return;
  }
  if (auto* area_break = dynamic_cast<AreaBreakRenderer*>(renderer.get())) {
    auto* element = dynamic_cast<AreaBreak*>(area_break->GetModelElement());
    if (!has_area_) NewArea();
    NewArea(element ? element->GetPageSize() : std::nullopt);
    return;
  }
  if (!has_area_) NewArea();
  IRendererPtr current = renderer;
  int guard = 0;
  while (current && guard++ < 100000) {
    LayoutContext ctx(current_area_);
    LayoutResult result = current->Layout(ctx);
    switch (result.GetStatus()) {
      case LayoutResult::kFull: {
        current->GetOccupiedArea() =
            LayoutArea(current_area_.GetPageNumber(),
                       current->GetOccupiedArea().GetBBox());
        DrawRenderer(current);
        float new_top = result.GetOccupiedArea().GetBBox().GetY();
        Rectangle& bbox = current_area_.GetBBox();
        bbox.SetHeight(std::max(0.f, new_top - bbox.GetY()));
        current_area_.SetEmptyArea(false);
        current = nullptr;
        break;
      }
      case LayoutResult::kPartial: {
        IRendererPtr split = result.GetSplitRenderer();
        if (split) {
          split->GetOccupiedArea() =
              LayoutArea(current_area_.GetPageNumber(),
                         split->GetOccupiedArea().GetBBox());
          DrawRenderer(split);
        }
        NewArea();
        current = result.GetOverflowRenderer();
        if (current) current->SetParent(this);
        break;
      }
      default: {
        if (current_area_.IsEmptyArea()) {
          if (current->HasOwnProperty(property::kForcedPlacement)) {
            // cannot be placed at all: drop it (iText logs the same situation)
            current = nullptr;
          } else {
            current->SetPropertyAny(property::kForcedPlacement, true);
          }
        } else {
          NewArea();
        }
        break;
      }
    }
  }
}

// ============================================================ DocumentRenderer
DocumentRenderer::DocumentRenderer(Document* document)
    : RootRenderer(document), document_(document) {}

void DocumentRenderer::NewArea(
    const std::optional<kernel::PageSize>& page_size) {
  kernel::PdfDocument* pdf = document_->GetPdfDocument();
  ++current_page_number_;
  kernel::PageSize size = page_size ? *page_size : document_->GetPageSize();
  if (current_page_number_ > pdf->GetNumberOfPages()) {
    pdf->AddNewPage(size);
  } else {
    size = kernel::PageSize(pdf->GetPage(current_page_number_)->GetPageSize());
  }
  current_area_ =
      LayoutArea(current_page_number_, document_->GetPageEffectiveArea(size));
  current_area_.SetEmptyArea(true);
  has_area_ = true;
}

kernel::PdfCanvas& DocumentRenderer::GetCanvas(int page_number) {
  auto it = canvases_.find(page_number);
  if (it != canvases_.end()) return *it->second;
  kernel::PdfDocument* pdf = document_->GetPdfDocument();
  while (pdf->GetNumberOfPages() < page_number)
    pdf->AddNewPage(document_->GetPageSize());
  auto canvas = std::make_unique<kernel::PdfCanvas>(pdf->GetPage(page_number));
  auto& ref = *canvas;
  canvases_[page_number] = std::move(canvas);
  return ref;
}

void DocumentRenderer::AddFixedChild(IRendererPtr renderer) {
  RootRenderer::AddFixedChild(std::move(renderer));
}

void DocumentRenderer::Flush() {}

// ============================================================ Document
Document::Document(kernel::PdfDocument& pdf_doc, bool immediate_flush)
    : Document(pdf_doc, pdf_doc.GetDefaultPageSize(), immediate_flush) {}

Document::Document(kernel::PdfDocument& pdf_doc,
                   const kernel::PageSize& page_size, bool immediate_flush)
    : page_size_(page_size), immediate_flush_(immediate_flush) {
  pdf_document_ = &pdf_doc;
  pdf_doc.SetDefaultPageSize(page_size);
}

Document::~Document() {
  try {
    if (!closed_) Close();
  } catch (...) {
  }
}

RootRenderer& Document::GetRenderer() {
  if (!renderer_) renderer_ = std::make_unique<DocumentRenderer>(this);
  return *renderer_;
}

Rectangle Document::GetPageEffectiveArea(
    const kernel::PageSize& page_size) const {
  return Rectangle(page_size.GetLeft() + left_margin_,
                   page_size.GetBottom() + bottom_margin_,
                   page_size.GetWidth() - left_margin_ - right_margin_,
                   page_size.GetHeight() - bottom_margin_ - top_margin_);
}

void Document::Close() {
  if (closed_) return;
  closed_ = true;
  if (renderer_) renderer_->Close();
  renderer_.reset();
  pdf_document_->Close();
}

// ============================================================ Canvas
namespace {
class CanvasRenderer : public RootRenderer {
 public:
  CanvasRenderer(Canvas* canvas) : RootRenderer(canvas), canvas_(canvas) {}
  void NewArea(const std::optional<kernel::PageSize>&) override {
    if (has_area_) {
      // a Canvas has a single area: further content overflows and is dropped
      current_area_ =
          LayoutArea(1, Rectangle(canvas_->GetRootArea().GetX(),
                                  canvas_->GetRootArea().GetY(),
                                  canvas_->GetRootArea().GetWidth(), 0));
      current_area_.SetEmptyArea(false);
      overflowed_ = true;
      return;
    }
    current_area_ = LayoutArea(1, canvas_->GetRootArea());
    has_area_ = true;
  }

 protected:
  kernel::PdfCanvas& GetCanvas(int) override { return canvas_->GetPdfCanvas(); }

 private:
  Canvas* canvas_;
  bool overflowed_ = false;
};
}  // namespace

Canvas::Canvas(kernel::PdfCanvas& pdf_canvas, const Rectangle& root_area)
    : pdf_canvas_(&pdf_canvas), root_area_(root_area) {
  pdf_document_ = pdf_canvas.GetDocument();
}

Canvas::Canvas(kernel::PdfPage* page, const Rectangle& root_area)
    : owned_canvas_(std::make_unique<kernel::PdfCanvas>(page)),
      pdf_canvas_(owned_canvas_.get()),
      root_area_(root_area) {
  pdf_document_ = page->GetDocument();
}

Canvas::~Canvas() = default;

RootRenderer& Canvas::GetRenderer() {
  if (!renderer_) renderer_ = std::make_unique<CanvasRenderer>(this);
  return *renderer_;
}

}  // namespace layout
}  // namespace pdfcpp
