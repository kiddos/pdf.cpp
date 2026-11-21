#include "pdfcpp/kernel/pdf/PdfWriter.h"

#include "pdfcpp/kernel/crypto/PdfEncryption.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfXrefTable.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfObjectStream
PdfObjectStream::PdfObjectStream() : PdfStream(std::make_shared<io::ByteArrayOutputStream>()) {}

std::shared_ptr<PdfObjectStream> PdfObjectStream::create(PdfDocument* doc) {
    auto os = std::make_shared<PdfObjectStream>();
    os->indexBaos_ = std::make_shared<io::ByteArrayOutputStream>();
    os->indexStream_ = std::make_unique<PdfOutputStream>(os->indexBaos_);
    os->makeIndirect(doc, doc->getXref()->createNewIndirectReference(doc));
    os->getOutputStream()->setDocument(doc);
    os->put(PdfName::Type, PdfName::ObjStm);
    os->put(PdfName::N, os->size_);
    os->put(PdfName::First, makeNumber(0));
    return os;
}

void PdfObjectStream::addObject(PdfObject& object) {
    if (size_->intValue() == MAX_OBJ_STREAM_SIZE) throw PdfException("PdfObjectStream reached max size.");
    PdfOutputStream* outputStream = getOutputStream();
    indexStream_->writeInteger(object.getIndirectReference()->getObjNumber())
        .writeSpace()
        .writeLong(outputStream->getCurrentPos())
        .writeSpace();
    outputStream->write(object);
    object.getIndirectReference()->setObjStreamNumber(getIndirectReference()->getObjNumber());
    object.getIndirectReference()->setIndex(size_->intValue());
    outputStream->writeSpace();
    size_->increment();
    getAsNumber(PdfName::First)->setValue(static_cast<int>(indexStream_->getCurrentPos()));
}

// ============================================================ PdfWriter
PdfWriter::PdfWriter(const std::string& filename, const WriterProperties& properties)
    : PdfOutputStream(std::make_shared<io::FileOutputStream>(filename)), properties_(properties) {}

PdfWriter::PdfWriter(std::shared_ptr<io::OutputStream> os, const WriterProperties& properties)
    : PdfOutputStream(std::move(os)), properties_(properties) {}

PdfWriter::PdfWriter(io::OutputStream& os, const WriterProperties& properties)
    : PdfOutputStream(io::borrow(os)), properties_(properties) {
    setCloseStream(false);
}

PdfWriter::~PdfWriter() {
    try {
        if (!finished_) flush();
    } catch (...) {
    }
}

void PdfWriter::initCryptoIfSpecified(const PdfVersion& version) {
    if (!properties_.isStandardEncryptionUsed()) return;
    const auto& ep = properties_.encryptionProperties;
    ownedCrypto_ = std::make_unique<PdfEncryption>(ep.userPassword_, ep.ownerPassword_, ep.standardEncryptPermissions_,
                                                   ep.encryptionAlgorithm_,
                                                   document_->getOriginalDocumentId()->getValueBytes(), version);
    crypto_ = ownedCrypto_.get();
}

void PdfWriter::flushObject(PdfObject& pdfObject, bool canBeInObjStm) {
    // Keep the object alive during flushing; releasing content may drop the last owner.
    PdfObjectPtr keepAlive = pdfObject.shared_from_this();
    PdfIndirectReference* indirectReference = pdfObject.getIndirectReference();
    if (isFullCompression() && canBeInObjStm) {
        getObjectStream()->addObject(pdfObject);
    } else {
        indirectReference->setOffset(getCurrentPos());
        writeToBody(pdfObject);
    }
    indirectReference->setState(PdfObject::FLUSHED)->clearState(PdfObject::MUST_BE_FLUSHED);
    switch (pdfObject.getType()) {
        case PdfObject::ARRAY: {
            auto& array = static_cast<PdfArray&>(pdfObject);
            markArrayContentToFlush(array);
            array.releaseContent();
            break;
        }
        case PdfObject::STREAM:
        case PdfObject::DICTIONARY: {
            auto& dictionary = static_cast<PdfDictionary&>(pdfObject);
            markDictionaryContentToFlush(dictionary);
            dictionary.releaseContent();
            break;
        }
        case PdfObject::INDIRECT_REFERENCE:
            markObjectToFlush(static_cast<PdfIndirectReference&>(pdfObject).getRefersTo(false));
            break;
        default:
            break;
    }
    // Flushed objects are no longer needed in memory.
    if (indirectReference->refersToRaw().get() == &pdfObject && !pdfObject.isReleaseForbidden()) {
        // keep reference (Java keeps refersTo for flushed objects too); nothing to do
    }
}

static bool checkTypeOfPdfDictionary(const PdfObject* dictionary, const PdfNamePtr& expectedType) {
    return dictionary->isDictionary() && nameEquals(static_cast<const PdfDictionary*>(dictionary)->getAsName(PdfName::Type), expectedType);
}

