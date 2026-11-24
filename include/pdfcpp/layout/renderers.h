#ifndef PDFCPP_LAYOUT_RENDERERS_H_
#define PDFCPP_LAYOUT_RENDERERS_H_

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "pdfcpp/layout/elements.h"

namespace pdfcpp {
namespace layout {

using kernel::Rectangle;

/// Port of LayoutArea.
class LayoutArea {
 public:
  LayoutArea() = default;
  LayoutArea(int page_number, const Rectangle& b_box)
      : page_number_(page_number), b_box_(b_box) {}
  int GetPageNumber() const { return page_number_; }
  Rectangle& GetBBox() { return b_box_; }
  const Rectangle& GetBBox() const { return b_box_; }
  void SetBBox(const Rectangle& r) { b_box_ = r; }
  bool IsEmptyArea() const { return empty_area_; }
  void SetEmptyArea(bool v) { empty_area_ = v; }

 private:
  int page_number_ = 1;
  Rectangle b_box_;
  bool empty_area_ = true;
};

/// Port of LayoutContext.
class LayoutContext {
 public:
  explicit LayoutContext(const LayoutArea& area) : area_(area) {}
  LayoutArea& GetArea() { return area_; }
  const LayoutArea& GetArea() const { return area_; }

 private:
  LayoutArea area_;
};

/// Port of LayoutResult.
class LayoutResult {
 public:
  static constexpr int kFull = 1;
  static constexpr int kPartial = 2;
  static constexpr int kNothing = 3;
  LayoutResult(int status, const LayoutArea& occupied_area,
               IRendererPtr split_renderer, IRendererPtr overflow_renderer,
               IRendererPtr cause_of_nothing = nullptr)
      : status_(status),
        occupied_area_(occupied_area),
        split_renderer_(std::move(split_renderer)),
        overflow_renderer_(std::move(overflow_renderer)),
        cause_of_nothing_(std::move(cause_of_nothing)) {}
  int GetStatus() const { return status_; }
  const LayoutArea& GetOccupiedArea() const { return occupied_area_; }
  const IRendererPtr& GetSplitRenderer() const { return split_renderer_; }
  const IRendererPtr& GetOverflowRenderer() const { return overflow_renderer_; }
  const IRendererPtr& GetCauseOfNothing() const { return cause_of_nothing_; }

 private:
  int status_;
  LayoutArea occupied_area_;
  IRendererPtr split_renderer_, overflow_renderer_, cause_of_nothing_;
};

/// Port of DrawContext.
class DrawContext {
 public:
  DrawContext(kernel::PdfDocument* document, kernel::PdfCanvas& canvas)
      : document_(document), canvas_(canvas) {}
  kernel::PdfDocument* GetDocument() const { return document_; }
  kernel::PdfCanvas& GetCanvas() const { return canvas_; }

 private:
  kernel::PdfDocument* document_;
  kernel::PdfCanvas& canvas_;
};

/// Port of IRenderer.
class IRenderer : public virtual IPropertyContainer,
                  public std::enable_shared_from_this<IRenderer> {
 public:
  virtual void AddChild(IRendererPtr renderer) = 0;
  virtual LayoutResult Layout(LayoutContext& layout_context) = 0;
  virtual void Draw(DrawContext& draw_context) = 0;
  virtual LayoutArea& GetOccupiedArea() = 0;
  virtual IRenderer* GetParent() const = 0;
  virtual IRenderer* SetParent(IRenderer* parent) = 0;
  virtual const std::vector<IRendererPtr>& GetChildRenderers() const = 0;
  virtual IPropertyContainer* GetModelElement() const = 0;
  virtual void Move(float dx, float dy) = 0;
  /// Creates a fresh renderer of the same kind for the same element (used when
  /// splitting).
  virtual IRendererPtr GetNextRenderer() = 0;
  virtual bool IsFlushed() const { return false; }
};

/// Port of AbstractRenderer: property resolution,
/// margins/borders/paddings/background.
class AbstractRenderer : public IRenderer {
 public:
  AbstractRenderer() = default;
  explicit AbstractRenderer(std::shared_ptr<IElement> model_element)
      : model_element_owner_(std::move(model_element)) {
    model_element_ = model_element_owner_.get();
  }
  explicit AbstractRenderer(IPropertyContainer* model_element)
      : model_element_(model_element) {}

