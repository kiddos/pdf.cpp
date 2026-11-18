#include "pdfcpp/kernel/pdf/Navigation.h"

#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfDestination
static PdfArrayPtr destArray(PdfPage* page, const PdfNamePtr& type) {
    auto arr = std::make_shared<PdfArray>();
    arr->add(page->getPdfObject());
    arr->add(type);
    return arr;
}

std::shared_ptr<PdfDestination> PdfDestination::createXYZ(PdfPage* page, float left, float top, float zoom) {
    auto arr = destArray(page, PdfName::XYZ);
    arr->add(makeNumber(static_cast<double>(left)));
    arr->add(makeNumber(static_cast<double>(top)));
    arr->add(makeNumber(static_cast<double>(zoom)));
    return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::createFit(PdfPage* page) {
    return std::make_shared<PdfDestination>(destArray(page, PdfName::Fit));
}

std::shared_ptr<PdfDestination> PdfDestination::createFitH(PdfPage* page, float top) {
    auto arr = destArray(page, PdfName::FitH);
    arr->add(makeNumber(static_cast<double>(top)));
    return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::createFitR(PdfPage* page, const Rectangle& rect) {
    auto arr = destArray(page, PdfName::FitR);
    arr->add(makeNumber(static_cast<double>(rect.getLeft())));
    arr->add(makeNumber(static_cast<double>(rect.getBottom())));
    arr->add(makeNumber(static_cast<double>(rect.getRight())));
    arr->add(makeNumber(static_cast<double>(rect.getTop())));
    return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::createNamed(const std::string& name) {
    return std::make_shared<PdfDestination>(std::make_shared<PdfString>(name));
}

// ============================================================ PdfAction
std::shared_ptr<PdfAction> PdfAction::createURI(const std::string& uri, bool isMap) {
    auto action = std::make_shared<PdfAction>();
    action->put(PdfName::S, PdfName::URI).put(PdfName::URI, std::make_shared<PdfString>(uri));
    if (isMap) action->put(PdfName::IsMap, PdfBoolean::TRUE_VALUE);
    return action;
}

std::shared_ptr<PdfAction> PdfAction::createGoTo(const std::shared_ptr<PdfDestination>& destination) {
    auto action = std::make_shared<PdfAction>();
    action->put(PdfName::S, PdfName::GoTo).put(PdfName::D, destination->getPdfObject());
    return action;
}

std::shared_ptr<PdfAction> PdfAction::createGoTo(const std::string& namedDestination) {
    return createGoTo(PdfDestination::createNamed(namedDestination));
}

std::shared_ptr<PdfAction> PdfAction::createGoToR(const std::string& filename, int pageNum, bool newWindow) {
    auto action = std::make_shared<PdfAction>();
    auto dest = std::make_shared<PdfArray>();
    dest->add(makeNumber(pageNum - 1));
    dest->add(PdfName::Fit);
    action->put(PdfName::S, PdfName::GoToR).put(PdfName::F, std::make_shared<PdfString>(filename)).put(PdfName::D, dest);
    if (newWindow) action->put(PdfName::NewWindow, PdfBoolean::TRUE_VALUE);
    return action;
}

std::shared_ptr<PdfAction> PdfAction::createJavaScript(const std::string& javaScript) {
    auto action = std::make_shared<PdfAction>();
    action->put(PdfName::S, PdfName::JavaScript).put(PdfName::JS, std::make_shared<PdfString>(javaScript, "UnicodeBig"));
    return action;
}

std::shared_ptr<PdfAction> PdfAction::createNamed(const PdfNamePtr& namedAction) {
    auto action = std::make_shared<PdfAction>();
    action->put(PdfName::S, PdfName::Named).put(PdfName::N, namedAction);
    return action;
}

PdfAction& PdfAction::next(const std::shared_ptr<PdfAction>& nextAction) {
    PdfObjectPtr current = getPdfObject()->get(PdfName::Next);
    if (!current) {
        put(PdfName::Next, nextAction->getPdfObject());
    } else if (current->isArray()) {
        std::static_pointer_cast<PdfArray>(current)->add(nextAction->getPdfObject());
    } else {
        auto arr = std::make_shared<PdfArray>();
        arr->add(current);
        arr->add(nextAction->getPdfObject());
        put(PdfName::Next, arr);
    }
    return *this;
}

// ============================================================ PdfAnnotation
PdfAnnotation::PdfAnnotation(const PdfNamePtr& subtype, const Rectangle& rect)
    : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) {
    getPdfObject()->put(PdfName::Type, PdfName::Annot);
    getPdfObject()->put(PdfName::Subtype, subtype);
    getPdfObject()->put(PdfName::Rect, std::make_shared<PdfArray>(rect));
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::createLink(const Rectangle& rect) {
    auto a = std::make_shared<PdfAnnotation>(PdfName::Link, rect);
    a->setBorder(0, 0, 0);
    return a;
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::createText(const Rectangle& rect) {
    return std::make_shared<PdfAnnotation>(PdfName::Text, rect);
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::createSquare(const Rectangle& rect) {
    return std::make_shared<PdfAnnotation>(PdfName::Square, rect);
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::createFreeText(const Rectangle& rect, const std::string& contents) {
    auto a = std::make_shared<PdfAnnotation>(PdfName::FreeText, rect);
    a->setContents(contents);
    a->put(PdfName::DA, std::make_shared<PdfString>("/Helv 12 Tf 0 g"));
    return a;
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::createHighlight(const Rectangle& rect, const std::vector<float>& quadPoints) {
    auto a = std::make_shared<PdfAnnotation>(PdfName::Highlight, rect);
    a->put(PdfName::QuadPoints, std::make_shared<PdfArray>(quadPoints));
    return a;
}

PdfAnnotation& PdfAnnotation::setNormalAppearance(const PdfStreamPtr& appearance) {
    auto ap = getPdfObject()->getAsDictionary(PdfName::AP);
    if (!ap) {
        ap = std::make_shared<PdfDictionary>();
        put(PdfName::AP, ap);
    }
    ap->put(PdfName::N, appearance);
    return *this;
}

// ============================================================ PdfOutline
PdfOutline::PdfOutline(const std::string& title, PdfDictionaryPtr content, PdfDocument* document)
    : title_(title), content_(std::move(content)), document_(document) {}

std::shared_ptr<PdfOutline> PdfOutline::getOutlines(PdfDocument& document, bool) {
    auto& catalog = document.getCatalog();
    auto outlinesDict = catalog.getPdfObject()->getAsDictionary(PdfName::Outlines);
    bool created = false;
    if (!outlinesDict) {
        outlinesDict = std::make_shared<PdfDictionary>();
        outlinesDict->put(PdfName::Type, PdfName::Outlines);
        if (document.getWriter()) {
            outlinesDict->makeIndirect(&document);
            catalog.put(PdfName::Outlines, outlinesDict);
            catalog.put(PdfName::PageMode, PdfName::UseOutlines);
        }
        created = true;
    }
    auto root = std::make_shared<PdfOutline>("Outlines", outlinesDict, &document);
    if (!created) root->initFromDictionary(0);
    return root;
}

void PdfOutline::initFromDictionary(int depth) {
    if (depth > 64) return;
    auto child = content_->getAsDictionary(PdfName::First);
    std::set<const PdfObject*> seen;
    while (child && !seen.count(child.get())) {
        seen.insert(child.get());
        auto titleStr = child->getAsString(PdfName::Title);
        auto outline = std::make_shared<PdfOutline>(titleStr ? titleStr->toUnicodeString() : std::string(), child, document_);
        outline->parent_ = this;
        auto count = child->getAsNumber(PdfName::Count);
        outline->open_ = !count || count->intValue() >= 0;
        outline->initFromDictionary(depth + 1);
        children_.push_back(outline);
        child = child->getAsDictionary(PdfName::Next);
    }
}

void PdfOutline::setTitle(const std::string& title) {
    title_ = title;
    content_->put(PdfName::Title, std::make_shared<PdfString>(title, "UnicodeBig"));
    content_->setModified();
}

std::shared_ptr<PdfOutline> PdfOutline::addOutline(const std::string& title, int position) {
    auto dict = std::make_shared<PdfDictionary>();
    if (document_ && document_->getWriter()) dict->makeIndirect(document_);
    auto outline = std::make_shared<PdfOutline>(title, dict, document_);
    outline->setTitle(title);
    outline->parent_ = this;
    if (position < 0 || position > static_cast<int>(children_.size())) children_.push_back(outline);
    else children_.insert(children_.begin() + position, outline);
    rebuildLinks();
    return outline;
}

void PdfOutline::removeOutline() {
    if (!parent_) return;
    auto& siblings = parent_->children_;
    siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [this](const std::shared_ptr<PdfOutline>& o) { return o.get() == this; }),
                   siblings.end());
    parent_->rebuildLinks();
}

void PdfOutline::rebuildLinks() {
    if (children_.empty()) {
        content_->remove(PdfName::First);
        content_->remove(PdfName::Last);
        content_->remove(PdfName::Count);
    } else {
        content_->put(PdfName::First, children_.front()->content_);
        content_->put(PdfName::Last, children_.back()->content_);
        int count = static_cast<int>(children_.size());
        content_->put(PdfName::Count, makeNumber(parent_ == nullptr || open_ ? count : -count));
        for (size_t i = 0; i < children_.size(); ++i) {
            auto& c = children_[i]->content_;
            c->put(PdfName::Parent, content_);
            if (i > 0) c->put(PdfName::Prev, children_[i - 1]->content_);
            else c->remove(PdfName::Prev);
            if (i + 1 < children_.size()) c->put(PdfName::Next, children_[i + 1]->content_);
            else c->remove(PdfName::Next);
            c->setModified();
        }
    }
    content_->setModified();
    if (parent_) parent_->rebuildLinks();
}

// ============================================================ PdfMerger / PdfSplitter
PdfMerger& PdfMerger::merge(PdfDocument& from, int fromPage, int toPage) {
    std::vector<int> pages;
    for (int p = fromPage; p <= toPage; ++p) pages.push_back(p);
    return merge(from, pages);
}

PdfMerger& PdfMerger::merge(PdfDocument& from, const std::vector<int>& pages) {
    from.copyPagesTo(pages, destination_);
    if (closeSourceDocuments_) from.close();
    return *this;
}

Bytes PdfSplitter::extractPageRange(int fromPage, int toPage) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument part(writer);
        pdfDocument_.copyPagesTo(fromPage, toPage, part);
        part.close();
    }
    return baos.toByteArray();
}

std::vector<Bytes> PdfSplitter::splitByPageCount(int pageCount) {
    std::vector<Bytes> parts;
    int n = pdfDocument_.getNumberOfPages();
    for (int start = 1; start <= n; start += pageCount) parts.push_back(extractPageRange(start, std::min(n, start + pageCount - 1)));
    return parts;
}

}  // namespace kernel
}  // namespace pdfcpp
