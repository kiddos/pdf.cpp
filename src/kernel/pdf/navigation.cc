#include "pdfcpp/kernel/pdf/navigation.h"

#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ PdfDestination
static PdfArrayPtr DestArray(PdfPage* page, const PdfNamePtr& type) {
  auto arr = std::make_shared<PdfArray>();
  arr->Add(page->GetPdfObject());
  arr->Add(type);
  return arr;
}

std::shared_ptr<PdfDestination> PdfDestination::CreateXyz(PdfPage* page,
                                                          float left, float top,
                                                          float zoom) {
  auto arr = DestArray(page, PdfName::kXYZ);
  arr->Add(MakeNumber(static_cast<double>(left)));
  arr->Add(MakeNumber(static_cast<double>(top)));
  arr->Add(MakeNumber(static_cast<double>(zoom)));
  return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::CreateFit(PdfPage* page) {
  return std::make_shared<PdfDestination>(DestArray(page, PdfName::kFit));
}

std::shared_ptr<PdfDestination> PdfDestination::CreateFitH(PdfPage* page,
                                                           float top) {
  auto arr = DestArray(page, PdfName::kFitH);
  arr->Add(MakeNumber(static_cast<double>(top)));
  return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::CreateFitR(
    PdfPage* page, const Rectangle& rect) {
  auto arr = DestArray(page, PdfName::kFitR);
  arr->Add(MakeNumber(static_cast<double>(rect.GetLeft())));
  arr->Add(MakeNumber(static_cast<double>(rect.GetBottom())));
  arr->Add(MakeNumber(static_cast<double>(rect.GetRight())));
  arr->Add(MakeNumber(static_cast<double>(rect.GetTop())));
  return std::make_shared<PdfDestination>(arr);
}

std::shared_ptr<PdfDestination> PdfDestination::CreateNamed(
    const std::string& name) {
  return std::make_shared<PdfDestination>(std::make_shared<PdfString>(name));
}

// ============================================================ PdfAction
std::shared_ptr<PdfAction> PdfAction::CreateUri(const std::string& uri,
                                                bool is_map) {
  auto action = std::make_shared<PdfAction>();
  action->Put(PdfName::kS, PdfName::kURI)
      .Put(PdfName::kURI, std::make_shared<PdfString>(uri));
  if (is_map) action->Put(PdfName::kIsMap, PdfBoolean::kTrueValue);
  return action;
}

std::shared_ptr<PdfAction> PdfAction::CreateGoTo(
    const std::shared_ptr<PdfDestination>& destination) {
  auto action = std::make_shared<PdfAction>();
  action->Put(PdfName::kS, PdfName::kGoTo)
      .Put(PdfName::kD, destination->GetPdfObject());
  return action;
}

std::shared_ptr<PdfAction> PdfAction::CreateGoTo(
    const std::string& named_destination) {
  return CreateGoTo(PdfDestination::CreateNamed(named_destination));
}

std::shared_ptr<PdfAction> PdfAction::CreateGoToR(const std::string& filename,
                                                  int page_num,
                                                  bool new_window) {
  auto action = std::make_shared<PdfAction>();
  auto dest = std::make_shared<PdfArray>();
  dest->Add(MakeNumber(page_num - 1));
  dest->Add(PdfName::kFit);
  action->Put(PdfName::kS, PdfName::kGoToR)
      .Put(PdfName::kF, std::make_shared<PdfString>(filename))
      .Put(PdfName::kD, dest);
  if (new_window) action->Put(PdfName::kNewWindow, PdfBoolean::kTrueValue);
  return action;
}

std::shared_ptr<PdfAction> PdfAction::CreateJavaScript(
    const std::string& java_script) {
  auto action = std::make_shared<PdfAction>();
  action->Put(PdfName::kS, PdfName::kJavaScript)
      .Put(PdfName::kJS,
           std::make_shared<PdfString>(java_script, "UnicodeBig"));
  return action;
}

std::shared_ptr<PdfAction> PdfAction::CreateNamed(
    const PdfNamePtr& named_action) {
  auto action = std::make_shared<PdfAction>();
  action->Put(PdfName::kS, PdfName::kNamed).Put(PdfName::kN, named_action);
  return action;
}

PdfAction& PdfAction::Next(const std::shared_ptr<PdfAction>& next_action) {
  PdfObjectPtr current = GetPdfObject()->Get(PdfName::kNext);
  if (!current) {
    Put(PdfName::kNext, next_action->GetPdfObject());
  } else if (current->IsArray()) {
    std::static_pointer_cast<PdfArray>(current)->Add(
        next_action->GetPdfObject());
  } else {
    auto arr = std::make_shared<PdfArray>();
    arr->Add(current);
    arr->Add(next_action->GetPdfObject());
    Put(PdfName::kNext, arr);
  }
  return *this;
}

// ============================================================ PdfAnnotation
PdfAnnotation::PdfAnnotation(const PdfNamePtr& subtype, const Rectangle& rect)
    : PdfObjectWrapper<PdfDictionary>(std::make_shared<PdfDictionary>()) {
  GetPdfObject()->Put(PdfName::kType, PdfName::kAnnot);
  GetPdfObject()->Put(PdfName::kSubtype, subtype);
  GetPdfObject()->Put(PdfName::kRect, std::make_shared<PdfArray>(rect));
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::CreateLink(
    const Rectangle& rect) {
  auto a = std::make_shared<PdfAnnotation>(PdfName::kLink, rect);
  a->SetBorder(0, 0, 0);
  return a;
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::CreateText(
    const Rectangle& rect) {
  return std::make_shared<PdfAnnotation>(PdfName::kText, rect);
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::CreateSquare(
    const Rectangle& rect) {
  return std::make_shared<PdfAnnotation>(PdfName::kSquare, rect);
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::CreateFreeText(
    const Rectangle& rect, const std::string& contents) {
  auto a = std::make_shared<PdfAnnotation>(PdfName::kFreeText, rect);
  a->SetContents(contents);
  a->Put(PdfName::kDA, std::make_shared<PdfString>("/Helv 12 Tf 0 g"));
  return a;
}

std::shared_ptr<PdfAnnotation> PdfAnnotation::CreateHighlight(
    const Rectangle& rect, const std::vector<float>& quad_points) {
  auto a = std::make_shared<PdfAnnotation>(PdfName::kHighlight, rect);
  a->Put(PdfName::kQuadPoints, std::make_shared<PdfArray>(quad_points));
  return a;
}

PdfAnnotation& PdfAnnotation::SetNormalAppearance(
    const PdfStreamPtr& appearance) {
  auto ap = GetPdfObject()->GetAsDictionary(PdfName::kAP);
  if (!ap) {
    ap = std::make_shared<PdfDictionary>();
    Put(PdfName::kAP, ap);
  }
  ap->Put(PdfName::kN, appearance);
  return *this;
}

// ============================================================ PdfOutline
PdfOutline::PdfOutline(const std::string& title, PdfDictionaryPtr content,
                       PdfDocument* document)
    : title_(title), content_(std::move(content)), document_(document) {}

std::shared_ptr<PdfOutline> PdfOutline::GetOutlines(PdfDocument& document,
                                                    bool) {
  auto& catalog = document.GetCatalog();
  auto outlines_dict =
      catalog.GetPdfObject()->GetAsDictionary(PdfName::kOutlines);
  bool created = false;
  if (!outlines_dict) {
    outlines_dict = std::make_shared<PdfDictionary>();
    outlines_dict->Put(PdfName::kType, PdfName::kOutlines);
    if (document.GetWriter()) {
      outlines_dict->MakeIndirect(&document);
      catalog.Put(PdfName::kOutlines, outlines_dict);
      catalog.Put(PdfName::kPageMode, PdfName::kUseOutlines);
    }
    created = true;
  }
  auto root =
      std::make_shared<PdfOutline>("Outlines", outlines_dict, &document);
  if (!created) root->InitFromDictionary(0);
  return root;
}

void PdfOutline::InitFromDictionary(int depth) {
  if (depth > 64) return;
  auto child = content_->GetAsDictionary(PdfName::kFirst);
  std::set<const PdfObject*> seen;
  while (child && !seen.count(child.get())) {
    seen.insert(child.get());
    auto title_str = child->GetAsString(PdfName::kTitle);
    auto outline = std::make_shared<PdfOutline>(
        title_str ? title_str->ToUnicodeString() : std::string(), child,
        document_);
    outline->parent_ = this;
    auto count = child->GetAsNumber(PdfName::kCount);
    outline->open_ = !count || count->IntValue() >= 0;
    outline->InitFromDictionary(depth + 1);
    children_.push_back(outline);
    child = child->GetAsDictionary(PdfName::kNext);
  }
}

void PdfOutline::SetTitle(const std::string& title) {
  title_ = title;
  content_->Put(PdfName::kTitle,
                std::make_shared<PdfString>(title, "UnicodeBig"));
  content_->SetModified();
}

std::shared_ptr<PdfOutline> PdfOutline::AddOutline(const std::string& title,
                                                   int position) {
  auto dict = std::make_shared<PdfDictionary>();
  if (document_ && document_->GetWriter()) dict->MakeIndirect(document_);
  auto outline = std::make_shared<PdfOutline>(title, dict, document_);
  outline->SetTitle(title);
  outline->parent_ = this;
  if (position < 0 || position > static_cast<int>(children_.size()))
    children_.push_back(outline);
  else
    children_.insert(children_.begin() + position, outline);
  RebuildLinks();
  return outline;
}

void PdfOutline::RemoveOutline() {
  if (!parent_) return;
  // The parent's children_ may hold the only owning reference to this outline:
  // keep it alive (and work through a local parent pointer) until we are done
  // touching members.
  PdfOutline* parent = parent_;
  std::shared_ptr<PdfOutline> self;
  auto& siblings = parent->children_;
  for (auto it = siblings.begin(); it != siblings.end();) {
    if (it->get() == this) {
      if (!self) self = *it;
      it = siblings.erase(it);
    } else {
      ++it;
    }
  }
  parent_ = nullptr;
  content_->Remove(PdfName::kParent);
  content_->Remove(PdfName::kPrev);
  content_->Remove(PdfName::kNext);
  parent->RebuildLinks();
}

void PdfOutline::SetOpen(bool open) {
  if (open_ == open) return;
  open_ = open;
  if (!children_.empty())
    RebuildLinks();  // ancestors' visible counts depend on this item's state
}

int PdfOutline::VisibleDescendants() const {
  int n = 0;
  for (const auto& c : children_) {
    ++n;
    if (c->open_) n += c->VisibleDescendants();
  }
  return n;
}

void PdfOutline::RebuildLinks() {
  if (children_.empty()) {
    content_->Remove(PdfName::kFirst);
    content_->Remove(PdfName::kLast);
    content_->Remove(PdfName::kCount);
  } else {
    content_->Put(PdfName::kFirst, children_.front()->content_);
    content_->Put(PdfName::kLast, children_.back()->content_);
    // Root: total visible items; item: +visible descendants when open, minus
    // that number when closed.
    int count = VisibleDescendants();
    content_->Put(PdfName::kCount,
                  MakeNumber(parent_ == nullptr || open_ ? count : -count));
    for (size_t i = 0; i < children_.size(); ++i) {
      auto& c = children_[i]->content_;
      c->Put(PdfName::kParent, content_);
      if (i > 0)
        c->Put(PdfName::kPrev, children_[i - 1]->content_);
      else
        c->Remove(PdfName::kPrev);
      if (i + 1 < children_.size())
        c->Put(PdfName::kNext, children_[i + 1]->content_);
      else
        c->Remove(PdfName::kNext);
      c->SetModified();
    }
  }
  content_->SetModified();
  if (parent_) parent_->RebuildLinks();
}

// ============================================================ PdfMerger /
// PdfSplitter
PdfMerger& PdfMerger::Merge(PdfDocument& from, int from_page, int to_page) {
  std::vector<int> pages;
  for (int p = from_page; p <= to_page; ++p) pages.push_back(p);
  return Merge(from, pages);
}

PdfMerger& PdfMerger::Merge(PdfDocument& from, const std::vector<int>& pages) {
  from.CopyPagesTo(pages, destination_);
  if (close_source_documents_) from.Close();
  return *this;
}

Bytes PdfSplitter::ExtractPageRange(int from_page, int to_page) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument part(writer);
    pdf_document_.CopyPagesTo(from_page, to_page, part);
    part.Close();
  }
  return baos.ToByteArray();
}

std::vector<Bytes> PdfSplitter::SplitByPageCount(int page_count) {
  std::vector<Bytes> parts;
  int n = pdf_document_.GetNumberOfPages();
  if (page_count <= 0) throw PdfException("Page count must be positive.");
  for (int start = 1; start <= n; start += page_count)
    parts.push_back(
        ExtractPageRange(start, std::min(n, start + page_count - 1)));
  return parts;
}

}  // namespace kernel
}  // namespace pdfcpp
