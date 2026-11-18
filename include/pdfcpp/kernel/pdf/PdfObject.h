#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/source/ByteBuffer.h"
#include "pdfcpp/kernel/exceptions/PdfException.h"
#include "pdfcpp/kernel/geom/Geom.h"

namespace pdfcpp {
namespace io {
class ByteArrayOutputStream;
class OutputStream;
}
namespace kernel {

class PdfDocument;
class PdfReader;
class PdfWriter;
class PdfEncryption;
class PdfOutputStream;
class PdfObject;
class PdfPrimitiveObject;
class PdfIndirectReference;
class PdfName;
class PdfNumber;
class PdfString;
class PdfBoolean;
class PdfNull;
class PdfLiteral;
class PdfArray;
class PdfDictionary;
class PdfStream;

using PdfObjectPtr = std::shared_ptr<PdfObject>;
using PdfNamePtr = std::shared_ptr<PdfName>;
using PdfNumberPtr = std::shared_ptr<PdfNumber>;
using PdfStringPtr = std::shared_ptr<PdfString>;
using PdfBooleanPtr = std::shared_ptr<PdfBoolean>;
using PdfNullPtr = std::shared_ptr<PdfNull>;
using PdfLiteralPtr = std::shared_ptr<PdfLiteral>;
using PdfArrayPtr = std::shared_ptr<PdfArray>;
using PdfDictionaryPtr = std::shared_ptr<PdfDictionary>;
using PdfStreamPtr = std::shared_ptr<PdfStream>;
using PdfIndirectReferencePtr = std::shared_ptr<PdfIndirectReference>;

/// Filter applied when copying objects (port of ICopyFilter).
class ICopyFilter {
public:
    virtual ~ICopyFilter() = default;
    /// Returns true if the given (key/index, value) of `parent` should be copied.
    virtual bool shouldProcess(PdfObject* newParent, PdfName* name, PdfObject* value) = 0;
};

/// Copy filter which accepts everything (port of NullCopyFilter).
class NullCopyFilter : public ICopyFilter {
public:
    static NullCopyFilter& getInstance();
    bool shouldProcess(PdfObject*, PdfName*, PdfObject*) override { return true; }
};

/// Base class of all PDF objects (port of com.itextpdf.kernel.pdf.PdfObject).
///
/// Memory model: objects are owned by std::shared_ptr. An indirect object keeps a raw pointer to
/// its PdfIndirectReference, which is owned by the document's xref table. References own the
/// object they refer to (strong pointer); the document clears those links on close to break cycles.
class PdfObject : public std::enable_shared_from_this<PdfObject> {
public:
    static constexpr uint8_t ARRAY = 1;
    static constexpr uint8_t BOOLEAN = 2;
    static constexpr uint8_t DICTIONARY = 3;
    static constexpr uint8_t LITERAL = 4;
    static constexpr uint8_t INDIRECT_REFERENCE = 5;
    static constexpr uint8_t NAME = 6;
    static constexpr uint8_t NULL_ = 7;
    static constexpr uint8_t NUMBER = 8;
    static constexpr uint8_t STREAM = 9;
    static constexpr uint8_t STRING = 10;

    static constexpr uint16_t FLUSHED = 1;
    static constexpr uint16_t FREE = 1 << 1;
    static constexpr uint16_t READING = 1 << 2;
    static constexpr uint16_t MODIFIED = 1 << 3;
    static constexpr uint16_t ORIGINAL_OBJECT_STREAM = 1 << 4;
    static constexpr uint16_t MUST_BE_FLUSHED = 1 << 5;
    static constexpr uint16_t MUST_BE_INDIRECT = 1 << 6;
    static constexpr uint16_t FORBID_RELEASE = 1 << 7;
    static constexpr uint16_t READ_ONLY = 1 << 8;
    static constexpr uint16_t UNENCRYPTED = 1 << 9;

    virtual ~PdfObject() = default;
    PdfObject(const PdfObject&) = delete;
    PdfObject& operator=(const PdfObject&) = delete;

    virtual uint8_t getType() const = 0;

