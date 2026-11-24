#ifndef PDFCPP_KERNEL_PDF_PDF_OBJECT_H_
#define PDFCPP_KERNEL_PDF_PDF_OBJECT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pdfcpp/io/source/byte_buffer.h"
#include "pdfcpp/kernel/exceptions/pdf_exception.h"
#include "pdfcpp/kernel/geom/geom.h"

namespace pdfcpp {
namespace io {
class ByteArrayOutputStream;
class OutputStream;
}  // namespace io
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
  virtual bool ShouldProcess(PdfObject* new_parent, PdfName* name,
                             PdfObject* value) = 0;
};

/// Copy filter which accepts everything (port of NullCopyFilter).
class NullCopyFilter : public ICopyFilter {
 public:
  static NullCopyFilter& GetInstance();
  bool ShouldProcess(PdfObject*, PdfName*, PdfObject*) override { return true; }
};

/// Base class of all PDF objects (port of com.itextpdf.kernel.pdf.PdfObject).
///
/// Memory model: objects are owned by std::shared_ptr. An indirect object keeps
/// a raw pointer to its PdfIndirectReference, which is owned by the document's
/// xref table. References own the object they refer to (strong pointer); the
/// document clears those links on close to break cycles.
class PdfObject : public std::enable_shared_from_this<PdfObject> {
 public:
  static constexpr uint8_t kArray = 1;
  static constexpr uint8_t kBoolean = 2;
  static constexpr uint8_t kDictionary = 3;
  static constexpr uint8_t kLiteral = 4;
  static constexpr uint8_t kIndirectReference = 5;
  static constexpr uint8_t kName = 6;
  static constexpr uint8_t kNull = 7;
  static constexpr uint8_t kNumber = 8;
  static constexpr uint8_t kStream = 9;
  static constexpr uint8_t kString = 10;

  static constexpr uint16_t kFlushed = 1;
  static constexpr uint16_t kFree = 1 << 1;
  static constexpr uint16_t kReading = 1 << 2;
  static constexpr uint16_t kModified = 1 << 3;
  static constexpr uint16_t kOriginalObjectStream = 1 << 4;
  static constexpr uint16_t kMustBeFlushed = 1 << 5;
  static constexpr uint16_t kMustBeIndirect = 1 << 6;
  static constexpr uint16_t kForbidRelease = 1 << 7;
  static constexpr uint16_t kReadOnly = 1 << 8;
  static constexpr uint16_t kUnencrypted = 1 << 9;

  virtual ~PdfObject() = default;
  PdfObject(const PdfObject&) = delete;
  PdfObject& operator=(const PdfObject&) = delete;

  virtual uint8_t GetType() const = 0;

  void Flush(bool can_be_in_obj_stm = true);
  PdfIndirectReference* GetIndirectReference() const {
    return indirect_reference_;
  }
  bool ContainsIndirectReference() const;
  bool IsIndirect() const;
  virtual PdfObject* MakeIndirect(PdfDocument* document,
                                  PdfIndirectReference* reference = nullptr);
  bool IsFlushed() const;
  bool IsModified() const;
  PdfObjectPtr Clone() const { return Clone(NullCopyFilter::GetInstance()); }
  PdfObjectPtr Clone(ICopyFilter& filter) const;
  PdfObjectPtr CopyTo(PdfDocument* document,
                      bool allow_duplicating = true) const {
    return CopyTo(document, allow_duplicating, NullCopyFilter::GetInstance());
  }
  PdfObjectPtr CopyTo(PdfDocument* document, bool allow_duplicating,
                      ICopyFilter& copy_filter) const;
  PdfObject* SetModified();
  bool IsReleaseForbidden() const { return CheckState(kForbidRelease); }
  void Release();

  bool IsNull() const { return GetType() == kNull; }
  bool IsBoolean() const { return GetType() == kBoolean; }
  bool IsNumber() const { return GetType() == kNumber; }
  bool IsString() const { return GetType() == kString; }
  bool IsName() const { return GetType() == kName; }
  bool IsArray() const { return GetType() == kArray; }
  bool IsDictionary() const { return GetType() == kDictionary; }
  bool IsStream() const { return GetType() == kStream; }
  bool IsIndirectReference() const { return GetType() == kIndirectReference; }
  bool IsLiteral() const { return GetType() == kLiteral; }