  // IPropertyContainer
  bool HasProperty(int property) const override;
  bool HasOwnProperty(int property) const override {
    return properties_.count(property) > 0;
  }
  const std::any* GetPropertyAny(int property) const override;
  const std::any* GetOwnPropertyAny(int property) const override {
    auto it = properties_.find(property);
    return it == properties_.end() ? nullptr : &it->second;
  }
  std::any GetDefaultPropertyAny(int property) const override;
  void SetPropertyAny(int property, std::any value) override {
    properties_[property] = std::move(value);
  }
  void DeleteOwnProperty(int property) override { properties_.erase(property); }

  // IRenderer
  void AddChild(IRendererPtr renderer) override;
  void Draw(DrawContext& draw_context) override;
  LayoutArea& GetOccupiedArea() override { return occupied_area_; }
  IRenderer* GetParent() const override { return parent_; }
  IRenderer* SetParent(IRenderer* parent) override {
    parent_ = parent;
    return this;
  }
  const std::vector<IRendererPtr>& GetChildRenderers() const override {
    return child_renderers_;
  }
  std::vector<IRendererPtr>& ChildRenderers() { return child_renderers_; }
  IPropertyContainer* GetModelElement() const override {
    return model_element_;
  }
  const std::shared_ptr<IElement>& GetModelElementOwner() const {
    return model_element_owner_;
  }
  void Move(float dx, float dy) override;

  // property helpers
  std::shared_ptr<kernel::PdfFont> GetPropertyAsFont(int property) const;
  ColorPtr GetPropertyAsColor(int property) const;
  std::optional<float> GetPropertyAsFloat(int property) const;
  std::optional<UnitValue> GetPropertyAsUnitValue(int property) const;
  bool GetPropertyAsBoolean(int property) const;
  std::optional<int> GetPropertyAsInteger(int property) const;
  float ResolveFontSize() const;

  std::array<BorderPtr, 4> GetBorders() const;  // top, right, bottom, left
  std::array<float, 4> GetMargins(float base = 0) const;
  std::array<float, 4> GetPaddings(float base = 0) const;
  Rectangle& ApplyMargins(Rectangle& rect, bool reverse) const;
  Rectangle& ApplyBorderBox(Rectangle& rect, bool reverse) const;
  Rectangle& ApplyPaddings(Rectangle& rect, bool reverse) const;
  Rectangle GetInnerAreaBBox() const;
  std::optional<float> RetrieveWidth(float parent_box_width) const;
  std::optional<float> RetrieveHeight() const;
  std::optional<float> RetrieveMinHeight() const;
  std::optional<float> RetrieveMaxHeight() const;
  void DrawBackground(DrawContext& draw_context);
  void DrawBorder(DrawContext& draw_context);
  virtual void DrawChildren(DrawContext& draw_context);
  bool IsFixedLayout() const;
  bool IsPositioned() const { return IsFixedLayout(); }

 protected:
  template <typename R>
  std::shared_ptr<R> CreateSplitLike() const {
    auto r = std::make_shared<R>(model_element_owner_);
    r->model_element_ = model_element_;
    r->properties_ = properties_;
    r->parent_ = parent_;
    return r;
  }