    void flush(bool canBeInObjStm = true);
    PdfIndirectReference* getIndirectReference() const { return indirectReference_; }
    bool containsIndirectReference() const;
    bool isIndirect() const;
    virtual PdfObject* makeIndirect(PdfDocument* document, PdfIndirectReference* reference = nullptr);
    bool isFlushed() const;
    bool isModified() const;
    PdfObjectPtr clone() const { return clone(NullCopyFilter::getInstance()); }
    PdfObjectPtr clone(ICopyFilter& filter) const;
    PdfObjectPtr copyTo(PdfDocument* document, bool allowDuplicating = true) const {
        return copyTo(document, allowDuplicating, NullCopyFilter::getInstance());
    }
    PdfObjectPtr copyTo(PdfDocument* document, bool allowDuplicating, ICopyFilter& copyFilter) const;
    PdfObject* setModified();
    bool isReleaseForbidden() const { return checkState(FORBID_RELEASE); }
    void release();

    bool isNull() const { return getType() == NULL_; }
    bool isBoolean() const { return getType() == BOOLEAN; }
    bool isNumber() const { return getType() == NUMBER; }
    bool isString() const { return getType() == STRING; }
    bool isName() const { return getType() == NAME; }
    bool isArray() const { return getType() == ARRAY; }
    bool isDictionary() const { return getType() == DICTIONARY; }
    bool isStream() const { return getType() == STREAM; }
    bool isIndirectReference() const { return getType() == INDIRECT_REFERENCE; }
    bool isLiteral() const { return getType() == LITERAL; }

    virtual std::string toString() const = 0;
    /// Structural equality (Java equals()).
    virtual bool equals(const PdfObject& other) const { return this == &other; }

    bool checkState(uint16_t state) const { return (state_ & state) == state; }
    PdfObject* setState(uint16_t state) {
        state_ |= state;
        return this;
    }
    PdfObject* clearState(uint16_t state) {
        state_ &= static_cast<uint16_t>(~state);
        return this;
    }

    virtual PdfObject* setIndirectReference(PdfIndirectReference* indirectReference) {
        indirectReference_ = indirectReference;
        return this;
    }

    /// shared_ptr to this object (objects must be created via std::make_shared).
    PdfObjectPtr self() { return shared_from_this(); }
    std::shared_ptr<const PdfObject> self() const { return shared_from_this(); }

    static bool equalContent(const PdfObject* obj1, const PdfObject* obj2);

    // Internal API (used by reader/writer/copying)
    virtual PdfObjectPtr newInstance() const = 0;
    virtual void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter);
    PdfObjectPtr processCopying(PdfDocument* documentTo, bool allowDuplicating, ICopyFilter& filter) const;
    virtual void releaseContent() {}

protected:
    PdfObject() = default;
    PdfIndirectReference* indirectReference_ = nullptr;

private:
    uint16_t state_ = 0;
};

/// Port of PdfPrimitiveObject: objects with a byte content representation.
class PdfPrimitiveObject : public PdfObject {
public:
    PdfObject* makeIndirect(PdfDocument* document, PdfIndirectReference* reference = nullptr) override;
    PdfObject* setIndirectReference(PdfIndirectReference* indirectReference) override;
    const Bytes& getInternalContent() const;
    bool hasContent() const { return contentSet_; }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;
    int compareContent(const PdfPrimitiveObject& o) const;

protected:
    PdfPrimitiveObject() = default;
    explicit PdfPrimitiveObject(bool directOnly) : directOnly_(directOnly) {}
    explicit PdfPrimitiveObject(Bytes content) : content_(std::move(content)), contentSet_(true) {}
    virtual void generateContent() const = 0;
    void setContent(Bytes content) const {
        content_ = std::move(content);
        contentSet_ = true;
    }
    void clearContent() const {
        content_.clear();
        contentSet_ = false;
    }
    mutable Bytes content_;
    mutable bool contentSet_ = false;
    bool directOnly_ = false;
};

/// Port of PdfNull.
class PdfNull : public PdfPrimitiveObject {
public:
    static const PdfNullPtr PDF_NULL;
    PdfNull() = default;
    uint8_t getType() const override { return NULL_; }
    std::string toString() const override { return "null"; }
    bool equals(const PdfObject& o) const override { return o.getType() == NULL_; }
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfNull>(); }

protected:
    void generateContent() const override { setContent(toBytes("null")); }
};

/// Port of PdfBoolean.
class PdfBoolean : public PdfPrimitiveObject {
public:
    static const PdfBooleanPtr TRUE_VALUE;
    static const PdfBooleanPtr FALSE_VALUE;
    static PdfBooleanPtr valueOf(bool value) { return value ? TRUE_VALUE : FALSE_VALUE; }