  virtual std::string ToString() const = 0;
  /// Structural equality (Java equals()).
  virtual bool Equals(const PdfObject& other) const { return this == &other; }

  bool CheckState(uint16_t state) const { return (state_ & state) == state; }
  PdfObject* SetState(uint16_t state) {
    state_ |= state;
    return this;
  }
  PdfObject* ClearState(uint16_t state) {
    state_ &= static_cast<uint16_t>(~state);
    return this;
  }

  virtual PdfObject* SetIndirectReference(
      PdfIndirectReference* indirect_reference) {
    indirect_reference_ = indirect_reference;
    return this;
  }

  /// shared_ptr to this object (objects must be created via std::make_shared).
  PdfObjectPtr Self() { return shared_from_this(); }
  std::shared_ptr<const PdfObject> Self() const { return shared_from_this(); }

  static bool EqualContent(const PdfObject* obj1, const PdfObject* obj2);

  // Internal API (used by reader/writer/copying)
  virtual PdfObjectPtr NewInstance() const = 0;
  virtual void CopyContent(const PdfObject& from, PdfDocument* document,
                           ICopyFilter& filter);
  PdfObjectPtr ProcessCopying(PdfDocument* document_to, bool allow_duplicating,
                              ICopyFilter& filter) const;
  virtual void ReleaseContent() {}

 protected:
  PdfObject() = default;
  PdfIndirectReference* indirect_reference_ = nullptr;

 private:
  uint16_t state_ = 0;
};

/// Port of PdfPrimitiveObject: objects with a byte content representation.
class PdfPrimitiveObject : public PdfObject {
 public:
  PdfObject* MakeIndirect(PdfDocument* document,
                          PdfIndirectReference* reference = nullptr) override;
  PdfObject* SetIndirectReference(
      PdfIndirectReference* indirect_reference) override;
  const Bytes& GetInternalContent() const;
  bool HasContent() const { return content_set_; }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;
  int CompareContent(const PdfPrimitiveObject& o) const;

 protected:
  PdfPrimitiveObject() = default;
  explicit PdfPrimitiveObject(bool direct_only) : direct_only_(direct_only) {}
  explicit PdfPrimitiveObject(Bytes content)
      : content_(std::move(content)), content_set_(true) {}
  virtual void GenerateContent() const = 0;
  void SetContent(Bytes content) const {
    content_ = std::move(content);
    content_set_ = true;
  }
  void ClearContent() const {
    content_.clear();
    content_set_ = false;
  }
  mutable Bytes content_;
  mutable bool content_set_ = false;
  bool direct_only_ = false;
};

/// Port of PdfNull.
class PdfNull : public PdfPrimitiveObject {
 public:
  static const PdfNullPtr kPdfNull;
  PdfNull() = default;
  uint8_t GetType() const override { return kNull; }
  std::string ToString() const override { return "null"; }
  bool Equals(const PdfObject& o) const override {
    return o.GetType() == kNull;
  }
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfNull>();
  }

 protected:
  void GenerateContent() const override { SetContent(ToBytes("null")); }
};

/// Port of PdfBoolean.
class PdfBoolean : public PdfPrimitiveObject {
 public:
  static const PdfBooleanPtr kTrueValue;
  static const PdfBooleanPtr kFalseValue;
  static PdfBooleanPtr ValueOf(bool value) {
    return value ? kTrueValue : kFalseValue;
  }

  explicit PdfBoolean(bool value = false) : value_(value) {}
  PdfBoolean(bool value, bool direct_only)
      : PdfPrimitiveObject(direct_only), value_(value) {}
  bool GetValue() const { return value_; }
  uint8_t GetType() const override { return kBoolean; }
  std::string ToString() const override { return value_ ? "true" : "false"; }
  bool Equals(const PdfObject& o) const override {
    return o.GetType() == kBoolean &&
           static_cast<const PdfBoolean&>(o).value_ == value_;
  }
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfBoolean>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;

 protected:
  void GenerateContent() const override {
    SetContent(ToBytes(value_ ? "true" : "false"));
  }

