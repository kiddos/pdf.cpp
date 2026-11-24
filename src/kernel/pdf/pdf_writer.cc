#include "pdfcpp/kernel/pdf/pdf_writer.h"

#include "pdfcpp/kernel/crypto/pdf_encryption.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_xref_table.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfObjectStream
PdfObjectStream::PdfObjectStream()
    : PdfStream(std::make_shared<io::ByteArrayOutputStream>()) {}

std::shared_ptr<PdfObjectStream> PdfObjectStream::Create(PdfDocument* doc) {
  auto os = std::make_shared<PdfObjectStream>();
  os->index_baos_ = std::make_shared<io::ByteArrayOutputStream>();
  os->index_stream_ = std::make_unique<PdfOutputStream>(os->index_baos_);
  os->MakeIndirect(doc, doc->GetXref()->CreateNewIndirectReference(doc));
  os->GetOutputStream()->SetDocument(doc);
  os->Put(PdfName::kType, PdfName::kObjStm);
  os->Put(PdfName::kN, os->size_);
  os->Put(PdfName::kFirst, MakeNumber(0));
  return os;
}

void PdfObjectStream::AddObject(PdfObject& object) {
  if (size_->IntValue() == kMaxObjStreamSize)
    throw PdfException("PdfObjectStream reached max size.");
  PdfOutputStream* output_stream = GetOutputStream();
  index_stream_->WriteInteger(object.GetIndirectReference()->GetObjNumber())
      .WriteSpace()
      .WriteLong(output_stream->GetCurrentPos())
      .WriteSpace();
  output_stream->Write(object);
  object.GetIndirectReference()->SetObjStreamNumber(
      GetIndirectReference()->GetObjNumber());
  object.GetIndirectReference()->SetIndex(size_->IntValue());
  output_stream->WriteSpace();
  size_->Increment();
  GetAsNumber(PdfName::kFirst)
      ->SetValue(static_cast<int>(index_stream_->GetCurrentPos()));
}

// ============================================================ PdfWriter
PdfWriter::PdfWriter(const std::string& filename,
                     const WriterProperties& properties)
    : PdfOutputStream(std::make_shared<io::FileOutputStream>(filename)),
      properties_(properties) {}

PdfWriter::PdfWriter(std::shared_ptr<io::OutputStream> os,
                     const WriterProperties& properties)
    : PdfOutputStream(std::move(os)), properties_(properties) {}

PdfWriter::PdfWriter(io::OutputStream& os, const WriterProperties& properties)
    : PdfOutputStream(io::Borrow(os)), properties_(properties) {
  SetCloseStream(false);
}

PdfWriter::~PdfWriter() {
  try {
    if (!finished_) Flush();
  } catch (...) {
  }
}

void PdfWriter::InitCryptoIfSpecified(const PdfVersion& version) {
  if (!properties_.IsStandardEncryptionUsed()) return;
  const auto& ep = properties_.encryption_properties;
  owned_crypto_ = std::make_unique<PdfEncryption>(
      ep.user_password_, ep.owner_password_, ep.standard_encrypt_permissions_,
      ep.encryption_algorithm_,
      document_->GetOriginalDocumentId()->GetValueBytes(), version);
  crypto_ = owned_crypto_.get();
}

