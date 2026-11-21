#include "pdfcpp/kernel/pdf/PdfObject.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/io/source/OutputStream.h"
#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/io/util/TextUtil.h"
#include "pdfcpp/kernel/crypto/PdfEncryption.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfOutputStream.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/PdfXrefTable.h"

namespace pdfcpp {
namespace kernel {

NullCopyFilter& NullCopyFilter::getInstance() {
    static NullCopyFilter instance;
    return instance;
}

// ============================================================ PdfObject
void PdfObject::flush(bool canBeInObjStm) {
    if (isFlushed() || getIndirectReference() == nullptr || getIndirectReference()->isFree()) return;
    PdfDocument* document = getIndirectReference()->getDocument();
    if (document != nullptr) {
        if (document->isAppendMode() && !isModified()) return;
        document->flushObject(this, canBeInObjStm && getType() != STREAM && getType() != INDIRECT_REFERENCE &&
                                        getIndirectReference()->getGenNumber() == 0);
    }
}

bool PdfObject::containsIndirectReference() const {
    if (isIndirect()) return true;
    if (getType() == DICTIONARY || getType() == STREAM) {
        for (const auto& kv : static_cast<const PdfDictionary*>(this)->entrySet())
            if (kv.second->containsIndirectReference()) return true;
    } else if (getType() == ARRAY) {
        for (const auto& v : *static_cast<const PdfArray*>(this))
            if (v->containsIndirectReference()) return true;
    }
    return false;
}

bool PdfObject::isIndirect() const { return indirectReference_ != nullptr || checkState(MUST_BE_INDIRECT); }

PdfObject* PdfObject::makeIndirect(PdfDocument* document, PdfIndirectReference* reference) {
    if (document == nullptr || indirectReference_ != nullptr) return this;
    if (document->getWriter() == nullptr)
        throw PdfException(KernelExceptionMessageConstant::THERE_IS_NO_ASSOCIATE_PDF_WRITER_FOR_MAKING_INDIRECTS);
    if (reference == nullptr) {
        indirectReference_ = document->createNextIndirectReference();
        indirectReference_->setRefersTo(shared_from_this());
    } else {
        reference->setState(MODIFIED);
        indirectReference_ = reference;
        indirectReference_->setRefersTo(shared_from_this());
    }
    setState(FORBID_RELEASE);
    clearState(MUST_BE_INDIRECT);
    return this;
}

bool PdfObject::isFlushed() const { return indirectReference_ != nullptr && indirectReference_->checkState(FLUSHED); }

bool PdfObject::isModified() const { return indirectReference_ != nullptr && indirectReference_->checkState(MODIFIED); }

PdfObjectPtr PdfObject::clone(ICopyFilter& filter) const {
    PdfObjectPtr newObject = newInstance();
    if (indirectReference_ != nullptr || checkState(MUST_BE_INDIRECT)) newObject->setState(MUST_BE_INDIRECT);
    newObject->copyContent(*this, nullptr, filter);
    return newObject;
}

PdfObjectPtr PdfObject::copyTo(PdfDocument* document, bool allowDuplicating, ICopyFilter& copyFilter) const {
    if (document == nullptr) throw PdfException(KernelExceptionMessageConstant::DOCUMENT_FOR_COPY_TO_CANNOT_BE_NULL);
    if (indirectReference_ != nullptr) {
        if (indirectReference_->getWriter() != nullptr || checkState(MUST_BE_INDIRECT))
            throw PdfException(
                KernelExceptionMessageConstant::CANNOT_COPY_INDIRECT_OBJECT_FROM_THE_DOCUMENT_THAT_IS_BEING_WRITTEN);
        if (!indirectReference_->getReader()->isOpenedWithFullPermission())
            throw BadPasswordException(BadPasswordException::PdfReaderNotOpenedWithOwnerPassword);
    }
    return processCopying(document, allowDuplicating, copyFilter);
}

PdfObject* PdfObject::setModified() {
    if (indirectReference_ != nullptr) {
        indirectReference_->setState(MODIFIED);
        setState(FORBID_RELEASE);
    }
    return this;
}

void PdfObject::release() {
    if (isReleaseForbidden()) return;
    if (indirectReference_ != nullptr && indirectReference_->getReader() != nullptr &&
        !indirectReference_->checkState(FLUSHED)) {
        PdfIndirectReference* ref = indirectReference_;
        indirectReference_ = nullptr;
        setState(READ_ONLY);
        ref->setRefersTo(nullptr);  // may destroy this object; nothing must be touched afterwards
    }
}

void PdfObject::copyContent(const PdfObject&, PdfDocument*, ICopyFilter&) {
    if (isFlushed()) throw PdfException(KernelExceptionMessageConstant::CANNOT_COPY_FLUSHED_OBJECT);
}

bool PdfObject::equalContent(const PdfObject* obj1, const PdfObject* obj2) {
    const PdfObject* d1 = obj1;
    const PdfObject* d2 = obj2;
    if (obj1 && obj1->isIndirectReference())
        d1 = const_cast<PdfIndirectReference*>(static_cast<const PdfIndirectReference*>(obj1))->getRefersTo(true);
    if (obj2 && obj2->isIndirectReference())
        d2 = const_cast<PdfIndirectReference*>(static_cast<const PdfIndirectReference*>(obj2))->getRefersTo(true);
    return d1 != nullptr && d2 != nullptr && d1->equals(*d2);
}

PdfObjectPtr PdfObject::processCopying(PdfDocument* documentTo, bool allowDuplicating, ICopyFilter& filter) const {
    if (documentTo != nullptr) {
        PdfWriter* writer = documentTo->getWriter();
        if (writer == nullptr) throw PdfException(KernelExceptionMessageConstant::CANNOT_COPY_TO_DOCUMENT_OPENED_IN_READING_MODE);
        return writer->copyObject(const_cast<PdfObject*>(this), documentTo, allowDuplicating, filter);
    }
    const PdfObject* obj = this;
    if (obj->isIndirectReference()) {
        PdfObject* refTo = const_cast<PdfIndirectReference*>(static_cast<const PdfIndirectReference*>(obj))->getRefersTo();
        obj = refTo != nullptr ? refTo : obj;
    }
    if (obj->isIndirect() && !allowDuplicating) return const_cast<PdfObject*>(obj)->shared_from_this();
    return obj->clone();
}

// ============================================================ PdfPrimitiveObject
PdfObject* PdfPrimitiveObject::makeIndirect(PdfDocument* document, PdfIndirectReference* reference) {
    if (directOnly_) return this;
    return PdfObject::makeIndirect(document, reference);
}

PdfObject* PdfPrimitiveObject::setIndirectReference(PdfIndirectReference* indirectReference) {
    if (!directOnly_) PdfObject::setIndirectReference(indirectReference);
    return this;
}

const Bytes& PdfPrimitiveObject::getInternalContent() const {
    if (!contentSet_) generateContent();
    return content_;
}

void PdfPrimitiveObject::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfObject::copyContent(from, document, filter);
    const auto& object = static_cast<const PdfPrimitiveObject&>(from);
    if (object.contentSet_) setContent(object.content_);
}

int PdfPrimitiveObject::compareContent(const PdfPrimitiveObject& o) const {
    const Bytes& a = getInternalContent();
    const Bytes& b = o.getInternalContent();
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        int8_t x = static_cast<int8_t>(a[i]), y = static_cast<int8_t>(b[i]);
        if (x > y) return 1;
        if (x < y) return -1;
    }
    return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}

