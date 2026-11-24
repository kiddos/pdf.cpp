#include "pdfcpp/kernel/pdf/pdf_document.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <set>

#include "pdfcpp/kernel/crypto/pdf_encryption.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/pdf_xref_table.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfVersion
const PdfVersion PdfVersion::kPdf10(1, 0), PdfVersion::kPdf11(1, 1),
    PdfVersion::kPdf12(1, 2), PdfVersion::kPdf13(1, 3),
    PdfVersion::kPdf14(1, 4), PdfVersion::kPdf15(1, 5),
    PdfVersion::kPdf16(1, 6), PdfVersion::kPdf17(1, 7),
    PdfVersion::kPdf20(2, 0);

PdfVersion PdfVersion::FromString(const std::string& value) {
  for (const PdfVersion* v : {&kPdf10, &kPdf11, &kPdf12, &kPdf13, &kPdf14,
                              &kPdf15, &kPdf16, &kPdf17, &kPdf20})
    if (v->ToString() == value) return *v;
  throw std::invalid_argument("The provided pdf version was not found.");
}

PdfVersion PdfVersion::FromPdfName(const PdfName& name) {
  for (const PdfVersion* v : {&kPdf10, &kPdf11, &kPdf12, &kPdf13, &kPdf14,
                              &kPdf15, &kPdf16, &kPdf17, &kPdf20})
    if (v->ToPdfName()->GetValue() == name.GetValue()) return *v;
  throw std::invalid_argument("The provided pdf version was not found.");
}