void PdfWriter::FlushObject(PdfObject& pdf_object, bool can_be_in_obj_stm) {
  // Keep the object alive during flushing; releasing content may drop the last
  // owner.
  PdfObjectPtr keep_alive = pdf_object.shared_from_this();
  PdfIndirectReference* indirect_reference = pdf_object.GetIndirectReference();
  if (IsFullCompression() && can_be_in_obj_stm) {
    GetObjectStream()->AddObject(pdf_object);
  } else {
    indirect_reference->SetOffset(GetCurrentPos());
    WriteToBody(pdf_object);
  }
  indirect_reference->SetState(PdfObject::kFlushed)
      ->ClearState(PdfObject::kMustBeFlushed);
  switch (pdf_object.GetType()) {
    case PdfObject::kArray: {
      auto& array = static_cast<PdfArray&>(pdf_object);
      MarkArrayContentToFlush(array);
      array.ReleaseContent();
      break;
    }
    case PdfObject::kStream:
    case PdfObject::kDictionary: {
      auto& dictionary = static_cast<PdfDictionary&>(pdf_object);
      MarkDictionaryContentToFlush(dictionary);
      dictionary.ReleaseContent();
      break;
    }
    case PdfObject::kIndirectReference:
      MarkObjectToFlush(
          static_cast<PdfIndirectReference&>(pdf_object).GetRefersTo(false));
      break;
    default:
      break;
  }
  // Flushed objects are no longer needed in memory.
  if (indirect_reference->RefersToRaw().get() == &pdf_object &&
      !pdf_object.IsReleaseForbidden()) {
    // keep reference (Java keeps refersTo for flushed objects too); nothing to
    // do
  }
}

static bool CheckTypeOfPdfDictionary(const PdfObject* dictionary,
                                     const PdfNamePtr& expected_type) {
  return dictionary->IsDictionary() &&
         NameEquals(static_cast<const PdfDictionary*>(dictionary)
                        ->GetAsName(PdfName::kType),
                    expected_type);
}

static void SerializeForSmartMode(const PdfObject* obj, std::string& out,
                                  int depth,
                                  std::set<const PdfObject*>& visited);

PdfObjectPtr PdfWriter::CopyObject(PdfObject* obj, PdfDocument* document_to,
                                   bool allow_duplicating,
                                   ICopyFilter& copy_filter) {
  PdfObjectPtr obj_ptr;
  if (obj && obj->IsIndirectReference())
    obj_ptr = static_cast<PdfIndirectReference*>(obj)->GetRefersToPtr(true);
  else if (obj)
    obj_ptr = obj->shared_from_this();
  if (!obj_ptr) obj_ptr = PdfNull::kPdfNull;
  if (CheckTypeOfPdfDictionary(obj_ptr.get(), PdfName::kCatalog))
    obj_ptr = PdfNull::kPdfNull;
  PdfIndirectReference* indirect_reference = obj_ptr->GetIndirectReference();
  bool try_to_find_duplicate =
      !allow_duplicating && indirect_reference != nullptr;
  if (try_to_find_duplicate) {
    auto it = copied_objects_.find(indirect_reference);
    if (it != copied_objects_.end()) return it->second->GetRefersToPtr();
  }
  std::string serialized;
  if (properties_.smart_mode && try_to_find_duplicate &&
      !CheckTypeOfPdfDictionary(obj_ptr.get(), PdfName::kPage)) {
    std::set<const PdfObject*> visited;
    SerializeForSmartMode(obj_ptr.get(), serialized, 0, visited);
    auto it = smart_mode_cache_.find(serialized);
    if (it != smart_mode_cache_.end()) {
      copied_objects_[indirect_reference] = it->second;
      return it->second->GetRefersToPtr();
    }
  }
  PdfObjectPtr new_object = obj_ptr->NewInstance();
  if (new_object.get() == PdfNull::kPdfNull.get())
    new_object = std::make_shared<PdfNull>();
  if (indirect_reference != nullptr) {
    PdfIndirectReference* ind_ref =
        new_object->MakeIndirect(document_to)->GetIndirectReference();
    if (!serialized.empty()) smart_mode_cache_[serialized] = ind_ref;
    copied_objects_[indirect_reference] = ind_ref;
  }
  new_object->CopyContent(*obj_ptr, document_to, copy_filter);
  return new_object;
}