// ============================================================ PdfNull / PdfBoolean
const PdfNullPtr PdfNull::PDF_NULL = [] {
    auto n = std::make_shared<PdfNull>();
    return n;
}();

const PdfBooleanPtr PdfBoolean::TRUE_VALUE = std::make_shared<PdfBoolean>(true, true);
const PdfBooleanPtr PdfBoolean::FALSE_VALUE = std::make_shared<PdfBoolean>(false, true);

void PdfBoolean::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfPrimitiveObject::copyContent(from, document, filter);
    value_ = static_cast<const PdfBoolean&>(from).value_;
}

// ============================================================ PdfNumber
PdfNumber::PdfNumber(const Bytes& content) : PdfPrimitiveObject(content) {
    isDouble_ = true;
    valueValid_ = false;
}

double PdfNumber::getValue() const {
    if (!valueValid_) generateValue();
    return value_;
}

int PdfNumber::intValue() const {
    double v = getValue();
    if (std::isnan(v)) return 0;
    if (v > 2147483647.0) return 2147483647;
    if (v < -2147483648.0) return -2147483647 - 1;
    return static_cast<int>(v);
}

void PdfNumber::setValue(int value) {
    value_ = value;
    isDouble_ = false;
    valueValid_ = true;
    clearContent();
}

void PdfNumber::setValue(double value) {
    value_ = value;
    isDouble_ = true;
    valueValid_ = true;
    clearContent();
}

std::string PdfNumber::toString() const {
    if (contentSet_) return toLatin1String(content_);
    if (isDouble_) return toLatin1String(io::ByteUtils::getIsoBytes(getValue()));
    return std::to_string(intValue());
}

bool PdfNumber::equals(const PdfObject& o) const {
    if (o.getType() != NUMBER) return false;
    double a = getValue(), b = static_cast<const PdfNumber&>(o).getValue();
    return a == b || (std::isnan(a) && std::isnan(b));
}

void PdfNumber::generateContent() const {
    if (isDouble_) setContent(io::ByteUtils::getIsoBytes(value_));
    else setContent(io::ByteUtils::getIsoBytes(static_cast<int>(value_)));
}

void PdfNumber::generateValue() const {
    std::string s = toLatin1String(content_);
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) v = std::nan("");
    value_ = v;
    isDouble_ = true;
    valueValid_ = true;
}