// ============================================================ PdfDate
std::string PdfDate::GetCurrentDateString() {
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
PdfDocumentInfo::PdfDocumentInfo(PdfDictionaryPtr pdf_object,
                                 PdfDocument* pdf_document)
    : info_dictionary_(std::move(pdf_object)), document_(pdf_document) {
  if (document_ && document_->GetWriter() != nullptr)
    info_dictionary_->MakeIndirect(document_);
}

PdfDocumentInfo& PdfDocumentInfo::Put(const PdfNamePtr& key,
                                      const PdfObjectPtr& value) {
  info_dictionary_->Put(key, value);
  info_dictionary_->SetModified();
  return *this;
}

std::string PdfDocumentInfo::GetStringValue(const PdfNamePtr& name) const {
  auto s = info_dictionary_->GetAsString(name);
  return s ? s->ToUnicodeString() : std::string();
}

PdfDocumentInfo& PdfDocumentInfo::AddCreationDate() {
  return Put(PdfName::kCreationDate,
             std::make_shared<PdfString>(PdfDate::GetCurrentDateString()));
}

PdfDocumentInfo& PdfDocumentInfo::RemoveCreationDate() {
  info_dictionary_->Remove(PdfName::kCreationDate);
  info_dictionary_->SetModified();
  return *this;
}

PdfDocumentInfo& PdfDocumentInfo::AddModDate() {
  return Put(PdfName::kModDate,
             std::make_shared<PdfString>(PdfDate::GetCurrentDateString()));
}

void PdfDocumentInfo::SetMoreInfo(
    const std::map<std::string, std::string>& more_info) {
  for (const auto& kv : more_info) SetMoreInfo(kv.first, kv.second);
}

void PdfDocumentInfo::SetMoreInfo(const std::string& key,
                                  const std::string& value) {
  auto key_name = std::make_shared<PdfName>(key);
  if (value.empty()) {
    info_dictionary_->Remove(key_name);
    info_dictionary_->SetModified();
  } else {
    Put(key_name, std::make_shared<PdfString>(value, IoUnicode()));
  }
}

std::string PdfDocumentInfo::GetMoreInfo(const std::string& key) const {
  return GetStringValue(std::make_shared<PdfName>(key));
}

// ============================================================ PdfResources
PdfResources::PdfResources(PdfDictionaryPtr pdf_object)
    : PdfObjectWrapper<PdfDictionary>(std::move(pdf_object)) {
  BuildResources(*GetPdfObject());
}

PdfResources::PdfResources()
    : PdfResources(std::make_shared<PdfDictionary>()) {}

void PdfResources::BuildResources(const PdfDictionary& dictionary) {
  for (const auto& resource_type : dictionary.KeySet()) {
    auto resources = dictionary.GetAsDictionary(resource_type);
    if (!resources) continue;
    auto& m = resource_to_name_[resource_type->GetValue()];
    for (const auto& kv : resources->EntrySet()) {
      PdfObject* key = kv.second.get();
      if (key && key->IsIndirectReference()) {
        PdfObject* target =
            static_cast<PdfIndirectReference*>(key)->RefersToRaw().get();
        m[key] = kv.first;
        if (target) m[target] = kv.first;
      } else {
        m[key] = kv.first;
      }
    }
  }
}

PdfNamePtr PdfResources::AddResource(const PdfObjectPtr& resource,
                                     const PdfNamePtr& res_type,
                                     const std::string& prefix) {
  PdfObject* key = resource.get();
  auto& m = resource_to_name_[res_type->GetValue()];
  auto it = m.find(key);
  if (it == m.end() && resource->GetIndirectReference() != nullptr) {
    it = m.find(resource->GetIndirectReference());
  }
  if (it != m.end()) return it->second;
  auto res_dict = GetPdfObject()->GetAsDictionary(res_type);
  int& counter = counters_[prefix];
  PdfNamePtr name;
  do {
    name = std::make_shared<PdfName>(prefix + std::to_string(++counter));
  } while (res_dict && res_dict->ContainsKey(name));
  AddResource(resource, res_type, name);
  return name;
}

void PdfResources::AddResource(const PdfObjectPtr& resource,
                               const PdfNamePtr& res_type,
                               const PdfNamePtr& res_name) {
  auto res_dict = GetPdfObject()->GetAsDictionary(res_type);
  if (!res_dict) {
    res_dict = std::make_shared<PdfDictionary>();
    GetPdfObject()->Put(res_type, res_dict);
  }
  res_dict->Put(res_name, resource);
  resource_to_name_[res_type->GetValue()][resource.get()] = res_name;
  if (resource->GetIndirectReference())
    resource_to_name_[res_type->GetValue()][resource->GetIndirectReference()] =
        res_name;
  is_modified_ = true;
  GetPdfObject()->SetModified();
}

PdfNamePtr PdfResources::AddImage(const PdfStreamPtr& image) {
  return AddResource(image, PdfName::kXObject, "Im");
}
PdfNamePtr PdfResources::AddForm(const PdfStreamPtr& form) {
  return AddResource(form, PdfName::kXObject, "Fm");
}
PdfNamePtr PdfResources::AddExtGState(const PdfDictionaryPtr& ext_g_state) {
  return AddResource(ext_g_state, PdfName::kExtGState, "Gs");
}
PdfNamePtr PdfResources::AddProperties(const PdfDictionaryPtr& properties) {
  return AddResource(properties, PdfName::kProperties, "Pr");
}
PdfNamePtr PdfResources::AddColorSpace(const PdfObjectPtr& color_space) {
  return AddResource(color_space, PdfName::kColorSpace, "Cs");
}
PdfNamePtr PdfResources::AddPattern(const PdfDictionaryPtr& pattern) {
  return AddResource(pattern, PdfName::kPattern, "P");
}
PdfNamePtr PdfResources::AddShading(const PdfDictionaryPtr& shading) {
  return AddResource(shading, PdfName::kShading, "Sh");
}

PdfStreamPtr PdfResources::GetImage(const PdfNamePtr& name) const {
  return PdfCast<PdfStream>(GetResourceObject(PdfName::kXObject, name));
}
PdfStreamPtr PdfResources::GetForm(const PdfNamePtr& name) const {
  return PdfCast<PdfStream>(GetResourceObject(PdfName::kXObject, name));
}

PdfNamePtr PdfResources::GetResourceName(const PdfObject* resource) const {
  for (const auto& kv : resource_to_name_) {
    auto it = kv.second.find(resource);
    if (it != kv.second.end()) return it->second;
    if (resource && resource->GetIndirectReference()) {
      it = kv.second.find(resource->GetIndirectReference());
      if (it != kv.second.end()) return it->second;
    }
  }
  return nullptr;
}

std::vector<PdfNamePtr> PdfResources::GetResourceNames() const {
  std::vector<PdfNamePtr> names;
  for (const auto& type : GetPdfObject()->KeySet()) {
    auto n = GetResourceNames(type);
    names.insert(names.end(), n.begin(), n.end());
  }
  return names;
}

std::vector<PdfNamePtr> PdfResources::GetResourceNames(
    const PdfNamePtr& res_type) const {
  auto resource_category = GetPdfObject()->GetAsDictionary(res_type);
  return resource_category ? resource_category->KeySet()
                           : std::vector<PdfNamePtr>();
}

PdfObjectPtr PdfResources::GetResourceObject(const PdfNamePtr& res_type,
                                             const PdfNamePtr& res_name) const {
  auto resource = GetResource(res_type);
  return resource ? resource->Get(res_name) : nullptr;
}

// ============================================================ PdfPagesTree
PdfPagesTree::PdfPagesTree(PdfDocument* document, PdfDictionaryPtr root)
    : document_(document), original_root_(std::move(root)) {
  if (original_root_) {
    std::set<const PdfObject*> visited;
    LoadPages(original_root_, visited, 0);
  }
  pages_.resize(page_refs_.size());
}

PdfPagesTree::~PdfPagesTree() = default;

void PdfPagesTree::LoadPages(const PdfDictionaryPtr& node,
                             std::set<const PdfObject*>& visited, int depth) {
  if (!node || depth > 256 || visited.count(node.get())) return;
  visited.insert(node.get());
  auto kids = node->GetAsArray(PdfName::kKids);
  auto type = node->GetAsName(PdfName::kType);
  if (!kids) {
    if (!NameEquals(type, PdfName::kPages)) page_refs_.push_back(node);
    return;
  }
  if (NameEquals(type, PdfName::kPage)) {
    page_refs_.push_back(node);
    return;
  }
  for (int i = 0; i < kids->size(); ++i) {
    auto kid = kids->GetAsDictionary(i);
    if (kid) LoadPages(kid, visited, depth + 1);
  }
}

PdfPage* PdfPagesTree::GetPage(int page_num) {
  if (page_num < 1 || page_num > GetNumberOfPages())
    throw std::out_of_range(Format(
        kernel_exception_message_constant::kRequestedPageNumberIsOutOfBounds,
        page_num));
  --page_num;
  if (!pages_[page_num])
    pages_[page_num] =
        std::make_shared<PdfPage>(page_refs_[page_num], document_);
  return pages_[page_num].get();
}

PdfPage* PdfPagesTree::GetPage(const PdfDictionary* page_dictionary) {
  int n = GetPageNumber(page_dictionary);
  return n > 0 ? GetPage(n) : nullptr;
}

int PdfPagesTree::GetPageNumber(const PdfPage* page) const {
  for (size_t i = 0; i < pages_.size(); ++i)
    if (pages_[i].get() == page) return static_cast<int>(i) + 1;
  return page ? GetPageNumber(page->GetPdfObject().get()) : -1;
}

int PdfPagesTree::GetPageNumber(const PdfDictionary* page_dictionary) const {
  for (size_t i = 0; i < page_refs_.size(); ++i)
    if (page_refs_[i].get() == page_dictionary) return static_cast<int>(i) + 1;
  return -1;
}

void PdfPagesTree::AddPage(std::shared_ptr<PdfPage> pdf_page) {
  AddPage(GetNumberOfPages() + 1, std::move(pdf_page));
}

void PdfPagesTree::AddPage(int index, std::shared_ptr<PdfPage> pdf_page) {
  if (index < 1 || index > GetNumberOfPages() + 1)
    index = GetNumberOfPages() + 1;
  page_refs_.insert(page_refs_.begin() + (index - 1), pdf_page->GetPdfObject());
  pages_.insert(pages_.begin() + (index - 1), std::move(pdf_page));
  modified_ = true;
}

std::shared_ptr<PdfPage> PdfPagesTree::RemovePage(int page_num) {
  if (page_num < 1 || page_num > GetNumberOfPages()) return nullptr;
  GetPage(page_num);
  auto page = pages_[page_num - 1];
  pages_.erase(pages_.begin() + (page_num - 1));
  page_refs_.erase(page_refs_.begin() + (page_num - 1));
  modified_ = true;
  return page;
}

PdfObjectPtr PdfPagesTree::GenerateTree() {
  if (page_refs_.empty())
    throw PdfException(kernel_exception_message_constant::kDocumentHasNoPages);
  if (!modified_ && original_root_ && document_->IsAppendMode())
    return original_root_;
  if (!modified_ && original_root_) {
    // Keep the original tree if every page is still in place.
    return original_root_;
  }
  // Push inheritable attributes down before re-parenting.
  static const PdfNamePtr* inheritable[] = {
      &PdfName::kResources, &PdfName::kMediaBox, &PdfName::kCropBox,
      &PdfName::kRotate};
  for (size_t i = 0; i < page_refs_.size(); ++i) {
    auto& page = page_refs_[i];
    for (const PdfNamePtr* key : inheritable) {
      if (page->ContainsKey(*key)) continue;
      auto parent = page->GetAsDictionary(PdfName::kParent);
      int guard = 0;
      while (parent && guard++ < 256) {
        if (auto v = parent->Get(*key, false)) {
          page->Put(*key, v);
          break;
        }
        parent = parent->GetAsDictionary(PdfName::kParent);
      }
    }
  }
  // Build a balanced tree with leaf nodes of up to 10 kids (as iText does).
  const int leaf_size = 10;
  std::vector<PdfDictionaryPtr> level;
  for (size_t i = 0; i < page_refs_.size(); i += leaf_size) {
    auto node = std::make_shared<PdfDictionary>();
    node->MakeIndirect(document_);
    node->Put(PdfName::kType, PdfName::kPages);
    auto kids = std::make_shared<PdfArray>();
    size_t end = std::min(page_refs_.size(), i + leaf_size);
    for (size_t k = i; k < end; ++k) {
      page_refs_[k]->MakeIndirect(document_);
      kids->Add(page_refs_[k]);
      page_refs_[k]->Put(PdfName::kParent, node);
      page_refs_[k]->SetModified();
    }
    node->Put(PdfName::kKids, kids);
    node->Put(PdfName::kCount, MakeNumber(static_cast<int>(end - i)));
    level.push_back(node);
  }
  while (level.size() > 1) {
    std::vector<PdfDictionaryPtr> next;
    for (size_t i = 0; i < level.size(); i += leaf_size) {
      auto node = std::make_shared<PdfDictionary>();
      node->MakeIndirect(document_);
      node->Put(PdfName::kType, PdfName::kPages);
      auto kids = std::make_shared<PdfArray>();
      int count = 0;
      size_t end = std::min(level.size(), i + leaf_size);
      for (size_t k = i; k < end; ++k) {
        kids->Add(level[k]);
        level[k]->Put(PdfName::kParent, node);
        count += level[k]->GetAsNumber(PdfName::kCount)->IntValue();
      }
      node->Put(PdfName::kKids, kids);
      node->Put(PdfName::kCount, MakeNumber(count));
      next.push_back(node);
    }
    level.swap(next);
  }
  // Free the nodes of the old tree that are no longer used.
  if (original_root_ && original_root_->GetIndirectReference() &&
      !document_->IsAppendMode()) {
    std::vector<PdfDictionaryPtr> stack{original_root_};
    std::set<const PdfObject*> seen;
    while (!stack.empty()) {
      auto n = stack.back();
      stack.pop_back();
      if (!n || seen.count(n.get())) continue;
      seen.insert(n.get());
      if (!NameEquals(n->GetAsName(PdfName::kType), PdfName::kPages)) continue;
      if (auto kids = n->GetAsArray(PdfName::kKids))
        for (int i = 0; i < kids->size(); ++i)
          stack.push_back(kids->GetAsDictionary(i));
      if (n->GetIndirectReference() && !n->IsFlushed())
        n->GetIndirectReference()->SetFree();
    }
  }
  original_root_ = level[0];
  modified_ = false;
  return level[0];
}

void PdfPagesTree::ClearPageRefs() {
  page_refs_.clear();
  pages_.clear();
}

// ============================================================ PdfCatalog
PdfCatalog::PdfCatalog(PdfDictionaryPtr pdf_object, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(pdf_object)),
      document_(document) {
  if (!GetPdfObject())
    throw PdfException("Document has no PDF Catalog object.");
  SetForbidRelease();
  page_tree_ = std::make_unique<PdfPagesTree>(
      document, GetPdfObject()->GetAsDictionary(PdfName::kPages));
}

PdfCatalog::PdfCatalog(PdfDocument* pdf_document) : document_(pdf_document) {
  SetPdfObject(std::make_shared<PdfDictionary>());
  GetPdfObject()->MakeIndirect(pdf_document);
  GetPdfObject()->Put(PdfName::kType, PdfName::kCatalog);
  SetForbidRelease();
  page_tree_ = std::make_unique<PdfPagesTree>(pdf_document, nullptr);
}

PdfCatalog& PdfCatalog::Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
  GetPdfObject()->Put(key, value);
  GetPdfObject()->SetModified();
  return *this;
}