    explicit PdfBoolean(bool value = false) : value_(value) {}
    PdfBoolean(bool value, bool directOnly) : PdfPrimitiveObject(directOnly), value_(value) {}
    bool getValue() const { return value_; }
    uint8_t getType() const override { return BOOLEAN; }
    std::string toString() const override { return value_ ? "true" : "false"; }
    bool equals(const PdfObject& o) const override {
        return o.getType() == BOOLEAN && static_cast<const PdfBoolean&>(o).value_ == value_;
    }
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfBoolean>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;

protected:
    void generateContent() const override { setContent(toBytes(value_ ? "true" : "false")); }

private:
    bool value_;
};

/// Port of PdfNumber.
class PdfNumber : public PdfPrimitiveObject {
public:
    explicit PdfNumber(double value) { setValue(value); }
    explicit PdfNumber(float value) { setValue(static_cast<double>(value)); }
    explicit PdfNumber(int value) { setValue(value); }
    explicit PdfNumber(long long value) { setValue(static_cast<double>(value)); }
    explicit PdfNumber(int64_t value, bool) { setValue(static_cast<double>(value)); }
    explicit PdfNumber(const Bytes& content);
    PdfNumber() = default;

    uint8_t getType() const override { return NUMBER; }
    double getValue() const;
    double doubleValue() const { return getValue(); }
    float floatValue() const { return static_cast<float>(getValue()); }
    int64_t longValue() const { return static_cast<int64_t>(getValue()); }
    int intValue() const;
    void setValue(int value);
    void setValue(double value);
    void increment() { setValue(getValue() + 1); }
    void decrement() { setValue(getValue() - 1); }
    std::string toString() const override;
    bool equals(const PdfObject& o) const override;
    bool hasDecimalPoint() const { return toString().find('.') != std::string::npos; }
    bool isDoubleNumber() const { return isDouble_; }
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfNumber>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;

protected:
    void generateContent() const override;
    void generateValue() const;

private:
    mutable double value_ = 0;
    mutable bool isDouble_ = false;
    mutable bool valueValid_ = true;
};

/// Port of PdfName. `getValue()` returns the decoded name bytes (usually ASCII).
class PdfName : public PdfPrimitiveObject {
public:
    explicit PdfName(std::string value) : value_(std::move(value)), valueSet_(true) {}
    explicit PdfName(const char* value) : PdfName(std::string(value)) {}
    explicit PdfName(const Bytes& content) : PdfPrimitiveObject(content) {}
    PdfName() = default;

    uint8_t getType() const override { return NAME; }
    const std::string& getValue() const;
    int compareTo(const PdfName& o) const { return getValue().compare(o.getValue()); }
    bool equals(const PdfObject& o) const override {
        return o.getType() == NAME && static_cast<const PdfName&>(o).getValue() == getValue();
    }
    bool operator==(const PdfName& o) const { return getValue() == o.getValue(); }
    bool operator!=(const PdfName& o) const { return !(*this == o); }
    bool operator<(const PdfName& o) const { return getValue() < o.getValue(); }
    std::string toString() const override;
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfName>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;

    /// Returns the predefined static name with that value, or a new name.
    static PdfNamePtr of(const std::string& value);
    static PdfNamePtr createDirectName(const std::string& name);
    /// All predefined names (PdfName.staticNames in Java).
    static const std::vector<const PdfNamePtr*>& allStaticNames();

#include "pdfcpp/kernel/pdf/PdfNameConstants.inc"

protected:
    void generateContent() const override;
    void generateValue() const;

private:
    PdfName(std::string value, bool directOnly) : PdfPrimitiveObject(directOnly), value_(std::move(value)), valueSet_(true) {}
    mutable std::string value_;
    mutable bool valueSet_ = false;
};

/// Compares PdfName pointers by value (TreeMap ordering in Java).
struct PdfNameLess {
    bool operator()(const PdfNamePtr& a, const PdfNamePtr& b) const { return a->getValue() < b->getValue(); }
};

inline bool nameEquals(const PdfObject* o, const PdfNamePtr& n) {
    return o && n && o->getType() == PdfObject::NAME && static_cast<const PdfName*>(o)->getValue() == n->getValue();
}
inline bool nameEquals(const PdfObjectPtr& o, const PdfNamePtr& n) { return nameEquals(o.get(), n); }