void PdfNumber::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfPrimitiveObject::copyContent(from, document, filter);
    const auto& number = static_cast<const PdfNumber&>(from);
    value_ = number.value_;
    isDouble_ = number.isDouble_;
    valueValid_ = number.valueValid_;
}

// ============================================================ PdfName
PdfNamePtr PdfName::createDirectName(const std::string& name) {
    return PdfNamePtr(new PdfName(name, true));
}

namespace {
const std::map<std::string, PdfNamePtr>& staticNames();
}

PdfNamePtr PdfName::of(const std::string& value) {
    const auto& names = staticNames();
    auto it = names.find(value);
    if (it != names.end()) return it->second;
    return std::make_shared<PdfName>(value);
}

const std::string& PdfName::getValue() const {
    if (!valueSet_) generateValue();
    return value_;
}

void PdfName::generateValue() const {
    // PdfNameUtil.decodeName: decode #xx escapes
    std::string buf;
    const Bytes& content = content_;
    for (size_t k = 0; k < content.size(); ++k) {
        char c = static_cast<char>(content[k]);
        if (c == '#' && k + 2 < content.size()) {
            int c1 = io::ByteBuffer::getHex(content[k + 1]);
            int c2 = io::ByteBuffer::getHex(content[k + 2]);
            if (c1 >= 0 && c2 >= 0) {
                c = static_cast<char>((c1 << 4) + c2);
                k += 2;
            }
        }
        buf += c;
    }
    value_ = buf;
    valueSet_ = true;
}

void PdfName::generateContent() const {
    static const char* hex = "0123456789abcdef";
    Bytes buf;
    buf.reserve(value_.size() + 20);
    for (char ch : value_) {
        uint8_t c = static_cast<uint8_t>(ch);
        switch (c) {
            case ' ': case '%': case '(': case ')': case '<': case '>': case '[': case ']':
            case '{': case '}': case '/': case '#':
                buf.push_back('#');
                buf.push_back(hex[c >> 4]);
                buf.push_back(hex[c & 0xf]);
                break;
            default:
                if (c >= 32 && c <= 126) {
                    buf.push_back(c);
                } else {
                    buf.push_back('#');
                    buf.push_back(hex[c >> 4]);
                    buf.push_back(hex[c & 0xf]);
                }
        }
    }
    setContent(std::move(buf));
}

std::string PdfName::toString() const {
    if (contentSet_) return "/" + toLatin1String(content_);
    return "/" + getValue();
}

void PdfName::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfPrimitiveObject::copyContent(from, document, filter);
    const auto& name = static_cast<const PdfName&>(from);
    value_ = name.value_;
    valueSet_ = name.valueSet_;
}

// ============================================================ PdfString
PdfString::PdfString(const Bytes& content) {
    value_.clear();
    for (uint8_t b : content) io::TextUtil::appendUtf8(value_, b);
    valueSet_ = true;
}

PdfString::PdfString(const Bytes& content, bool hexWriting) : PdfPrimitiveObject(content), hexWriting_(hexWriting) {}

PdfString* PdfString::setHexWriting(bool hexWriting) {
    if (!valueSet_) generateValue();
    clearContent();
    hexWriting_ = hexWriting;
    return this;
}

const std::string& PdfString::getValue() const {
    if (!valueSet_) generateValue();
    return value_;
}

std::string PdfString::toUnicodeString() const {
    if (!encoding_.empty()) return getValue();
    if (!contentSet_) generateContent();
    Bytes b = decodeContent();
    if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) return io::PdfEncodings::convertToString(b, io::PdfEncodings::UNICODE_BIG);
    if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) return io::PdfEncodings::convertToString(b, io::PdfEncodings::UTF8);
    return io::PdfEncodings::convertToString(b, io::PdfEncodings::PDF_DOC_ENCODING);
}

Bytes PdfString::getValueBytes() const {
    if (!valueSet_) generateValue();
    if (encoding_ == io::PdfEncodings::UNICODE_BIG && io::PdfEncodings::isPdfDocEncoding(value_))
        return io::PdfEncodings::convertToBytes(value_, io::PdfEncodings::PDF_DOC_ENCODING);
    return io::PdfEncodings::convertToBytes(value_, encoding_);
}

bool PdfString::equals(const PdfObject& o) const {
    if (this == &o) return true;
    if (o.getType() != STRING) return false;
    const auto& that = static_cast<const PdfString&>(o);
    return getValue() == that.getValue() && getEncoding() == that.getEncoding();
}

std::string PdfString::toString() const {
    if (!valueSet_) return toLatin1String(decodeContent());
    return getValue();
}

void PdfString::setDecryption(int decryptInfoNum, int decryptInfoGen, PdfEncryption* decryption) {
    decryptInfoNum_ = decryptInfoNum;
    decryptInfoGen_ = decryptInfoGen;
    decryption_ = decryption;
}