 private:
  bool value_;
};

/// Port of PdfNumber.
class PdfNumber : public PdfPrimitiveObject {
 public:
  explicit PdfNumber(double value) { SetValue(value); }
  explicit PdfNumber(float value) { SetValue(static_cast<double>(value)); }
  explicit PdfNumber(int value) { SetValue(value); }
  explicit PdfNumber(long long value) { SetValue(static_cast<double>(value)); }
  explicit PdfNumber(int64_t value, bool) {
    SetValue(static_cast<double>(value));
  }
  explicit PdfNumber(const Bytes& content);
  PdfNumber() = default;

  uint8_t GetType() const override { return kNumber; }
  double GetValue() const;
  double DoubleValue() const { return GetValue(); }
  float FloatValue() const { return static_cast<float>(GetValue()); }
  int64_t LongValue() const { return static_cast<int64_t>(GetValue()); }
  int IntValue() const;
  void SetValue(int value);
  void SetValue(double value);
  void Increment() { SetValue(GetValue() + 1); }
  void Decrement() { SetValue(GetValue() - 1); }
  std::string ToString() const override;
  bool Equals(const PdfObject& o) const override;
  bool HasDecimalPoint() const {
    return ToString().find('.') != std::string::npos;
  }
  bool IsDoubleNumber() const { return is_double_; }
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfNumber>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;

 protected:
  void GenerateContent() const override;
  void GenerateValue() const;

 private:
  mutable double value_ = 0;
  mutable bool is_double_ = false;
  mutable bool value_valid_ = true;
};

/// Port of PdfName. `GetValue()` returns the decoded name bytes (usually
/// ASCII).
class PdfName : public PdfPrimitiveObject {
 public:
  explicit PdfName(std::string value)
      : value_(std::move(value)), value_set_(true) {}
  explicit PdfName(const char* value) : PdfName(std::string(value)) {}
  explicit PdfName(const Bytes& content) : PdfPrimitiveObject(content) {}
  PdfName() = default;

  uint8_t GetType() const override { return PdfObject::kName; }
  const std::string& GetValue() const;
  int CompareTo(const PdfName& o) const {
    return GetValue().compare(o.GetValue());
  }
  bool Equals(const PdfObject& o) const override {
    return o.GetType() == PdfObject::kName &&
           static_cast<const PdfName&>(o).GetValue() == GetValue();
  }
  bool operator==(const PdfName& o) const { return GetValue() == o.GetValue(); }
  bool operator!=(const PdfName& o) const { return !(*this == o); }
  bool operator<(const PdfName& o) const { return GetValue() < o.GetValue(); }
  std::string ToString() const override;
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfName>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;

  /// Returns the predefined static name with that value, or a new name.
  static PdfNamePtr Of(const std::string& value);
  static PdfNamePtr CreateDirectName(const std::string& name);
  /// All predefined names (PdfName.staticNames in Java).
  static const std::vector<const PdfNamePtr*>& AllStaticNames();

#include "pdfcpp/kernel/pdf/pdf_name_constants.inc"

 protected:
  void GenerateContent() const override;
  void GenerateValue() const;

 private:
  PdfName(std::string value, bool direct_only)
      : PdfPrimitiveObject(direct_only),
        value_(std::move(value)),
        value_set_(true) {}
  mutable std::string value_;
  mutable bool value_set_ = false;
};

/// Compares PdfName pointers by value (TreeMap ordering in Java).
struct PdfNameLess {
  bool operator()(const PdfNamePtr& a, const PdfNamePtr& b) const {
    return a->GetValue() < b->GetValue();
  }
};

inline bool NameEquals(const PdfObject* o, const PdfNamePtr& n) {
  return o && n && o->GetType() == PdfObject::kName &&
         static_cast<const PdfName*>(o)->GetValue() == n->GetValue();
}
inline bool NameEquals(const PdfObjectPtr& o, const PdfNamePtr& n) {
  return NameEquals(o.get(), n);
}

