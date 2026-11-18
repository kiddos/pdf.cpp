#pragma once

#include <map>
#include <memory>

#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/layout/Renderers.h"

namespace pdfcpp {
namespace layout {

class RootElementBase;

/// Port of RootRenderer / DocumentRenderer / CanvasRenderer: places top-level renderers into areas.
class RootRenderer : public AbstractRenderer {
public:
    explicit RootRenderer(RootElementBase* root);
    void addChild(IRendererPtr renderer) override;
    LayoutResult layout(LayoutContext&) override { throw std::logic_error("RootRenderer cannot be laid out"); }
    IRendererPtr getNextRenderer() override { return nullptr; }
    /// Forces a new area (page).
    virtual void newArea(const std::optional<kernel::PageSize>& pageSize = std::nullopt) = 0;
    LayoutArea& getCurrentArea() { return currentArea_; }
    kernel::PdfCanvas& getPageCanvas(int pageNumber) { return getCanvas(pageNumber); }
    virtual void flush() {}
    virtual void close() { flush(); }

protected:
    virtual kernel::PdfCanvas& getCanvas(int pageNumber) = 0;
    virtual void addFixedChild(IRendererPtr renderer);
    void drawRenderer(const IRendererPtr& renderer);
    RootElementBase* root_;
    LayoutArea currentArea_;
    bool hasArea_ = false;
};

/// Port of DocumentRenderer.
class DocumentRenderer : public RootRenderer {
public:
    explicit DocumentRenderer(class Document* document);
    void newArea(const std::optional<kernel::PageSize>& pageSize = std::nullopt) override;
    void flush() override;

protected:
    kernel::PdfCanvas& getCanvas(int pageNumber) override;
    void addFixedChild(IRendererPtr renderer) override;

private:
    class Document* document_;
    std::map<int, std::unique_ptr<kernel::PdfCanvas>> canvases_;
    int currentPageNumber_ = 0;
};

/// Port of RootElement: shared base of Document and Canvas.
class RootElementBase : public ElementPropertyContainer<RootElementBase> {
public:
    std::any getDefaultPropertyAny(int property) const override;
    kernel::PdfDocument* getPdfDocument() const { return pdfDocument_; }
    std::shared_ptr<kernel::PdfFont> getDefaultFont() const;
    virtual RootRenderer& getRenderer() = 0;

    /// Adds an element and lays it out immediately.
    RootElementBase& add(IElementPtr element);
    /// Draws a single line of text aligned at (x, y) (port of showTextAligned).
    RootElementBase& showTextAligned(const std::string& text, float x, float y, TextAlignment textAlign,
                                     VerticalAlignment vertAlign = VerticalAlignment::BOTTOM, float radAngle = 0);
    RootElementBase& showTextAligned(std::shared_ptr<Paragraph> p, float x, float y, int pageNumber, TextAlignment textAlign,
                                     VerticalAlignment vertAlign, float radAngle);

protected:
    kernel::PdfDocument* pdfDocument_ = nullptr;
    mutable std::shared_ptr<kernel::PdfFont> defaultFont_;
};

/// Port of com.itextpdf.layout.Document.
class Document : public RootElementBase {
public:
    explicit Document(kernel::PdfDocument& pdfDoc, bool immediateFlush = true);
    Document(kernel::PdfDocument& pdfDoc, const kernel::PageSize& pageSize, bool immediateFlush = true);
    ~Document() override;

    Document& add(IElementPtr element) {
        RootElementBase::add(std::move(element));
        return *this;
    }
    template <typename E>
    Document& add(std::shared_ptr<E> element) {
        return add(std::static_pointer_cast<IElement>(element));
    }
    Document& add(IElement& element) { return add(element.shared()); }
    void close();
    RootRenderer& getRenderer() override;
    void setRenderer(std::unique_ptr<RootRenderer> renderer) { renderer_ = std::move(renderer); }
    void flush() { getRenderer().flush(); }
    void relayout() {}
    float getLeftMargin() const { return leftMargin_; }
    void setLeftMargin(float v) { leftMargin_ = v; }
    float getRightMargin() const { return rightMargin_; }
    void setRightMargin(float v) { rightMargin_ = v; }
    float getTopMargin() const { return topMargin_; }
    void setTopMargin(float v) { topMargin_ = v; }
    float getBottomMargin() const { return bottomMargin_; }
    void setBottomMargin(float v) { bottomMargin_ = v; }
    void setMargins(float topMargin, float rightMargin, float bottomMargin, float leftMargin) {
        topMargin_ = topMargin;
        rightMargin_ = rightMargin;
        bottomMargin_ = bottomMargin;
        leftMargin_ = leftMargin;
    }
    /// Page area available for content on the given page (page box minus margins).
    Rectangle getPageEffectiveArea(const kernel::PageSize& pageSize) const;
    const kernel::PageSize& getPageSize() const { return pageSize_; }
    bool isImmediateFlush() const { return immediateFlush_; }

private:
    kernel::PageSize pageSize_;
    float leftMargin_ = 36, rightMargin_ = 36, topMargin_ = 36, bottomMargin_ = 36;
    bool immediateFlush_;
    std::unique_ptr<RootRenderer> renderer_;
    bool closed_ = false;
};

/// Port of com.itextpdf.layout.Canvas: lays out elements inside a rectangle of a PdfCanvas.
class Canvas : public RootElementBase {
public:
    Canvas(kernel::PdfCanvas& pdfCanvas, const Rectangle& rootArea);
    Canvas(kernel::PdfPage* page, const Rectangle& rootArea);
    ~Canvas() override;
    Canvas& add(IElementPtr element) {
        RootElementBase::add(std::move(element));
        return *this;
    }
    template <typename E>
    Canvas& add(std::shared_ptr<E> element) {
        return add(std::static_pointer_cast<IElement>(element));
    }
    Canvas& add(IElement& element) { return add(element.shared()); }
    RootRenderer& getRenderer() override;
    kernel::PdfCanvas& getPdfCanvas() { return *pdfCanvas_; }
    const Rectangle& getRootArea() const { return rootArea_; }
    void close() {}

private:
    std::unique_ptr<kernel::PdfCanvas> ownedCanvas_;
    kernel::PdfCanvas* pdfCanvas_;
    Rectangle rootArea_;
    std::unique_ptr<RootRenderer> renderer_;
};

}  // namespace layout
}  // namespace pdfcpp