PdfCatalog& PdfCatalog::Remove(const PdfNamePtr& key) {
  GetPdfObject()->Remove(key);
  GetPdfObject()->SetModified();
  return *this;
}

void PdfCatalog::Flush() {
  throw PdfException(
      "PdfCatalog cannot be flushed manually; it is flushed on PdfDocument "
      "close.");
}

// ============================================================ PdfPage
PdfPage::PdfPage(PdfDictionaryPtr pdf_object, PdfDocument*)
    : PdfObjectWrapper<PdfDictionary>(std::move(pdf_object)) {
  SetForbidRelease();
}

PdfPage::PdfPage(PdfDocument* pdf_document, const PageSize& page_size) {
  SetPdfObject(std::make_shared<PdfDictionary>());
  GetPdfObject()->MakeIndirect(pdf_document);
  auto content_stream = std::make_shared<PdfStream>();
  content_stream->MakeIndirect(pdf_document);
  GetPdfObject()->Put(PdfName::kContents, content_stream);
  GetPdfObject()->Put(PdfName::kType, PdfName::kPage);
  GetPdfObject()->Put(
      PdfName::kMediaBox,
      std::make_shared<PdfArray>(static_cast<const Rectangle&>(page_size)));
  GetPdfObject()->Put(
      PdfName::kTrimBox,
      std::make_shared<PdfArray>(static_cast<const Rectangle&>(page_size)));
  SetForbidRelease();
}

PdfPage::~PdfPage() = default;

PdfDocument* PdfPage::GetDocument() const {
  auto* ref = GetPdfObject()->GetIndirectReference();
  return ref ? ref->GetDocument() : nullptr;
}

PdfObjectPtr PdfPage::GetInheritableValue(const PdfNamePtr& pdf_name,
                                          bool allow_parent_inheritance) const {
  PdfObjectPtr resources = GetPdfObject()->Get(pdf_name);
  if (!resources && allow_parent_inheritance) {
    auto parent = GetPdfObject()->GetAsDictionary(PdfName::kParent);
    int guard = 0;
    while (parent && guard++ < 256) {
      resources = parent->Get(pdf_name);
      if (resources) break;
      parent = parent->GetAsDictionary(PdfName::kParent);
    }
  }
  return resources;
}

