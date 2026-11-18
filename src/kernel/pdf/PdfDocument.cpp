#include "pdfcpp/kernel/pdf/PdfDocument.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <set>

#include "pdfcpp/kernel/crypto/PdfEncryption.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/PdfXrefTable.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfVersion
const PdfVersion PdfVersion::PDF_1_0(1, 0), PdfVersion::PDF_1_1(1, 1), PdfVersion::PDF_1_2(1, 2),
    PdfVersion::PDF_1_3(1, 3), PdfVersion::PDF_1_4(1, 4), PdfVersion::PDF_1_5(1, 5), PdfVersion::PDF_1_6(1, 6),
    PdfVersion::PDF_1_7(1, 7), PdfVersion::PDF_2_0(2, 0);

PdfVersion PdfVersion::fromString(const std::string& value) {
    for (const PdfVersion* v : {&PDF_1_0, &PDF_1_1, &PDF_1_2, &PDF_1_3, &PDF_1_4, &PDF_1_5, &PDF_1_6, &PDF_1_7, &PDF_2_0})
        if (v->toString() == value) return *v;
    throw std::invalid_argument("The provided pdf version was not found.");
}

PdfVersion PdfVersion::fromPdfName(const PdfName& name) {
    for (const PdfVersion* v : {&PDF_1_0, &PDF_1_1, &PDF_1_2, &PDF_1_3, &PDF_1_4, &PDF_1_5, &PDF_1_6, &PDF_1_7, &PDF_2_0})
        if (v->toPdfName()->getValue() == name.getValue()) return *v;
    throw std::invalid_argument("The provided pdf version was not found.");
}

// ============================================================ PdfDate
std::string PdfDate::getCurrentDateString() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "D:%Y%m%d%H%M%S", &local);
    std::string s(buf);
    long offset;
#if defined(_WIN32)
    offset = 0;
#else
    offset = local.tm_gmtoff;
#endif
    if (offset == 0) {
        s += "Z";
    } else {
        char sign = offset > 0 ? '+' : '-';
        long a = std::labs(offset) / 60;
        char tz[16];
        std::snprintf(tz, sizeof tz, "%c%02ld'%02ld'", sign, a / 60, a % 60);
        s += tz;
    }
    return s;
}

// ============================================================ PdfDocumentInfo
PdfDocumentInfo::PdfDocumentInfo(PdfDictionaryPtr pdfObject, PdfDocument* pdfDocument)
    : infoDictionary_(std::move(pdfObject)), document_(pdfDocument) {
    if (document_ && document_->getWriter() != nullptr) infoDictionary_->makeIndirect(document_);
}

PdfDocumentInfo& PdfDocumentInfo::put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    infoDictionary_->put(key, value);
    infoDictionary_->setModified();
    return *this;
}

std::string PdfDocumentInfo::getStringValue(const PdfNamePtr& name) const {
    auto s = infoDictionary_->getAsString(name);
    return s ? s->toUnicodeString() : std::string();
}

PdfDocumentInfo& PdfDocumentInfo::addCreationDate() {
    return put(PdfName::CreationDate, std::make_shared<PdfString>(PdfDate::getCurrentDateString()));
}

PdfDocumentInfo& PdfDocumentInfo::removeCreationDate() {
    infoDictionary_->remove(PdfName::CreationDate);
    infoDictionary_->setModified();
    return *this;
}

PdfDocumentInfo& PdfDocumentInfo::addModDate() {
    return put(PdfName::ModDate, std::make_shared<PdfString>(PdfDate::getCurrentDateString()));
}

void PdfDocumentInfo::setMoreInfo(const std::map<std::string, std::string>& moreInfo) {
    for (const auto& kv : moreInfo) setMoreInfo(kv.first, kv.second);
}

void PdfDocumentInfo::setMoreInfo(const std::string& key, const std::string& value) {
    auto keyName = std::make_shared<PdfName>(key);
    if (value.empty()) {
        infoDictionary_->remove(keyName);
        infoDictionary_->setModified();
    } else {
        put(keyName, std::make_shared<PdfString>(value, io_unicode()));
    }
}

std::string PdfDocumentInfo::getMoreInfo(const std::string& key) const { return getStringValue(std::make_shared<PdfName>(key)); }

// ============================================================ PdfResources
PdfResources::PdfResources(PdfDictionaryPtr pdfObject) : PdfObjectWrapper<PdfDictionary>(std::move(pdfObject)) {
    buildResources(*getPdfObject());
}

PdfResources::PdfResources() : PdfResources(std::make_shared<PdfDictionary>()) {}

void PdfResources::buildResources(const PdfDictionary& dictionary) {
    for (const auto& resourceType : dictionary.keySet()) {
        auto resources = dictionary.getAsDictionary(resourceType);
        if (!resources) continue;
        auto& m = resourceToName_[resourceType->getValue()];
        for (const auto& kv : resources->entrySet()) {
            PdfObject* key = kv.second.get();
            if (key && key->isIndirectReference()) {
                PdfObject* target = static_cast<PdfIndirectReference*>(key)->refersToRaw().get();
                m[key] = kv.first;
                if (target) m[target] = kv.first;
            } else {
                m[key] = kv.first;
            }
        }
    }
}


PdfNamePtr PdfResources::addResource(const PdfObjectPtr& resource, const PdfNamePtr& resType, const std::string& prefix) {
    PdfObject* key = resource.get();
    auto& m = resourceToName_[resType->getValue()];
    auto it = m.find(key);
    if (it == m.end() && resource->getIndirectReference() != nullptr) {
        it = m.find(resource->getIndirectReference());
    }
    if (it != m.end()) return it->second;
    auto resDict = getPdfObject()->getAsDictionary(resType);
    int& counter = counters_[prefix];
    PdfNamePtr name;
    do {
        name = std::make_shared<PdfName>(prefix + std::to_string(++counter));
    } while (resDict && resDict->containsKey(name));
    addResource(resource, resType, name);
    return name;
}