static void serializeForSmartMode(const PdfObject* obj, std::string& out, int depth, std::set<const PdfObject*>& visited);

PdfObjectPtr PdfWriter::copyObject(PdfObject* obj, PdfDocument* documentTo, bool allowDuplicating, ICopyFilter& copyFilter) {
    PdfObjectPtr objPtr;
    if (obj && obj->isIndirectReference()) objPtr = static_cast<PdfIndirectReference*>(obj)->getRefersToPtr(true);
    else if (obj) objPtr = obj->shared_from_this();
    if (!objPtr) objPtr = PdfNull::PDF_NULL;
    if (checkTypeOfPdfDictionary(objPtr.get(), PdfName::Catalog)) objPtr = PdfNull::PDF_NULL;
    PdfIndirectReference* indirectReference = objPtr->getIndirectReference();
    bool tryToFindDuplicate = !allowDuplicating && indirectReference != nullptr;
    if (tryToFindDuplicate) {
        auto it = copiedObjects_.find(indirectReference);
        if (it != copiedObjects_.end()) return it->second->getRefersToPtr();
    }
    std::string serialized;
    if (properties_.smartMode && tryToFindDuplicate && !checkTypeOfPdfDictionary(objPtr.get(), PdfName::Page)) {
        std::set<const PdfObject*> visited;
        serializeForSmartMode(objPtr.get(), serialized, 0, visited);
        auto it = smartModeCache_.find(serialized);
        if (it != smartModeCache_.end()) {
            copiedObjects_[indirectReference] = it->second;
            return it->second->getRefersToPtr();
        }
    }
    PdfObjectPtr newObject = objPtr->newInstance();
    if (newObject.get() == PdfNull::PDF_NULL.get()) newObject = std::make_shared<PdfNull>();
    if (indirectReference != nullptr) {
        PdfIndirectReference* indRef = newObject->makeIndirect(documentTo)->getIndirectReference();
        if (!serialized.empty()) smartModeCache_[serialized] = indRef;
        copiedObjects_[indirectReference] = indRef;
    }
    newObject->copyContent(*objPtr, documentTo, copyFilter);
    return newObject;
}

static void serializeForSmartMode(const PdfObject* obj, std::string& out, int depth, std::set<const PdfObject*>& visited) {
    if (!obj || depth > 100) {
        out += "null";
        return;
    }
    if (obj->isIndirectReference()) {
        auto* ref = const_cast<PdfIndirectReference*>(static_cast<const PdfIndirectReference*>(obj));
        PdfObject* target = ref->getRefersTo(true);
        if (!target) {
            out += "null";
            return;
        }
        if (visited.count(target)) {
            out += "$ref" + std::to_string(ref->getObjNumber());
            return;
        }
        // Pages are never merged: serialize by identity
        if (target->isDictionary() &&
            nameEquals(static_cast<PdfDictionary*>(target)->getAsName(PdfName::Type), PdfName::Page)) {
            out += "$page" + std::to_string(reinterpret_cast<uintptr_t>(target));
            return;
        }
        obj = target;
    }
    visited.insert(obj);
    switch (obj->getType()) {
        case PdfObject::DICTIONARY:
        case PdfObject::STREAM: {
            auto* d = static_cast<const PdfDictionary*>(obj);
            out += "<<";
            for (const auto& kv : d->entrySet()) {
                if (nameEquals(kv.first, PdfName::Parent)) continue;
                out += "/" + kv.first->getValue();
                serializeForSmartMode(kv.second.get(), out, depth + 1, visited);
            }
            out += ">>";
            if (obj->getType() == PdfObject::STREAM) {
                Bytes b = const_cast<PdfStream*>(static_cast<const PdfStream*>(obj))->getBytes(false);
                out += "$stream";
                out.append(reinterpret_cast<const char*>(b.data()), b.size());
            }
            break;
        }
        case PdfObject::ARRAY: {
            out += "[";
            for (const auto& e : *static_cast<const PdfArray*>(obj)) {
                serializeForSmartMode(e.get(), out, depth + 1, visited);
                out += " ";
            }
            out += "]";
            break;
        }
        case PdfObject::STRING: {
            Bytes b = static_cast<const PdfString*>(obj)->getValueBytes();
            out += "(" + std::string(b.begin(), b.end()) + ")";
            break;
        }
        case PdfObject::NAME:
            out += "/" + static_cast<const PdfName*>(obj)->getValue();
            break;
        default:
            out += obj->toString();
    }
    visited.erase(obj);
}

void PdfWriter::writeToBody(PdfObject& pdfObj) {
    if (crypto_ != nullptr)
        crypto_->setHashKeyForNextObject(pdfObj.getIndirectReference()->getObjNumber(),
                                         pdfObj.getIndirectReference()->getGenNumber());
    writeInteger(pdfObj.getIndirectReference()->getObjNumber())
        .writeSpace()
        .writeInteger(pdfObj.getIndirectReference()->getGenNumber())
        .writeString(" obj\n");
    write(pdfObj);
    writeString("\nendobj\n");
}

