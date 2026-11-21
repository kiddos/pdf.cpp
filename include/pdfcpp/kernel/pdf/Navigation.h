#pragma once

#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/Color.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"

namespace pdfcpp {
namespace kernel {

/// Port of PdfDestination / PdfExplicitDestination / PdfStringDestination.
class PdfDestination : public PdfObjectWrapper<PdfObject> {
public:
    explicit PdfDestination(PdfObjectPtr pdfObject) : PdfObjectWrapper<PdfObject>(std::move(pdfObject)) {}
    static std::shared_ptr<PdfDestination> createXYZ(PdfPage* page, float left, float top, float zoom);
    static std::shared_ptr<PdfDestination> createFit(PdfPage* page);
    static std::shared_ptr<PdfDestination> createFitH(PdfPage* page, float top);
    static std::shared_ptr<PdfDestination> createFitR(PdfPage* page, const Rectangle& rect);
    /// Named destination (port of PdfStringDestination).
    static std::shared_ptr<PdfDestination> createNamed(const std::string& name);
};

/// Port of PdfAction.
class PdfAction : public PdfObjectWrapper<PdfDictionary> {
public:
    PdfAction() : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) { put(PdfName::Type, PdfName::Action); }
    explicit PdfAction(PdfDictionaryPtr dict) : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
    static std::shared_ptr<PdfAction> createURI(const std::string& uri, bool isMap = false);
    static std::shared_ptr<PdfAction> createGoTo(const std::shared_ptr<PdfDestination>& destination);
    static std::shared_ptr<PdfAction> createGoTo(const std::string& namedDestination);
    static std::shared_ptr<PdfAction> createGoToR(const std::string& filename, int pageNum, bool newWindow = false);
    static std::shared_ptr<PdfAction> createJavaScript(const std::string& javaScript);
    static std::shared_ptr<PdfAction> createNamed(const PdfNamePtr& namedAction);
    PdfAction& next(const std::shared_ptr<PdfAction>& nextAction);
    PdfAction& put(const PdfNamePtr& key, const PdfObjectPtr& value) {
        getPdfObject()->put(key, value);
        return *this;
    }
};

/// Port of PdfAnnotation (+ PdfLinkAnnotation, PdfTextAnnotation, PdfSquareAnnotation, PdfFreeTextAnnotation subsets).
class PdfAnnotation : public PdfObjectWrapper<PdfDictionary> {
public:
    static constexpr int INVISIBLE = 1, HIDDEN = 2, PRINT = 4, NO_ZOOM = 8, NO_ROTATE = 16, NO_VIEW = 32, READ_ONLY = 64,
                         LOCKED = 128, TOGGLE_NO_VIEW = 256, LOCKED_CONTENTS = 512;
    static constexpr int STYLE_SOLID = 'S', STYLE_DASHED = 'D', STYLE_BEVELED = 'B', STYLE_INSET = 'I', STYLE_UNDERLINE = 'U';

    PdfAnnotation(const PdfNamePtr& subtype, const Rectangle& rect);
    explicit PdfAnnotation(PdfDictionaryPtr dict) : PdfObjectWrapper<PdfDictionary>(std::move(dict)) {}
    /// Link annotation.
    static std::shared_ptr<PdfAnnotation> createLink(const Rectangle& rect);
    /// Sticky note.
    static std::shared_ptr<PdfAnnotation> createText(const Rectangle& rect);
    static std::shared_ptr<PdfAnnotation> createSquare(const Rectangle& rect);
    static std::shared_ptr<PdfAnnotation> createFreeText(const Rectangle& rect, const std::string& contents);
    static std::shared_ptr<PdfAnnotation> createHighlight(const Rectangle& rect, const std::vector<float>& quadPoints);