static void SerializeForSmartMode(const PdfObject* obj, std::string& out,
                                  int depth,
                                  std::set<const PdfObject*>& visited) {
  if (!obj || depth > 100) {
    out += "null";
    return;
  }
  if (obj->IsIndirectReference()) {
    auto* ref = const_cast<PdfIndirectReference*>(
        static_cast<const PdfIndirectReference*>(obj));
    PdfObject* target = ref->GetRefersTo(true);
    if (!target) {
      out += "null";
      return;
    }
    if (visited.count(target)) {
      out += "$ref" + std::to_string(ref->GetObjNumber());
      return;
    }
    // Pages are never merged: serialize by identity
    if (target->IsDictionary() &&
        NameEquals(
            static_cast<PdfDictionary*>(target)->GetAsName(PdfName::kType),
            PdfName::kPage)) {
      out += "$page" + std::to_string(reinterpret_cast<uintptr_t>(target));
      return;
    }
    obj = target;
  }
  visited.insert(obj);
  switch (obj->GetType()) {
    case PdfObject::kDictionary:
    case PdfObject::kStream: {
      auto* d = static_cast<const PdfDictionary*>(obj);
      out += "<<";
      for (const auto& kv : d->EntrySet()) {
        if (NameEquals(kv.first, PdfName::kParent)) continue;
        out += "/" + kv.first->GetValue();
        SerializeForSmartMode(kv.second.get(), out, depth + 1, visited);
      }
      out += ">>";
      if (obj->GetType() == PdfObject::kStream) {
        Bytes b = const_cast<PdfStream*>(static_cast<const PdfStream*>(obj))
                      ->GetBytes(false);
        out += "$stream";
        out.append(reinterpret_cast<const char*>(b.data()), b.size());
      }
      break;
    }
    case PdfObject::kArray: {
      out += "[";
      for (const auto& e : *static_cast<const PdfArray*>(obj)) {
        SerializeForSmartMode(e.get(), out, depth + 1, visited);
        out += " ";
      }
      out += "]";
      break;
    }
    case PdfObject::kString: {
      Bytes b = static_cast<const PdfString*>(obj)->GetValueBytes();
      out += "(" + std::string(b.begin(), b.end()) + ")";
      break;
    }
    case PdfObject::kName:
      out += "/" + static_cast<const PdfName*>(obj)->GetValue();
      break;
    default:
      out += obj->ToString();
  }
  visited.erase(obj);
}

void PdfWriter::WriteToBody(PdfObject& pdf_obj) {
  if (crypto_ != nullptr)
    crypto_->SetHashKeyForNextObject(
        pdf_obj.GetIndirectReference()->GetObjNumber(),
        pdf_obj.GetIndirectReference()->GetGenNumber());
  WriteInteger(pdf_obj.GetIndirectReference()->GetObjNumber())
      .WriteSpace()
      .WriteInteger(pdf_obj.GetIndirectReference()->GetGenNumber())
      .WriteString(" obj\n");
  Write(pdf_obj);
  WriteString("\nendobj\n");
}

void PdfWriter::WriteHeader() {
  WriteByte('%');
  WriteString(document_->GetPdfVersion().ToString());
  WriteString("\n%\xe2\xe3\xcf\xd3\n");
}

void PdfWriter::FlushWaitingObjects(
    const std::set<PdfIndirectReference*>& forbidden_to_flush) {
  PdfXrefTable* xref = document_->GetXref();
  bool need_flush = true;
  while (need_flush) {
    need_flush = false;
    for (int i = 1; i < xref->size(); i++) {
      PdfIndirectReference* indirect_reference = xref->Get(i);
      if (indirect_reference != nullptr && !indirect_reference->IsFree() &&
          indirect_reference->CheckState(PdfObject::kMustBeFlushed) &&
          !forbidden_to_flush.count(indirect_reference)) {
        PdfObjectPtr obj = indirect_reference->GetRefersToPtr(false);
        if (obj) {
          obj->Flush();
          need_flush = true;
        }
      }
    }
  }
  if (object_stream_ && object_stream_->GetSize() > 0) {
    object_stream_->Flush();
    object_stream_ = nullptr;
  }
}