void PdfResources::addResource(const PdfObjectPtr& resource, const PdfNamePtr& resType, const PdfNamePtr& resName) {
    auto resDict = getPdfObject()->getAsDictionary(resType);
    if (!resDict) {
        resDict = std::make_shared<PdfDictionary>();
        getPdfObject()->put(resType, resDict);
    }
    resDict->put(resName, resource);
    resourceToName_[resType->getValue()][resource.get()] = resName;
    if (resource->getIndirectReference()) resourceToName_[resType->getValue()][resource->getIndirectReference()] = resName;
    isModified_ = true;
    getPdfObject()->setModified();
}

PdfNamePtr PdfResources::addImage(const PdfStreamPtr& image) { return addResource(image, PdfName::XObject, "Im"); }
PdfNamePtr PdfResources::addForm(const PdfStreamPtr& form) { return addResource(form, PdfName::XObject, "Fm"); }
PdfNamePtr PdfResources::addExtGState(const PdfDictionaryPtr& extGState) { return addResource(extGState, PdfName::ExtGState, "Gs"); }
PdfNamePtr PdfResources::addProperties(const PdfDictionaryPtr& properties) { return addResource(properties, PdfName::Properties, "Pr"); }
PdfNamePtr PdfResources::addColorSpace(const PdfObjectPtr& colorSpace) { return addResource(colorSpace, PdfName::ColorSpace, "Cs"); }
PdfNamePtr PdfResources::addPattern(const PdfDictionaryPtr& pattern) { return addResource(pattern, PdfName::Pattern, "P"); }
PdfNamePtr PdfResources::addShading(const PdfDictionaryPtr& shading) { return addResource(shading, PdfName::Shading, "Sh"); }

PdfStreamPtr PdfResources::getImage(const PdfNamePtr& name) const { return pdf_cast<PdfStream>(getResourceObject(PdfName::XObject, name)); }
PdfStreamPtr PdfResources::getForm(const PdfNamePtr& name) const { return pdf_cast<PdfStream>(getResourceObject(PdfName::XObject, name)); }

PdfNamePtr PdfResources::getResourceName(const PdfObject* resource) const {
    for (const auto& kv : resourceToName_) {
        auto it = kv.second.find(resource);
        if (it != kv.second.end()) return it->second;
        if (resource && resource->getIndirectReference()) {
            it = kv.second.find(resource->getIndirectReference());
            if (it != kv.second.end()) return it->second;
        }
    }
    return nullptr;
}

std::vector<PdfNamePtr> PdfResources::getResourceNames() const {
    std::vector<PdfNamePtr> names;
    for (const auto& type : getPdfObject()->keySet()) {
        auto n = getResourceNames(type);
        names.insert(names.end(), n.begin(), n.end());
    }
    return names;
}

std::vector<PdfNamePtr> PdfResources::getResourceNames(const PdfNamePtr& resType) const {
    auto resourceCategory = getPdfObject()->getAsDictionary(resType);
    return resourceCategory ? resourceCategory->keySet() : std::vector<PdfNamePtr>();
}

PdfObjectPtr PdfResources::getResourceObject(const PdfNamePtr& resType, const PdfNamePtr& resName) const {
    auto resource = getResource(resType);
    return resource ? resource->get(resName) : nullptr;
}

// ============================================================ PdfPagesTree
PdfPagesTree::PdfPagesTree(PdfDocument* document, PdfDictionaryPtr root) : document_(document), originalRoot_(std::move(root)) {
    if (originalRoot_) {
        std::set<const PdfObject*> visited;
        loadPages(originalRoot_, visited, 0);
    }
    pages_.resize(pageRefs_.size());
}

PdfPagesTree::~PdfPagesTree() = default;

void PdfPagesTree::loadPages(const PdfDictionaryPtr& node, std::set<const PdfObject*>& visited, int depth) {
    if (!node || depth > 256 || visited.count(node.get())) return;
    visited.insert(node.get());
    auto kids = node->getAsArray(PdfName::Kids);
    auto type = node->getAsName(PdfName::Type);
    if (!kids) {
        if (!nameEquals(type, PdfName::Pages)) pageRefs_.push_back(node);
        return;
    }
    if (nameEquals(type, PdfName::Page)) {
        pageRefs_.push_back(node);
        return;
    }
    for (int i = 0; i < kids->size(); ++i) {
        auto kid = kids->getAsDictionary(i);
        if (kid) loadPages(kid, visited, depth + 1);
    }
}

PdfPage* PdfPagesTree::getPage(int pageNum) {
    if (pageNum < 1 || pageNum > getNumberOfPages())
        throw std::out_of_range(format(KernelExceptionMessageConstant::REQUESTED_PAGE_NUMBER_IS_OUT_OF_BOUNDS, pageNum));
    --pageNum;
    if (!pages_[pageNum]) pages_[pageNum] = std::make_shared<PdfPage>(pageRefs_[pageNum], document_);
    return pages_[pageNum].get();
}

PdfPage* PdfPagesTree::getPage(const PdfDictionary* pageDictionary) {
    int n = getPageNumber(pageDictionary);
    return n > 0 ? getPage(n) : nullptr;
}

int PdfPagesTree::getPageNumber(const PdfPage* page) const {
    for (size_t i = 0; i < pages_.size(); ++i)
        if (pages_[i].get() == page) return static_cast<int>(i) + 1;
    return page ? getPageNumber(page->getPdfObject().get()) : -1;
}

int PdfPagesTree::getPageNumber(const PdfDictionary* pageDictionary) const {
    for (size_t i = 0; i < pageRefs_.size(); ++i)
        if (pageRefs_[i].get() == pageDictionary) return static_cast<int>(i) + 1;
    return -1;
}

void PdfPagesTree::addPage(std::shared_ptr<PdfPage> pdfPage) { addPage(getNumberOfPages() + 1, std::move(pdfPage)); }

void PdfPagesTree::addPage(int index, std::shared_ptr<PdfPage> pdfPage) {
    if (index < 1 || index > getNumberOfPages() + 1) index = getNumberOfPages() + 1;
    pageRefs_.insert(pageRefs_.begin() + (index - 1), pdfPage->getPdfObject());
    pages_.insert(pages_.begin() + (index - 1), std::move(pdfPage));
    modified_ = true;
}

std::shared_ptr<PdfPage> PdfPagesTree::removePage(int pageNum) {
    if (pageNum < 1 || pageNum > getNumberOfPages()) return nullptr;
    getPage(pageNum);
    auto page = pages_[pageNum - 1];
    pages_.erase(pages_.begin() + (pageNum - 1));
    pageRefs_.erase(pageRefs_.begin() + (pageNum - 1));
    modified_ = true;
    return page;
}