    PdfNamePtr getSubtype() const { return getPdfObject()->getAsName(PdfName::Subtype); }
    PdfStringPtr getContents() const { return getPdfObject()->getAsString(PdfName::Contents); }
    PdfAnnotation& setContents(const std::string& contents) {
        return put(PdfName::Contents, std::make_shared<PdfString>(contents, "UnicodeBig"));
    }
    int getFlags() const {
        auto f = getPdfObject()->getAsNumber(PdfName::F);
        return f ? f->intValue() : 0;
    }
    PdfAnnotation& setFlags(int flags) { return put(PdfName::F, makeNumber(flags)); }
    PdfAnnotation& setFlag(int flag) { return setFlags(getFlags() | flag); }
    PdfAnnotation& resetFlag(int flag) { return setFlags(getFlags() & ~flag); }
    bool hasFlag(int flag) const { return (getFlags() & flag) != 0; }
    PdfAnnotation& setColor(const ColorPtr& color) { return put(PdfName::C, std::make_shared<PdfArray>(color->getColorValue())); }
    PdfAnnotation& setTitle(const std::string& title) { return put(PdfName::T, std::make_shared<PdfString>(title, "UnicodeBig")); }
    PdfAnnotation& setBorder(float hRadius, float vRadius, float width) {
        return put(PdfName::Border, std::make_shared<PdfArray>(std::vector<float>{hRadius, vRadius, width}));
    }
    PdfAnnotation& setAction(const std::shared_ptr<PdfAction>& action) { return put(PdfName::A, action->getPdfObject()); }
    PdfAnnotation& setDestination(const std::shared_ptr<PdfDestination>& destination) {
        return put(PdfName::Dest, destination->getPdfObject());
    }
    PdfAnnotation& setHighlightMode(const PdfNamePtr& mode) { return put(PdfName::H, mode); }
    PdfAnnotation& setOpen(bool open) { return put(PdfName::Open, PdfBoolean::valueOf(open)); }
    PdfAnnotation& setNormalAppearance(const PdfStreamPtr& appearance);
    PdfAnnotation& setRectangle(const Rectangle& rect) { return put(PdfName::Rect, std::make_shared<PdfArray>(rect)); }
    std::optional<Rectangle> getRectangle() const { return getPdfObject()->getAsRectangle(PdfName::Rect); }
    PdfAnnotation& put(const PdfNamePtr& key, const PdfObjectPtr& value) {
        getPdfObject()->put(key, value);
        getPdfObject()->setModified();
        return *this;
    }
};

/// Port of PdfOutline (bookmarks).
class PdfOutline {
public:
    static constexpr int FLAG_ITALIC = 1, FLAG_BOLD = 2;
    /// Root outline of a document (creates /Outlines if needed).
    static std::shared_ptr<PdfOutline> getOutlines(PdfDocument& document, bool updateOutlines = false);

    PdfOutline(const std::string& title, PdfDictionaryPtr content, PdfDocument* document);
    const std::string& getTitle() const { return title_; }
    void setTitle(const std::string& title);
    void setColor(const ColorPtr& color) { content_->put(PdfName::C, std::make_shared<PdfArray>(color->getColorValue())); }
    void setStyle(int style) { content_->put(PdfName::F, makeNumber(style)); }
    const PdfDictionaryPtr& getContent() const { return content_; }
    const std::vector<std::shared_ptr<PdfOutline>>& getAllChildren() const { return children_; }
    PdfOutline* getParent() const { return parent_; }
    void addDestination(const std::shared_ptr<PdfDestination>& destination) { content_->put(PdfName::Dest, destination->getPdfObject()); }
    void addAction(const std::shared_ptr<PdfAction>& action) { content_->put(PdfName::A, action->getPdfObject()); }
    void setOpen(bool open);
    bool isOpen() const { return open_; }
    std::shared_ptr<PdfOutline> addOutline(const std::string& title, int position = -1);
    void removeOutline();

private:
    void rebuildLinks();
    /// Number of descendants visible when this item is open (PDF 1.7, 12.3.3 /Count semantics).
    int visibleDescendants() const;
    void initFromDictionary(int depth);
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
    explicit PdfMerger(PdfDocument& destination, bool mergeTags = true, bool mergeOutlines = true)
        : destination_(destination) {}
    PdfMerger& setCloseSourceDocuments(bool v) {
        closeSourceDocuments_ = v;
        return *this;
    }
    PdfMerger& merge(PdfDocument& from, int fromPage, int toPage);
    PdfMerger& merge(PdfDocument& from, const std::vector<int>& pages);
    void close() { destination_.close(); }

private:
    PdfDocument& destination_;
    bool closeSourceDocuments_ = false;
};

/// Port of PdfSplitter: splits a document into documents of N pages each.
class PdfSplitter {
public:
    explicit PdfSplitter(PdfDocument& pdfDocument) : pdfDocument_(pdfDocument) {}
    /// Returns the bytes of each part (the Java version calls a factory for the next document).
    std::vector<Bytes> splitByPageCount(int pageCount);
    /// Extracts the given 1-based page ranges [from, to] into a new document.
    Bytes extractPageRange(int fromPage, int toPage);

private:
    PdfDocument& pdfDocument_;
};

}  // namespace kernel
}  // namespace pdfcpp