Rectangle PdfPage::GetMediaBox() const {
  auto media_box = PdfCast<PdfArray>(GetInheritableValue(PdfName::kMediaBox));
  if (!media_box) return PageSize::kLetter;  // lenient default (Java throws)
  if (media_box->size() != 4) throw PdfException("Wrong media box size.");
  return media_box->ToRectangle();
}

PdfPage& PdfPage::SetMediaBox(const Rectangle& rectangle) {
  return Put(PdfName::kMediaBox, std::make_shared<PdfArray>(rectangle));
}

Rectangle PdfPage::GetCropBox() const {
  auto crop_box = PdfCast<PdfArray>(GetInheritableValue(PdfName::kCropBox));
  if (!crop_box) return GetMediaBox();
  Rectangle crop = crop_box->ToRectangle();
  Rectangle media = GetMediaBox(), inter;
  if (crop.GetIntersection(media, inter)) return inter;
  return crop;
}

PdfPage& PdfPage::SetCropBox(const Rectangle& rectangle) {
  return Put(PdfName::kCropBox, std::make_shared<PdfArray>(rectangle));
}

Rectangle PdfPage::BoxOrDefault(const PdfNamePtr& name,
                                const Rectangle& def) const {
  auto box = GetPdfObject()->GetAsRectangle(name);
  return box ? *box : def;
}

Rectangle PdfPage::GetBleedBox() const {
  return BoxOrDefault(PdfName::kBleedBox, GetCropBox());
}
PdfPage& PdfPage::SetBleedBox(const Rectangle& r) {
  return Put(PdfName::kBleedBox, std::make_shared<PdfArray>(r));
}
Rectangle PdfPage::GetArtBox() const {
  return BoxOrDefault(PdfName::kArtBox, GetCropBox());
}
PdfPage& PdfPage::SetArtBox(const Rectangle& r) {
  return Put(PdfName::kArtBox, std::make_shared<PdfArray>(r));
}
Rectangle PdfPage::GetTrimBox() const {
  return BoxOrDefault(PdfName::kTrimBox, GetCropBox());
}
PdfPage& PdfPage::SetTrimBox(const Rectangle& r) {
  return Put(PdfName::kTrimBox, std::make_shared<PdfArray>(r));
}

Rectangle PdfPage::GetPageSizeWithRotation() const {
  PageSize rect(GetPageSize());
  int rotation = GetRotation();
  while (rotation > 0) {
    rect = rect.Rotate();
    rotation -= 90;
  }
  return rect;
}

int PdfPage::GetRotation() const {
  auto rotate = PdfCast<PdfNumber>(GetInheritableValue(PdfName::kRotate));
  int rotate_value = rotate ? rotate->IntValue() : 0;
  rotate_value %= 360;
  return rotate_value < 0 ? rotate_value + 360 : rotate_value;
}

PdfPage& PdfPage::SetRotation(int deg_angle) {
  return Put(PdfName::kRotate, MakeNumber(deg_angle));
}

PdfStreamPtr PdfPage::GetContentStream(int index) const {
  int count = GetContentStreamCount();
  if (index >= count || index < 0)
    throw std::out_of_range("Index: " + std::to_string(index) +
                            ", Size: " + std::to_string(count));
  PdfObjectPtr contents = GetPdfObject()->Get(PdfName::kContents);
  if (contents->IsStream())
    return std::static_pointer_cast<PdfStream>(contents);
  if (contents->IsArray())
    return std::static_pointer_cast<PdfArray>(contents)->GetAsStream(index);
  return nullptr;
}

int PdfPage::GetContentStreamCount() const {
  PdfObjectPtr contents = GetPdfObject()->Get(PdfName::kContents);
  if (!contents) return 0;
  if (contents->IsStream()) return 1;
  if (contents->IsArray())
    return std::static_pointer_cast<PdfArray>(contents)->size();
  return 0;
}

PdfStreamPtr PdfPage::GetFirstContentStream() const {
  return GetContentStreamCount() > 0 ? GetContentStream(0) : nullptr;
}
PdfStreamPtr PdfPage::GetLastContentStream() const {
  int count = GetContentStreamCount();
  return count > 0 ? GetContentStream(count - 1) : nullptr;
}

static PdfStreamPtr NewContentStream(PdfPage& page, bool before) {
  PdfObjectPtr contents = page.GetPdfObject()->Get(PdfName::kContents);
  PdfArrayPtr array;
  if (contents && contents->IsStream()) {
    array = std::make_shared<PdfArray>();
    // keep the original reference
    PdfObjectPtr raw = page.GetPdfObject()->Get(PdfName::kContents, false);
    array->Add(raw);
    page.GetPdfObject()->Put(PdfName::kContents, array);
  } else if (contents && contents->IsArray()) {
    array = std::static_pointer_cast<PdfArray>(contents);
  } else {
    array = nullptr;
  }
  auto content_stream = std::make_shared<PdfStream>();
  content_stream->MakeIndirect(page.GetDocument());
  if (array) {
    if (before)
      array->Add(0, content_stream);
    else
      array->Add(content_stream);
    if (array->GetIndirectReference() != nullptr)
      array->SetModified();
    else
      page.GetPdfObject()->SetModified();
  } else {
    page.GetPdfObject()->Put(PdfName::kContents, content_stream);
    page.GetPdfObject()->SetModified();
  }
  return content_stream;
}

PdfStreamPtr PdfPage::NewContentStreamBefore() {
  return NewContentStream(*this, true);
}
PdfStreamPtr PdfPage::NewContentStreamAfter() {
  return NewContentStream(*this, false);
}

PdfResources* PdfPage::GetResources() {
  if (!resources_) {
    bool read_only = false;
    auto resources =
        PdfCast<PdfDictionary>(GetPdfObject()->Get(PdfName::kResources));
    if (!resources) {
      resources =
          PdfCast<PdfDictionary>(GetInheritableValue(PdfName::kResources));
      if (resources) read_only = true;
    }
    if (!resources) {
      resources = std::make_shared<PdfDictionary>();
      GetPdfObject()->Put(PdfName::kResources, resources);
    } else if (read_only) {
      // copy inherited resources into the page to allow modification
      resources = std::static_pointer_cast<PdfDictionary>(resources->Clone());
      GetPdfObject()->Put(PdfName::kResources, resources);
      read_only = false;
    }
    resources_ = std::make_shared<PdfResources>(resources);
    resources_->SetReadOnly(read_only);
  }
  return resources_.get();
}

PdfPage& PdfPage::SetResources(std::shared_ptr<PdfResources> pdf_resources) {
  Put(PdfName::kResources, pdf_resources->GetPdfObject());
  resources_ = std::move(pdf_resources);
  return *this;
}

