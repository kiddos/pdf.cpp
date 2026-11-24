#include "pdfcpp/kernel/pdf/pdf_xref_table.h"

#include <set>

#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

namespace pdfcpp {
namespace kernel {

static constexpr int64_t kMaxOffsetInCrossReferenceStream = 9999999999LL;

PdfXrefTable::PdfXrefTable(int capacity) {
  if (capacity < 1) capacity = 32;
  xref_.resize(capacity);
  auto zero =
      std::make_shared<PdfIndirectReference>(nullptr, 0, kMaxGeneration, 0);
  zero->SetState(PdfObject::kFree);
  Add(zero);
}

PdfXrefTable::~PdfXrefTable() { ReleaseAll(); }

void PdfXrefTable::ReleaseAll() {
  // Break strong cycles: references own their objects, objects may own
  // references. Objects are kept alive until all links are cut, then released
  // together.
  std::vector<PdfObjectPtr> keep_alive;
  for (auto& ref : xref_) {
    if (ref && ref->RefersToRaw()) {
      keep_alive.push_back(ref->RefersToRaw());
      ref->SetRefersTo(nullptr);
    }
  }
  for (auto& obj : keep_alive) {
    if (obj.use_count() == 1) obj->SetIndirectReference(nullptr);
  }
}

PdfIndirectReference* PdfXrefTable::Add(PdfIndirectReferencePtr reference) {
  if (!reference) return nullptr;
  int obj_nr = reference->GetObjNumber();
  count_ = std::max(count_, obj_nr);
  EnsureCount(obj_nr);
  xref_[obj_nr] = std::move(reference);
  return xref_[obj_nr].get();
}

int PdfXrefTable::GetCountOfIndirectObjects() const {
  int n = 0;
  for (const auto& ref : xref_)
    if (ref && !ref->IsFree()) n++;
  return n;
}

PdfIndirectReference* PdfXrefTable::Get(int index) const {
  if (index > count_ || index < 0 || index >= static_cast<int>(xref_.size()))
    return nullptr;
  return xref_[index].get();
}

PdfIndirectReferencePtr PdfXrefTable::GetPtr(int index) const {
  if (index > count_ || index < 0 || index >= static_cast<int>(xref_.size()))
    return nullptr;
  return xref_[index];
}

PdfIndirectReference* PdfXrefTable::CreateNextIndirectReference(
    PdfDocument* document) {
  auto reference = std::make_shared<PdfIndirectReference>(document, ++count_);
  PdfIndirectReference* r = Add(reference);
  r->SetState(PdfObject::kModified);
  return r;
}

void PdfXrefTable::FreeReference(PdfIndirectReference* reference) {
  if (reference->IsFree()) return;
  if (reference->CheckState(PdfObject::kMustBeFlushed)) return;
  if (reference->CheckState(PdfObject::kFlushed)) return;
  reference->SetState(PdfObject::kFree)->SetState(PdfObject::kModified);
  AppendNewRefToFreeList(reference);
  if (reference->GetGenNumber() < kMaxGeneration)
    reference->SetGenNumber(reference->GetGenNumber() + 1);
}

void PdfXrefTable::SetCapacity(int capacity) {
  if (capacity > static_cast<int>(xref_.size())) ExtendXref(capacity);
}

void PdfXrefTable::EnsureCount(int count) {
  if (count >= static_cast<int>(xref_.size())) ExtendXref(count << 1);
}

void PdfXrefTable::ExtendXref(int capacity) { xref_.resize(capacity); }

std::vector<int> PdfXrefTable::CreateSections(
    PdfDocument* document, bool drop_objects_from_object_stream) const {
  std::vector<int> sections;
  int first = 0, len = 0;
  for (int i = 0; i < size(); i++) {
    PdfIndirectReference* reference =
        i < static_cast<int>(xref_.size()) ? xref_[i].get() : nullptr;
    if (document->IsAppendMode() && reference != nullptr &&
        (!reference->CheckState(PdfObject::kModified) ||
         (drop_objects_from_object_stream &&
          reference->GetObjStreamNumber() != 0)))
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

int PdfXrefTable::GetOffsetSize(int64_t startxref) {
  int size = 5;
  int64_t mask = 0xff00000000LL;
  for (; size > 1; size--) {
    if ((mask & startxref) != 0) break;
    mask >>= 8;
  }
  return size;
}

void PdfXrefTable::WriteXrefTableAndTrailer(PdfDocument* document,
                                            const PdfObjectPtr& file_id,
                                            const PdfObjectPtr& crypto) {
  PdfWriter* writer = document->GetWriter();
  if (!document->IsAppendMode()) {
    for (int i = count_; i > 0; --i) {
      PdfIndirectReference* last_ref = Get(i);
      if (last_ref == nullptr || last_ref->IsFree()) {
        RemoveFreeRefFromList(i);
        --count_;
      } else {
        break;
      }
    }
  }
  PdfStreamPtr xref_stream;
  if (writer->IsFullCompression()) {
    xref_stream = std::make_shared<PdfStream>();
    xref_stream->MakeIndirect(document);
  }
  std::vector<int> sections = CreateSections(document, false);
  bool no_modified_objects =
      sections.empty() || (xref_stream && sections.size() == 2 &&
                           sections[0] == count_ && sections[1] == 1);
  if (document->IsAppendMode() && no_modified_objects) return;

  int64_t startxref = writer->GetCurrentPos();
  int64_t x_ref_stm_pos = -1;
  if (xref_stream) {
    xref_stream->Put(PdfName::kType, PdfName::kXRef);
    xref_stream->Put(PdfName::kID, file_id);
    if (crypto) xref_stream->Put(PdfName::kEncrypt, crypto);
    xref_stream->Put(PdfName::kSize, MakeNumber(size()));
    int offset_size = GetOffsetSize(std::max<int64_t>(startxref, size()));
    xref_stream->Put(PdfName::kW, std::make_shared<PdfArray>(
                                      std::vector<int>{1, offset_size, 2}));
    if (auto info = document->GetTrailer()->Get(PdfName::kInfo, false))
      xref_stream->Put(PdfName::kInfo, info);
    xref_stream->Put(PdfName::kRoot, document->GetCatalog().GetPdfObject());
    auto index = std::make_shared<PdfArray>();
    for (int s : sections) index->Add(MakeNumber(s));
    if (document->IsAppendMode() && !document->GetReader()->HasHybridXref())
      xref_stream->Put(PdfName::kPrev,
                       std::make_shared<PdfNumber>(static_cast<double>(
                           document->GetReader()->GetLastXref())));
    xref_stream->Put(PdfName::kIndex, index);
    xref_stream->GetIndirectReference()->SetOffset(startxref);
    auto* out = xref_stream->GetOutputStream();
    for (size_t k = 0; k < sections.size(); k += 2) {
      int first = sections[k], len = sections[k + 1];
      for (int i = first; i < first + len; i++) {
        PdfIndirectReference* reference = Get(i);
        if (reference->IsFree()) {
          out->Write(0);
          out->WriteBigEndian(reference->GetOffset(), offset_size);
          out->WriteBigEndian(reference->GetGenNumber(), 2);
        } else if (reference->GetObjStreamNumber() == 0) {
          out->Write(1);
          out->WriteBigEndian(reference->GetOffset(), offset_size);
          out->WriteBigEndian(reference->GetGenNumber(), 2);
        } else {
          out->Write(2);
          out->WriteBigEndian(reference->GetObjStreamNumber(), offset_size);
          out->WriteBigEndian(reference->GetIndex(), 2);
        }
      }
    }
    xref_stream->Flush();
    x_ref_stm_pos = startxref;
  }
  bool needs_regular_xref =
      !writer->IsFullCompression() ||
      (document->IsAppendMode() && document->GetReader()->HasHybridXref());
  if (needs_regular_xref) {
    startxref = writer->GetCurrentPos();
    writer->WriteString("xref\n");
    if (x_ref_stm_pos != -1) sections = CreateSections(document, true);
    for (size_t k = 0; k < sections.size(); k += 2) {
      int first = sections[k], len = sections[k + 1];
      writer->WriteInteger(first).WriteSpace().WriteInteger(len).WriteByte(
          '\n');
      for (int i = first; i < first + len; i++) {
        PdfIndirectReference* reference = Get(i);
        if (reference->GetOffset() > kMaxOffsetInCrossReferenceStream)
          throw PdfException("Xref table has an entry with too big offset.");
        std::string off = "0000000000" + std::to_string(reference->GetOffset());
        std::string gen = "00000" + std::to_string(reference->GetGenNumber());
        writer->WriteString(off.substr(off.size() - 10))
            .WriteSpace()
            .WriteString(gen.substr(gen.size() - 5))
            .WriteSpace();
        writer->WriteString(reference->IsFree() ? "f \n" : "n \n");
      }
    }
    PdfDictionaryPtr trailer = document->GetTrailer();
    trailer->Remove(PdfName::kW);
    trailer->Remove(PdfName::kIndex);
    trailer->Remove(PdfName::kType);
    trailer->Remove(PdfName::kLength);
    trailer->Put(PdfName::kSize, MakeNumber(size()));
    trailer->Put(PdfName::kID, file_id);
    if (x_ref_stm_pos != -1)
      trailer->Put(PdfName::kXRefStm, std::make_shared<PdfNumber>(
                                          static_cast<double>(x_ref_stm_pos)));
    if (crypto) trailer->Put(PdfName::kEncrypt, crypto);
    writer->WriteString("trailer\n");
    if (document->IsAppendMode())
      trailer->Put(PdfName::kPrev,
                   std::make_shared<PdfNumber>(static_cast<double>(
                       document->GetReader()->GetLastXref())));
    writer->Write(*trailer);
    writer->Write('\n');
  }
  writer->WriteString("startxref\n")
      .WriteLong(startxref)
      .WriteString("\n%%EOF\n");
  free_references_linked_list_.clear();
}

void PdfXrefTable::InitFreeReferencesList(PdfDocument* pdf_document) {
  free_references_linked_list_.clear();
  xref_[0]->SetState(PdfObject::kFree);
  std::set<int> free_references;
  for (int i = 1; i < size() && i < static_cast<int>(xref_.size()); ++i) {
    PdfIndirectReference* ref = xref_[i].get();
    if (ref == nullptr || ref->IsFree()) free_references.insert(i);
  }
  PdfIndirectReference* prev_free_ref = xref_[0].get();
  while (!free_references.empty()) {
    int curr_free_ref_obj_nr = -1;
    if (prev_free_ref->GetOffset() <= 2147483647LL)
      curr_free_ref_obj_nr = static_cast<int>(prev_free_ref->GetOffset());
    if (free_references.find(curr_free_ref_obj_nr) == free_references.end() ||
        curr_free_ref_obj_nr >= static_cast<int>(xref_.size()) ||
        xref_[curr_free_ref_obj_nr] == nullptr)
      break;
    free_references_linked_list_[curr_free_ref_obj_nr] = prev_free_ref;
    prev_free_ref = xref_[curr_free_ref_obj_nr].get();
    free_references.erase(curr_free_ref_obj_nr);
  }
  while (!free_references.empty()) {
    int next = *free_references.begin();
    free_references.erase(free_references.begin());
    if (xref_[next] == nullptr) {
      if (pdf_document->IsAppendMode()) continue;
      auto r = std::make_shared<PdfIndirectReference>(pdf_document, next, 0);
      r->SetState(PdfObject::kFree)->SetState(PdfObject::kModified);
      xref_[next] = r;
    } else if (xref_[next]->GetGenNumber() == kMaxGeneration &&
               xref_[next]->GetOffset() == 0) {
      continue;
    }
    if (prev_free_ref->GetOffset() != next) {
      prev_free_ref->SetState(PdfObject::kModified);
      prev_free_ref->SetOffset(next);
    }
    free_references_linked_list_[next] = prev_free_ref;
    prev_free_ref = xref_[next].get();
  }
  if (prev_free_ref->GetOffset() != 0) {
    prev_free_ref->SetState(PdfObject::kModified);
    prev_free_ref->SetOffset(0);
  }
  free_references_linked_list_[0] = prev_free_ref;
}

void PdfXrefTable::clear() {
  for (int i = 1; i <= count_ && i < static_cast<int>(xref_.size()); i++) {
    if (xref_[i] && xref_[i]->IsFree()) continue;
    xref_[i] = nullptr;
  }
  count_ = 1;
}

void PdfXrefTable::ClearAllReferences() {
  for (int i = 1; i <= count_ && i < static_cast<int>(xref_.size()); i++)
    xref_[i] = nullptr;
  count_ = 1;
}

void PdfXrefTable::AppendNewRefToFreeList(PdfIndirectReference* reference) {
  reference->SetOffset(0);
  if (free_references_linked_list_.empty()) return;
  PdfIndirectReference* last_free_ref = free_references_linked_list_[0];
  last_free_ref->SetState(PdfObject::kModified);
  last_free_ref->SetOffset(reference->GetObjNumber());
  free_references_linked_list_[reference->GetObjNumber()] = last_free_ref;
  free_references_linked_list_[0] = reference;
}

PdfIndirectReference* PdfXrefTable::RemoveFreeRefFromList(int free_ref_obj_nr) {
  if (free_references_linked_list_.empty() || free_ref_obj_nr == 0)
    return nullptr;
  if (free_ref_obj_nr >= static_cast<int>(xref_.size())) return nullptr;
  PdfIndirectReference* free_ref = xref_[free_ref_obj_nr].get();
  if (free_ref == nullptr || !free_ref->IsFree()) return nullptr;
  auto it = free_references_linked_list_.find(free_ref->GetObjNumber());
  if (it != free_references_linked_list_.end()) {
    PdfIndirectReference* prev_free_ref = it->second;
    free_references_linked_list_.erase(it);
    if (prev_free_ref != nullptr) {
      free_references_linked_list_[static_cast<int>(free_ref->GetOffset())] =
          prev_free_ref;
      prev_free_ref->SetState(PdfObject::kModified);
      prev_free_ref->SetOffset(free_ref->GetOffset());
    }
  }
  return free_ref;
}

}  // namespace kernel
}  // namespace pdfcpp