  std::map<int, std::any> properties_;
  mutable std::map<int, std::any> default_cache_;
  std::shared_ptr<IElement> model_element_owner_;
  IPropertyContainer* model_element_ = nullptr;
  IRenderer* parent_ = nullptr;
  std::vector<IRendererPtr> child_renderers_;
  LayoutArea occupied_area_;
  template <typename>
  friend class RendererFactory;
  friend class BlockRenderer;
  friend class TableRenderer;
};

/// Port of BlockRenderer (+ DivRenderer/CellRenderer/ListItemRenderer
/// behaviour).
class BlockRenderer : public AbstractRenderer {
 public:
  using AbstractRenderer::AbstractRenderer;
  LayoutResult Layout(LayoutContext& layout_context) override;
  IRendererPtr GetNextRenderer() override;
  void Draw(DrawContext& draw_context) override;
  /// Baseline of the first line of text inside this block (absolute y), if any.
  virtual std::optional<float> GetFirstYLineRecursively() const;

 protected:
  virtual std::shared_ptr<BlockRenderer> CreateSplitRenderer() const;
  virtual std::shared_ptr<BlockRenderer> CreateOverflowRenderer() const;
};

/// One placed text fragment inside a line.
struct LineItem {
  std::shared_ptr<AbstractRenderer> source;  // TextRenderer or ImageRenderer
  io::GlyphLine glyphs;
  std::string text;
  float x = 0;  // offset from line start
  float width = 0;
  float ascent = 0, descent = 0;
  bool is_image = false;
  bool is_space = false;
};

/// A laid-out line of a paragraph (port of LineRenderer).
struct LineInfo {
  std::vector<LineItem> items;
  float width = 0;
  float ascent = 0, descent = 0;
  float height = 0;
  float baseline_offset = 0;  // from top of line box
  float left_indent = 0;
  float available_width = 0;
  bool last_line = false;
};

/// Port of TextRenderer.
class TextRenderer : public AbstractRenderer {
 public:
  explicit TextRenderer(std::shared_ptr<IElement> text_element);
  TextRenderer(std::shared_ptr<IElement> text_element, std::string text);
  LayoutResult Layout(LayoutContext& layout_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<TextRenderer>(model_element_owner_);
  }
  void Draw(DrawContext&) override {}
  const std::string& GetText() const { return text_; }
  float GetAscent() const;
  float GetDescent() const;
  /// Draws the given glyphs at (x, baseline y) using this renderer's text
  /// properties.
  void DrawGlyphs(DrawContext& draw_context, const io::GlyphLine& glyphs,
                  float x, float y, float width);

 private:
  std::string text_;
};

/// Port of ImageRenderer.
class ImageRenderer : public AbstractRenderer {
 public:
  explicit ImageRenderer(std::shared_ptr<IElement> image);
  LayoutResult Layout(LayoutContext& layout_context) override;
  void Draw(DrawContext& draw_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<ImageRenderer>(model_element_owner_);
  }
  float GetImageWidth() const { return image_width_; }
  float GetImageHeight() const { return image_height_; }
  /// Computes width/height for the given available width/height (applies
  /// autoscale/scale/width props).
  void CalculateImageSize(float available_width, float available_height);

 private:
  float image_width_ = 0, image_height_ = 0;
};

/// Port of ParagraphRenderer (with LineRenderer folded in).
class ParagraphRenderer : public BlockRenderer {
 public:
  explicit ParagraphRenderer(std::shared_ptr<IElement> paragraph);
  LayoutResult Layout(LayoutContext& layout_context) override;
  void DrawChildren(DrawContext& draw_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<ParagraphRenderer>(model_element_owner_);
  }
  std::optional<float> GetFirstYLineRecursively() const override;
  std::any GetDefaultPropertyAny(int property) const override;
  void Move(float dx, float dy) override {
    BlockRenderer::Move(dx, dy);
    content_box_.MoveRight(dx);
    content_box_.MoveUp(dy);
  }
  const std::vector<LineInfo>& GetLines() const { return lines_; }