/// Port of PdfString.
/// The textual value (getValue) is UTF-8. getValueBytes() gives the raw encoded bytes.
class PdfString : public PdfPrimitiveObject {
public:
    /// Creates a string from text (UTF-8) to be encoded with the given encoding ("" = PDFDocEncoding/bytes).
    PdfString(std::string value, std::string encoding) : value_(std::move(value)), valueSet_(true), encoding_(std::move(encoding)) {}
    explicit PdfString(const std::string& value) : PdfString(value, "") {}
    explicit PdfString(const char* value) : PdfString(std::string(value), "") {}
    /// Creates a string from raw bytes (each byte becomes a Latin-1 char of the value).
    explicit PdfString(const Bytes& content);
    /// Creates a string from encoded content as it appears in the file (escaped or hex).
    PdfString(const Bytes& content, bool hexWriting);
    PdfString() = default;

    uint8_t getType() const override { return STRING; }
    bool isHexWriting() const { return hexWriting_; }
    PdfString* setHexWriting(bool hexWriting);
    /// The value as a Java-style string (UTF-8 here). For byte strings each byte maps to U+00XX.
    const std::string& getValue() const;
    const std::string& getEncoding() const { return encoding_; }
    /// Decodes the string as a PDF text string (UTF-16BE BOM, UTF-8 BOM or PDFDocEncoding). Returns UTF-8.
    std::string toUnicodeString() const;
    /// Raw (decrypted, unescaped) bytes of the string.
    Bytes getValueBytes() const;
    bool equals(const PdfObject& o) const override;
    std::string toString() const override;
    void markAsUnencryptedObject() { setState(UNENCRYPTED); }
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfString>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;

    void setDecryption(int decryptInfoNum, int decryptInfoGen, PdfEncryption* decryption);
    bool encrypt(PdfEncryption* encrypt);
    Bytes decodeContent() const;

protected:
    void generateValue() const;
    void generateContent() const override;
    Bytes encodeBytes(const Bytes& bytes) const;

private:
    mutable std::string value_;
    mutable bool valueSet_ = false;
    std::string encoding_;
    bool hexWriting_ = false;
    int decryptInfoNum_ = 0;
    int decryptInfoGen_ = 0;
    mutable PdfEncryption* decryption_ = nullptr;
};

/// Port of PdfLiteral: raw bytes written verbatim.
class PdfLiteral : public PdfPrimitiveObject {
public:
    explicit PdfLiteral(Bytes content) : PdfPrimitiveObject(std::move(content)) {}
    explicit PdfLiteral(int size) : PdfPrimitiveObject(Bytes(size, ' ')) {}
    explicit PdfLiteral(const std::string& content) : PdfPrimitiveObject(toBytes(content)) {}
    PdfLiteral() = default;
    uint8_t getType() const override { return LITERAL; }
    std::string toString() const override { return contentSet_ ? toLatin1String(content_) : std::string(); }
    void setPosition(int64_t position) { position_ = position; }
    int64_t getPosition() const { return position_; }
    int getBytesCount() const { return static_cast<int>(content_.size()); }
    bool equals(const PdfObject& o) const override {
        return o.getType() == LITERAL && static_cast<const PdfLiteral&>(o).content_ == content_;
    }
    PdfObjectPtr newInstance() const override { return std::make_shared<PdfLiteral>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;

protected:
    void generateContent() const override {}

private:
    int64_t position_ = 0;
};

/// Port of PdfIndirectReference.
class PdfIndirectReference : public PdfObject {
public:
    PdfIndirectReference(PdfDocument* doc, int objNr, int genNr = 0) : objNr_(objNr), genNr_(genNr), pdfDocument_(doc) {}
    PdfIndirectReference(PdfDocument* doc, int objNr, int genNr, int64_t offset)
        : objNr_(objNr), genNr_(genNr), offsetOrIndex_(offset), pdfDocument_(doc) {}