PdfObjectPtr PdfPagesTree::generateTree() {
    if (pageRefs_.empty()) throw PdfException(KernelExceptionMessageConstant::DOCUMENT_HAS_NO_PAGES);
    if (!modified_ && originalRoot_ && document_->isAppendMode()) return originalRoot_;
    if (!modified_ && originalRoot_) {
        // Keep the original tree if every page is still in place.
        return originalRoot_;
    }
    // Push inheritable attributes down before re-parenting.
    static const PdfNamePtr* inheritable[] = {&PdfName::Resources, &PdfName::MediaBox, &PdfName::CropBox, &PdfName::Rotate};
    for (size_t i = 0; i < pageRefs_.size(); ++i) {
        auto& page = pageRefs_[i];
        for (const PdfNamePtr* key : inheritable) {
            if (page->containsKey(*key)) continue;
            auto parent = page->getAsDictionary(PdfName::Parent);
            int guard = 0;
            while (parent && guard++ < 256) {
                if (auto v = parent->get(*key, false)) {
                    page->put(*key, v);
                    break;
                }
                parent = parent->getAsDictionary(PdfName::Parent);
            }
        }
    }
    // Build a balanced tree with leaf nodes of up to 10 kids (as iText does).
    const int leafSize = 10;
    std::vector<PdfDictionaryPtr> level;
    for (size_t i = 0; i < pageRefs_.size(); i += leafSize) {
        auto node = std::make_shared<PdfDictionary>();
        node->makeIndirect(document_);
        node->put(PdfName::Type, PdfName::Pages);
        auto kids = std::make_shared<PdfArray>();
        size_t end = std::min(pageRefs_.size(), i + leafSize);
        for (size_t k = i; k < end; ++k) {
            pageRefs_[k]->makeIndirect(document_);
            kids->add(pageRefs_[k]);
            pageRefs_[k]->put(PdfName::Parent, node);
            pageRefs_[k]->setModified();
        }
        node->put(PdfName::Kids, kids);
        node->put(PdfName::Count, makeNumber(static_cast<int>(end - i)));
        level.push_back(node);
    }
    while (level.size() > 1) {
        std::vector<PdfDictionaryPtr> next;
        for (size_t i = 0; i < level.size(); i += leafSize) {
            auto node = std::make_shared<PdfDictionary>();
            node->makeIndirect(document_);
            node->put(PdfName::Type, PdfName::Pages);
            auto kids = std::make_shared<PdfArray>();
            int count = 0;
            size_t end = std::min(level.size(), i + leafSize);
            for (size_t k = i; k < end; ++k) {
                kids->add(level[k]);
                level[k]->put(PdfName::Parent, node);
                count += level[k]->getAsNumber(PdfName::Count)->intValue();
            }
            node->put(PdfName::Kids, kids);
            node->put(PdfName::Count, makeNumber(count));
            next.push_back(node);
        }
        level.swap(next);
    }
    // Free the nodes of the old tree that are no longer used.
    if (originalRoot_ && originalRoot_->getIndirectReference() && !document_->isAppendMode()) {
        std::vector<PdfDictionaryPtr> stack{originalRoot_};
        std::set<const PdfObject*> seen;
        while (!stack.empty()) {
            auto n = stack.back();
            stack.pop_back();
            if (!n || seen.count(n.get())) continue;
            seen.insert(n.get());
            if (!nameEquals(n->getAsName(PdfName::Type), PdfName::Pages)) continue;
            if (auto kids = n->getAsArray(PdfName::Kids))
                for (int i = 0; i < kids->size(); ++i) stack.push_back(kids->getAsDictionary(i));
            if (n->getIndirectReference() && !n->isFlushed()) n->getIndirectReference()->setFree();
        }
    }
    originalRoot_ = level[0];
    modified_ = false;
    return level[0];
}

void PdfPagesTree::clearPageRefs() {
    pageRefs_.clear();
    pages_.clear();
}

// ============================================================ PdfCatalog
PdfCatalog::PdfCatalog(PdfDictionaryPtr pdfObject, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(pdfObject)), document_(document) {
    if (!getPdfObject()) throw PdfException("Document has no PDF Catalog object.");
    setForbidRelease();
    pageTree_ = std::make_unique<PdfPagesTree>(document, getPdfObject()->getAsDictionary(PdfName::Pages));
}

PdfCatalog::PdfCatalog(PdfDocument* pdfDocument) : document_(pdfDocument) {
    setPdfObject(std::make_shared<PdfDictionary>());
    getPdfObject()->makeIndirect(pdfDocument);
    getPdfObject()->put(PdfName::Type, PdfName::Catalog);
    setForbidRelease();
    pageTree_ = std::make_unique<PdfPagesTree>(pdfDocument, nullptr);
}

PdfCatalog& PdfCatalog::put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    getPdfObject()->put(key, value);
    getPdfObject()->setModified();
    return *this;
}

PdfCatalog& PdfCatalog::remove(const PdfNamePtr& key) {
    getPdfObject()->remove(key);
    getPdfObject()->setModified();
    return *this;
}

void PdfCatalog::flush() {
    throw PdfException("PdfCatalog cannot be flushed manually; it is flushed on PdfDocument close.");
}

// ============================================================ PdfPage
PdfPage::PdfPage(PdfDictionaryPtr pdfObject, PdfDocument*) : PdfObjectWrapper<PdfDictionary>(std::move(pdfObject)) {
    setForbidRelease();
}

PdfPage::PdfPage(PdfDocument* pdfDocument, const PageSize& pageSize) {
    setPdfObject(std::make_shared<PdfDictionary>());
    getPdfObject()->makeIndirect(pdfDocument);
    auto contentStream = std::make_shared<PdfStream>();
    contentStream->makeIndirect(pdfDocument);
    getPdfObject()->put(PdfName::Contents, contentStream);
    getPdfObject()->put(PdfName::Type, PdfName::Page);
    getPdfObject()->put(PdfName::MediaBox, std::make_shared<PdfArray>(static_cast<const Rectangle&>(pageSize)));
    getPdfObject()->put(PdfName::TrimBox, std::make_shared<PdfArray>(static_cast<const Rectangle&>(pageSize)));
    setForbidRelease();
}