void PdfString::generateValue() const {
    Bytes decoded = decodeContent();
    value_.clear();
    for (uint8_t b : decoded) io::TextUtil::appendUtf8(value_, b);
    valueSet_ = true;
    if (decryption_ != nullptr) {
        decryption_ = nullptr;
        clearContent();
    }
}

void PdfString::generateContent() const { setContent(encodeBytes(getValueBytes())); }

bool PdfString::encrypt(PdfEncryption* encrypt) {
    if (checkState(UNENCRYPTED)) return false;
    if (encrypt != decryption_) {
        if (decryption_ != nullptr) generateValue();
        if (encrypt != nullptr && !encrypt->isEmbeddedFilesOnly()) {
            Bytes b = encrypt->encryptByteArray(getValueBytes());
            setContent(encodeBytes(b));
            return true;
        }
    }
    return false;
}

Bytes PdfString::decodeContent() const {
    Bytes decoded = io::PdfTokenizer::decodeStringContent(content_, hexWriting_);
    if (decryption_ != nullptr && !checkState(UNENCRYPTED)) {
        decryption_->setHashKeyForNextObject(decryptInfoNum_, decryptInfoGen_);
        decoded = decryption_->decryptByteArray(decoded);
    }
    return decoded;
}

Bytes PdfString::encodeBytes(const Bytes& bytes) const {
    if (bytes.empty()) return bytes;
    if (hexWriting_) {
        io::ByteBuffer buf(static_cast<int>(bytes.size()) * 2);
        for (uint8_t b : bytes) buf.appendHex(b);
        return buf.toByteArray();
    }
    io::ByteBuffer buf = io::StreamUtil::createBufferedEscapedString(bytes);
    return buf.toByteArray(1, buf.size() - 2);
}

void PdfString::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfPrimitiveObject::copyContent(from, document, filter);
    const auto& s = static_cast<const PdfString&>(from);
    value_ = s.value_;
    valueSet_ = s.valueSet_;
    hexWriting_ = s.hexWriting_;
    decryption_ = s.decryption_;
    decryptInfoNum_ = s.decryptInfoNum_;
    decryptInfoGen_ = s.decryptInfoGen_;
    encoding_ = s.encoding_;
}

// ============================================================ PdfLiteral
void PdfLiteral::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfPrimitiveObject::copyContent(from, document, filter);
    position_ = static_cast<const PdfLiteral&>(from).position_;
}

// ============================================================ PdfIndirectReference
PdfObject* PdfIndirectReference::getRefersTo(bool recursively) { return getRefersToPtr(recursively).get(); }

PdfObjectPtr PdfIndirectReference::getRefersToPtr(bool recursively) {
    if (!recursively) {
        if (refersTo_ == nullptr && !checkState(FLUSHED) && !checkState(MODIFIED) && !checkState(FREE) &&
            getReader() != nullptr) {
            if (resolvingReferenceInProcess_)
                throw XrefCycledReferencesException(
                    format(KernelExceptionMessageConstant::XREF_STREAM_HAS_SELF_REFERENCED_OBJECT, objNr_));
            resolvingReferenceInProcess_ = true;
            try {
                refersTo_ = getReader()->readObject(this);
            } catch (...) {
                resolvingReferenceInProcess_ = false;
                throw;
            }
            resolvingReferenceInProcess_ = false;
        }
        return refersTo_;
    }
    PdfObjectPtr current = getRefersToPtr(false);
    for (int i = 0; i < LENGTH_OF_INDIRECTS_CHAIN; i++) {
        if (current && current->getType() == INDIRECT_REFERENCE)
            current = static_cast<PdfIndirectReference*>(current.get())->getRefersToPtr(false);
        else
            break;
    }
    return current;
}

void PdfIndirectReference::setFree() { getDocument()->getXref()->freeReference(this); }

std::string PdfIndirectReference::toString() const {
    std::string states = " ";
    if (checkState(FREE)) states += "Free; ";
    if (checkState(MODIFIED)) states += "Modified; ";
    if (checkState(MUST_BE_FLUSHED)) states += "MustBeFlushed; ";
    if (checkState(READING)) states += "Reading; ";
    if (checkState(FLUSHED)) states += "Flushed; ";
    if (checkState(ORIGINAL_OBJECT_STREAM)) states += "OriginalObjectStream; ";
    if (checkState(FORBID_RELEASE)) states += "ForbidRelease; ";
    if (checkState(READ_ONLY)) states += "ReadOnly; ";
    return std::to_string(objNr_) + " " + std::to_string(genNr_) + " R" + states.substr(0, states.size() - 1);
}

bool PdfIndirectReference::equals(const PdfObject& o) const {
    if (this == &o) return true;
    if (o.getType() != INDIRECT_REFERENCE) return false;
    const auto& that = static_cast<const PdfIndirectReference&>(o);
    bool docsEqual = pdfDocument_ == that.pdfDocument_;
    if (!docsEqual)
        docsEqual = pdfDocument_ && that.pdfDocument_ && pdfDocument_->getDocumentId() == that.pdfDocument_->getDocumentId();
    return objNr_ == that.objNr_ && genNr_ == that.genNr_ && docsEqual;
}