    int getObjNumber() const { return objNr_; }
    int getGenNumber() const { return genNr_; }
    /// Resolves the referenced object, reading it lazily from the reader if needed.
    PdfObject* getRefersTo(bool recursively = true);
    PdfObjectPtr getRefersToPtr(bool recursively = true);
    void setRefersTo(PdfObjectPtr refersTo) { refersTo_ = std::move(refersTo); }
    const PdfObjectPtr& refersToRaw() const { return refersTo_; }
    int getObjStreamNumber() const { return objectStreamNumber_; }
    int64_t getOffset() const { return objectStreamNumber_ == 0 ? offsetOrIndex_ : -1; }
    int getIndex() const { return objectStreamNumber_ == 0 ? -1 : static_cast<int>(offsetOrIndex_); }
    uint8_t getType() const override { return INDIRECT_REFERENCE; }
    PdfDocument* getDocument() const { return pdfDocument_; }
    void setFree();
    bool isFree() const { return checkState(FREE); }
    std::string toString() const override;
    bool equals(const PdfObject& o) const override;
    int compareTo(const PdfIndirectReference& o) const;
    PdfWriter* getWriter() const;
    PdfReader* getReader() const;
    PdfObjectPtr newInstance() const override;
    void copyContent(const PdfObject&, PdfDocument*, ICopyFilter&) override {}

    void setObjStreamNumber(int n) { objectStreamNumber_ = n; }
    void setIndex(int64_t index) { offsetOrIndex_ = index; }
    void setOffset(int64_t offset) {
        offsetOrIndex_ = offset;
        objectStreamNumber_ = 0;
    }
    void fixOffset(int64_t offset) {
        if (!isFree()) offsetOrIndex_ = offset;
    }
    void setGenNumber(int gen) { genNr_ = gen; }
    /// Shared pointer to this reference.
    PdfIndirectReferencePtr selfRef() { return std::static_pointer_cast<PdfIndirectReference>(shared_from_this()); }

private:
    static constexpr int LENGTH_OF_INDIRECTS_CHAIN = 31;
    int objNr_;
    int genNr_;
    PdfObjectPtr refersTo_;
    int objectStreamNumber_ = 0;
    int64_t offsetOrIndex_ = 0;
    PdfDocument* pdfDocument_ = nullptr;
    bool resolvingReferenceInProcess_ = false;
};

/// Port of PdfArray.
class PdfArray : public PdfObject {
public:
    PdfArray() = default;
    explicit PdfArray(PdfObjectPtr obj) { list_.push_back(std::move(obj)); }
    explicit PdfArray(const Rectangle& rectangle);
    explicit PdfArray(const std::vector<PdfObjectPtr>& objects) : list_(objects) {}
    explicit PdfArray(const std::vector<float>& numbers);
    explicit PdfArray(const std::vector<double>& numbers);
    explicit PdfArray(const std::vector<int>& numbers);
    explicit PdfArray(const std::vector<bool>& values);
    PdfArray(const std::vector<std::string>& strings, bool asNames);

    int size() const { return static_cast<int>(list_.size()); }
    bool isEmpty() const { return list_.empty(); }
    bool contains(const PdfObject* o) const;
    bool contains(const PdfObjectPtr& o) const { return contains(o.get()); }
    void add(PdfObjectPtr pdfObject) { list_.push_back(std::move(pdfObject)); }
    void add(int index, PdfObjectPtr element) { list_.insert(list_.begin() + index, std::move(element)); }
    PdfObjectPtr set(int index, PdfObjectPtr element);
    void addAll(const std::vector<PdfObjectPtr>& c) { list_.insert(list_.end(), c.begin(), c.end()); }
    void addAll(const PdfArray& a) { list_.insert(list_.end(), a.list_.begin(), a.list_.end()); }
    /// Returns the element; indirect references are resolved when asDirect is true.
    PdfObjectPtr get(int index, bool asDirect = true) const;
    void remove(int index) { list_.erase(list_.begin() + index); }
    void remove(const PdfObject* o);
    void clear() { list_.clear(); }
    int indexOf(const PdfObject* o) const;
    std::vector<PdfObjectPtr> subList(int fromIndex, int toIndex) const {
        return std::vector<PdfObjectPtr>(list_.begin() + fromIndex, list_.begin() + toIndex);
    }
    const std::vector<PdfObjectPtr>& toList() const { return list_; }
    std::vector<PdfObjectPtr>& getList() { return list_; }

    uint8_t getType() const override { return ARRAY; }
    std::string toString() const override;
    bool equals(const PdfObject& o) const override;