PdfPage::~PdfPage() = default;

PdfDocument* PdfPage::getDocument() const {
    auto* ref = getPdfObject()->getIndirectReference();
    return ref ? ref->getDocument() : nullptr;
}

PdfObjectPtr PdfPage::getInheritableValue(const PdfNamePtr& pdfName, bool allowParentInheritance) const {
    PdfObjectPtr resources = getPdfObject()->get(pdfName);
    if (!resources && allowParentInheritance) {
        auto parent = getPdfObject()->getAsDictionary(PdfName::Parent);
        int guard = 0;
        while (parent && guard++ < 256) {
            resources = parent->get(pdfName);
            if (resources) break;
            parent = parent->getAsDictionary(PdfName::Parent);
        }
    }
    return resources;
}

Rectangle PdfPage::getMediaBox() const {
    auto mediaBox = pdf_cast<PdfArray>(getInheritableValue(PdfName::MediaBox));
    if (!mediaBox) return PageSize::LETTER;  // lenient default (Java throws)
    if (mediaBox->size() != 4) throw PdfException("Wrong media box size.");
    return mediaBox->toRectangle();
}

PdfPage& PdfPage::setMediaBox(const Rectangle& rectangle) { return put(PdfName::MediaBox, std::make_shared<PdfArray>(rectangle)); }

Rectangle PdfPage::getCropBox() const {
    auto cropBox = pdf_cast<PdfArray>(getInheritableValue(PdfName::CropBox));
    if (!cropBox) return getMediaBox();
    Rectangle crop = cropBox->toRectangle();
    Rectangle media = getMediaBox(), inter;
    if (crop.getIntersection(media, inter)) return inter;
    return crop;
}

PdfPage& PdfPage::setCropBox(const Rectangle& rectangle) { return put(PdfName::CropBox, std::make_shared<PdfArray>(rectangle)); }

Rectangle PdfPage::boxOrDefault(const PdfNamePtr& name, const Rectangle& def) const {
    auto box = getPdfObject()->getAsRectangle(name);
    return box ? *box : def;
}

Rectangle PdfPage::getBleedBox() const { return boxOrDefault(PdfName::BleedBox, getCropBox()); }
PdfPage& PdfPage::setBleedBox(const Rectangle& r) { return put(PdfName::BleedBox, std::make_shared<PdfArray>(r)); }
Rectangle PdfPage::getArtBox() const { return boxOrDefault(PdfName::ArtBox, getCropBox()); }
PdfPage& PdfPage::setArtBox(const Rectangle& r) { return put(PdfName::ArtBox, std::make_shared<PdfArray>(r)); }
Rectangle PdfPage::getTrimBox() const { return boxOrDefault(PdfName::TrimBox, getCropBox()); }
PdfPage& PdfPage::setTrimBox(const Rectangle& r) { return put(PdfName::TrimBox, std::make_shared<PdfArray>(r)); }

Rectangle PdfPage::getPageSizeWithRotation() const {
    PageSize rect(getPageSize());
    int rotation = getRotation();
    while (rotation > 0) {
        rect = rect.rotate();
        rotation -= 90;
    }
    return rect;
}

int PdfPage::getRotation() const {
    auto rotate = pdf_cast<PdfNumber>(getInheritableValue(PdfName::Rotate));
    int rotateValue = rotate ? rotate->intValue() : 0;
    rotateValue %= 360;
    return rotateValue < 0 ? rotateValue + 360 : rotateValue;
}

PdfPage& PdfPage::setRotation(int degAngle) { return put(PdfName::Rotate, makeNumber(degAngle)); }

PdfStreamPtr PdfPage::getContentStream(int index) const {
    int count = getContentStreamCount();
    if (index >= count || index < 0)
        throw std::out_of_range("Index: " + std::to_string(index) + ", Size: " + std::to_string(count));
    PdfObjectPtr contents = getPdfObject()->get(PdfName::Contents);
    if (contents->isStream()) return std::static_pointer_cast<PdfStream>(contents);
    if (contents->isArray()) return std::static_pointer_cast<PdfArray>(contents)->getAsStream(index);
    return nullptr;
}

int PdfPage::getContentStreamCount() const {
    PdfObjectPtr contents = getPdfObject()->get(PdfName::Contents);
    if (!contents) return 0;
    if (contents->isStream()) return 1;
    if (contents->isArray()) return std::static_pointer_cast<PdfArray>(contents)->size();
    return 0;
}

PdfStreamPtr PdfPage::getFirstContentStream() const { return getContentStreamCount() > 0 ? getContentStream(0) : nullptr; }
PdfStreamPtr PdfPage::getLastContentStream() const {
    int count = getContentStreamCount();
    return count > 0 ? getContentStream(count - 1) : nullptr;
}

static PdfStreamPtr newContentStream(PdfPage& page, bool before) {
    PdfObjectPtr contents = page.getPdfObject()->get(PdfName::Contents);
    PdfArrayPtr array;
    if (contents && contents->isStream()) {
        array = std::make_shared<PdfArray>();
        // keep the original reference
        PdfObjectPtr raw = page.getPdfObject()->get(PdfName::Contents, false);
        array->add(raw);
        page.getPdfObject()->put(PdfName::Contents, array);
    } else if (contents && contents->isArray()) {
        array = std::static_pointer_cast<PdfArray>(contents);
    } else {
        array = nullptr;
    }
    auto contentStream = std::make_shared<PdfStream>();
    contentStream->makeIndirect(page.getDocument());
    if (array) {
        if (before) array->add(0, contentStream);
        else array->add(contentStream);
        if (array->getIndirectReference() != nullptr) array->setModified();
        else page.getPdfObject()->setModified();
    } else {
        page.getPdfObject()->put(PdfName::Contents, contentStream);
        page.getPdfObject()->setModified();
    }
    return contentStream;
}

PdfStreamPtr PdfPage::newContentStreamBefore() { return newContentStream(*this, true); }
PdfStreamPtr PdfPage::newContentStreamAfter() { return newContentStream(*this, false); }