/// Port of PdfString.
/// The textual value (GetValue) is UTF-8. GetValueBytes() gives the raw encoded
/// bytes.
class PdfString : public PdfPrimitiveObject {
 public:
  /// Creates a string from text (UTF-8) to be encoded with the given encoding
  /// ("" = PDFDocEncoding/bytes).
  PdfString(std::string value, std::string encoding)
      : value_(std::move(value)),
        value_set_(true),
        encoding_(std::move(encoding)) {}
  explicit PdfString(const std::string& value) : PdfString(value, "") {}
  explicit PdfString(const char* value) : PdfString(std::string(value), "") {}
  /// Creates a string from raw bytes (each byte becomes a Latin-1 char of the
  /// value).
  explicit PdfString(const Bytes& content);
  /// Creates a string from encoded content as it appears in the file (escaped
  /// or hex).
  PdfString(const Bytes& content, bool hex_writing);
  PdfString() = default;

  uint8_t GetType() const override { return kString; }
  bool IsHexWriting() const { return hex_writing_; }
  PdfString* SetHexWriting(bool hex_writing);
  /// The value as a Java-style string (UTF-8 here). For byte strings each byte
  /// maps to U+00XX.
  const std::string& GetValue() const;
  const std::string& GetEncoding() const { return encoding_; }
  /// Decodes the string as a PDF text string (UTF-16BE BOM, UTF-8 BOM or
  /// PDFDocEncoding). Returns UTF-8.
  std::string ToUnicodeString() const;
  /// Raw (decrypted, unescaped) bytes of the string.
  Bytes GetValueBytes() const;
  bool Equals(const PdfObject& o) const override;
  std::string ToString() const override;
  void MarkAsUnencryptedObject() { SetState(kUnencrypted); }
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfString>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;

  void SetDecryption(int decrypt_info_num, int decrypt_info_gen,
                     PdfEncryption* decryption);
  bool Encrypt(PdfEncryption* encrypt);
  Bytes DecodeContent() const;

 protected:
  void GenerateValue() const;
  void GenerateContent() const override;
  Bytes EncodeBytes(const Bytes& bytes) const;

 private:
  mutable std::string value_;
  mutable bool value_set_ = false;
  std::string encoding_;
  bool hex_writing_ = false;
  int decrypt_info_num_ = 0;
  int decrypt_info_gen_ = 0;
  mutable PdfEncryption* decryption_ = nullptr;
};

/// Port of PdfLiteral: raw bytes written verbatim.
class PdfLiteral : public PdfPrimitiveObject {
 public:
  explicit PdfLiteral(Bytes content) : PdfPrimitiveObject(std::move(content)) {}
  explicit PdfLiteral(int size) : PdfPrimitiveObject(Bytes(size, ' ')) {}
  explicit PdfLiteral(const std::string& content)
      : PdfPrimitiveObject(ToBytes(content)) {}
  PdfLiteral() = default;
  uint8_t GetType() const override { return kLiteral; }
  std::string ToString() const override {
    return content_set_ ? ToLatin1String(content_) : std::string();
  }
  void SetPosition(int64_t position) { position_ = position; }
  int64_t GetPosition() const { return position_; }
  int GetBytesCount() const { return static_cast<int>(content_.size()); }
  bool Equals(const PdfObject& o) const override {
    return o.GetType() == kLiteral &&
           static_cast<const PdfLiteral&>(o).content_ == content_;
  }
  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfLiteral>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;

 protected:
  void GenerateContent() const override {}

 private:
  int64_t position_ = 0;
};

/// Port of PdfIndirectReference.
class PdfIndirectReference : public PdfObject {
 public:
  PdfIndirectReference(PdfDocument* doc, int obj_nr, int gen_nr = 0)
      : obj_nr_(obj_nr), gen_nr_(gen_nr), pdf_document_(doc) {}
  PdfIndirectReference(PdfDocument* doc, int obj_nr, int gen_nr, int64_t offset)
      : obj_nr_(obj_nr),
        gen_nr_(gen_nr),
        offset_or_index_(offset),
        pdf_document_(doc) {}

