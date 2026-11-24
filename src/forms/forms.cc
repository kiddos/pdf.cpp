#include "pdfcpp/forms/forms.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "pdfcpp/io/font/pdf_encodings.h"
#include "pdfcpp/io/source/byte_utils.h"
#include "pdfcpp/io/util/text_util.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

namespace pdfcpp {
namespace forms {

using namespace kernel;

// ============================================================ PdfFormField
PdfFormField::PdfFormField(PdfDictionaryPtr dict, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(dict)), document_(document) {}

PdfObjectPtr PdfFormField::GetInheritable(const PdfNamePtr& key) const {
  auto dict = GetPdfObject();
  std::set<const PdfObject*> seen;
  while (dict && !seen.count(dict.get())) {
    seen.insert(dict.get());
    if (auto v = dict->Get(key)) return v;
    dict = dict->GetAsDictionary(PdfName::kParent);
  }
  return nullptr;
}

std::string PdfFormField::GetPartialFieldName() const {
  auto t = GetPdfObject()->GetAsString(PdfName::kT);
  return t ? t->ToUnicodeString() : std::string();
}

std::string PdfFormField::GetFieldName() const {
  std::string name = GetPartialFieldName();
  auto parent = GetPdfObject()->GetAsDictionary(PdfName::kParent);
  std::set<const PdfObject*> seen;
  while (parent && !seen.count(parent.get())) {
    seen.insert(parent.get());
    auto t = parent->GetAsString(PdfName::kT);
    if (t) name = t->ToUnicodeString() + "." + name;
    parent = parent->GetAsDictionary(PdfName::kParent);
  }
  return name;
}

PdfNamePtr PdfFormField::GetFormType() const {
  return PdfCast<PdfName>(GetInheritable(PdfName::kFT));
}

PdfObjectPtr PdfFormField::GetValue() const {
  return GetInheritable(PdfName::kV);
}

std::string PdfFormField::GetValueAsString() const {
  PdfObjectPtr value = GetValue();
  if (!value) return std::string();
  if (value->IsString())
    return static_cast<PdfString&>(*value).ToUnicodeString();
  if (value->IsName()) return static_cast<PdfName&>(*value).GetValue();
  if (value->IsStream()) {
    Bytes b = static_cast<PdfStream&>(*value).GetBytes();
    return std::string(b.begin(), b.end());
  }
  if (value->IsArray()) {
    std::string out;
    for (const auto& v : *std::static_pointer_cast<PdfArray>(value)) {
      if (!out.empty()) out += ", ";
      if (v->IsString()) out += static_cast<PdfString&>(*v).ToUnicodeString();
    }
    return out;
  }
  return value->ToString();
}

int PdfFormField::GetFieldFlags() const {
  auto ff = PdfCast<PdfNumber>(GetInheritable(PdfName::kFf));
  return ff ? ff->IntValue() : 0;
}

PdfFormField& PdfFormField::SetFieldFlags(int flags) {
  GetPdfObject()->Put(PdfName::kFf, MakeNumber(flags));
  GetPdfObject()->SetModified();
  return *this;
}

PdfFormField& PdfFormField::SetFieldFlag(int flag, bool value) {
  int flags = GetFieldFlags();
  return SetFieldFlags(value ? (flags | flag) : (flags & ~flag));
}

std::vector<PdfFormFieldPtr> PdfFormField::GetKids() const {
  std::vector<PdfFormFieldPtr> kids;
  auto arr = GetPdfObject()->GetAsArray(PdfName::kKids);
  if (!arr) return kids;
  for (int i = 0; i < arr->size(); ++i) {
    auto kid = arr->GetAsDictionary(i);
    if (kid && kid->ContainsKey(PdfName::kT))
      kids.push_back(std::make_shared<PdfFormField>(kid, document_));
  }
  return kids;
}

std::vector<PdfDictionaryPtr> PdfFormField::GetWidgets() const {
  std::vector<PdfDictionaryPtr> widgets;
  if (NameEquals(GetPdfObject()->GetAsName(PdfName::kSubtype),
                 PdfName::kWidget))
    widgets.push_back(GetPdfObject());
  auto arr = GetPdfObject()->GetAsArray(PdfName::kKids);
  if (arr) {
    for (int i = 0; i < arr->size(); ++i) {
      auto kid = arr->GetAsDictionary(i);
      if (kid && !kid->ContainsKey(PdfName::kT) &&
          (NameEquals(kid->GetAsName(PdfName::kSubtype), PdfName::kWidget) ||
           !kid->ContainsKey(PdfName::kSubtype)))
        widgets.push_back(kid);
    }
  }
  return widgets;
}

PdfFormField& PdfFormField::AddKid(const PdfFormFieldPtr& kid) {
  auto kids = GetPdfObject()->GetAsArray(PdfName::kKids);
  if (!kids) {
    kids = std::make_shared<PdfArray>();
    GetPdfObject()->Put(PdfName::kKids, kids);
  }
  kid->GetPdfObject()->Put(PdfName::kParent, GetPdfObject());
  if (document_ && document_->GetWriter())
    kid->GetPdfObject()->MakeIndirect(document_);
  kids->Add(kid->GetPdfObject());
  return *this;
}

std::vector<std::string> PdfFormField::GetOptions() const {
  std::vector<std::string> options;
  auto opt = PdfCast<PdfArray>(GetInheritable(PdfName::kOpt));
  if (!opt) return options;
  for (int i = 0; i < opt->size(); ++i) {
    PdfObjectPtr o = opt->Get(i);
    if (o->IsString())
      options.push_back(static_cast<PdfString&>(*o).ToUnicodeString());
    else if (o->IsArray()) {
      auto pair = std::static_pointer_cast<PdfArray>(o);
      auto s = pair->GetAsString(pair->size() > 1 ? 1 : 0);
      if (s) options.push_back(s->ToUnicodeString());
    }
  }
  return options;
}

std::vector<std::string> PdfFormField::GetAppearanceStates() const {
  std::set<std::string> states;
  for (const auto& w : GetWidgets()) {
    auto ap = w->GetAsDictionary(PdfName::kAP);
    if (!ap) continue;
    for (const PdfNamePtr* key : {&PdfName::kN, &PdfName::kD}) {
      auto d = ap->GetAsDictionary(*key);
      if (!d) continue;
      for (const auto& k : d->KeySet())
        if (k->GetValue() != "Off") states.insert(k->GetValue());
    }
  }
  return std::vector<std::string>(states.begin(), states.end());
}

PdfFormField::DaInfo PdfFormField::ParseDefaultAppearance() const {
  DaInfo info;
  auto da = PdfCast<PdfString>(GetInheritable(PdfName::kDA));
  if (!da && form_) da = form_->GetDefaultAppearance();
  if (da) {
    Bytes bytes = da->GetValueBytes();
    PdfCanvasParser parser(bytes, nullptr);
    std::vector<PdfObjectPtr> ops;
    while (parser.Parse(ops)) {
      std::string op = ops.back()->ToString();
      auto num = [&](size_t i) {
        return i < ops.size() && ops[i]->IsNumber()
                   ? static_cast<PdfNumber&>(*ops[i]).FloatValue()
                   : 0.f;
      };
      if (op == "Tf" && ops.size() >= 3) {
        if (ops[0]->IsName())
          info.font_name = std::static_pointer_cast<PdfName>(ops[0]);
        info.font_size = num(1);
      } else if (op == "g" && ops.size() >= 2) {
        info.color = std::make_shared<DeviceGray>(num(0));
      } else if (op == "rg" && ops.size() >= 4) {
        info.color = std::make_shared<DeviceRgb>(num(0), num(1), num(2));
      } else if (op == "k" && ops.size() >= 5) {
        info.color =
            std::make_shared<DeviceCmyk>(num(0), num(1), num(2), num(3));
      }
    }
  }
  if (form_ && info.font_name) info.font = form_->ResolveFont(info.font_name);
  if (!info.font && pending_font_) info.font = pending_font_;
  if (!info.font && form_) {
    info.font = form_->GetDefaultFont();
    info.font_name = form_->RegisterFont(info.font);
  }
  if (!info.font) {
    // field not attached to a form yet: Helvetica, as iText does
    static std::shared_ptr<PdfFont> helv;
    if (!helv)
      helv = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica,
                                        io::PdfEncodings::kWinansi);
    info.font = helv;
    if (!info.font_name) info.font_name = PdfName::Of("Helv");
  }
  if (!info.color) info.color = DeviceGray::kBlack;
  return info;
}

void PdfFormField::UpdateDefaultAppearance(const DaInfo& da) {
  std::ostringstream s;
  s << "/" << (da.font_name ? da.font_name->GetValue() : std::string("Helv"))
    << " " << io::ByteUtils::FormatDouble(da.font_size) << " Tf ";
  const auto& v = da.color ? da.color->GetColorValue() : std::vector<float>{0};
  for (float c : v) s << io::ByteUtils::FormatDouble(c) << " ";
  s << (v.size() == 1 ? "g" : v.size() == 3 ? "rg" : "k");
  GetPdfObject()->Put(PdfName::kDA, std::make_shared<PdfString>(s.str()));
  GetPdfObject()->SetModified();
}

void PdfFormField::SetAcroForm(PdfAcroForm* form) {
  form_ = form;
  if (form_ && pending_font_) {
    DaInfo da = ParseDefaultAppearance();
    da.font = pending_font_;
    da.font_name = form_->RegisterFont(pending_font_);
    pending_font_ = nullptr;
    UpdateDefaultAppearance(da);
  }
}

PdfFormField& PdfFormField::SetFontAndSize(std::shared_ptr<PdfFont> font,
                                           float size) {
  DaInfo da = ParseDefaultAppearance();
  if (font && form_) {
    da.font = font;
    da.font_name = form_->RegisterFont(font);
  } else if (font) {
    // Not attached yet: the font can't be registered in /DR, so remember it
    // until SetAcroForm().
    pending_font_ = font;
    da.font = font;
  }
  da.font_size = size;
  UpdateDefaultAppearance(da);
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetFont(std::shared_ptr<PdfFont> font) {
  return SetFontAndSize(std::move(font), ParseDefaultAppearance().font_size);
}

PdfFormField& PdfFormField::SetFontSize(float font_size) {
  return SetFontAndSize(nullptr, font_size);
}

PdfFormField& PdfFormField::SetColor(const ColorPtr& color) {
  DaInfo da = ParseDefaultAppearance();
  da.color = color;
  UpdateDefaultAppearance(da);
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetJustification(int justification) {
  GetPdfObject()->Put(PdfName::kQ, MakeNumber(justification));
  GetPdfObject()->SetModified();  // persist even when appearance regeneration
                                  // is skipped/deferred
  appearance_dirty_ = true;
  RegenerateField();
  return *this;
}

int PdfFormField::GetJustification() const {
  auto q = PdfCast<PdfNumber>(GetInheritable(PdfName::kQ));
  return q ? q->IntValue() : 0;
}

static PdfDictionaryPtr Mk(const PdfDictionaryPtr& widget, bool create) {
  auto mk_dict = widget->GetAsDictionary(PdfName::kMK);
  if (!mk_dict && create) {
    mk_dict = std::make_shared<PdfDictionary>();
    widget->Put(PdfName::kMK, mk_dict);
  }
  return mk_dict;
}

PdfFormField& PdfFormField::SetBackgroundColor(const ColorPtr& color) {
  for (const auto& w : GetWidgets()) {
    Mk(w, true)->Put(PdfName::kBG,
                     std::make_shared<PdfArray>(color->GetColorValue()));
    w->SetModified();
  }
  appearance_dirty_ = true;
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetBorderColor(const ColorPtr& color) {
  for (const auto& w : GetWidgets()) {
    Mk(w, true)->Put(PdfName::kBC,
                     std::make_shared<PdfArray>(color->GetColorValue()));
    w->SetModified();
  }
  appearance_dirty_ = true;
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetBorderWidth(float width) {
  for (const auto& w : GetWidgets()) {
    auto bs = std::make_shared<PdfDictionary>();
    bs->Put(PdfName::kW, MakeNumber(static_cast<double>(width)));
    w->Put(PdfName::kBS, bs);
    w->SetModified();
  }
  appearance_dirty_ = true;
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetCheckBoxAppearanceOnStateName(
    const std::string& on_state_name) {
  if (on_state_name.empty() || on_state_name == "Off") return *this;
  if (!NameEquals(GetFormType(), PdfName::kBtn) ||
      GetFieldFlag(kFfPushButton) || GetFieldFlag(kFfRadio))
    return *this;
  std::string value = GetValueAsString();
  for (const auto& w : GetWidgets())
    RegenerateCheckBoxWidget(w, value, on_state_name);
  return *this;
}

PdfFormField& PdfFormField::SetValue(const std::string& value) {
  has_display_value_ = false;
  PdfNamePtr type = GetFormType();
  if (NameEquals(type, PdfName::kBtn)) {
    if (GetFieldFlag(kFfPushButton)) return *this;
    // checkbox / radio: value is an appearance state name
    PdfNamePtr v = std::make_shared<PdfName>(value.empty() ? "Off" : value);
    GetPdfObject()->Put(PdfName::kV, v);
    for (const auto& w : GetWidgets()) {
      auto ap = w->GetAsDictionary(PdfName::kAP);
      auto n = ap ? ap->GetAsDictionary(PdfName::kN) : nullptr;
      bool has_state = n && n->ContainsKey(v);
      w->Put(PdfName::kAS, has_state ? v : PdfName::Of("Off"));
      w->SetModified();
    }
  } else {
    GetPdfObject()->Put(PdfName::kV, std::make_shared<PdfString>(
                                         value, io::PdfEncodings::kUnicodeBig));
  }
  GetPdfObject()->SetModified();
  RegenerateField();
  return *this;
}

PdfFormField& PdfFormField::SetValue(const std::string& value,
                                     const std::string& display) {
  SetValue(value);
  display_value_ = display;
  has_display_value_ = true;
  RegenerateField();
  return *this;
}

static std::array<float, 4> WidgetBox(const PdfDictionaryPtr& widget) {
  auto rect = widget->GetAsRectangle(PdfName::kRect);
  Rectangle r = rect ? *rect : Rectangle(0, 0, 0, 0);
  return {r.GetX(), r.GetY(), r.GetWidth(), r.GetHeight()};
}

static float BorderWidthOf(const PdfDictionaryPtr& widget) {
  auto bs = widget->GetAsDictionary(PdfName::kBS);
  if (bs) {
    if (auto w = bs->GetAsNumber(PdfName::kW)) return w->FloatValue();
    return 1;
  }
  auto mk_dict = widget->GetAsDictionary(PdfName::kMK);
  return mk_dict && mk_dict->ContainsKey(PdfName::kBC) ? 1.f : 0.f;
}

static ColorPtr ColorFromArray(const PdfArrayPtr& arr) {
  if (!arr) return nullptr;
  auto v = arr->ToFloatArray();
  if (v.size() == 1) return std::make_shared<DeviceGray>(v[0]);
  if (v.size() == 3) return std::make_shared<DeviceRgb>(v[0], v[1], v[2]);
  if (v.size() == 4)
    return std::make_shared<DeviceCmyk>(v[0], v[1], v[2], v[3]);
  return nullptr;
}

static void DrawBackgroundAndBorder(PdfCanvas& canvas,
                                    const PdfDictionaryPtr& widget, float w,
                                    float h, float bw) {
  auto mk_dict = widget->GetAsDictionary(PdfName::kMK);
  ColorPtr bg =
      mk_dict ? ColorFromArray(mk_dict->GetAsArray(PdfName::kBG)) : nullptr;
  ColorPtr bc =
      mk_dict ? ColorFromArray(mk_dict->GetAsArray(PdfName::kBC)) : nullptr;
  if (bg)
    canvas.SaveState()
        .SetFillColor(bg)
        .Rectangle(0, 0, w, h)
        .Fill()
        .RestoreState();
  if (bc && bw > 0)
    canvas.SaveState()
        .SetStrokeColor(bc)
        .SetLineWidth(bw)
        .Rectangle(bw / 2, bw / 2, w - bw, h - bw)
        .Stroke()
        .RestoreState();
}

bool PdfFormField::RegenerateTextWidget(const PdfDictionaryPtr& widget,
                                        const std::string& text) {
  auto box = WidgetBox(widget);
  float w = box[2], h = box[3];
  if (w <= 0 || h <= 0) return false;
  DaInfo da = ParseDefaultAppearance();
  float bw = BorderWidthOf(widget);
  float padding = std::max(1.f, bw) + 1;
  bool multiline = IsMultiline();
  float font_size = da.font_size;
  const auto& metrics = da.font->GetFontProgram()->GetFontMetrics();
  float asc = static_cast<float>(metrics.GetTypoAscender()),
        desc = static_cast<float>(metrics.GetTypoDescender());
  if (asc - desc <= 0) {
    asc = 800;
    desc = -200;
  }
  std::string shown = text;
  if (GetFieldFlag(kFfPassword))
    shown = std::string(io::TextUtil::Utf8ToCodePoints(text).size(), '*');
  if (font_size <= 0) {
    // auto size (iText caps it at 12 for multiline text)
    font_size =
        multiline ? 12
                  : std::min((h - 2 * padding) * 1000 / (asc - desc), 12.f * 2);
    float text_width = da.font->GetWidth(shown, 1);
    if (!multiline && text_width > 0)
      font_size = std::min(font_size, (w - 2 * padding) / text_width);
    font_size = std::max(4.f, font_size);
  }
  auto x_object = std::make_shared<PdfFormXObject>(Rectangle(0, 0, w, h));
  PdfCanvas canvas(*x_object, document_);
  DrawBackgroundAndBorder(canvas, widget, w, h, bw);
  canvas.BeginMarkedContent(PdfName::kTx);
  canvas.SaveState()
      .Rectangle(padding - 1, padding - 1, w - 2 * (padding - 1),
                 h - 2 * (padding - 1))
      .Clip()
      .EndPath();
  std::vector<std::string> lines;
  if (multiline) {
    float max_width = w - 2 * padding;
    std::string remaining = shown;
    size_t pos;
    while ((pos = remaining.find('\n')) != std::string::npos) {
      for (const auto& l :
           da.font->SplitString(remaining.substr(0, pos), font_size, max_width))
        lines.push_back(l);
      remaining = remaining.substr(pos + 1);
    }
    for (const auto& l : da.font->SplitString(remaining, font_size, max_width))
      lines.push_back(l);
  } else {
    lines.push_back(shown);
  }
  float line_height = (asc - desc) * font_size / 1000.f;
  float y;
  if (multiline)
    y = h - padding - asc * font_size / 1000.f;
  else
    y = (h - line_height) / 2 - desc * font_size / 1000.f;
  int q = GetJustification();
  canvas.BeginText().SetFontAndSize(da.font, font_size).SetFillColor(da.color);
  for (const auto& line : lines) {
    float lw = da.font->GetWidth(line, font_size);
    float x = q == kAlignCenter
                  ? (w - lw) / 2
                  : (q == kAlignRight ? w - padding - lw : padding);
    canvas.SetTextMatrix(x, y);
    canvas.ShowText(line);
    y -= line_height;
  }
  canvas.EndText().RestoreState().EndMarkedContent();
  auto ap = std::make_shared<PdfDictionary>();
  x_object->GetPdfObject()->MakeIndirect(document_);
  ap->Put(PdfName::kN, x_object->GetPdfObject());
  widget->Put(PdfName::kAP, ap);
  widget->SetModified();
  return true;
}

bool PdfFormField::RegenerateCheckBoxWidget(
    const PdfDictionaryPtr& widget, const std::string& value,
    const std::string& on_state_override) {
  auto box = WidgetBox(widget);
  float w = box[2], h = box[3];
  if (w <= 0 || h <= 0) return false;
  // Keep existing appearances of read documents: SetValue() only switches /AS
  // between the existing states, so a value change must not replace custom
  // artwork with the generated check mark. Redraw only when asked to (new
  // on-state name) or when appearance properties (border, background, ...)
  // changed.
  auto existing = widget->GetAsDictionary(PdfName::kAP);
  if (on_state_override.empty() && !appearance_dirty_ && existing &&
      existing->GetAsDictionary(PdfName::kN) && document_->GetReader())
    return true;
  std::string on_state = "Yes";
  if (!on_state_override.empty()) {
    on_state = on_state_override;
  } else if (existing) {
    auto n = existing->GetAsDictionary(PdfName::kN);
    if (n)
      for (const auto& k : n->KeySet())
        if (k->GetValue() != "Off") on_state = k->GetValue();
  }
  float bw = BorderWidthOf(widget);
  auto zapf = PdfFontFactory::CreateFont(io::standard_fonts::kZapfdingbats);
  auto make_state = [&](bool on) {
    auto x = std::make_shared<PdfFormXObject>(Rectangle(0, 0, w, h));
    PdfCanvas canvas(*x, document_);
    DrawBackgroundAndBorder(canvas, widget, w, h, bw);
    if (on) {
      float size = std::min(w, h) * 0.8f;
      // ZapfDingbats code '4' is the check mark (font-specific encoding, as
      // iText's /MK /CA (4))
      std::string check = "4";
      float cw = zapf->GetWidth(check, size);
      canvas.BeginText()
          .SetFontAndSize(zapf, size)
          .SetTextMatrix((w - cw) / 2, (h - size * 0.7f) / 2)
          .ShowText(check)
          .EndText();
    }
    x->GetPdfObject()->MakeIndirect(document_);
    return x->GetPdfObject();
  };
  auto n = std::make_shared<PdfDictionary>();
  n->Put(PdfName::Of(on_state), make_state(true));
  n->Put(PdfName::Of("Off"), make_state(false));
  auto ap = std::make_shared<PdfDictionary>();
  ap->Put(PdfName::kN, n);
  widget->Put(PdfName::kAP, ap);
  widget->Put(PdfName::kAS,
              value == on_state ? PdfName::Of(on_state) : PdfName::Of("Off"));
  widget->SetModified();
  return true;
}

bool PdfFormField::RegenerateField() {
  if (!form_)
    return false;  // appearances are generated once the field is added to a
                   // form
  if (!form_->IsGenerateAppearance()) return false;
  PdfNamePtr type = GetFormType();
  bool ok = true;
  if (NameEquals(type, PdfName::kTx) || NameEquals(type, PdfName::kCh)) {
    std::string text = has_display_value_ ? display_value_ : GetValueAsString();
    for (const auto& w : GetWidgets()) ok = RegenerateTextWidget(w, text) && ok;
  } else if (NameEquals(type, PdfName::kBtn) && !GetFieldFlag(kFfPushButton)) {
    std::string value = GetValueAsString();
    for (const auto& w : GetWidgets())
      ok = RegenerateCheckBoxWidget(w, value) && ok;
  }
  for (const auto& kid : GetKids()) {
    kid->SetAcroForm(form_);
    ok = kid->RegenerateField() && ok;
  }
  return ok;
}

// ============================================================ PdfAcroForm
std::shared_ptr<PdfAcroForm> PdfAcroForm::GetAcroForm(
    PdfDocument& document, bool create_if_not_exist) {
  auto acro_form_dict =
      document.GetCatalog().GetPdfObject()->GetAsDictionary(PdfName::kAcroForm);
  if (!acro_form_dict) {
    if (!create_if_not_exist) return nullptr;
    acro_form_dict = std::make_shared<PdfDictionary>();
    acro_form_dict->Put(PdfName::kFields, std::make_shared<PdfArray>());
    if (document.GetWriter()) acro_form_dict->MakeIndirect(&document);
    document.GetCatalog().Put(PdfName::kAcroForm, acro_form_dict);
  }
  return std::make_shared<PdfAcroForm>(acro_form_dict, &document);
}

PdfAcroForm::PdfAcroForm(PdfDictionaryPtr dict, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(dict)), document_(document) {}

PdfArrayPtr PdfAcroForm::GetFields(bool create) {
  auto fields = GetPdfObject()->GetAsArray(PdfName::kFields);
  if (!fields && create) {
    fields = std::make_shared<PdfArray>();
    GetPdfObject()->Put(PdfName::kFields, fields);
  }
  return fields;
}

void PdfAcroForm::AddField(const PdfFormFieldPtr& field, PdfPage* page) {
  field->SetAcroForm(this);
  if (document_->GetWriter()) field->GetPdfObject()->MakeIndirect(document_);
  GetFields(true)->Add(field->GetPdfObject());
  GetPdfObject()->SetModified();
  for (const auto& w : field->GetWidgets()) {
    // Without an explicit page, use the page the widget was assigned to (/P,
    // e.g. via a builder's setPage).
    PdfPage* target = page;
    if (!target) {
      auto p = w->GetAsDictionary(PdfName::kP);
      if (p) target = document_->GetPage(p.get());
    }
    if (!target) continue;
    bool present = false;
    if (auto annots = target->GetPdfObject()->GetAsArray(PdfName::kAnnots))
      present = annots->Contains(w);
    if (!present) target->AddAnnotation(w);
  }
  field->RegenerateField();
}

void PdfAcroForm::CollectFields(const PdfDictionaryPtr& dict,
                                std::map<std::string, PdfFormFieldPtr>& out,
                                int depth) {
  if (!dict || depth > 64) return;
  auto field = std::make_shared<PdfFormField>(dict, document_);
  field->SetAcroForm(this);
  std::string name = field->GetFieldName();
  if (!name.empty()) out[name] = field;
  auto kids = dict->GetAsArray(PdfName::kKids);
  if (!kids) return;
  for (int i = 0; i < kids->size(); ++i) {
    auto kid = kids->GetAsDictionary(i);
    if (kid && kid->ContainsKey(PdfName::kT))
      CollectFields(kid, out, depth + 1);
  }
}

std::map<std::string, PdfFormFieldPtr> PdfAcroForm::GetAllFormFields() {
  std::map<std::string, PdfFormFieldPtr> out;
  auto fields = GetFields(false);
  if (!fields) return out;
  for (int i = 0; i < fields->size(); ++i)
    CollectFields(fields->GetAsDictionary(i), out, 0);
  return out;
}

std::map<std::string, PdfFormFieldPtr> PdfAcroForm::GetRootFormFields() {
  std::map<std::string, PdfFormFieldPtr> out;
  auto fields = GetFields(false);
  if (!fields) return out;
  for (int i = 0; i < fields->size(); ++i) {
    auto dict = fields->GetAsDictionary(i);
    if (!dict) continue;
    auto field = std::make_shared<PdfFormField>(dict, document_);
    field->SetAcroForm(this);
    out[field->GetFieldName()] = field;
  }
  return out;
}

PdfFormFieldPtr PdfAcroForm::GetField(const std::string& field_name) {
  auto all = GetAllFormFields();
  auto it = all.find(field_name);
  return it == all.end() ? nullptr : it->second;
}

PdfAcroForm& PdfAcroForm::SetNeedAppearances(bool need_appearances) {
  GetPdfObject()->Put(PdfName::kNeedAppearances,
                      PdfBoolean::ValueOf(need_appearances));
  GetPdfObject()->SetModified();
  return *this;
}

PdfAcroForm& PdfAcroForm::SetDefaultAppearance(const std::string& appearance) {
  GetPdfObject()->Put(PdfName::kDA, std::make_shared<PdfString>(appearance));
  GetPdfObject()->SetModified();
  return *this;
}

std::shared_ptr<PdfFont> PdfAcroForm::GetDefaultFont() {
  if (!default_font_)
    default_font_ = PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica,
                                               io::PdfEncodings::kWinansi);
  return default_font_;
}

std::shared_ptr<PdfFont> PdfAcroForm::ResolveFont(const PdfNamePtr& name) {
  auto dr = GetDefaultResources();
  auto fonts = dr ? dr->GetAsDictionary(PdfName::kFont) : nullptr;
  auto font_dict = fonts ? fonts->GetAsDictionary(name) : nullptr;
  if (font_dict) {
    auto it = font_cache_.find(font_dict.get());
    if (it != font_cache_.end()) return it->second;
    std::shared_ptr<PdfFont> font;
    // standard 14 fonts referenced by name are re-created so that new text can
    // be encoded
    auto base_font = font_dict->GetAsName(PdfName::kBaseFont);
    if (base_font &&
        io::standard_fonts::IsStandardFont(base_font->GetValue()) &&
        !font_dict->ContainsKey(PdfName::kFontDescriptor))
      font = PdfFontFactory::CreateFont(base_font->GetValue(),
                                        io::PdfEncodings::kWinansi);
    else
      font = PdfFontFactory::CreateFont(font_dict);
    font_cache_[font_dict.get()] = font;
    return font;
  }
  // well-known abbreviations used by Acrobat
  const std::string& n = name->GetValue();
  if (n == "Helv") return GetDefaultFont();
  if (n == "HeBo")
    return PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  if (n == "Cour")
    return PdfFontFactory::CreateFont(io::standard_fonts::kCourier);
  if (n == "TiRo")
    return PdfFontFactory::CreateFont(io::standard_fonts::kTimesRoman);
  if (n == "ZaDb")
    return PdfFontFactory::CreateFont(io::standard_fonts::kZapfdingbats);
  return nullptr;
}

PdfNamePtr PdfAcroForm::RegisterFont(const std::shared_ptr<PdfFont>& font) {
  auto dr = GetDefaultResources();
  if (!dr) {
    dr = std::make_shared<PdfDictionary>();
    SetDefaultResources(dr);
  }
  auto fonts = dr->GetAsDictionary(PdfName::kFont);
  if (!fonts) {
    fonts = std::make_shared<PdfDictionary>();
    dr->Put(PdfName::kFont, fonts);
  }
  for (const auto& kv : fonts->EntrySet()) {
    auto d = PdfCast<PdfDictionary>(
        kv.second->IsIndirectReference() ? fonts->Get(kv.first) : kv.second);
    if (d.get() == font->GetPdfObject().get()) return kv.first;
  }
  if (document_->GetWriter()) document_->AddFont(font);
  PdfNamePtr name;
  if (font.get() == default_font_.get() &&
      !fonts->ContainsKey(PdfName::Of("Helv"))) {
    name = PdfName::Of("Helv");
  } else {
    int i = 0;
    do {
      name = std::make_shared<PdfName>("F" + std::to_string(++i));
    } while (fonts->ContainsKey(name));
  }
  fonts->Put(name, font->GetPdfObject());
  font_cache_[font->GetPdfObject().get()] = font;
  GetPdfObject()->SetModified();
  return name;
}

static PdfPage* FindWidgetPage(PdfDocument* doc,
                               const PdfDictionaryPtr& widget) {
  auto p = widget->GetAsDictionary(PdfName::kP);
  if (p) {
    PdfPage* page = doc->GetPage(p.get());
    if (page) return page;
  }
  for (int i = 1; i <= doc->GetNumberOfPages(); ++i) {
    auto annots = doc->GetPage(i)->GetPdfObject()->GetAsArray(PdfName::kAnnots);
    if (annots && annots->Contains(widget)) return doc->GetPage(i);
  }
  return nullptr;
}

namespace {
/// Port of PdfFormXObject.calcAppearanceTransformToAnnotRect: maps the
/// appearance BBox - transformed by the form's /Matrix - onto the annotation
/// rectangle.
AffineTransform CalcAppearanceTransformToAnnotRect(
    const PdfStreamPtr& appearance, const Rectangle& annot_b_box) {
  auto bbox_arr = appearance->GetAsArray(PdfName::kBBox);
  std::vector<float> b = bbox_arr && bbox_arr->size() == 4
                             ? bbox_arr->ToFloatArray()
                             : std::vector<float>{0, 0, 0, 0};
  double llx = std::min(b[0], b[2]), lly = std::min(b[1], b[3]);
  double urx = std::max(b[0], b[2]), ury = std::max(b[1], b[3]);
  auto matrix = appearance->GetAsArray(PdfName::kMatrix);
  if (matrix && matrix->size() == 6) {
    auto m = matrix->ToFloatArray();
    AffineTransform t(m[0], m[1], m[2], m[3], m[4], m[5]);
    double pts[8] = {b[0], b[1], b[0], b[3], b[2], b[1], b[2], b[3]};
    double out[8];
    t.Transform(pts, 0, out, 0, 4);
    llx = std::min({out[0], out[2], out[4], out[6]});
    lly = std::min({out[1], out[3], out[5], out[7]});
    urx = std::max({out[0], out[2], out[4], out[6]});
    ury = std::max({out[1], out[3], out[5], out[7]});
  }
  double w = urx - llx, h = ury - lly;
  AffineTransform at = AffineTransform::GetTranslateInstance(-llx, -lly);
  double scale_x = w == 0 ? 1 : annot_b_box.GetWidth() / w;
  double scale_y = h == 0 ? 1 : annot_b_box.GetHeight() / h;
  at.PreConcatenate(AffineTransform::GetScaleInstance(scale_x, scale_y));
  at.PreConcatenate(AffineTransform::GetTranslateInstance(annot_b_box.GetX(),
                                                          annot_b_box.GetY()));
  return at;
}

/// Port of prepareFieldsForFlattening: a field together with all of its
/// descendant fields.
void AddFieldWithDescendants(const PdfFormFieldPtr& field,
                             std::vector<PdfFormFieldPtr>& out,
                             std::set<const PdfObject*>& seen, int depth) {
  if (!field || depth > 64 || !seen.insert(field->GetPdfObject().get()).second)
    return;
  out.push_back(field);
  for (const auto& kid : field->GetKids())
    AddFieldWithDescendants(kid, out, seen, depth + 1);
}

/// Port of removeFieldFromParentAndAcroForm: also prunes parents left without
/// kids.
void RemoveFieldFromParentAndAcroForm(const PdfArrayPtr& form_fields,
                                      const PdfDictionaryPtr& field_object,
                                      int depth) {
  if (!field_object || depth > 64) return;
  if (form_fields) form_fields->Remove(field_object.get());
  auto parent = field_object->GetAsDictionary(PdfName::kParent);
  if (!parent) return;
  auto kids = parent->GetAsArray(PdfName::kKids);
  if (!kids) {
    if (form_fields) form_fields->Remove(parent.get());
  } else {
    kids->Remove(field_object.get());
    parent->SetModified();
    if (kids->IsEmpty())
      RemoveFieldFromParentAndAcroForm(form_fields, parent, depth + 1);
  }
}
}  // namespace

void PdfAcroForm::FlattenFields() {
  if (!document_->GetWriter())
    throw PdfException(
        "The document must be opened in writing mode to flatten fields.");
  auto all = GetAllFormFields();
  std::vector<PdfFormFieldPtr> to_flatten;
  if (fields_for_flattening_.empty()) {
    for (auto& kv : all) to_flatten.push_back(kv.second);
  } else {
    // A named non-terminal field flattens its whole subtree (otherwise its
    // kids' widgets would be orphaned).
    std::set<const PdfObject*> seen;
    for (const auto& name : fields_for_flattening_) {
      auto it = all.find(name);
      if (it != all.end())
        AddFieldWithDescendants(it->second, to_flatten, seen, 0);
    }
  }
  std::map<PdfPage*, std::unique_ptr<PdfCanvas>> canvases;
  std::set<const PdfObject*> flattened_widgets;
  for (auto& field : to_flatten) {
    for (const auto& widget : field->GetWidgets()) {
      if (flattened_widgets.count(widget.get())) continue;
      flattened_widgets.insert(widget.get());
      PdfPage* page = FindWidgetPage(document_, widget);
      if (!page) continue;
      auto f = widget->GetAsNumber(PdfName::kF);
      bool hidden = f && (f->IntValue() & 2);
      auto ap = widget->GetAsDictionary(PdfName::kAP);
      PdfObjectPtr normal = ap ? ap->Get(PdfName::kN) : nullptr;
      PdfStreamPtr appearance;
      if (normal && normal->IsStream()) {
        appearance = std::static_pointer_cast<PdfStream>(normal);
      } else if (normal && normal->IsDictionary()) {
        auto as = widget->GetAsName(PdfName::kAS);
        if (as)
          appearance =
              std::static_pointer_cast<PdfDictionary>(normal)->GetAsStream(as);
      }
      auto rect = widget->GetAsRectangle(PdfName::kRect);
      if (appearance && rect && !hidden) {
        auto& canvas = canvases[page];
        if (!canvas) canvas = std::make_unique<PdfCanvas>(page, true);
        if (!appearance->GetAsArray(
                PdfName::kBBox))  // tolerate a missing BBox:
                                  // assume it matches /Rect
          appearance->Put(PdfName::kBBox,
                          std::make_shared<PdfArray>(Rectangle(
                              0, 0, rect->GetWidth(), rect->GetHeight())));
        PdfFormXObject x_object(appearance);
        AffineTransform at =
            CalcAppearanceTransformToAnnotRect(appearance, *rect);
        float m[6];
        at.GetMatrix(m);
        canvas->AddXObjectWithTransformationMatrix(x_object, m[0], m[1], m[2],
                                                   m[3], m[4], m[5]);
      }
      // remove the widget from the page
      auto annots = page->GetPdfObject()->GetAsArray(PdfName::kAnnots);
      if (annots) {
        annots->Remove(widget.get());
        if (annots->GetIndirectReference()) annots->SetModified();
        page->GetPdfObject()->SetModified();
      }
    }
  }
  // remove flattened fields from the form
  for (auto& field : to_flatten)
    RemoveFieldFromParentAndAcroForm(GetFields(false), field->GetPdfObject(),
                                     0);
  GetPdfObject()->SetModified();
  auto fields = GetFields(false);
  if (!fields || fields->IsEmpty())
    document_->GetCatalog().Remove(PdfName::kAcroForm);
  fields_for_flattening_.clear();
}

bool PdfAcroForm::RemoveField(const std::string& field_name) {
  auto field = GetField(field_name);
  if (!field) return false;
  for (const auto& widget : field->GetWidgets()) {
    PdfPage* page = FindWidgetPage(document_, widget);
    if (!page) continue;
    auto annots = page->GetPdfObject()->GetAsArray(PdfName::kAnnots);
    if (annots) annots->Remove(widget.get());
    page->GetPdfObject()->SetModified();
  }
  auto parent = field->GetPdfObject()->GetAsDictionary(PdfName::kParent);
  if (parent) {
    auto kids = parent->GetAsArray(PdfName::kKids);
    if (kids) kids->Remove(field->GetPdfObject().get());
    parent->SetModified();
  } else if (auto fields = GetFields(false)) {
    fields->Remove(field->GetPdfObject().get());
  }
  GetPdfObject()->SetModified();
  return true;
}

void PdfAcroForm::RenameField(const std::string& old_name,
                              const std::string& new_name) {
  auto field = GetField(old_name);
  if (!field) return;
  field->GetPdfObject()->Put(
      PdfName::kT,
      std::make_shared<PdfString>(new_name, io::PdfEncodings::kUnicodeBig));
  field->GetPdfObject()->SetModified();
}

// ============================================================ builders
static PdfFormFieldPtr MakeField(PdfDocument* doc, const std::string& name,
                                 const PdfNamePtr& type,
                                 const Rectangle& rect) {
  auto dict = std::make_shared<PdfDictionary>();
  dict->Put(PdfName::kFT, type);
  dict->Put(PdfName::kT,
            std::make_shared<PdfString>(name, io::PdfEncodings::kUnicodeBig));
  dict->Put(PdfName::kType, PdfName::kAnnot);
  dict->Put(PdfName::kSubtype, PdfName::kWidget);
  dict->Put(PdfName::kRect, std::make_shared<PdfArray>(rect));
  dict->Put(PdfName::kF, MakeNumber(4));  // print
  if (doc->GetWriter()) dict->MakeIndirect(doc);
  return std::make_shared<PdfFormField>(dict, doc);
}

PdfFormFieldPtr TextFormFieldBuilder::CreateText() {
  auto field = MakeField(document_, name_, PdfName::kTx, rect_);
  field->GetPdfObject()->Put(PdfName::kDA,
                             std::make_shared<PdfString>("/Helv 0 Tf 0 g"));
  field->GetPdfObject()->Put(PdfName::kV, std::make_shared<PdfString>(""));
  // Port of setPageToField: the widget's /P is used by
  // PdfAcroForm::AddField(field) to place it.
  if (page_) field->GetPdfObject()->Put(PdfName::kP, page_->GetPdfObject());
  // Registered in the form's /DR (and written to /DA) once the field is added
  // to a form.
  if (font_) field->SetFont(font_);
  return field;
}

PdfFormFieldPtr TextFormFieldBuilder::CreateMultilineText() {
  auto field = CreateText();
  field->GetPdfObject()->Put(PdfName::kFf,
                             MakeNumber(PdfFormField::kFfMultiline));
  return field;
}

PdfFormFieldPtr CheckBoxFormFieldBuilder::CreateCheckBox() {
  auto field = MakeField(document_, name_, PdfName::kBtn, rect_);
  field->GetPdfObject()->Put(PdfName::kV, PdfName::Of("Off"));
  field->GetPdfObject()->Put(PdfName::kAS, PdfName::Of("Off"));
  auto mk_dict = std::make_shared<PdfDictionary>();
  mk_dict->Put(PdfName::kBC, std::make_shared<PdfArray>(std::vector<float>{0}));
  mk_dict->Put(PdfName::kCA, std::make_shared<PdfString>("4"));
  field->GetPdfObject()->Put(PdfName::kMK, mk_dict);
  return field;
}

PdfFormFieldPtr ChoiceFormFieldBuilder::Create(bool combo) {
  auto field = MakeField(document_, name_, PdfName::kCh, rect_);
  if (combo)
    field->GetPdfObject()->Put(PdfName::kFf,
                               MakeNumber(PdfFormField::kFfCombo));
  auto opt = std::make_shared<PdfArray>();
  for (const auto& o : options_)
    opt->Add(std::make_shared<PdfString>(o, io::PdfEncodings::kUnicodeBig));
  field->GetPdfObject()->Put(PdfName::kOpt, opt);
  field->GetPdfObject()->Put(PdfName::kDA,
                             std::make_shared<PdfString>("/Helv 0 Tf 0 g"));
  return field;
}

PdfFormFieldPtr ChoiceFormFieldBuilder::CreateComboBox() {
  return Create(true);
}
PdfFormFieldPtr ChoiceFormFieldBuilder::CreateList() { return Create(false); }

}  // namespace forms
}  // namespace pdfcpp