PdfResources* PdfPage::getResources() {
    if (!resources_) {
        bool readOnly = false;
        auto resources = pdf_cast<PdfDictionary>(getPdfObject()->get(PdfName::Resources));
        if (!resources) {
            resources = pdf_cast<PdfDictionary>(getInheritableValue(PdfName::Resources));
            if (resources) readOnly = true;
        }
        if (!resources) {
            resources = std::make_shared<PdfDictionary>();
            getPdfObject()->put(PdfName::Resources, resources);
        } else if (readOnly) {
            // copy inherited resources into the page to allow modification
            resources = std::static_pointer_cast<PdfDictionary>(resources->clone());
            getPdfObject()->put(PdfName::Resources, resources);
            readOnly = false;
        }
        resources_ = std::make_shared<PdfResources>(resources);
        resources_->setReadOnly(readOnly);
    }
    return resources_.get();
}

PdfPage& PdfPage::setResources(std::shared_ptr<PdfResources> pdfResources) {
    put(PdfName::Resources, pdfResources->getPdfObject());
    resources_ = std::move(pdfResources);
    return *this;
}

void PdfPage::flush(bool flushResourcesContentStreams) {
    if (getPdfObject()->isFlushed()) return;
    PdfDocument* document = getDocument();
    if (document == nullptr || document->getWriter() == nullptr) return;
    if (document->isAppendMode() && !getPdfObject()->isModified()) return;
    if (flushResourcesContentStreams) {
        getResources();
        for (int i = 0; i < getContentStreamCount(); i++) {
            auto s = getContentStream(i);
            if (s) s->flush(false);
        }
        if (resources_) resources_->getPdfObject()->flush();
    }
    getPdfObject()->flush();
}

Bytes PdfPage::getContentBytes() const {
    Bytes out;
    int count = getContentStreamCount();
    for (int i = 0; i < count; ++i) {
        Bytes b = getStreamBytes(i);
        out.insert(out.end(), b.begin(), b.end());
        if (i < count - 1) out.push_back('\n');
    }
    return out;
}

Bytes PdfPage::getStreamBytes(int index) const {
    auto s = getContentStream(index);
    return s ? s->getBytes() : Bytes();
}

PdfArrayPtr PdfPage::getAnnots(bool create) {
    auto annots = getPdfObject()->getAsArray(PdfName::Annots);
    if (!annots && create) {
        annots = std::make_shared<PdfArray>();
        put(PdfName::Annots, annots);
    }
    return annots;
}

int PdfPage::getAnnotsSize() const {
    auto annots = getPdfObject()->getAsArray(PdfName::Annots);
    return annots ? annots->size() : 0;
}

PdfPage& PdfPage::addAnnotation(const PdfDictionaryPtr& annotation) {
    auto annots = getAnnots(true);
    annotation->makeIndirect(getDocument());
    annotation->put(PdfName::P, getPdfObject());
    annots->add(annotation);
    if (annots->getIndirectReference()) annots->setModified();
    else getPdfObject()->setModified();
    return *this;
}

std::vector<PdfDictionaryPtr> PdfPage::getAnnotations() const {
    std::vector<PdfDictionaryPtr> result;
    auto annots = getPdfObject()->getAsArray(PdfName::Annots);
    if (!annots) return result;
    for (int i = 0; i < annots->size(); ++i)
        if (auto d = annots->getAsDictionary(i)) result.push_back(d);
    return result;
}

PdfPage& PdfPage::put(const PdfNamePtr& key, const PdfObjectPtr& value) {
    getPdfObject()->put(key, value);
    getPdfObject()->setModified();
    return *this;
}

PdfPage& PdfPage::remove(const PdfNamePtr& key) {
    getPdfObject()->remove(key);
    getPdfObject()->setModified();
    return *this;
}

namespace {
/// Copy filter skipping /Parent (the new page gets a new parent) and /Annots handled separately.
class PageCopyFilter : public ICopyFilter {
public:
    explicit PageCopyFilter(const PdfObject*) {}
    bool shouldProcess(PdfObject* newParent, PdfName* name, PdfObject*) override {
        if (name && name->getValue() == "Parent") return false;
        // /P back-pointer of annotations to their original page
        if (name && name->getValue() == "P" && newParent && newParent->isDictionary()) return false;
        return true;
    }
};
}  // namespace

std::shared_ptr<PdfPage> PdfPage::copyTo(PdfDocument* toDocument) {
    PageCopyFilter filter(getPdfObject().get());
    // copy page dictionary without /Parent, /Annots, /StructParents, /B
    auto src = getPdfObject();
    auto dict = std::make_shared<PdfDictionary>();
    dict->makeIndirect(toDocument);
    for (const auto& kv : src->entrySet()) {
        const std::string& k = kv.first->getValue();
        if (k == "Parent" || k == "Annots" || k == "StructParents" || k == "B") continue;
        dict->put(kv.first, kv.second->copyTo(toDocument, false, filter));
    }
    // inheritable attributes
    for (const PdfNamePtr* key : {&PdfName::Resources, &PdfName::MediaBox, &PdfName::CropBox, &PdfName::Rotate}) {
        if (!dict->containsKey(*key)) {
            auto v = getInheritableValue(*key);
            if (v) dict->put(*key, v->copyTo(toDocument, false, filter));
        }
    }
    auto page = std::make_shared<PdfPage>(dict, toDocument);
    auto annots = src->getAsArray(PdfName::Annots);
    if (annots) {
        auto newAnnots = std::make_shared<PdfArray>();
        for (int i = 0; i < annots->size(); ++i) {
            auto a = annots->getAsDictionary(i);
            if (!a) continue;
            auto subtype = a->getAsName(PdfName::Subtype);
            if (nameEquals(subtype, PdfName::Popup)) continue;
            if (nameEquals(subtype, PdfName::Link)) {
                // internal links point to pages of the source document: drop them (as iText does when the
                // target page is not copied)
                auto action = a->getAsDictionary(PdfName::A);
                if (a->containsKey(PdfName::Dest) || (action && nameEquals(action->getAsName(PdfName::S), PdfName::GoTo))) continue;
            }
            auto copy = pdf_cast<PdfDictionary>(a->copyTo(toDocument, false, filter));
            if (copy) {
                copy->put(PdfName::P, dict);
                newAnnots->add(copy);
            }
        }
        if (!newAnnots->isEmpty()) dict->put(PdfName::Annots, newAnnots);
    }
    return page;
}