    PdfArrayPtr getAsArray(int index) const;
    PdfDictionaryPtr getAsDictionary(int index) const;
    PdfStreamPtr getAsStream(int index) const;
    PdfNumberPtr getAsNumber(int index) const;
    PdfNamePtr getAsName(int index) const;
    PdfStringPtr getAsString(int index) const;
    PdfBooleanPtr getAsBoolean(int index) const;
    Rectangle toRectangle() const;
    std::vector<float> toFloatArray() const;
    std::vector<double> toDoubleArray() const;
    std::vector<int64_t> toLongArray() const;
    std::vector<int> toIntArray() const;
    std::vector<bool> toBooleanArray() const;

    std::vector<PdfObjectPtr>::const_iterator begin() const { return list_.begin(); }
    std::vector<PdfObjectPtr>::const_iterator end() const { return list_.end(); }

    PdfObjectPtr newInstance() const override { return std::make_shared<PdfArray>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;
    void releaseContent() override { list_.clear(); }

private:
    std::vector<PdfObjectPtr> list_;
};

/// Port of PdfDictionary (sorted by key like Java's TreeMap).
class PdfDictionary : public PdfObject {
public:
    using Map = std::map<PdfNamePtr, PdfObjectPtr, PdfNameLess>;

    PdfDictionary() = default;
    explicit PdfDictionary(const Map& map) : map_(map) {}
    /// Shallow copy of another dictionary's entries.
    explicit PdfDictionary(const PdfDictionary* dictionary) {
        if (dictionary) map_ = dictionary->map_;
    }

    int size() const { return static_cast<int>(map_.size()); }
    bool isEmpty() const { return map_.empty(); }
    bool containsKey(const PdfNamePtr& key) const { return map_.find(key) != map_.end(); }
    bool containsValue(const PdfObject* value) const;
    /// Returns the value; indirect references are resolved when asDirect is true.
    PdfObjectPtr get(const PdfNamePtr& key, bool asDirect = true) const;
    PdfArrayPtr getAsArray(const PdfNamePtr& key) const;
    PdfDictionaryPtr getAsDictionary(const PdfNamePtr& key) const;
    PdfStreamPtr getAsStream(const PdfNamePtr& key) const;
    PdfNumberPtr getAsNumber(const PdfNamePtr& key) const;
    PdfNamePtr getAsName(const PdfNamePtr& key) const;
    PdfStringPtr getAsString(const PdfNamePtr& key) const;
    PdfBooleanPtr getAsBoolean(const PdfNamePtr& key) const;
    std::optional<Rectangle> getAsRectangle(const PdfNamePtr& key) const;
    std::optional<float> getAsFloat(const PdfNamePtr& key) const;
    std::optional<int> getAsInt(const PdfNamePtr& key) const;
    std::optional<bool> getAsBool(const PdfNamePtr& key) const;
    /// Puts the value; returns the previous value or nullptr.
    PdfObjectPtr put(const PdfNamePtr& key, PdfObjectPtr value);
    PdfObjectPtr remove(const PdfNamePtr& key);
    void putAll(const PdfDictionary& d);
    void clear() { map_.clear(); }
    std::vector<PdfNamePtr> keySet() const;
    std::vector<PdfObjectPtr> values(bool asDirects = true) const;
    const Map& entrySet() const { return map_; }
    Map& getMap() { return map_; }

    uint8_t getType() const override { return DICTIONARY; }
    std::string toString() const override;
    bool equals(const PdfObject& o) const override;
    PdfDictionaryPtr clone(const std::vector<PdfNamePtr>& excludeKeys) const;
    PdfDictionaryPtr copyTo(PdfDocument* document, const std::vector<PdfNamePtr>& excludeKeys, bool allowDuplicating) const;
    using PdfObject::clone;
    using PdfObject::copyTo;
    void mergeDifferent(const PdfDictionary& other);