  int GetObjNumber() const { return obj_nr_; }
  int GetGenNumber() const { return gen_nr_; }
  /// Resolves the referenced object, reading it lazily from the reader if
  /// needed.
  PdfObject* GetRefersTo(bool recursively = true);
  PdfObjectPtr GetRefersToPtr(bool recursively = true);
  void SetRefersTo(PdfObjectPtr refers_to) {
    refers_to_ = std::move(refers_to);
  }
  const PdfObjectPtr& RefersToRaw() const { return refers_to_; }
  int GetObjStreamNumber() const { return object_stream_number_; }
  int64_t GetOffset() const {
    return object_stream_number_ == 0 ? offset_or_index_ : -1;
  }
  int GetIndex() const {
    return object_stream_number_ == 0 ? -1 : static_cast<int>(offset_or_index_);
  }
  uint8_t GetType() const override { return kIndirectReference; }
  PdfDocument* GetDocument() const { return pdf_document_; }
  void SetFree();
  bool IsFree() const { return CheckState(kFree); }
  std::string ToString() const override;
  bool Equals(const PdfObject& o) const override;
  int CompareTo(const PdfIndirectReference& o) const;
  PdfWriter* GetWriter() const;
  PdfReader* GetReader() const;
  PdfObjectPtr NewInstance() const override;
  void CopyContent(const PdfObject&, PdfDocument*, ICopyFilter&) override {}

  void SetObjStreamNumber(int n) { object_stream_number_ = n; }
  void SetIndex(int64_t index) { offset_or_index_ = index; }
  void SetOffset(int64_t offset) {
    offset_or_index_ = offset;
    object_stream_number_ = 0;
  }
  void FixOffset(int64_t offset) {
    if (!IsFree()) offset_or_index_ = offset;
  }
  void SetGenNumber(int gen) { gen_nr_ = gen; }
  /// Shared pointer to this reference.
  PdfIndirectReferencePtr SelfRef() {
    return std::static_pointer_cast<PdfIndirectReference>(shared_from_this());
  }

 private:
  static constexpr int kLengthOfIndirectsChain = 31;
  int obj_nr_;
  int gen_nr_;
  PdfObjectPtr refers_to_;
  int object_stream_number_ = 0;
  int64_t offset_or_index_ = 0;
  PdfDocument* pdf_document_ = nullptr;
  bool resolving_reference_in_process_ = false;
};

/// Port of PdfArray.
class PdfArray : public PdfObject {
 public:
  PdfArray() = default;
  explicit PdfArray(PdfObjectPtr obj) { list_.push_back(std::move(obj)); }
  explicit PdfArray(const Rectangle& rectangle);
  explicit PdfArray(const std::vector<PdfObjectPtr>& objects)
      : list_(objects) {}
  explicit PdfArray(const std::vector<float>& numbers);
  explicit PdfArray(const std::vector<double>& numbers);
  explicit PdfArray(const std::vector<int>& numbers);
  explicit PdfArray(const std::vector<bool>& values);
  PdfArray(const std::vector<std::string>& strings, bool as_names);

  int size() const { return static_cast<int>(list_.size()); }
  bool IsEmpty() const { return list_.empty(); }
  bool Contains(const PdfObject* o) const;
  bool Contains(const PdfObjectPtr& o) const { return Contains(o.get()); }
  void Add(PdfObjectPtr pdf_object) { list_.push_back(std::move(pdf_object)); }
  void Add(int index, PdfObjectPtr element) {
    list_.insert(list_.begin() + index, std::move(element));
  }
  PdfObjectPtr Set(int index, PdfObjectPtr element);
  void AddAll(const std::vector<PdfObjectPtr>& c) {
    list_.insert(list_.end(), c.begin(), c.end());
  }
  void AddAll(const PdfArray& a) {
    list_.insert(list_.end(), a.list_.begin(), a.list_.end());
  }
  /// Returns the element; indirect references are resolved when as_direct is
  /// true.
  PdfObjectPtr Get(int index, bool as_direct = true) const;
  void Remove(int index) { list_.erase(list_.begin() + index); }
  void Remove(const PdfObject* o);
  void clear() { list_.clear(); }
  int IndexOf(const PdfObject* o) const;
  std::vector<PdfObjectPtr> SubList(int from_index, int to_index) const {
    return std::vector<PdfObjectPtr>(list_.begin() + from_index,
                                     list_.begin() + to_index);
  }
  const std::vector<PdfObjectPtr>& ToList() const { return list_; }
  std::vector<PdfObjectPtr>& GetList() { return list_; }