// ============================================================ PdfDocument
static std::atomic<int64_t> g_documentIdCounter{0};

PdfDocument::PdfDocument(PdfReader& reader, const DocumentProperties& properties) : documentId_(++g_documentIdCounter) {
    init(&reader, nullptr, properties);
}

PdfDocument::PdfDocument(PdfWriter& writer, const DocumentProperties& properties) : documentId_(++g_documentIdCounter) {
    init(nullptr, &writer, properties);
}

PdfDocument::PdfDocument(PdfReader& reader, PdfWriter& writer, const StampingProperties& properties)
    : documentId_(++g_documentIdCounter) {
    init(&reader, &writer, properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfReader> reader, const DocumentProperties& properties)
    : ownedReader_(std::move(reader)), documentId_(++g_documentIdCounter) {
    init(ownedReader_.get(), nullptr, properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfWriter> writer, const DocumentProperties& properties)
    : ownedWriter_(std::move(writer)), documentId_(++g_documentIdCounter) {
    init(nullptr, ownedWriter_.get(), properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfReader> reader, std::unique_ptr<PdfWriter> writer,
                         const StampingProperties& properties)
    : ownedReader_(std::move(reader)), ownedWriter_(std::move(writer)), documentId_(++g_documentIdCounter) {
    init(ownedReader_.get(), ownedWriter_.get(), properties);
}

void PdfDocument::init(PdfReader* reader, PdfWriter* writer, const StampingProperties& properties) {
    if (!reader && !writer) throw PdfException(KernelExceptionMessageConstant::PDF_DOCUMENT_MUST_BE_CREATED_WITH_READER_OR_WRITER);
    reader_ = reader;
    writer_ = writer;
    properties_ = properties;
    if (!writer) properties_.appendMode = false;
    xref_ = std::make_unique<PdfXrefTable>();
    std::optional<PdfVersion> newVersion;
    if (writer) newVersion = writer->getPdfVersion();
    open(newVersion);
}

PdfDocument::~PdfDocument() {
    try {
        if (!closed_ && writer_ == nullptr) close();
    } catch (...) {
    }
    // Pages/catalog/fonts hold objects; release them before the xref table breaks cycles.
    documentFonts_.clear();
    defaultFont_.reset();
    info_.reset();
    catalog_.reset();
    trailer_.reset();
    if (xref_) xref_->releaseAll();
}

void PdfDocument::readDocumentIds() {
    auto id = reader_->getTrailer()->getAsArray(PdfName::ID);
    if (id && id->size() == 2) {
        originalDocumentId_ = id->getAsString(0);
        modifiedDocumentId_ = id->getAsString(1);
    }
    if (!originalDocumentId_ && modifiedDocumentId_) originalDocumentId_ = modifiedDocumentId_;
}

void PdfDocument::open(std::optional<PdfVersion> newPdfVersion) {
    if (reader_) {
        if (reader_->getDocument() != nullptr) throw PdfException("Given PdfReader instance has already been utilized.");
        reader_->setDocument(this);
        reader_->readPdf();
        pdfVersion_ = reader_->getHeaderPdfVersion();
        trailer_ = std::make_shared<PdfDictionary>(reader_->getTrailer().get());
        readDocumentIds();
        auto catalogDictionary = pdf_cast<PdfDictionary>(trailer_->get(PdfName::Root, true));
        if (!catalogDictionary) throw PdfException("Corrupted root entry in trailer.");
        catalog_ = std::make_unique<PdfCatalog>(catalogDictionary, this);
        if (auto v = catalog_->getPdfObject()->getAsName(PdfName::Version)) {
            try {
                PdfVersion catalogVersion = PdfVersion::fromPdfName(*v);
                if (catalogVersion.compareTo(pdfVersion_) > 0) pdfVersion_ = catalogVersion;
            } catch (...) {
            }
        }
        if (properties_.appendMode && (reader_->hasRebuiltXref() || reader_->hasFixedXref()))
            throw PdfException("Append mode requires a document without errors, even if recovery is possible.");
    }
    xref_->initFreeReferencesList(this);
    if (writer_) {
        if (reader_ && reader_->hasXrefStm() && !writer_->getProperties().isFullCompression.has_value())
            writer_->getProperties().isFullCompression = true;
        if (reader_ && !reader_->isOpenedWithFullPermission())
            throw BadPasswordException(BadPasswordException::PdfReaderNotOpenedWithOwnerPassword);
        if (reader_ && properties_.preserveEncryption_) writer_->setCrypto(reader_->getDecrypt());
        writer_->setDocument(this);
        if (!reader_) {
            catalog_ = std::make_unique<PdfCatalog>(this);
            getDocumentInfo().addCreationDate();
        }
        getDocumentInfo().addModDate();
        if (!trailer_) trailer_ = std::make_shared<PdfDictionary>();
        if (!trailer_->isEmpty()) {
            for (const PdfNamePtr* key : {&PdfName::Encrypt, &PdfName::Size, &PdfName::Prev, &PdfName::Root,
                                         &PdfName::Info, &PdfName::ID, &PdfName::XRefStm})
                trailer_->remove(*key);
        }
        trailer_->put(PdfName::Root, catalog_->getPdfObject()->getIndirectReference()->selfRef());
        if (reader_ && reader_->getTrailer()->containsKey(PdfName::ID))
            trailer_->put(PdfName::ID, reader_->getTrailer()->get(PdfName::ID));
        PdfStringPtr readerModifiedId = modifiedDocumentId_;
        const auto& wp = writer_->getProperties();
        if (wp.initialDocumentId && !(reader_ && reader_->getDecrypt() && (properties_.appendMode || properties_.preserveEncryption_)))
            originalDocumentId_ = wp.initialDocumentId;
        if (wp.modifiedDocumentId) modifiedDocumentId_ = wp.modifiedDocumentId;
        if (!originalDocumentId_ && modifiedDocumentId_) originalDocumentId_ = modifiedDocumentId_;
        if (!modifiedDocumentId_) {
            if (!originalDocumentId_) originalDocumentId_ = std::make_shared<PdfString>(PdfEncryption::generateNewDocumentId());
            modifiedDocumentId_ = originalDocumentId_;
        }
        if (!wp.modifiedDocumentId && readerModifiedId && modifiedDocumentId_->equals(*readerModifiedId))
            modifiedDocumentId_ = std::make_shared<PdfString>(PdfEncryption::generateNewDocumentId());
    }
    if (properties_.appendMode) {
        auto file = reader_->getSafeFile();
        file->seek(0);
        Bytes buffer(8192);
        int n;
        while ((n = file->read(buffer.data(), 0, static_cast<int>(buffer.size()))) > 0) writer_->write(buffer.data(), 0, n);
        writer_->write('\n');
        writer_->getProperties().isFullCompression = reader_->hasXrefStm();
        writer_->setCrypto(reader_->getDecrypt());
        if (newPdfVersion && pdfVersion_.compareTo(PdfVersion::PDF_1_4) >= 0 &&
            newPdfVersion->compareTo(reader_->getHeaderPdfVersion()) > 0) {
            catalog_->put(PdfName::Version, newPdfVersion->toPdfName());
            pdfVersion_ = *newPdfVersion;
        }
    } else if (writer_) {
        if (newPdfVersion) pdfVersion_ = *newPdfVersion;
        writer_->writeHeader();
        if (writer_->getCrypto() == nullptr) writer_->initCryptoIfSpecified(pdfVersion_);
    }
}

PdfObjectPtr PdfDocument::getPdfObject(int objNum) {
    checkClosingStatus();
    PdfIndirectReference* reference = xref_->get(objNum);
    if (reference == nullptr) return nullptr;
    return reference->isFree() ? nullptr : reference->getRefersToPtr();
}

int PdfDocument::getNumberOfPdfObjects() const { return xref_->size(); }

PdfPage* PdfDocument::getPage(int pageNum) {
    checkClosingStatus();
    return catalog_->getPageTree().getPage(pageNum);
}

PdfPage* PdfDocument::getPage(const PdfDictionary* pageDictionary) {
    checkClosingStatus();
    return catalog_->getPageTree().getPage(pageDictionary);
}

PdfPage* PdfDocument::addNewPage(const PageSize& pageSize) {
    checkClosingStatus();
    auto page = std::make_shared<PdfPage>(this, pageSize);
    catalog_->getPageTree().addPage(page);
    return page.get();
}

PdfPage* PdfDocument::addNewPage(int index, const PageSize& pageSize) {
    checkClosingStatus();
    auto page = std::make_shared<PdfPage>(this, pageSize);
    catalog_->getPageTree().addPage(index, page);
    return page.get();
}

PdfPage* PdfDocument::addPage(std::shared_ptr<PdfPage> page) {
    checkClosingStatus();
    if (page->isFlushed()) throw PdfException(KernelExceptionMessageConstant::FLUSHED_PAGE_CANNOT_BE_ADDED_OR_INSERTED);
    if (page->getDocument() != nullptr && page->getDocument() != this)
        throw PdfException("Page cannot be added to document because it belongs to another document.");
    PdfPage* raw = page.get();
    catalog_->getPageTree().addPage(std::move(page));
    return raw;
}

PdfPage* PdfDocument::addPage(int index, std::shared_ptr<PdfPage> page) {
    checkClosingStatus();
    if (page->isFlushed()) throw PdfException(KernelExceptionMessageConstant::FLUSHED_PAGE_CANNOT_BE_ADDED_OR_INSERTED);
    PdfPage* raw = page.get();
    catalog_->getPageTree().addPage(index, std::move(page));
    return raw;
}

int PdfDocument::getNumberOfPages() const {
    checkClosingStatus();
    return catalog_->getPageTree().getNumberOfPages();
}

int PdfDocument::getPageNumber(const PdfPage* page) const { return catalog_->getPageTree().getPageNumber(page); }
int PdfDocument::getPageNumber(const PdfDictionary* pageDictionary) const {
    return catalog_->getPageTree().getPageNumber(pageDictionary);
}

bool PdfDocument::movePage(PdfPage* page, int insertBefore) {
    checkClosingStatus();
    int pageNum = getPageNumber(page);
    if (pageNum > 0) {
        movePage(pageNum, insertBefore);
        return true;
    }
    return false;
}

void PdfDocument::movePage(int pageNumber, int insertBefore) {
    checkClosingStatus();
    if (insertBefore < 1 || insertBefore > getNumberOfPages() + 1)
        throw std::out_of_range(format(KernelExceptionMessageConstant::REQUESTED_PAGE_NUMBER_IS_OUT_OF_BOUNDS, insertBefore));
    auto& tree = catalog_->getPageTree();
    auto page = tree.removePage(pageNumber);
    if (insertBefore > pageNumber) --insertBefore;
    tree.addPage(insertBefore, page);
}

bool PdfDocument::removePage(PdfPage* page) {
    int pageNum = getPageNumber(page);
    if (pageNum < 1) return false;
    removePage(pageNum);
    return true;
}

void PdfDocument::removePage(int pageNum) {
    checkClosingStatus();
    auto removed = catalog_->getPageTree().removePage(pageNum);
    if (removed && !isAppendMode() && removed->getPdfObject()->getIndirectReference() && writer_) {
        // keep object but it will not be referenced anymore
    }
}

PdfDocumentInfo& PdfDocument::getDocumentInfo() {
    checkClosingStatus();
    if (!info_) {
        PdfDictionaryPtr infoDict;
        if (trailer_) infoDict = trailer_->getAsDictionary(PdfName::Info);
        info_ = std::make_unique<PdfDocumentInfo>(infoDict ? infoDict : std::make_shared<PdfDictionary>(), this);
    }
    return *info_;
}

PdfIndirectReference* PdfDocument::createNextIndirectReference() {
    checkClosingStatus();
    return xref_->createNextIndirectReference(this);
}

void PdfDocument::flushObject(PdfObject* pdfObject, bool canBeInObjStm) { writer_->flushObject(*pdfObject, canBeInObjStm); }

void PdfDocument::checkClosingStatus() const {
    if (closed_) throw PdfException(KernelExceptionMessageConstant::DOCUMENT_CLOSED_IT_IS_IMPOSSIBLE_TO_EXECUTE_ACTION);
}

std::vector<PdfIndirectReference*> PdfDocument::listIndirectReferences() const {
    std::vector<PdfIndirectReference*> refs;
    for (int i = 0; i < xref_->size(); ++i)
        if (auto* r = xref_->get(i)) refs.push_back(r);
    return refs;
}

Bytes PdfDocument::getXmpMetadataBytes() const {
    if (!xmpMetadata_.empty()) return xmpMetadata_;
    if (reader_ && catalog_) {
        auto s = catalog_->getPdfObject()->getAsStream(PdfName::Metadata);
        if (s && !s->isFlushed()) return s->getBytes();
    }
    return Bytes();
}

void PdfDocument::close() {
    if (closed_) return;
    isClosing_ = true;
    try {
        if (writer_) {
            if (catalog_->getPdfObject()->isFlushed()) throw PdfException("Cannot close document with already flushed PDF Catalog.");
            if (!xmpMetadata_.empty()) {
                auto xmp = catalog_->getPdfObject()->getAsStream(PdfName::Metadata);
                if (isAppendMode() && xmp && !xmp->isFlushed() && xmp->getIndirectReference()) {
                    xmp->setData(xmpMetadata_);
                    xmp->setModified();
                } else {
                    xmp = std::make_shared<PdfStream>();
                    xmp->makeIndirect(this);
                    xmp->getOutputStream()->writeBytes(xmpMetadata_);
                    catalog_->getPdfObject()->put(PdfName::Metadata, xmp);
                    catalog_->getPdfObject()->setModified();
                }
                xmp->put(PdfName::Type, PdfName::Metadata);
                xmp->put(PdfName::Subtype, PdfName::XML);
            }
            if (getNumberOfPages() == 0) addNewPage();
            PdfObjectPtr crypto;
            std::set<PdfIndirectReference*> forbiddenToFlush;
            trailer_->put(PdfName::Info, getDocumentInfo().getPdfObject());
            trailer_->put(PdfName::Root, catalog_->getPdfObject());
            if (properties_.appendMode) {
                PdfObjectPtr pageRoot = catalog_->getPageTree().generateTree();
                if (catalog_->getPdfObject()->isModified() || pageRoot->isModified()) {
                    catalog_->put(PdfName::Pages, pageRoot);
                    catalog_->getPdfObject()->flush(false);
                }
                if (getDocumentInfo().getPdfObject()->isModified()) getDocumentInfo().getPdfObject()->flush(false);
                flushFonts();
                if (writer_->getCrypto() != nullptr) {
                    crypto = writer_->getCrypto()->getPdfObject();
                    if (crypto->getIndirectReference()) forbiddenToFlush.insert(crypto->getIndirectReference());
                    else if (auto encRef = reader_->getTrailer()->get(PdfName::Encrypt, false)) crypto = encRef;
                }
                writer_->flushModifiedWaitingObjects(forbiddenToFlush);
                for (int i = 0; i < xref_->size(); i++) {
                    PdfIndirectReference* ref = xref_->get(i);
                    if (ref && !ref->isFree() && ref->checkState(PdfObject::MODIFIED) && !ref->checkState(PdfObject::FLUSHED) &&
                        !forbiddenToFlush.count(ref))
                        ref->setFree();
                }
            } else {
                catalog_->getPdfObject()->put(PdfName::Pages, catalog_->getPageTree().generateTree());
                for (int pageNum = 1; pageNum <= getNumberOfPages(); pageNum++) {
                    PdfPage* page = getPage(pageNum);
                    if (page) page->flush();
                }
                catalog_->getPdfObject()->flush(false);
                getDocumentInfo().getPdfObject()->flush(false);
                flushFonts();
                if (writer_->getCrypto() != nullptr) {
                    crypto = writer_->getCrypto()->getPdfObject();
                    crypto->makeIndirect(this);
                    forbiddenToFlush.insert(crypto->getIndirectReference());
                }
                writer_->flushWaitingObjects(forbiddenToFlush);
                for (int i = 0; i < xref_->size(); i++) {
                    PdfIndirectReference* ref = xref_->get(i);
                    if (ref && !ref->isFree() && !ref->checkState(PdfObject::FLUSHED) && !forbiddenToFlush.count(ref)) {
                        PdfObjectPtr object;
                        if (isFlushUnusedObjects() && !ref->checkState(PdfObject::ORIGINAL_OBJECT_STREAM) &&
                            (object = ref->getRefersToPtr(false)) != nullptr)
                            object->flush();
                        else
                            ref->setFree();
                    }
                }
            }
            writer_->setCrypto(nullptr);
            if (!properties_.appendMode && crypto) crypto->flush(false);
            PdfObjectPtr fileId = PdfEncryption::createInfoId(originalDocumentId_->getValueBytes(),
                                                              modifiedDocumentId_->getValueBytes(),
                                                              properties_.preserveEncryption_);
            xref_->writeXrefTableAndTrailer(this, fileId, crypto);
            writer_->flush();
        }
        catalog_->getPageTree().clearPageRefs();
    } catch (...) {
        if (writer_ && isCloseWriter()) {
            try {
                writer_->finish();
            } catch (...) {
            }
        }
        closed_ = true;
        throw;
    }
    if (writer_ && isCloseWriter()) writer_->finish();
    if (reader_ && isCloseReader()) reader_->close();
    closed_ = true;
}

std::vector<PdfPage*> PdfDocument::copyPagesTo(int pageFrom, int pageTo, PdfDocument& toDocument, int insertBeforePage) {
    std::vector<int> pages;
    for (int i = pageFrom; i <= pageTo; i++) pages.push_back(i);
    return copyPagesTo(pages, toDocument, insertBeforePage);
}

std::vector<PdfPage*> PdfDocument::copyPagesTo(const std::vector<int>& pagesToCopy, PdfDocument& toDocument,
                                               int insertBeforePage) {
    checkClosingStatus();
    std::vector<PdfPage*> copiedPages;
    int insertAt = insertBeforePage < 1 ? toDocument.getNumberOfPages() + 1 : insertBeforePage;
    for (int pageNum : pagesToCopy) {
        PdfPage* page = getPage(pageNum);
        auto newPage = page->copyTo(&toDocument);
        copiedPages.push_back(toDocument.addPage(insertAt++, newPage));
    }
    return copiedPages;
}

}  // namespace kernel
}  // namespace pdfcpp