void PdfPage::Flush(bool flush_resources_content_streams) {
  if (GetPdfObject()->IsFlushed()) return;
  PdfDocument* document = GetDocument();
  if (document == nullptr || document->GetWriter() == nullptr) return;
  if (document->IsAppendMode() && !GetPdfObject()->IsModified()) return;
  if (flush_resources_content_streams) {
    GetResources();
    for (int i = 0; i < GetContentStreamCount(); i++) {
      auto s = GetContentStream(i);
      if (s) s->Flush(false);
    }
    if (resources_) resources_->GetPdfObject()->Flush();
  }
  GetPdfObject()->Flush();
}

Bytes PdfPage::GetContentBytes() const {
  Bytes out;
  int count = GetContentStreamCount();
  for (int i = 0; i < count; ++i) {
    Bytes b = GetStreamBytes(i);
    out.insert(out.end(), b.begin(), b.end());
    if (i < count - 1) out.push_back('\n');
  }
  return out;
}

Bytes PdfPage::GetStreamBytes(int index) const {
  auto s = GetContentStream(index);
  return s ? s->GetBytes() : Bytes();
}

PdfArrayPtr PdfPage::GetAnnots(bool create) {
  auto annots = GetPdfObject()->GetAsArray(PdfName::kAnnots);
  if (!annots && create) {
    annots = std::make_shared<PdfArray>();
    Put(PdfName::kAnnots, annots);
  }
  return annots;
}

int PdfPage::GetAnnotsSize() const {
  auto annots = GetPdfObject()->GetAsArray(PdfName::kAnnots);
  return annots ? annots->size() : 0;
}

PdfPage& PdfPage::AddAnnotation(const PdfDictionaryPtr& annotation) {
  auto annots = GetAnnots(true);
  annotation->MakeIndirect(GetDocument());
  annotation->Put(PdfName::kP, GetPdfObject());
  annots->Add(annotation);
  if (annots->GetIndirectReference())
    annots->SetModified();
  else
    GetPdfObject()->SetModified();
  return *this;
}

std::vector<PdfDictionaryPtr> PdfPage::GetAnnotations() const {
  std::vector<PdfDictionaryPtr> result;
  auto annots = GetPdfObject()->GetAsArray(PdfName::kAnnots);
  if (!annots) return result;
  for (int i = 0; i < annots->size(); ++i)
    if (auto d = annots->GetAsDictionary(i)) result.push_back(d);
  return result;
}

PdfPage& PdfPage::Put(const PdfNamePtr& key, const PdfObjectPtr& value) {
  GetPdfObject()->Put(key, value);
  GetPdfObject()->SetModified();
  return *this;
}

PdfPage& PdfPage::Remove(const PdfNamePtr& key) {
  GetPdfObject()->Remove(key);
  GetPdfObject()->SetModified();
  return *this;
}

namespace {
/// Copy filter skipping /Parent (the new page gets a new parent) and /Annots
/// handled separately.
class PageCopyFilter : public ICopyFilter {
 public:
  explicit PageCopyFilter(const PdfObject*) {}
  bool ShouldProcess(PdfObject* new_parent, PdfName* name,
                     PdfObject* value) override {
    if (name && name->GetValue() == "Parent") return false;
    // /P back-pointers (annotations, popups, structure elements) to a page of
    // the source document: copying them would drag the source page tree along.
    // Other /P entries (e.g. a JavaScript /P string) are kept.
    if (name && name->GetValue() == "P" && new_parent &&
        new_parent->IsDictionary() && IsPage(value))
      return false;
    return true;
  }

 private:
  static bool IsPage(PdfObject* value) {
    if (value && value->IsIndirectReference())
      value = static_cast<PdfIndirectReference*>(value)->GetRefersTo(true);
    if (!value || !value->IsDictionary()) return false;
    return NameEquals(
        static_cast<PdfDictionary*>(value)->GetAsName(PdfName::kType),
        PdfName::kPage);
  }
};
}  // namespace

std::shared_ptr<PdfPage> PdfPage::CopyTo(PdfDocument* to_document) {
  PageCopyFilter filter(GetPdfObject().get());
  // copy page dictionary without /Parent, /Annots, /StructParents, /B
  auto src = GetPdfObject();
  auto dict = std::make_shared<PdfDictionary>();
  dict->MakeIndirect(to_document);
  for (const auto& kv : src->EntrySet()) {
    const std::string& k = kv.first->GetValue();
    if (k == "Parent" || k == "Annots" || k == "StructParents" || k == "B")
      continue;
    dict->Put(kv.first, kv.second->CopyTo(to_document, false, filter));
  }
  // inheritable attributes
  for (const PdfNamePtr* key : {&PdfName::kResources, &PdfName::kMediaBox,
                                &PdfName::kCropBox, &PdfName::kRotate}) {
    if (!dict->ContainsKey(*key)) {
      auto v = GetInheritableValue(*key);
      if (v) dict->Put(*key, v->CopyTo(to_document, false, filter));
    }
  }
  auto page = std::make_shared<PdfPage>(dict, to_document);
  auto annots = src->GetAsArray(PdfName::kAnnots);
  if (annots) {
    auto new_annots = std::make_shared<PdfArray>();
    for (int i = 0; i < annots->size(); ++i) {
      auto a = annots->GetAsDictionary(i);
      if (!a) continue;
      auto subtype = a->GetAsName(PdfName::kSubtype);
      if (NameEquals(subtype, PdfName::kLink)) {
        // internal links point to pages of the source document: drop them (as
        // iText does when the target page is not copied)
        auto action = a->GetAsDictionary(PdfName::kA);
        if (a->ContainsKey(PdfName::kDest) ||
            (action &&
             NameEquals(action->GetAsName(PdfName::kS), PdfName::kGoTo)))
          continue;
      }
      // Popups are copied too: CopyTo de-duplicates indirect objects, so the
      // markup's /Popup and the popup's entry in /Annots end up as the same
      // copied object (no orphaned popup).
      auto copy = PdfCast<PdfDictionary>(a->CopyTo(to_document, false, filter));
      if (copy) {
        copy->Put(PdfName::kP, dict);
        new_annots->Add(copy);
      }
    }
    // The filter drops every /Parent; restore the popup -> markup link.
    for (int i = 0; i < new_annots->size(); ++i) {
      auto markup = new_annots->GetAsDictionary(i);
      auto popup = markup ? markup->GetAsDictionary(PdfName::kPopup) : nullptr;
      if (popup) popup->Put(PdfName::kParent, markup);
    }
    if (!new_annots->IsEmpty()) dict->Put(PdfName::kAnnots, new_annots);
  }
  return page;
}