  uint8_t GetType() const override { return kArray; }
  std::string ToString() const override;
  bool Equals(const PdfObject& o) const override;

  PdfArrayPtr GetAsArray(int index) const;
  PdfDictionaryPtr GetAsDictionary(int index) const;
  PdfStreamPtr GetAsStream(int index) const;
  PdfNumberPtr GetAsNumber(int index) const;
  PdfNamePtr GetAsName(int index) const;
  PdfStringPtr GetAsString(int index) const;
  PdfBooleanPtr GetAsBoolean(int index) const;
  Rectangle ToRectangle() const;
  std::vector<float> ToFloatArray() const;
  std::vector<double> ToDoubleArray() const;
  std::vector<int64_t> ToLongArray() const;
  std::vector<int> ToIntArray() const;
  std::vector<bool> ToBooleanArray() const;

  std::vector<PdfObjectPtr>::const_iterator begin() const {
    return list_.begin();
  }
  std::vector<PdfObjectPtr>::const_iterator end() const { return list_.end(); }

  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfArray>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;
  void ReleaseContent() override { list_.clear(); }

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
  bool IsEmpty() const { return map_.empty(); }
  bool ContainsKey(const PdfNamePtr& key) const {
    return map_.find(key) != map_.end();
  }
  bool ContainsValue(const PdfObject* value) const;
  /// Returns the value; indirect references are resolved when as_direct is
  /// true.
  PdfObjectPtr Get(const PdfNamePtr& key, bool as_direct = true) const;
  PdfArrayPtr GetAsArray(const PdfNamePtr& key) const;
  PdfDictionaryPtr GetAsDictionary(const PdfNamePtr& key) const;
  PdfStreamPtr GetAsStream(const PdfNamePtr& key) const;
  PdfNumberPtr GetAsNumber(const PdfNamePtr& key) const;
  PdfNamePtr GetAsName(const PdfNamePtr& key) const;
  PdfStringPtr GetAsString(const PdfNamePtr& key) const;
  PdfBooleanPtr GetAsBoolean(const PdfNamePtr& key) const;
  std::optional<Rectangle> GetAsRectangle(const PdfNamePtr& key) const;
  std::optional<float> GetAsFloat(const PdfNamePtr& key) const;
  std::optional<int> GetAsInt(const PdfNamePtr& key) const;
  std::optional<bool> GetAsBool(const PdfNamePtr& key) const;
  /// Puts the value; returns the previous value or nullptr.
  PdfObjectPtr Put(const PdfNamePtr& key, PdfObjectPtr value);
  PdfObjectPtr Remove(const PdfNamePtr& key);
  void PutAll(const PdfDictionary& d);
  void clear() { map_.clear(); }
  std::vector<PdfNamePtr> KeySet() const;
  std::vector<PdfObjectPtr> Values(bool as_directs = true) const;
  const Map& EntrySet() const { return map_; }
  Map& GetMap() { return map_; }

  uint8_t GetType() const override { return kDictionary; }
  std::string ToString() const override;
  bool Equals(const PdfObject& o) const override;
  PdfDictionaryPtr Clone(const std::vector<PdfNamePtr>& exclude_keys) const;
  PdfDictionaryPtr CopyTo(PdfDocument* document,
                          const std::vector<PdfNamePtr>& exclude_keys,
                          bool allow_duplicating) const;
  using PdfObject::Clone;
  using PdfObject::CopyTo;
  void MergeDifferent(const PdfDictionary& other);

  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfDictionary>();
  }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;
  void ReleaseContent() override { map_.clear(); }

 private:
  Map map_;
};

/// Compression level constants (port of CompressionConstants).
namespace compression_constants {
inline constexpr int kUndefinedCompression = -0x7fffffff - 1;
inline constexpr int kDefaultCompression = -1;
inline constexpr int kNoCompression = 0;
inline constexpr int kBestSpeed = 1;
inline constexpr int kBestCompression = 9;
}  // namespace compression_constants