void PdfWriter::writeHeader() {
    writeByte('%');
    writeString(document_->getPdfVersion().toString());
    writeString("\n%\xe2\xe3\xcf\xd3\n");
}

void PdfWriter::flushWaitingObjects(const std::set<PdfIndirectReference*>& forbiddenToFlush) {
    PdfXrefTable* xref = document_->getXref();
    bool needFlush = true;
    while (needFlush) {
        needFlush = false;
        for (int i = 1; i < xref->size(); i++) {
            PdfIndirectReference* indirectReference = xref->get(i);
            if (indirectReference != nullptr && !indirectReference->isFree() &&
                indirectReference->checkState(PdfObject::MUST_BE_FLUSHED) && !forbiddenToFlush.count(indirectReference)) {
                PdfObjectPtr obj = indirectReference->getRefersToPtr(false);
                if (obj) {
                    obj->flush();
                    needFlush = true;
                }
            }
        }
    }
    if (objectStream_ && objectStream_->getSize() > 0) {
        objectStream_->flush();
        objectStream_ = nullptr;
    }
}

void PdfWriter::flushModifiedWaitingObjects(const std::set<PdfIndirectReference*>& forbiddenToFlush) {
    PdfXrefTable* xref = document_->getXref();
    // Java makes a single pass. Repeat until nothing new gets flushed, so objects that become modified (or are
    // created) while flushing others - including lower-numbered ones - are written instead of being freed by
    // PdfDocument::close().
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 1; i < xref->size(); i++) {
            PdfIndirectReference* indirectReference = xref->get(i);
            if (indirectReference == nullptr || indirectReference->isFree() ||
                indirectReference->checkState(PdfObject::FLUSHED) || forbiddenToFlush.count(indirectReference))
                continue;
            if (indirectReference->checkState(PdfObject::MODIFIED)) {
                PdfObjectPtr obj = indirectReference->getRefersToPtr(false);
                if (obj && obj.get() != objectStream_.get()) {
                    obj->flush();
                    if (indirectReference->checkState(PdfObject::FLUSHED)) progress = true;
                }
            }
        }
    }
    if (objectStream_ && objectStream_->getSize() > 0) {
        objectStream_->flush();
        objectStream_ = nullptr;
    }
}

void PdfWriter::finish() {
    if (finished_) return;
    finished_ = true;
    flush();
    close();
}

PdfObjectStream* PdfWriter::getObjectStream() {
    if (!isFullCompression()) return nullptr;
    if (!objectStream_) {
        objectStream_ = PdfObjectStream::create(document_);
    } else if (objectStream_->getSize() == PdfObjectStream::MAX_OBJ_STREAM_SIZE) {
        objectStream_->flush();
        objectStream_ = PdfObjectStream::create(document_);
    }
    return objectStream_.get();
}

void PdfWriter::flushCopiedObjects(int64_t docId) {
    std::vector<PdfIndirectReference*> remove;
    for (auto& kv : copiedObjects_) {
        PdfDocument* document = kv.first->getDocument();
        if (document != nullptr && document->getDocumentId() == docId) {
            if (auto obj = kv.second->getRefersToPtr(false)) {
                obj->flush();
                remove.push_back(kv.first);
            }
        }
    }
    for (auto* r : remove) copiedObjects_.erase(r);
}

void PdfWriter::markArrayContentToFlush(PdfArray& array) {
    for (int i = 0; i < array.size(); i++) markObjectToFlush(array.get(i, false).get());
}

void PdfWriter::markDictionaryContentToFlush(PdfDictionary& dictionary) {
    for (const auto& item : dictionary.values(false)) markObjectToFlush(item.get());
}

void PdfWriter::markObjectToFlush(PdfObject* pdfObject) {
    if (pdfObject == nullptr) return;
    PdfIndirectReference* indirectReference = pdfObject->getIndirectReference();
    if (indirectReference != nullptr) {
        if (!indirectReference->checkState(PdfObject::FLUSHED)) indirectReference->setState(PdfObject::MUST_BE_FLUSHED);
    } else if (pdfObject->getType() == PdfObject::INDIRECT_REFERENCE) {
        if (!pdfObject->checkState(PdfObject::FLUSHED)) pdfObject->setState(PdfObject::MUST_BE_FLUSHED);
    } else if (pdfObject->getType() == PdfObject::ARRAY) {
        markArrayContentToFlush(static_cast<PdfArray&>(*pdfObject));
    } else if (pdfObject->getType() == PdfObject::DICTIONARY) {
        markDictionaryContentToFlush(static_cast<PdfDictionary&>(*pdfObject));
    }
}

}  // namespace kernel
}  // namespace pdfcpp
