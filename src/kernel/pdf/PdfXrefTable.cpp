#include "pdfcpp/kernel/pdf/PdfXrefTable.h"

#include <set>

#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"

namespace pdfcpp {
namespace kernel {

static constexpr int64_t MAX_OFFSET_IN_CROSS_REFERENCE_STREAM = 9999999999LL;

PdfXrefTable::PdfXrefTable(int capacity) {
    if (capacity < 1) capacity = 32;
    xref_.resize(capacity);
    auto zero = std::make_shared<PdfIndirectReference>(nullptr, 0, MAX_GENERATION, 0);
    zero->setState(PdfObject::FREE);
    add(zero);
}

PdfXrefTable::~PdfXrefTable() { releaseAll(); }

void PdfXrefTable::releaseAll() {
    // Break strong cycles: references own their objects, objects may own references.
    // Objects are kept alive until all links are cut, then released together.
    std::vector<PdfObjectPtr> keepAlive;
    for (auto& ref : xref_) {
        if (ref && ref->refersToRaw()) {
            keepAlive.push_back(ref->refersToRaw());
            ref->setRefersTo(nullptr);
        }
    }
    for (auto& obj : keepAlive) {
        if (obj.use_count() == 1) obj->setIndirectReference(nullptr);
    }
}

PdfIndirectReference* PdfXrefTable::add(PdfIndirectReferencePtr reference) {
    if (!reference) return nullptr;
    int objNr = reference->getObjNumber();
    count_ = std::max(count_, objNr);
    ensureCount(objNr);
    xref_[objNr] = std::move(reference);
    return xref_[objNr].get();
}

int PdfXrefTable::getCountOfIndirectObjects() const {
    int n = 0;
    for (const auto& ref : xref_)
        if (ref && !ref->isFree()) n++;
    return n;
}

PdfIndirectReference* PdfXrefTable::get(int index) const {
    if (index > count_ || index < 0 || index >= static_cast<int>(xref_.size())) return nullptr;
    return xref_[index].get();
}

PdfIndirectReferencePtr PdfXrefTable::getPtr(int index) const {
    if (index > count_ || index < 0 || index >= static_cast<int>(xref_.size())) return nullptr;
    return xref_[index];
}

PdfIndirectReference* PdfXrefTable::createNextIndirectReference(PdfDocument* document) {
    auto reference = std::make_shared<PdfIndirectReference>(document, ++count_);
    PdfIndirectReference* r = add(reference);
    r->setState(PdfObject::MODIFIED);
    return r;
}

void PdfXrefTable::freeReference(PdfIndirectReference* reference) {
    if (reference->isFree()) return;
    if (reference->checkState(PdfObject::MUST_BE_FLUSHED)) return;
    if (reference->checkState(PdfObject::FLUSHED)) return;
    reference->setState(PdfObject::FREE)->setState(PdfObject::MODIFIED);
    appendNewRefToFreeList(reference);
    if (reference->getGenNumber() < MAX_GENERATION) reference->setGenNumber(reference->getGenNumber() + 1);
}

void PdfXrefTable::setCapacity(int capacity) {
    if (capacity > static_cast<int>(xref_.size())) extendXref(capacity);
}

void PdfXrefTable::ensureCount(int count) {
    if (count >= static_cast<int>(xref_.size())) extendXref(count << 1);
}

void PdfXrefTable::extendXref(int capacity) { xref_.resize(capacity); }

std::vector<int> PdfXrefTable::createSections(PdfDocument* document, bool dropObjectsFromObjectStream) const {
    std::vector<int> sections;
    int first = 0, len = 0;
    for (int i = 0; i < size(); i++) {
        PdfIndirectReference* reference = i < static_cast<int>(xref_.size()) ? xref_[i].get() : nullptr;
        if (document->isAppendMode() && reference != nullptr &&
            (!reference->checkState(PdfObject::MODIFIED) ||
             (dropObjectsFromObjectStream && reference->getObjStreamNumber() != 0)))
            reference = nullptr;
        if (reference == nullptr) {
            if (len > 0) {
                sections.push_back(first);
                sections.push_back(len);
            }
            len = 0;
        } else if (len > 0) {
            len++;
        } else {
            first = i;
            len = 1;
        }
    }
    if (len > 0) {
        sections.push_back(first);
        sections.push_back(len);
    }
    return sections;
}

int PdfXrefTable::getOffsetSize(int64_t startxref) {
    int size = 5;
    int64_t mask = 0xff00000000LL;
    for (; size > 1; size--) {
        if ((mask & startxref) != 0) break;
        mask >>= 8;
    }
    return size;
}

void PdfXrefTable::writeXrefTableAndTrailer(PdfDocument* document, const PdfObjectPtr& fileId, const PdfObjectPtr& crypto) {
    PdfWriter* writer = document->getWriter();
    if (!document->isAppendMode()) {
        for (int i = count_; i > 0; --i) {
            PdfIndirectReference* lastRef = get(i);
            if (lastRef == nullptr || lastRef->isFree()) {
                removeFreeRefFromList(i);
                --count_;
            } else {
                break;
            }
        }
    }
    PdfStreamPtr xrefStream;
    if (writer->isFullCompression()) {
        xrefStream = std::make_shared<PdfStream>();
        xrefStream->makeIndirect(document);
    }
    std::vector<int> sections = createSections(document, false);
    bool noModifiedObjects = sections.empty() || (xrefStream && sections.size() == 2 && sections[0] == count_ && sections[1] == 1);
    if (document->isAppendMode() && noModifiedObjects) return;

    int64_t startxref = writer->getCurrentPos();
    int64_t xRefStmPos = -1;
    if (xrefStream) {
        xrefStream->put(PdfName::Type, PdfName::XRef);
        xrefStream->put(PdfName::ID, fileId);
        if (crypto) xrefStream->put(PdfName::Encrypt, crypto);
        xrefStream->put(PdfName::Size, makeNumber(size()));
        int offsetSize = getOffsetSize(std::max<int64_t>(startxref, size()));
        xrefStream->put(PdfName::W, std::make_shared<PdfArray>(std::vector<int>{1, offsetSize, 2}));
        if (auto info = document->getTrailer()->get(PdfName::Info, false)) xrefStream->put(PdfName::Info, info);
        xrefStream->put(PdfName::Root, document->getCatalog().getPdfObject());
        auto index = std::make_shared<PdfArray>();
        for (int s : sections) index->add(makeNumber(s));
        if (document->isAppendMode() && !document->getReader()->hasHybridXref())
            xrefStream->put(PdfName::Prev, std::make_shared<PdfNumber>(static_cast<double>(document->getReader()->getLastXref())));
        xrefStream->put(PdfName::Index, index);
        xrefStream->getIndirectReference()->setOffset(startxref);
        auto* out = xrefStream->getOutputStream();
        for (size_t k = 0; k < sections.size(); k += 2) {
            int first = sections[k], len = sections[k + 1];
            for (int i = first; i < first + len; i++) {
                PdfIndirectReference* reference = get(i);
                if (reference->isFree()) {
                    out->write(0);
                    out->writeBigEndian(reference->getOffset(), offsetSize);
                    out->writeBigEndian(reference->getGenNumber(), 2);
                } else if (reference->getObjStreamNumber() == 0) {
                    out->write(1);
                    out->writeBigEndian(reference->getOffset(), offsetSize);
                    out->writeBigEndian(reference->getGenNumber(), 2);
                } else {
                    out->write(2);
                    out->writeBigEndian(reference->getObjStreamNumber(), offsetSize);
                    out->writeBigEndian(reference->getIndex(), 2);
                }
            }
        }
        xrefStream->flush();
        xRefStmPos = startxref;
    }
    bool needsRegularXref = !writer->isFullCompression() || (document->isAppendMode() && document->getReader()->hasHybridXref());
    if (needsRegularXref) {
        startxref = writer->getCurrentPos();
        writer->writeString("xref\n");
        if (xRefStmPos != -1) sections = createSections(document, true);
        for (size_t k = 0; k < sections.size(); k += 2) {
            int first = sections[k], len = sections[k + 1];
            writer->writeInteger(first).writeSpace().writeInteger(len).writeByte('\n');
            for (int i = first; i < first + len; i++) {
                PdfIndirectReference* reference = get(i);
                if (reference->getOffset() > MAX_OFFSET_IN_CROSS_REFERENCE_STREAM)
                    throw PdfException("Xref table has an entry with too big offset.");
                std::string off = "0000000000" + std::to_string(reference->getOffset());
                std::string gen = "00000" + std::to_string(reference->getGenNumber());
                writer->writeString(off.substr(off.size() - 10)).writeSpace().writeString(gen.substr(gen.size() - 5)).writeSpace();
                writer->writeString(reference->isFree() ? "f \n" : "n \n");
            }
        }
        PdfDictionaryPtr trailer = document->getTrailer();
        trailer->remove(PdfName::W);
        trailer->remove(PdfName::Index);
        trailer->remove(PdfName::Type);
        trailer->remove(PdfName::Length);
        trailer->put(PdfName::Size, makeNumber(size()));
        trailer->put(PdfName::ID, fileId);
        if (xRefStmPos != -1) trailer->put(PdfName::XRefStm, std::make_shared<PdfNumber>(static_cast<double>(xRefStmPos)));
        if (crypto) trailer->put(PdfName::Encrypt, crypto);
        writer->writeString("trailer\n");
        if (document->isAppendMode())
            trailer->put(PdfName::Prev, std::make_shared<PdfNumber>(static_cast<double>(document->getReader()->getLastXref())));
        writer->write(*trailer);
        writer->write('\n');
    }
    writer->writeString("startxref\n").writeLong(startxref).writeString("\n%%EOF\n");
    freeReferencesLinkedList_.clear();
}

void PdfXrefTable::initFreeReferencesList(PdfDocument* pdfDocument) {
    freeReferencesLinkedList_.clear();
    xref_[0]->setState(PdfObject::FREE);
    std::set<int> freeReferences;
    for (int i = 1; i < size() && i < static_cast<int>(xref_.size()); ++i) {
        PdfIndirectReference* ref = xref_[i].get();
        if (ref == nullptr || ref->isFree()) freeReferences.insert(i);
    }
    PdfIndirectReference* prevFreeRef = xref_[0].get();
    while (!freeReferences.empty()) {
        int currFreeRefObjNr = -1;
        if (prevFreeRef->getOffset() <= 2147483647LL) currFreeRefObjNr = static_cast<int>(prevFreeRef->getOffset());
        if (freeReferences.find(currFreeRefObjNr) == freeReferences.end() || currFreeRefObjNr >= static_cast<int>(xref_.size()) ||
            xref_[currFreeRefObjNr] == nullptr)
            break;
        freeReferencesLinkedList_[currFreeRefObjNr] = prevFreeRef;
        prevFreeRef = xref_[currFreeRefObjNr].get();
        freeReferences.erase(currFreeRefObjNr);
    }
    while (!freeReferences.empty()) {
        int next = *freeReferences.begin();
        freeReferences.erase(freeReferences.begin());
        if (xref_[next] == nullptr) {
            if (pdfDocument->isAppendMode()) continue;
            auto r = std::make_shared<PdfIndirectReference>(pdfDocument, next, 0);
            r->setState(PdfObject::FREE)->setState(PdfObject::MODIFIED);
            xref_[next] = r;
        } else if (xref_[next]->getGenNumber() == MAX_GENERATION && xref_[next]->getOffset() == 0) {
            continue;
        }
        if (prevFreeRef->getOffset() != next) {
            prevFreeRef->setState(PdfObject::MODIFIED);
            prevFreeRef->setOffset(next);
        }
        freeReferencesLinkedList_[next] = prevFreeRef;
        prevFreeRef = xref_[next].get();
    }
    if (prevFreeRef->getOffset() != 0) {
        prevFreeRef->setState(PdfObject::MODIFIED);
        prevFreeRef->setOffset(0);
    }
    freeReferencesLinkedList_[0] = prevFreeRef;
}

void PdfXrefTable::clear() {
    for (int i = 1; i <= count_ && i < static_cast<int>(xref_.size()); i++) {
        if (xref_[i] && xref_[i]->isFree()) continue;
        xref_[i] = nullptr;
    }
    count_ = 1;
}

void PdfXrefTable::clearAllReferences() {
    for (int i = 1; i <= count_ && i < static_cast<int>(xref_.size()); i++) xref_[i] = nullptr;
    count_ = 1;
}

void PdfXrefTable::appendNewRefToFreeList(PdfIndirectReference* reference) {
    reference->setOffset(0);
    if (freeReferencesLinkedList_.empty()) return;
    PdfIndirectReference* lastFreeRef = freeReferencesLinkedList_[0];
    lastFreeRef->setState(PdfObject::MODIFIED);
    lastFreeRef->setOffset(reference->getObjNumber());
    freeReferencesLinkedList_[reference->getObjNumber()] = lastFreeRef;
    freeReferencesLinkedList_[0] = reference;
}

PdfIndirectReference* PdfXrefTable::removeFreeRefFromList(int freeRefObjNr) {
    if (freeReferencesLinkedList_.empty() || freeRefObjNr == 0) return nullptr;
    if (freeRefObjNr >= static_cast<int>(xref_.size())) return nullptr;
    PdfIndirectReference* freeRef = xref_[freeRefObjNr].get();
    if (freeRef == nullptr || !freeRef->isFree()) return nullptr;
    auto it = freeReferencesLinkedList_.find(freeRef->getObjNumber());
    if (it != freeReferencesLinkedList_.end()) {
        PdfIndirectReference* prevFreeRef = it->second;
        freeReferencesLinkedList_.erase(it);
        if (prevFreeRef != nullptr) {
            freeReferencesLinkedList_[static_cast<int>(freeRef->getOffset())] = prevFreeRef;
            prevFreeRef->setState(PdfObject::MODIFIED);
            prevFreeRef->setOffset(freeRef->getOffset());
        }
    }
    return freeRef;
}

}  // namespace kernel
}  // namespace pdfcpp