int PdfIndirectReference::compareTo(const PdfIndirectReference& o) const {
    if (objNr_ == o.objNr_) {
        if (genNr_ == o.genNr_) {
            if (pdfDocument_ == o.pdfDocument_) return 0;
            if (!pdfDocument_) return -1;
            if (!o.pdfDocument_) return 1;
            auto a = pdfDocument_->getDocumentId(), b = o.pdfDocument_->getDocumentId();
            return a == b ? 0 : (a > b ? 1 : -1);
        }
        return genNr_ > o.genNr_ ? 1 : -1;
    }
    return objNr_ > o.objNr_ ? 1 : -1;
}

PdfWriter* PdfIndirectReference::getWriter() const { return pdfDocument_ ? pdfDocument_->getWriter() : nullptr; }
PdfReader* PdfIndirectReference::getReader() const { return pdfDocument_ ? pdfDocument_->getReader() : nullptr; }
PdfObjectPtr PdfIndirectReference::newInstance() const { return PdfNull::PDF_NULL; }

// ============================================================ PdfArray
PdfArray::PdfArray(const Rectangle& r) {
    list_ = {makeNumber(static_cast<double>(r.getLeft())), makeNumber(static_cast<double>(r.getBottom())),
             makeNumber(static_cast<double>(r.getRight())), makeNumber(static_cast<double>(r.getTop()))};
}

PdfArray::PdfArray(const std::vector<float>& numbers) {
    for (float f : numbers) list_.push_back(makeNumber(static_cast<double>(f)));
}
PdfArray::PdfArray(const std::vector<double>& numbers) {
    for (double f : numbers) list_.push_back(makeNumber(f));
}
PdfArray::PdfArray(const std::vector<int>& numbers) {
    for (int f : numbers) list_.push_back(makeNumber(f));
}
PdfArray::PdfArray(const std::vector<bool>& values) {
    for (bool b : values) list_.push_back(PdfBoolean::valueOf(b));
}
PdfArray::PdfArray(const std::vector<std::string>& strings, bool asNames) {
    for (const auto& s : strings) {
        if (asNames) list_.push_back(std::make_shared<PdfName>(s));
        else list_.push_back(std::make_shared<PdfString>(s));
    }
}

static bool sameOrEqual(const PdfObject* a, const PdfObject* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return a->equals(*b);
}

bool PdfArray::contains(const PdfObject* o) const {
    for (const auto& e : list_) {
        if (sameOrEqual(e.get(), o)) return true;
        if (o && e && o->getIndirectReference() && o->getIndirectReference()->equals(*e)) return true;
        if (o && e && e->getIndirectReference() && e->getIndirectReference()->equals(*o)) return true;
    }
    return false;
}

PdfObjectPtr PdfArray::set(int index, PdfObjectPtr element) {
    PdfObjectPtr old = list_.at(index);
    list_[index] = std::move(element);
    return old;
}

PdfObjectPtr PdfArray::get(int index, bool asDirect) const {
    if (index < 0 || index >= static_cast<int>(list_.size())) return nullptr;
    const PdfObjectPtr& obj = list_[index];
    if (!asDirect || !obj || obj->getType() != INDIRECT_REFERENCE) return obj;
    return static_cast<PdfIndirectReference*>(obj.get())->getRefersToPtr(true);
}

void PdfArray::remove(const PdfObject* o) {
    int i = indexOf(o);
    if (i >= 0) list_.erase(list_.begin() + i);
}

int PdfArray::indexOf(const PdfObject* o) const {
    if (o == nullptr) return -1;
    for (size_t i = 0; i < list_.size(); ++i) {
        const PdfObject* e = list_[i].get();
        if (sameOrEqual(e, o)) return static_cast<int>(i);
        if (o->getIndirectReference() && o->getIndirectReference()->equals(*e)) return static_cast<int>(i);
    }
    return -1;
}

std::string PdfArray::toString() const {
    std::string s = "[";
    for (const auto& e : list_) {
        PdfIndirectReference* ref = e ? e->getIndirectReference() : nullptr;
        s += e == nullptr ? std::string("null") : ref == nullptr ? e->toString() : ref->toString();
        s += " ";
    }
    s += "]";
    return s;
}

bool PdfArray::equals(const PdfObject& o) const {
    if (this == &o) return true;
    if (o.getType() != ARRAY) return false;
    const auto& that = static_cast<const PdfArray&>(o);
    if (that.size() != size()) return false;
    for (int i = 0; i < size(); ++i)
        if (!equalContent(list_[i].get(), that.list_[i].get())) return false;
    return true;
}