 private:
  struct Piece {
    std::shared_ptr<AbstractRenderer> source;
    io::GlyphLine glyphs;
    std::string text;
    float width;
    float ascent, descent;
    bool is_space, is_newline, is_image;
    bool break_allowed_before;
  };
  void BuildPieces();
  /// Split and overflow parts share the child (text/image) renderers; their
  /// parent pointer must refer to the part currently being laid out or drawn -
  /// the renderer they were created for may be gone (e.g. inside table cells).
  void AdoptChildren();
  std::vector<Piece> pieces_;
  size_t start_piece_ = 0;
  bool pieces_built_ = false;
  bool continuation_ = false;  // overflow part: first line indent not applied
  std::vector<LineInfo> lines_;
  Rectangle content_box_;
};

/// Port of TableRenderer (separated borders model).
class TableRenderer : public AbstractRenderer {
 public:
  explicit TableRenderer(std::shared_ptr<IElement> table);
  LayoutResult Layout(LayoutContext& layout_context) override;
  void Draw(DrawContext& draw_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<TableRenderer>(model_element_owner_);
  }
  std::optional<float> GetFirstYLineRecursively() const;
  void Move(float dx, float dy) override {
    AbstractRenderer::Move(dx, dy);
    for (auto& pc : placed_) {
      pc.renderer->Move(dx, dy);
      pc.box.MoveRight(dx);
      pc.box.MoveUp(dy);
      pc.renderer->GetOccupiedArea().SetBBox(pc.box);
    }
  }

 private:
  struct PlacedCell {
    std::shared_ptr<BlockRenderer> renderer;
    Rectangle box;
  };
  std::vector<float> ComputeColumnWidths(float available_width) const;
  Table& GetTable() const { return *dynamic_cast<Table*>(model_element_); }
  int start_row_ = 0;
  bool is_overflow_part_ = false;
  std::vector<PlacedCell> placed_;
  std::map<std::pair<int, int>, std::shared_ptr<BlockRenderer>>
      pending_cell_overflow_;  // (row, col) -> remaining content
};

/// Port of ListRenderer.
class ListRenderer : public BlockRenderer {
 public:
  explicit ListRenderer(std::shared_ptr<IElement> list);
  LayoutResult Layout(LayoutContext& layout_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<ListRenderer>(model_element_owner_);
  }

 private:
  std::string MakeSymbol(int index) const;
  bool symbols_assigned_ = false;
};

/// Port of ListItemRenderer.
class ListItemRenderer : public BlockRenderer {
 public:
  explicit ListItemRenderer(std::shared_ptr<IElement> item);
  void SetSymbol(std::shared_ptr<TextRenderer> symbol,
                 float symbol_area_width) {
    symbol_ = std::move(symbol);
    symbol_area_width_ = symbol_area_width;
  }
  LayoutResult Layout(LayoutContext& layout_context) override;
  void Draw(DrawContext& draw_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<ListItemRenderer>(model_element_owner_);
  }

 protected:
  std::shared_ptr<BlockRenderer> CreateSplitRenderer() const override;
  std::shared_ptr<BlockRenderer> CreateOverflowRenderer() const override;

 private:
  std::shared_ptr<TextRenderer> symbol_;
  float symbol_area_width_ = 0;
  bool symbol_drawn_ = false;
};

/// Port of AreaBreakRenderer.
class AreaBreakRenderer : public AbstractRenderer {
 public:
  explicit AreaBreakRenderer(std::shared_ptr<IElement> area_break)
      : AbstractRenderer(std::move(area_break)) {}
  LayoutResult Layout(LayoutContext& layout_context) override {
    return LayoutResult(LayoutResult::kNothing, layout_context.GetArea(),
                        nullptr, nullptr);
  }
  IRendererPtr GetNextRenderer() override { return nullptr; }
};

/// Port of LineSeparatorRenderer.
class LineSeparatorRenderer : public BlockRenderer {
 public:
  explicit LineSeparatorRenderer(std::shared_ptr<IElement> sep)
      : BlockRenderer(std::move(sep)) {}
  LayoutResult Layout(LayoutContext& layout_context) override;
  void DrawChildren(DrawContext& draw_context) override;
  IRendererPtr GetNextRenderer() override {
    return std::make_shared<LineSeparatorRenderer>(model_element_owner_);
  }
};

}  // namespace layout
}  // namespace pdfcpp

#endif  // PDFCPP_LAYOUT_RENDERERS_H_