    PdfObjectPtr newInstance() const override { return std::make_shared<PdfDictionary>(); }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;
    void releaseContent() override { map_.clear(); }

private:
    Map map_;
};

/// Compression level constants (port of CompressionConstants).
namespace CompressionConstants {
inline constexpr int UNDEFINED_COMPRESSION = -0x7fffffff - 1;
inline constexpr int DEFAULT_COMPRESSION = -1;
inline constexpr int NO_COMPRESSION = 0;
inline constexpr int BEST_SPEED = 1;
inline constexpr int BEST_COMPRESSION = 9;
}  // namespace CompressionConstants

/// Port of PdfStream.
class PdfStream : public PdfDictionary {
public:
    PdfStream(const Bytes& bytes, int compressionLevel);
    explicit PdfStream(const Bytes& bytes) : PdfStream(bytes, CompressionConstants::UNDEFINED_COMPRESSION) {}
    explicit PdfStream(int compressionLevel) : PdfStream(Bytes(), compressionLevel) {}
    PdfStream() : PdfStream(Bytes()) {}
    /// Used by the reader: a stream whose data lives at `offset` in the source file.
    PdfStream(int64_t offset, const PdfDictionary& keys);
    ~PdfStream() override;

    PdfOutputStream* getOutputStream() const { return outputStream_.get(); }
    int getCompressionLevel() const { return compressionLevel_; }
    void setCompressionLevel(int compressionLevel) { compressionLevel_ = compressionLevel; }
    uint8_t getType() const override { return STREAM; }
    int getLength() const { return length_; }
    /// Returns the stream data (decoded through its filters when `decoded` is true).
    Bytes getBytes(bool decoded = true);
    void setData(const Bytes& bytes, bool append = false);
    std::string toString() const override { return PdfDictionary::toString(); }

    PdfObjectPtr newInstance() const override { return std::make_shared<PdfStream>(); }
    int64_t getOffset() const { return offset_; }
    void updateLength(int length) { length_ = length; }
    void copyContent(const PdfObject& from, PdfDocument* document, ICopyFilter& filter) override;
    void initOutputStream(std::shared_ptr<io::OutputStream> stream = nullptr);
    void releaseContent() override;
    /// Raw in-memory bytes written so far (nullptr if the stream is backed by a reader).
    io::ByteArrayOutputStream* getByteArrayOutputStream() const { return baos_.get(); }

protected:
    explicit PdfStream(std::shared_ptr<io::ByteArrayOutputStream> outputStream);
    int compressionLevel_ = CompressionConstants::UNDEFINED_COMPRESSION;
    std::shared_ptr<io::ByteArrayOutputStream> baos_;
    std::unique_ptr<PdfOutputStream> outputStream_;

private:
    int64_t offset_ = 0;
    int length_ = -1;
};

// ------------------------------------------------------------ helpers
template <typename T>
std::shared_ptr<T> pdf_cast(const PdfObjectPtr& o);

template <>
inline std::shared_ptr<PdfDictionary> pdf_cast<PdfDictionary>(const PdfObjectPtr& o) {
    return (o && (o->getType() == PdfObject::DICTIONARY || o->getType() == PdfObject::STREAM))
               ? std::static_pointer_cast<PdfDictionary>(o)
               : nullptr;
}
#define PDFCPP_DEFINE_CAST(T, TYPE)                                                                         \
    template <>                                                                                              \
    inline std::shared_ptr<T> pdf_cast<T>(const PdfObjectPtr& o) {                                           \
        return (o && o->getType() == PdfObject::TYPE) ? std::static_pointer_cast<T>(o) : nullptr;             \
    }
PDFCPP_DEFINE_CAST(PdfArray, ARRAY)
PDFCPP_DEFINE_CAST(PdfStream, STREAM)
PDFCPP_DEFINE_CAST(PdfNumber, NUMBER)
PDFCPP_DEFINE_CAST(PdfName, NAME)
PDFCPP_DEFINE_CAST(PdfString, STRING)
PDFCPP_DEFINE_CAST(PdfBoolean, BOOLEAN)
PDFCPP_DEFINE_CAST(PdfNull, NULL_)
PDFCPP_DEFINE_CAST(PdfLiteral, LITERAL)
PDFCPP_DEFINE_CAST(PdfIndirectReference, INDIRECT_REFERENCE)
#undef PDFCPP_DEFINE_CAST

/// Convenience factories.
inline PdfNumberPtr makeNumber(int v) { return std::make_shared<PdfNumber>(v); }
inline PdfNumberPtr makeNumber(double v) { return std::make_shared<PdfNumber>(v); }
inline PdfStringPtr makeString(const std::string& v) { return std::make_shared<PdfString>(v); }

}  // namespace kernel
}  // namespace pdfcpp