PdfArrayPtr PdfArray::getAsArray(int index) const { return pdf_cast<PdfArray>(get(index)); }
PdfDictionaryPtr PdfArray::getAsDictionary(int index) const {
    auto o = get(index);
    return (o && o->getType() == DICTIONARY) ? std::static_pointer_cast<PdfDictionary>(o) : nullptr;
}
PdfStreamPtr PdfArray::getAsStream(int index) const { return pdf_cast<PdfStream>(get(index)); }
PdfNumberPtr PdfArray::getAsNumber(int index) const { return pdf_cast<PdfNumber>(get(index)); }
PdfNamePtr PdfArray::getAsName(int index) const { return pdf_cast<PdfName>(get(index)); }
PdfStringPtr PdfArray::getAsString(int index) const { return pdf_cast<PdfString>(get(index)); }
PdfBooleanPtr PdfArray::getAsBoolean(int index) const { return pdf_cast<PdfBoolean>(get(index)); }

Rectangle PdfArray::toRectangle() const {
    if (size() != 4) throw PdfException("Cannot convert PdfArray to Rectangle.");
    float x1 = getAsNumber(0) ? getAsNumber(0)->floatValue() : 0;
    float y1 = getAsNumber(1) ? getAsNumber(1)->floatValue() : 0;
    float x2 = getAsNumber(2) ? getAsNumber(2)->floatValue() : 0;
    float y2 = getAsNumber(3) ? getAsNumber(3)->floatValue() : 0;
    float llx = std::min(x1, x2), lly = std::min(y1, y2), urx = std::max(x1, x2), ury = std::max(y1, y2);
    return Rectangle(llx, lly, urx - llx, ury - lly);
}

std::vector<float> PdfArray::toFloatArray() const {
    std::vector<float> r;
    for (int i = 0; i < size(); ++i) {
        auto n = getAsNumber(i);
        r.push_back(n ? n->floatValue() : 0.f);
    }
    return r;
}
std::vector<double> PdfArray::toDoubleArray() const {
    std::vector<double> r;
    for (int i = 0; i < size(); ++i) {
        auto n = getAsNumber(i);
        r.push_back(n ? n->doubleValue() : 0.0);
    }
    return r;
}
std::vector<int64_t> PdfArray::toLongArray() const {
    std::vector<int64_t> r;
    for (int i = 0; i < size(); ++i) {
        auto n = getAsNumber(i);
        r.push_back(n ? n->longValue() : 0);
    }
    return r;
}
std::vector<int> PdfArray::toIntArray() const {
    std::vector<int> r;
    for (int i = 0; i < size(); ++i) {
        auto n = getAsNumber(i);
        r.push_back(n ? n->intValue() : 0);
    }
    return r;
}
std::vector<bool> PdfArray::toBooleanArray() const {
    std::vector<bool> r;
    for (int i = 0; i < size(); ++i) {
        auto b = getAsBoolean(i);
        r.push_back(b ? b->getValue() : false);
    }
    return r;
}

void PdfArray::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfObject::copyContent(from, document, filter);
    const auto& array = static_cast<const PdfArray&>(from);
    for (const auto& entry : array.list_) {
        if (filter.shouldProcess(this, nullptr, entry.get()))
            list_.push_back(entry->processCopying(document, false, filter));
    }
}

// ============================================================ PdfDictionary
bool PdfDictionary::containsValue(const PdfObject* value) const {
    for (const auto& kv : map_)
        if (sameOrEqual(kv.second.get(), value)) return true;
    return false;
}

PdfObjectPtr PdfDictionary::get(const PdfNamePtr& key, bool asDirect) const {
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;
    const PdfObjectPtr& obj = it->second;
    if (!asDirect || !obj || obj->getType() != INDIRECT_REFERENCE) return obj;
    return static_cast<PdfIndirectReference*>(obj.get())->getRefersToPtr(true);
}

PdfArrayPtr PdfDictionary::getAsArray(const PdfNamePtr& key) const { return pdf_cast<PdfArray>(get(key)); }
PdfDictionaryPtr PdfDictionary::getAsDictionary(const PdfNamePtr& key) const {
    auto o = get(key);
    return (o && o->getType() == DICTIONARY) ? std::static_pointer_cast<PdfDictionary>(o) : nullptr;
}
PdfStreamPtr PdfDictionary::getAsStream(const PdfNamePtr& key) const { return pdf_cast<PdfStream>(get(key)); }
PdfNumberPtr PdfDictionary::getAsNumber(const PdfNamePtr& key) const { return pdf_cast<PdfNumber>(get(key)); }
PdfNamePtr PdfDictionary::getAsName(const PdfNamePtr& key) const { return pdf_cast<PdfName>(get(key)); }
PdfStringPtr PdfDictionary::getAsString(const PdfNamePtr& key) const { return pdf_cast<PdfString>(get(key)); }
PdfBooleanPtr PdfDictionary::getAsBoolean(const PdfNamePtr& key) const { return pdf_cast<PdfBoolean>(get(key)); }