// ============================================================ PdfDocument
static std::atomic<int64_t> g_document_id_counter{0};

PdfDocument::PdfDocument(PdfReader& reader,
                         const DocumentProperties& properties)
    : document_id_(++g_document_id_counter) {
  Init(&reader, nullptr, properties);
}

PdfDocument::PdfDocument(PdfWriter& writer,
                         const DocumentProperties& properties)
    : document_id_(++g_document_id_counter) {
  Init(nullptr, &writer, properties);
}

PdfDocument::PdfDocument(PdfReader& reader, PdfWriter& writer,
                         const StampingProperties& properties)
    : document_id_(++g_document_id_counter) {
  Init(&reader, &writer, properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfReader> reader,
                         const DocumentProperties& properties)
    : owned_reader_(std::move(reader)), document_id_(++g_document_id_counter) {
  Init(owned_reader_.get(), nullptr, properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfWriter> writer,
                         const DocumentProperties& properties)
    : owned_writer_(std::move(writer)), document_id_(++g_document_id_counter) {
  Init(nullptr, owned_writer_.get(), properties);
}

PdfDocument::PdfDocument(std::unique_ptr<PdfReader> reader,
                         std::unique_ptr<PdfWriter> writer,
                         const StampingProperties& properties)
    : owned_reader_(std::move(reader)),
      owned_writer_(std::move(writer)),
      document_id_(++g_document_id_counter) {
  Init(owned_reader_.get(), owned_writer_.get(), properties);
}

void PdfDocument::Init(PdfReader* reader, PdfWriter* writer,
                       const StampingProperties& properties) {
  if (!reader && !writer)
    throw PdfException(kernel_exception_message_constant::
                           kPdfDocumentMustBeCreatedWithReaderOrWriter);
  reader_ = reader;
  writer_ = writer;
  properties_ = properties;
  if (!writer) properties_.append_mode = false;
  xref_ = std::make_unique<PdfXrefTable>();
  std::optional<PdfVersion> new_version;
  if (writer) new_version = writer->GetPdfVersion();
  Open(new_version);
}

PdfDocument::~PdfDocument() {
  try {
    if (!closed_ && writer_ == nullptr) Close();
  } catch (...) {
  }
  // Pages/catalog/fonts hold objects; release them before the xref table breaks
  // cycles.
  document_fonts_.clear();
  default_font_.reset();
  info_.reset();
  catalog_.reset();
  trailer_.reset();
  if (xref_) xref_->ReleaseAll();
}

void PdfDocument::ReadDocumentIds() {
  auto id = reader_->GetTrailer()->GetAsArray(PdfName::kID);
  if (id && id->size() == 2) {
    original_document_id_ = id->GetAsString(0);
    modified_document_id_ = id->GetAsString(1);
  }
  if (!original_document_id_ && modified_document_id_)
    original_document_id_ = modified_document_id_;
}

void PdfDocument::Open(std::optional<PdfVersion> new_pdf_version) {
  if (reader_) {
    if (reader_->GetDocument() != nullptr)
      throw PdfException("Given PdfReader instance has already been utilized.");
    reader_->SetDocument(this);
    reader_->ReadPdf();
    pdf_version_ = reader_->GetHeaderPdfVersion();
    trailer_ = std::make_shared<PdfDictionary>(reader_->GetTrailer().get());
    ReadDocumentIds();
    auto catalog_dictionary =
        PdfCast<PdfDictionary>(trailer_->Get(PdfName::kRoot, true));
    if (!catalog_dictionary)
      throw PdfException("Corrupted root entry in trailer.");
    catalog_ = std::make_unique<PdfCatalog>(catalog_dictionary, this);
    if (auto v = catalog_->GetPdfObject()->GetAsName(PdfName::kVersion)) {
      try {
        PdfVersion catalog_version = PdfVersion::FromPdfName(*v);
        if (catalog_version.CompareTo(pdf_version_) > 0)
          pdf_version_ = catalog_version;
      } catch (...) {
      }
    }
    if (properties_.append_mode &&
        (reader_->HasRebuiltXref() || reader_->HasFixedXref()))
      throw PdfException(
          "Append mode requires a document without errors, even if recovery is "
          "possible.");
  }
  xref_->InitFreeReferencesList(this);
  if (writer_) {
    if (reader_ && reader_->HasXrefStm() &&
        !writer_->GetProperties().is_full_compression.has_value())
      writer_->GetProperties().is_full_compression = true;
    if (reader_ && !reader_->IsOpenedWithFullPermission())
      throw BadPasswordException(
          BadPasswordException::kPdfReaderNotOpenedWithOwnerPassword);
    if (reader_ && properties_.preserve_encryption_)
      writer_->SetCrypto(reader_->GetDecrypt());
    writer_->SetDocument(this);
    if (!reader_) {
      catalog_ = std::make_unique<PdfCatalog>(this);
      GetDocumentInfo().AddCreationDate();
    }
    GetDocumentInfo().AddModDate();
    if (!trailer_) trailer_ = std::make_shared<PdfDictionary>();
    if (!trailer_->IsEmpty()) {
      for (const PdfNamePtr* key :
           {&PdfName::kEncrypt, &PdfName::kSize, &PdfName::kPrev,
            &PdfName::kRoot, &PdfName::kInfo, &PdfName::kID,
            &PdfName::kXRefStm})
        trailer_->Remove(*key);
    }
    trailer_->Put(PdfName::kRoot,
                  catalog_->GetPdfObject()->GetIndirectReference()->SelfRef());
    if (reader_ && reader_->GetTrailer()->ContainsKey(PdfName::kID))
      trailer_->Put(PdfName::kID, reader_->GetTrailer()->Get(PdfName::kID));
    PdfStringPtr reader_modified_id = modified_document_id_;
    const auto& wp = writer_->GetProperties();
    if (wp.initial_document_id &&
        !(reader_ && reader_->GetDecrypt() &&
          (properties_.append_mode || properties_.preserve_encryption_)))
      original_document_id_ = wp.initial_document_id;
    if (wp.modified_document_id)
      modified_document_id_ = wp.modified_document_id;
    if (!original_document_id_ && modified_document_id_)
      original_document_id_ = modified_document_id_;
    if (!modified_document_id_) {
      if (!original_document_id_)
        original_document_id_ =
            std::make_shared<PdfString>(PdfEncryption::GenerateNewDocumentId());
      modified_document_id_ = original_document_id_;
    }
    if (!wp.modified_document_id && reader_modified_id &&
        modified_document_id_->Equals(*reader_modified_id))
      modified_document_id_ =
          std::make_shared<PdfString>(PdfEncryption::GenerateNewDocumentId());
  }
  if (properties_.append_mode) {
    auto file = reader_->GetSafeFile();
    file->Seek(0);
    Bytes buffer(8192);
    int n;
    while ((n = file->Read(buffer.data(), 0, static_cast<int>(buffer.size()))) >
           0)
      writer_->Write(buffer.data(), 0, n);
    writer_->Write('\n');
    writer_->GetProperties().is_full_compression = reader_->HasXrefStm();
    writer_->SetCrypto(reader_->GetDecrypt());
    if (new_pdf_version && pdf_version_.CompareTo(PdfVersion::kPdf14) >= 0 &&
        new_pdf_version->CompareTo(reader_->GetHeaderPdfVersion()) > 0) {
      catalog_->Put(PdfName::kVersion, new_pdf_version->ToPdfName());
      pdf_version_ = *new_pdf_version;
    }
  } else if (writer_) {
    if (new_pdf_version) pdf_version_ = *new_pdf_version;
    writer_->WriteHeader();
    if (writer_->GetCrypto() == nullptr)
      writer_->InitCryptoIfSpecified(pdf_version_);
  }
}

PdfObjectPtr PdfDocument::GetPdfObject(int obj_num) {
  CheckClosingStatus();
  PdfIndirectReference* reference = xref_->Get(obj_num);
  if (reference == nullptr) return nullptr;
  return reference->IsFree() ? nullptr : reference->GetRefersToPtr();
}

int PdfDocument::GetNumberOfPdfObjects() const { return xref_->size(); }

PdfPage* PdfDocument::GetPage(int page_num) {
  CheckClosingStatus();
  return catalog_->GetPageTree().GetPage(page_num);
}

PdfPage* PdfDocument::GetPage(const PdfDictionary* page_dictionary) {
  CheckClosingStatus();
  return catalog_->GetPageTree().GetPage(page_dictionary);
}

PdfPage* PdfDocument::AddNewPage(const PageSize& page_size) {
  CheckClosingStatus();
  auto page = std::make_shared<PdfPage>(this, page_size);
  catalog_->GetPageTree().AddPage(page);
  return page.get();
}

PdfPage* PdfDocument::AddNewPage(int index, const PageSize& page_size) {
  CheckClosingStatus();
  auto page = std::make_shared<PdfPage>(this, page_size);
  catalog_->GetPageTree().AddPage(index, page);
  return page.get();
}

PdfPage* PdfDocument::AddPage(std::shared_ptr<PdfPage> page) {
  CheckClosingStatus();
  if (page->IsFlushed())
    throw PdfException(
        kernel_exception_message_constant::kFlushedPageCannotBeAddedOrInserted);
  if (page->GetDocument() != nullptr && page->GetDocument() != this)
    throw PdfException(
        "Page cannot be added to document because it belongs to another "
        "document.");
  PdfPage* raw = page.get();
  catalog_->GetPageTree().AddPage(std::move(page));
  return raw;
}

PdfPage* PdfDocument::AddPage(int index, std::shared_ptr<PdfPage> page) {
  CheckClosingStatus();
  if (page->IsFlushed())
    throw PdfException(
        kernel_exception_message_constant::kFlushedPageCannotBeAddedOrInserted);
  if (page->GetDocument() != nullptr && page->GetDocument() != this)
    throw PdfException(
        "Page cannot be added to document because it belongs to another "
        "document.");
  PdfPage* raw = page.get();
  catalog_->GetPageTree().AddPage(index, std::move(page));
  return raw;
}

int PdfDocument::GetNumberOfPages() const {
  CheckClosingStatus();
  return catalog_->GetPageTree().GetNumberOfPages();
}

int PdfDocument::GetPageNumber(const PdfPage* page) const {
  return catalog_->GetPageTree().GetPageNumber(page);
}
int PdfDocument::GetPageNumber(const PdfDictionary* page_dictionary) const {
  return catalog_->GetPageTree().GetPageNumber(page_dictionary);
}

bool PdfDocument::MovePage(PdfPage* page, int insert_before) {
  CheckClosingStatus();
  int page_num = GetPageNumber(page);
  if (page_num > 0) {
    MovePage(page_num, insert_before);
    return true;
  }
  return false;
}

void PdfDocument::MovePage(int page_number, int insert_before) {
  CheckClosingStatus();
  if (insert_before < 1 || insert_before > GetNumberOfPages() + 1)
    throw std::out_of_range(Format(
        kernel_exception_message_constant::kRequestedPageNumberIsOutOfBounds,
        insert_before));
  auto& tree = catalog_->GetPageTree();
  auto page = tree.RemovePage(page_number);
  if (insert_before > page_number) --insert_before;
  tree.AddPage(insert_before, page);
}

bool PdfDocument::RemovePage(PdfPage* page) {
  int page_num = GetPageNumber(page);
  if (page_num < 1) return false;
  RemovePage(page_num);
  return true;
}

void PdfDocument::RemovePage(int page_num) {
  CheckClosingStatus();
  auto removed = catalog_->GetPageTree().RemovePage(page_num);
  if (removed && !IsAppendMode() &&
      removed->GetPdfObject()->GetIndirectReference() && writer_) {
    // keep object but it will not be referenced anymore
  }
}

PdfDocumentInfo& PdfDocument::GetDocumentInfo() {
  CheckClosingStatus();
  if (!info_) {
    PdfDictionaryPtr info_dict;
    if (trailer_) info_dict = trailer_->GetAsDictionary(PdfName::kInfo);
    info_ = std::make_unique<PdfDocumentInfo>(
        info_dict ? info_dict : std::make_shared<PdfDictionary>(), this);
  }
  return *info_;
}

PdfIndirectReference* PdfDocument::CreateNextIndirectReference() {
  CheckClosingStatus();
  return xref_->CreateNextIndirectReference(this);
}

void PdfDocument::FlushObject(PdfObject* pdf_object, bool can_be_in_obj_stm) {
  writer_->FlushObject(*pdf_object, can_be_in_obj_stm);
}

void PdfDocument::CheckClosingStatus() const {
  if (closed_)
    throw PdfException(kernel_exception_message_constant::
                           kDocumentClosedItIsImpossibleToExecuteAction);
}

std::vector<PdfIndirectReference*> PdfDocument::ListIndirectReferences() const {
  std::vector<PdfIndirectReference*> refs;
  for (int i = 0; i < xref_->size(); ++i)
    if (auto* r = xref_->Get(i)) refs.push_back(r);
  return refs;
}

Bytes PdfDocument::GetXmpMetadataBytes() const {
  if (!xmp_metadata_.empty()) return xmp_metadata_;
  if (reader_ && catalog_) {
    auto s = catalog_->GetPdfObject()->GetAsStream(PdfName::kMetadata);
    if (s && !s->IsFlushed()) return s->GetBytes();
  }
  return Bytes();
}

void PdfDocument::Close() {
  if (closed_) return;
  is_closing_ = true;
  try {
    if (writer_) {
      if (catalog_->GetPdfObject()->IsFlushed())
        throw PdfException(
            "Cannot close document with already flushed PDF Catalog.");
      if (!xmp_metadata_.empty()) {
        auto xmp = catalog_->GetPdfObject()->GetAsStream(PdfName::kMetadata);
        if (IsAppendMode() && xmp && !xmp->IsFlushed() &&
            xmp->GetIndirectReference()) {
          xmp->SetData(xmp_metadata_);
          xmp->SetModified();
        } else {
          xmp = std::make_shared<PdfStream>();
          xmp->MakeIndirect(this);
          xmp->GetOutputStream()->WriteBytes(xmp_metadata_);
          catalog_->GetPdfObject()->Put(PdfName::kMetadata, xmp);
          catalog_->GetPdfObject()->SetModified();
        }
        xmp->Put(PdfName::kType, PdfName::kMetadata);
        xmp->Put(PdfName::kSubtype, PdfName::kXML);
      }
      if (GetNumberOfPages() == 0) AddNewPage();
      PdfObjectPtr crypto;
      std::set<PdfIndirectReference*> forbidden_to_flush;
      trailer_->Put(PdfName::kInfo, GetDocumentInfo().GetPdfObject());
      trailer_->Put(PdfName::kRoot, catalog_->GetPdfObject());
      if (properties_.append_mode) {
        PdfObjectPtr page_root = catalog_->GetPageTree().GenerateTree();
        if (catalog_->GetPdfObject()->IsModified() || page_root->IsModified()) {
          catalog_->Put(PdfName::kPages, page_root);
          catalog_->GetPdfObject()->Flush(false);
        }
        if (GetDocumentInfo().GetPdfObject()->IsModified())
          GetDocumentInfo().GetPdfObject()->Flush(false);
        FlushFonts();
        if (writer_->GetCrypto() != nullptr) {
          crypto = writer_->GetCrypto()->GetPdfObject();
          if (crypto->GetIndirectReference())
            forbidden_to_flush.insert(crypto->GetIndirectReference());
          else if (auto enc_ref =
                       reader_->GetTrailer()->Get(PdfName::kEncrypt, false))
            crypto = enc_ref;
        }
        writer_->FlushModifiedWaitingObjects(forbidden_to_flush);
        for (int i = 0; i < xref_->size(); i++) {
          PdfIndirectReference* ref = xref_->Get(i);
          if (ref && !ref->IsFree() && ref->CheckState(PdfObject::kModified) &&
              !ref->CheckState(PdfObject::kFlushed) &&
              !forbidden_to_flush.count(ref))
            ref->SetFree();
        }
      } else {
        catalog_->GetPdfObject()->Put(PdfName::kPages,
                                      catalog_->GetPageTree().GenerateTree());
        for (int page_num = 1; page_num <= GetNumberOfPages(); page_num++) {
          PdfPage* page = GetPage(page_num);
          if (page) page->Flush();
        }
        catalog_->GetPdfObject()->Flush(false);
        GetDocumentInfo().GetPdfObject()->Flush(false);
        FlushFonts();
        if (writer_->GetCrypto() != nullptr) {
          crypto = writer_->GetCrypto()->GetPdfObject();
          crypto->MakeIndirect(this);
          forbidden_to_flush.insert(crypto->GetIndirectReference());
        }
        writer_->FlushWaitingObjects(forbidden_to_flush);
        for (int i = 0; i < xref_->size(); i++) {
          PdfIndirectReference* ref = xref_->Get(i);
          if (ref && !ref->IsFree() && !ref->CheckState(PdfObject::kFlushed) &&
              !forbidden_to_flush.count(ref)) {
            PdfObjectPtr object;
            if (IsFlushUnusedObjects() &&
                !ref->CheckState(PdfObject::kOriginalObjectStream) &&
                (object = ref->GetRefersToPtr(false)) != nullptr)
              object->Flush();
            else
              ref->SetFree();
          }
        }
        // Under full compression the objects flushed above go into a fresh
        // object stream; make sure it is written before the xref (it is only
        // flushed by the loop above if its own entry happens to be visited
        // after the last object was added).
        writer_->FlushWaitingObjects(forbidden_to_flush);
      }
      writer_->SetCrypto(nullptr);
      if (!properties_.append_mode && crypto) crypto->Flush(false);
      PdfObjectPtr file_id =
          PdfEncryption::CreateInfoId(original_document_id_->GetValueBytes(),
                                      modified_document_id_->GetValueBytes(),
                                      properties_.preserve_encryption_);
      xref_->WriteXrefTableAndTrailer(this, file_id, crypto);
      writer_->Flush();
    }
    catalog_->GetPageTree().ClearPageRefs();
  } catch (...) {
    if (writer_ && IsCloseWriter()) {
      try {
        writer_->Finish();
      } catch (...) {
      }
    }
    closed_ = true;
    throw;
  }
  if (writer_ && IsCloseWriter()) writer_->Finish();
  if (reader_ && IsCloseReader()) reader_->Close();
  closed_ = true;
}

std::vector<PdfPage*> PdfDocument::CopyPagesTo(int page_from, int page_to,
                                               PdfDocument& to_document,
                                               int insert_before_page) {
  std::vector<int> pages;
  for (int i = page_from; i <= page_to; i++) pages.push_back(i);
  return CopyPagesTo(pages, to_document, insert_before_page);
}

std::vector<PdfPage*> PdfDocument::CopyPagesTo(
    const std::vector<int>& pages_to_copy, PdfDocument& to_document,
    int insert_before_page) {
  CheckClosingStatus();
  std::vector<PdfPage*> copied_pages;
  int insert_at = insert_before_page < 1 ? to_document.GetNumberOfPages() + 1
                                         : insert_before_page;
  for (int page_num : pages_to_copy) {
    PdfPage* page = GetPage(page_num);
    auto new_page = page->CopyTo(&to_document);
    copied_pages.push_back(to_document.AddPage(insert_at++, new_page));
  }
  return copied_pages;
}

}  // namespace kernel
}  // namespace pdfcpp