void PdfWriter::FlushModifiedWaitingObjects(
    const std::set<PdfIndirectReference*>& forbidden_to_flush) {
  PdfXrefTable* xref = document_->GetXref();
  // Java makes a single pass. Repeat until nothing new gets flushed, so objects
  // that become modified (or are created) while flushing others - including
  // lower-numbered ones - are written instead of being freed by
  // PdfDocument::close().
  bool progress = true;
  while (progress) {
    progress = false;
    for (int i = 1; i < xref->size(); i++) {
      PdfIndirectReference* indirect_reference = xref->Get(i);
      if (indirect_reference == nullptr || indirect_reference->IsFree() ||
          indirect_reference->CheckState(PdfObject::kFlushed) ||
          forbidden_to_flush.count(indirect_reference))
        continue;
      if (indirect_reference->CheckState(PdfObject::kModified)) {
        PdfObjectPtr obj = indirect_reference->GetRefersToPtr(false);
        if (obj && obj.get() != object_stream_.get()) {
          obj->Flush();
          if (indirect_reference->CheckState(PdfObject::kFlushed))
            progress = true;
        }
      }
    }
  }
  if (object_stream_ && object_stream_->GetSize() > 0) {
    object_stream_->Flush();
    object_stream_ = nullptr;
  }
}

void PdfWriter::Finish() {
  if (finished_) return;
  finished_ = true;
  Flush();
  Close();
}

PdfObjectStream* PdfWriter::GetObjectStream() {
  if (!IsFullCompression()) return nullptr;
  if (!object_stream_) {
    object_stream_ = PdfObjectStream::Create(document_);
  } else if (object_stream_->GetSize() == PdfObjectStream::kMaxObjStreamSize) {
    object_stream_->Flush();
    object_stream_ = PdfObjectStream::Create(document_);
  }
  return object_stream_.get();
}

void PdfWriter::FlushCopiedObjects(int64_t doc_id) {
  std::vector<PdfIndirectReference*> remove;
  for (auto& kv : copied_objects_) {
    PdfDocument* document = kv.first->GetDocument();
    if (document != nullptr && document->GetDocumentId() == doc_id) {
      if (auto obj = kv.second->GetRefersToPtr(false)) {
        obj->Flush();
        remove.push_back(kv.first);
      }
    }
  }
  for (auto* r : remove) copied_objects_.erase(r);
}

void PdfWriter::MarkArrayContentToFlush(PdfArray& array) {
  for (int i = 0; i < array.size(); i++)
    MarkObjectToFlush(array.Get(i, false).get());
}

void PdfWriter::MarkDictionaryContentToFlush(PdfDictionary& dictionary) {
  for (const auto& item : dictionary.Values(false))
    MarkObjectToFlush(item.get());
}

void PdfWriter::MarkObjectToFlush(PdfObject* pdf_object) {
  if (pdf_object == nullptr) return;
  PdfIndirectReference* indirect_reference = pdf_object->GetIndirectReference();
  if (indirect_reference != nullptr) {
    if (!indirect_reference->CheckState(PdfObject::kFlushed))
      indirect_reference->SetState(PdfObject::kMustBeFlushed);
  } else if (pdf_object->GetType() == PdfObject::kIndirectReference) {
    if (!pdf_object->CheckState(PdfObject::kFlushed))
      pdf_object->SetState(PdfObject::kMustBeFlushed);
  } else if (pdf_object->GetType() == PdfObject::kArray) {
    MarkArrayContentToFlush(static_cast<PdfArray&>(*pdf_object));
  } else if (pdf_object->GetType() == PdfObject::kDictionary) {
    MarkDictionaryContentToFlush(static_cast<PdfDictionary&>(*pdf_object));
  }
}

}  // namespace kernel
}  // namespace pdfcpp