std::optional<Rectangle> PdfDictionary::getAsRectangle(const PdfNamePtr& key) const {
    auto a = getAsArray(key);
    if (!a) return std::nullopt;
    return a->toRectangle();
}
std::optional<float> PdfDictionary::getAsFloat(const PdfNamePtr& key) const {
    auto n = getAsNumber(key);
    if (!n) return std::nullopt;
    return n->floatValue();
}
std::optional<int> PdfDictionary::getAsInt(const PdfNamePtr& key) const {
    auto n = getAsNumber(key);
    if (!n) return std::nullopt;
    return n->intValue();
}
std::optional<bool> PdfDictionary::getAsBool(const PdfNamePtr& key) const {
    auto b = getAsBoolean(key);
    if (!b) return std::nullopt;
    return b->getValue();
}

PdfObjectPtr PdfDictionary::put(const PdfNamePtr& key, PdfObjectPtr value) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        PdfObjectPtr old = it->second;
        it->second = std::move(value);
        return old;
    }
    map_.emplace(key, std::move(value));
    return nullptr;
}

PdfObjectPtr PdfDictionary::remove(const PdfNamePtr& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;
    PdfObjectPtr old = it->second;
    map_.erase(it);
    return old;
}

void PdfDictionary::putAll(const PdfDictionary& d) {
    for (const auto& kv : d.map_) map_[kv.first] = kv.second;
}

std::vector<PdfNamePtr> PdfDictionary::keySet() const {
    std::vector<PdfNamePtr> keys;
    keys.reserve(map_.size());
    for (const auto& kv : map_) keys.push_back(kv.first);
    return keys;
}

std::vector<PdfObjectPtr> PdfDictionary::values(bool asDirects) const {
    std::vector<PdfObjectPtr> v;
    v.reserve(map_.size());
    for (const auto& kv : map_) {
        if (asDirects && kv.second && kv.second->getType() == INDIRECT_REFERENCE)
            v.push_back(static_cast<PdfIndirectReference*>(kv.second.get())->getRefersToPtr(true));
        else
            v.push_back(kv.second);
    }
    return v;
}

std::string PdfDictionary::toString() const {
    if (!isFlushed()) {
        std::string s = "<<";
        for (const auto& kv : map_) {
            PdfIndirectReference* ref = kv.second ? kv.second->getIndirectReference() : nullptr;
            std::string value = kv.second == nullptr ? std::string("null") : ref == nullptr ? kv.second->toString() : ref->toString();
            s += kv.first->toString() + " " + value + " ";
        }
        s += ">>";
        return s;
    }
    return indirectReference_->toString();
}

bool PdfDictionary::equals(const PdfObject& o) const {
    if (this == &o) return true;
    if (o.getType() != getType()) return false;
    const auto& that = static_cast<const PdfDictionary&>(o);
    if (that.size() != size()) return false;
    for (const auto& kv : map_) {
        auto it = that.map_.find(kv.first);
        if (it == that.map_.end()) return false;
        if (!equalContent(kv.second.get(), it->second.get())) return false;
    }
    return true;
}

PdfDictionaryPtr PdfDictionary::clone(const std::vector<PdfNamePtr>& excludeKeys) const {
    Map excluded;
    auto self = const_cast<PdfDictionary*>(this);
    for (const auto& key : excludeKeys) {
        auto it = self->map_.find(key);
        if (it != self->map_.end()) {
            excluded.insert(*it);
            self->map_.erase(it);
        }
    }
    PdfDictionaryPtr dictionary;
    try {
        dictionary = std::static_pointer_cast<PdfDictionary>(PdfObject::clone());
    } catch (...) {
        for (const auto& kv : excluded) self->map_.insert(kv);
        throw;
    }
    for (const auto& kv : excluded) self->map_.insert(kv);
    return dictionary;
}

PdfDictionaryPtr PdfDictionary::copyTo(PdfDocument* document, const std::vector<PdfNamePtr>& excludeKeys,
                                       bool allowDuplicating) const {
    Map excluded;
    auto self = const_cast<PdfDictionary*>(this);
    for (const auto& key : excludeKeys) {
        auto it = self->map_.find(key);
        if (it != self->map_.end()) {
            excluded.insert(*it);
            self->map_.erase(it);
        }
    }
    PdfDictionaryPtr dictionary;
    try {
        dictionary = std::static_pointer_cast<PdfDictionary>(PdfObject::copyTo(document, allowDuplicating));
    } catch (...) {
        for (const auto& kv : excluded) self->map_.insert(kv);
        throw;
    }
    for (const auto& kv : excluded) self->map_.insert(kv);
    return dictionary;
}

void PdfDictionary::mergeDifferent(const PdfDictionary& other) {
    for (const auto& kv : other.map_)
        if (!containsKey(kv.first)) put(kv.first, kv.second);
}