/// Port of PdfStream.
class PdfStream : public PdfDictionary {
 public:
  PdfStream(const Bytes& bytes, int compression_level);
  explicit PdfStream(const Bytes& bytes)
      : PdfStream(bytes, compression_constants::kUndefinedCompression) {}
  explicit PdfStream(int compression_level)
      : PdfStream(Bytes(), compression_level) {}
  PdfStream() : PdfStream(Bytes()) {}
  /// Used by the reader: a stream whose data lives at `offset` in the source
  /// file.
  PdfStream(int64_t offset, const PdfDictionary& keys);
  ~PdfStream() override;

  PdfOutputStream* GetOutputStream() const { return output_stream_.get(); }
  int GetCompressionLevel() const { return compression_level_; }
  void SetCompressionLevel(int compression_level) {
    compression_level_ = compression_level;
  }
  uint8_t GetType() const override { return kStream; }
  int GetLength() const { return length_; }
  /// Returns the stream data (decoded through its filters when `decoded` is
  /// true).
  Bytes GetBytes(bool decoded = true);
  void SetData(const Bytes& bytes, bool append = false);
  std::string ToString() const override { return PdfDictionary::ToString(); }

  PdfObjectPtr NewInstance() const override {
    return std::make_shared<PdfStream>();
  }
  int64_t GetOffset() const { return offset_; }
  void UpdateLength(int length) { length_ = length; }
  void CopyContent(const PdfObject& from, PdfDocument* document,
                   ICopyFilter& filter) override;
  void InitOutputStream(std::shared_ptr<io::OutputStream> stream = nullptr);
  void ReleaseContent() override;
  /// Raw in-memory bytes written so far (nullptr if the stream is backed by a
  /// reader).
  io::ByteArrayOutputStream* GetByteArrayOutputStream() const {
    return baos_.get();
  }

 protected:
  explicit PdfStream(std::shared_ptr<io::ByteArrayOutputStream> output_stream);
  int compression_level_ = compression_constants::kUndefinedCompression;
  std::shared_ptr<io::ByteArrayOutputStream> baos_;
  std::unique_ptr<PdfOutputStream> output_stream_;

 private:
  int64_t offset_ = 0;
  int length_ = -1;
};

// ------------------------------------------------------------ helpers
template <typename T>
std::shared_ptr<T> PdfCast(const PdfObjectPtr& o);

template <>
inline std::shared_ptr<PdfDictionary> PdfCast<PdfDictionary>(
    const PdfObjectPtr& o) {
  return (o && (o->GetType() == PdfObject::kDictionary ||
                o->GetType() == PdfObject::kStream))
             ? std::static_pointer_cast<PdfDictionary>(o)
             : nullptr;
}
#define PDFCPP_DEFINE_CAST(T, TYPE)                             \
  template <>                                                   \
  inline std::shared_ptr<T> PdfCast<T>(const PdfObjectPtr& o) { \
    return (o && o->GetType() == PdfObject::TYPE)               \
               ? std::static_pointer_cast<T>(o)                 \
               : nullptr;                                       \
  }
PDFCPP_DEFINE_CAST(PdfArray, kArray)
PDFCPP_DEFINE_CAST(PdfStream, kStream)
PDFCPP_DEFINE_CAST(PdfNumber, kNumber)
PDFCPP_DEFINE_CAST(PdfName, kName)
PDFCPP_DEFINE_CAST(PdfString, kString)
PDFCPP_DEFINE_CAST(PdfBoolean, kBoolean)
PDFCPP_DEFINE_CAST(PdfNull, kNull)
PDFCPP_DEFINE_CAST(PdfLiteral, kLiteral)
PDFCPP_DEFINE_CAST(PdfIndirectReference, kIndirectReference)
#undef PDFCPP_DEFINE_CAST

/// Convenience factories.
inline PdfNumberPtr MakeNumber(int v) { return std::make_shared<PdfNumber>(v); }
inline PdfNumberPtr MakeNumber(double v) {
  return std::make_shared<PdfNumber>(v);
}
inline PdfStringPtr MakeString(const std::string& v) {
  return std::make_shared<PdfString>(v);
}

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_OBJECT_H_