void PdfDictionary::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfObject::copyContent(from, document, filter);
    const auto& dictionary = static_cast<const PdfDictionary&>(from);
    for (const auto& kv : dictionary.map_) {
        if (filter.shouldProcess(this, kv.first.get(), kv.second.get()))
            map_[kv.first] = kv.second->processCopying(document, false, filter);
    }
}

// ============================================================ PdfStream
PdfStream::PdfStream(const Bytes& bytes, int compressionLevel) : compressionLevel_(compressionLevel) {
    setState(MUST_BE_INDIRECT);
    baos_ = std::make_shared<io::ByteArrayOutputStream>(bytes.size());
    outputStream_ = std::make_unique<PdfOutputStream>(baos_);
    if (!bytes.empty()) outputStream_->writeBytes(bytes);
}

PdfStream::PdfStream(int64_t offset, const PdfDictionary& keys) : offset_(offset) {
    compressionLevel_ = CompressionConstants::UNDEFINED_COMPRESSION;
    putAll(keys);
    auto length = getAsNumber(PdfName::Length);
    length_ = length ? length->intValue() : 0;
}

PdfStream::PdfStream(std::shared_ptr<io::ByteArrayOutputStream> outputStream) {
    baos_ = std::move(outputStream);
    outputStream_ = std::make_unique<PdfOutputStream>(baos_);
    compressionLevel_ = CompressionConstants::UNDEFINED_COMPRESSION;
    setState(MUST_BE_INDIRECT);
}

PdfStream::~PdfStream() = default;

Bytes PdfStream::getBytes(bool decoded) {
    if (isFlushed()) throw PdfException(KernelExceptionMessageConstant::CANNOT_OPERATE_WITH_FLUSHED_PDF_STREAM);
    Bytes bytes;
    if (outputStream_ && baos_) {
        bytes = baos_->toByteArray();
        if (decoded && containsKey(PdfName::Filter)) bytes = PdfReader::decodeBytes(bytes, *this);
    } else if (getIndirectReference() != nullptr) {
        PdfReader* reader = getIndirectReference()->getReader();
        if (reader != nullptr) bytes = reader->readStreamBytes(*this, decoded);
    }
    return bytes;
}

void PdfStream::setData(const Bytes& bytes, bool append) {
    if (isFlushed()) throw PdfException(KernelExceptionMessageConstant::CANNOT_OPERATE_WITH_FLUSHED_PDF_STREAM);
    bool uninitialized = outputStream_ == nullptr;
    if (uninitialized) initOutputStream();
    if (append) {
        if ((uninitialized && getIndirectReference() != nullptr && getIndirectReference()->getReader() != nullptr) ||
            (!uninitialized && containsKey(PdfName::Filter))) {
            Bytes oldBytes;
            if (uninitialized) {
                // read original data from the reader (temporarily without the new output stream)
                auto savedOut = std::move(outputStream_);
                auto savedBaos = std::move(baos_);
                try {
                    oldBytes = getBytes();
                } catch (const PdfException&) {
                    throw PdfException(KernelExceptionMessageConstant::CANNOT_READ_A_STREAM_IN_ORDER_TO_APPEND_NEW_BYTES);
                }
                outputStream_ = std::move(savedOut);
                baos_ = std::move(savedBaos);
            } else {
                oldBytes = getBytes();
            }
            outputStream_->assignBytes(oldBytes, static_cast<int>(oldBytes.size()));
        }
        outputStream_->writeBytes(bytes);
    } else {
        outputStream_->assignBytes(bytes, static_cast<int>(bytes.size()));
    }
    offset_ = 0;
    remove(PdfName::Filter);
    remove(PdfName::DecodeParms);
}

void PdfStream::copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) {
    PdfDictionary::copyContent(from, document, filter);
    auto& stream = const_cast<PdfStream&>(static_cast<const PdfStream&>(from));
    Bytes bytes = stream.getBytes(false);
    if (!outputStream_) initOutputStream();
    outputStream_->writeBytes(bytes);
}

void PdfStream::initOutputStream(std::shared_ptr<io::OutputStream> stream) {
    if (outputStream_ == nullptr) {
        auto b = std::dynamic_pointer_cast<io::ByteArrayOutputStream>(stream);
        baos_ = b ? b : std::make_shared<io::ByteArrayOutputStream>();
        outputStream_ = std::make_unique<PdfOutputStream>(baos_);
    }
}

void PdfStream::releaseContent() {
    PdfDictionary::releaseContent();
    outputStream_.reset();
    baos_.reset();
}

// ============================================================ static names
namespace {
const std::map<std::string, PdfNamePtr>& staticNames() {
    static const std::map<std::string, PdfNamePtr> names = [] {
        std::map<std::string, PdfNamePtr> m;
        for (const PdfNamePtr* p : PdfName::allStaticNames()) m[(*p)->getValue()] = *p;
        return m;
    }();
    return names;
}
}  // namespace

}  // namespace kernel
}  // namespace pdfcpp
