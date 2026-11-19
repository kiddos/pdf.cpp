#include "pdfcpp/forms/Forms.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "pdfcpp/io/font/PdfEncodings.h"
#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/io/util/TextUtil.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace forms {

using namespace kernel;

// ============================================================ PdfFormField
PdfFormField::PdfFormField(PdfDictionaryPtr dict, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(dict)), document_(document) {}

PdfObjectPtr PdfFormField::getInheritable(const PdfNamePtr& key) const {
    auto dict = getPdfObject();
    std::set<const PdfObject*> seen;
    while (dict && !seen.count(dict.get())) {
        seen.insert(dict.get());
        if (auto v = dict->get(key)) return v;
        dict = dict->getAsDictionary(PdfName::Parent);
    }
    return nullptr;
}

std::string PdfFormField::getPartialFieldName() const {
    auto t = getPdfObject()->getAsString(PdfName::T);
    return t ? t->toUnicodeString() : std::string();
}

std::string PdfFormField::getFieldName() const {
    std::string name = getPartialFieldName();
    auto parent = getPdfObject()->getAsDictionary(PdfName::Parent);
    std::set<const PdfObject*> seen;
    while (parent && !seen.count(parent.get())) {
        seen.insert(parent.get());
        auto t = parent->getAsString(PdfName::T);
        if (t) name = t->toUnicodeString() + "." + name;
        parent = parent->getAsDictionary(PdfName::Parent);
    }
    return name;
}

PdfNamePtr PdfFormField::getFormType() const { return pdf_cast<PdfName>(getInheritable(PdfName::FT)); }

PdfObjectPtr PdfFormField::getValue() const { return getInheritable(PdfName::V); }

std::string PdfFormField::getValueAsString() const {
    PdfObjectPtr value = getValue();
    if (!value) return std::string();
    if (value->isString()) return static_cast<PdfString&>(*value).toUnicodeString();
    if (value->isName()) return static_cast<PdfName&>(*value).getValue();
    if (value->isStream()) {
        Bytes b = static_cast<PdfStream&>(*value).getBytes();
        return std::string(b.begin(), b.end());
    }
    if (value->isArray()) {
        std::string out;
        for (const auto& v : *std::static_pointer_cast<PdfArray>(value)) {
            if (!out.empty()) out += ", ";
            if (v->isString()) out += static_cast<PdfString&>(*v).toUnicodeString();
        }
        return out;
    }
    return value->toString();
}

int PdfFormField::getFieldFlags() const {
    auto ff = pdf_cast<PdfNumber>(getInheritable(PdfName::Ff));
    return ff ? ff->intValue() : 0;
}

PdfFormField& PdfFormField::setFieldFlags(int flags) {
    getPdfObject()->put(PdfName::Ff, makeNumber(flags));
    getPdfObject()->setModified();
    return *this;
}

PdfFormField& PdfFormField::setFieldFlag(int flag, bool value) {
    int flags = getFieldFlags();
    return setFieldFlags(value ? (flags | flag) : (flags & ~flag));
}

std::vector<PdfFormFieldPtr> PdfFormField::getKids() const {
    std::vector<PdfFormFieldPtr> kids;
    auto arr = getPdfObject()->getAsArray(PdfName::Kids);
    if (!arr) return kids;
    for (int i = 0; i < arr->size(); ++i) {
        auto kid = arr->getAsDictionary(i);
        if (kid && kid->containsKey(PdfName::T)) kids.push_back(std::make_shared<PdfFormField>(kid, document_));
    }
    return kids;
}

std::vector<PdfDictionaryPtr> PdfFormField::getWidgets() const {
    std::vector<PdfDictionaryPtr> widgets;
    if (nameEquals(getPdfObject()->getAsName(PdfName::Subtype), PdfName::Widget)) widgets.push_back(getPdfObject());
    auto arr = getPdfObject()->getAsArray(PdfName::Kids);
    if (arr) {
        for (int i = 0; i < arr->size(); ++i) {
            auto kid = arr->getAsDictionary(i);
            if (kid && !kid->containsKey(PdfName::T) &&
                (nameEquals(kid->getAsName(PdfName::Subtype), PdfName::Widget) || !kid->containsKey(PdfName::Subtype)))
                widgets.push_back(kid);
        }
    }
    return widgets;
}

PdfFormField& PdfFormField::addKid(const PdfFormFieldPtr& kid) {
    auto kids = getPdfObject()->getAsArray(PdfName::Kids);
    if (!kids) {
        kids = std::make_shared<PdfArray>();
        getPdfObject()->put(PdfName::Kids, kids);
    }
    kid->getPdfObject()->put(PdfName::Parent, getPdfObject());
    if (document_ && document_->getWriter()) kid->getPdfObject()->makeIndirect(document_);
    kids->add(kid->getPdfObject());
    return *this;
}

std::vector<std::string> PdfFormField::getOptions() const {
    std::vector<std::string> options;
    auto opt = pdf_cast<PdfArray>(getInheritable(PdfName::Opt));
    if (!opt) return options;
    for (int i = 0; i < opt->size(); ++i) {
        PdfObjectPtr o = opt->get(i);
        if (o->isString()) options.push_back(static_cast<PdfString&>(*o).toUnicodeString());
        else if (o->isArray()) {
            auto pair = std::static_pointer_cast<PdfArray>(o);
            auto s = pair->getAsString(pair->size() > 1 ? 1 : 0);
            if (s) options.push_back(s->toUnicodeString());
        }
    }
    return options;
}

std::vector<std::string> PdfFormField::getAppearanceStates() const {
    std::set<std::string> states;
    for (const auto& w : getWidgets()) {
        auto ap = w->getAsDictionary(PdfName::AP);
        if (!ap) continue;
        for (const PdfNamePtr* key : {&PdfName::N, &PdfName::D}) {
            auto d = ap->getAsDictionary(*key);
            if (!d) continue;
            for (const auto& k : d->keySet())
                if (k->getValue() != "Off") states.insert(k->getValue());
        }
    }
    return std::vector<std::string>(states.begin(), states.end());
}

PdfFormField::DaInfo PdfFormField::parseDefaultAppearance() const {
    DaInfo info;
    auto da = pdf_cast<PdfString>(getInheritable(PdfName::DA));
    if (!da && form_) da = form_->getDefaultAppearance();
    if (da) {
        Bytes bytes = da->getValueBytes();
        PdfCanvasParser parser(bytes, nullptr);
        std::vector<PdfObjectPtr> ops;
        while (parser.parse(ops)) {
            std::string op = ops.back()->toString();
            auto num = [&](size_t i) { return i < ops.size() && ops[i]->isNumber() ? static_cast<PdfNumber&>(*ops[i]).floatValue() : 0.f; };
            if (op == "Tf" && ops.size() >= 3) {
                if (ops[0]->isName()) info.fontName = std::static_pointer_cast<PdfName>(ops[0]);
                info.fontSize = num(1);
            } else if (op == "g" && ops.size() >= 2) {
                info.color = std::make_shared<DeviceGray>(num(0));
            } else if (op == "rg" && ops.size() >= 4) {
                info.color = std::make_shared<DeviceRgb>(num(0), num(1), num(2));
            } else if (op == "k" && ops.size() >= 5) {
                info.color = std::make_shared<DeviceCmyk>(num(0), num(1), num(2), num(3));
            }
        }
    }
    if (form_ && info.fontName) info.font = form_->resolveFont(info.fontName);
    if (!info.font && form_) {
        info.font = form_->getDefaultFont();
        info.fontName = form_->registerFont(info.font);
    }
    if (!info.font) {
        // field not attached to a form yet: Helvetica, as iText does
        static std::shared_ptr<PdfFont> helv;
        if (!helv) helv = PdfFontFactory::createFont(io::StandardFonts::HELVETICA, io::PdfEncodings::WINANSI);
        info.font = helv;
        if (!info.fontName) info.fontName = PdfName::of("Helv");
    }
    if (!info.color) info.color = DeviceGray::BLACK;
    return info;
}

void PdfFormField::updateDefaultAppearance(const DaInfo& da) {
    std::ostringstream s;
    s << "/" << (da.fontName ? da.fontName->getValue() : std::string("Helv")) << " " << io::ByteUtils::formatDouble(da.fontSize) << " Tf ";
    const auto& v = da.color ? da.color->getColorValue() : std::vector<float>{0};
    for (float c : v) s << io::ByteUtils::formatDouble(c) << " ";
    s << (v.size() == 1 ? "g" : v.size() == 3 ? "rg" : "k");
    getPdfObject()->put(PdfName::DA, std::make_shared<PdfString>(s.str()));
    getPdfObject()->setModified();
}

PdfFormField& PdfFormField::setFontAndSize(std::shared_ptr<PdfFont> font, float size) {
    DaInfo da = parseDefaultAppearance();
    if (font && form_) {
        da.font = font;
        da.fontName = form_->registerFont(font);
    }
    da.fontSize = size;
    updateDefaultAppearance(da);
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setFont(std::shared_ptr<PdfFont> font) {
    return setFontAndSize(std::move(font), parseDefaultAppearance().fontSize);
}

PdfFormField& PdfFormField::setFontSize(float fontSize) { return setFontAndSize(nullptr, fontSize); }

PdfFormField& PdfFormField::setColor(const ColorPtr& color) {
    DaInfo da = parseDefaultAppearance();
    da.color = color;
    updateDefaultAppearance(da);
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setJustification(int justification) {
    getPdfObject()->put(PdfName::Q, makeNumber(justification));
    regenerateField();
    return *this;
}

int PdfFormField::getJustification() const {
    auto q = pdf_cast<PdfNumber>(getInheritable(PdfName::Q));
    return q ? q->intValue() : 0;
}

static PdfDictionaryPtr mk(const PdfDictionaryPtr& widget, bool create) {
    auto mkDict = widget->getAsDictionary(PdfName::MK);
    if (!mkDict && create) {
        mkDict = std::make_shared<PdfDictionary>();
        widget->put(PdfName::MK, mkDict);
    }
    return mkDict;
}

PdfFormField& PdfFormField::setBackgroundColor(const ColorPtr& color) {
    for (const auto& w : getWidgets()) mk(w, true)->put(PdfName::BG, std::make_shared<PdfArray>(color->getColorValue()));
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setBorderColor(const ColorPtr& color) {
    for (const auto& w : getWidgets()) mk(w, true)->put(PdfName::BC, std::make_shared<PdfArray>(color->getColorValue()));
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setBorderWidth(float width) {
    for (const auto& w : getWidgets()) {
        auto bs = std::make_shared<PdfDictionary>();
        bs->put(PdfName::W, makeNumber(static_cast<double>(width)));
        w->put(PdfName::BS, bs);
    }
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setValue(const std::string& value) {
    hasDisplayValue_ = false;
    PdfNamePtr type = getFormType();
    if (nameEquals(type, PdfName::Btn)) {
        if (getFieldFlag(FF_PUSH_BUTTON)) return *this;
        // checkbox / radio: value is an appearance state name
        PdfNamePtr v = std::make_shared<PdfName>(value.empty() ? "Off" : value);
        getPdfObject()->put(PdfName::V, v);
        for (const auto& w : getWidgets()) {
            auto ap = w->getAsDictionary(PdfName::AP);
            auto n = ap ? ap->getAsDictionary(PdfName::N) : nullptr;
            bool hasState = n && n->containsKey(v);
            w->put(PdfName::AS, hasState ? v : PdfName::of("Off"));
            w->setModified();
        }
    } else {
        getPdfObject()->put(PdfName::V, std::make_shared<PdfString>(value, io::PdfEncodings::UNICODE_BIG));
    }
    getPdfObject()->setModified();
    regenerateField();
    return *this;
}

PdfFormField& PdfFormField::setValue(const std::string& value, const std::string& display) {
    setValue(value);
    displayValue_ = display;
    hasDisplayValue_ = true;
    regenerateField();
    return *this;
}

static std::array<float, 4> widgetBox(const PdfDictionaryPtr& widget) {
    auto rect = widget->getAsRectangle(PdfName::Rect);
    Rectangle r = rect ? *rect : Rectangle(0, 0, 0, 0);
    return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

static float borderWidthOf(const PdfDictionaryPtr& widget) {
    auto bs = widget->getAsDictionary(PdfName::BS);
    if (bs) {
        if (auto w = bs->getAsNumber(PdfName::W)) return w->floatValue();
        return 1;
    }
    auto mkDict = widget->getAsDictionary(PdfName::MK);
    return mkDict && mkDict->containsKey(PdfName::BC) ? 1.f : 0.f;
}

static ColorPtr colorFromArray(const PdfArrayPtr& arr) {
    if (!arr) return nullptr;
    auto v = arr->toFloatArray();
    if (v.size() == 1) return std::make_shared<DeviceGray>(v[0]);
    if (v.size() == 3) return std::make_shared<DeviceRgb>(v[0], v[1], v[2]);
    if (v.size() == 4) return std::make_shared<DeviceCmyk>(v[0], v[1], v[2], v[3]);
    return nullptr;
}

static void drawBackgroundAndBorder(PdfCanvas& canvas, const PdfDictionaryPtr& widget, float w, float h, float bw) {
    auto mkDict = widget->getAsDictionary(PdfName::MK);
    ColorPtr bg = mkDict ? colorFromArray(mkDict->getAsArray(PdfName::BG)) : nullptr;
    ColorPtr bc = mkDict ? colorFromArray(mkDict->getAsArray(PdfName::BC)) : nullptr;
    if (bg) canvas.saveState().setFillColor(bg).rectangle(0, 0, w, h).fill().restoreState();
    if (bc && bw > 0)
        canvas.saveState().setStrokeColor(bc).setLineWidth(bw).rectangle(bw / 2, bw / 2, w - bw, h - bw).stroke().restoreState();
}

bool PdfFormField::regenerateTextWidget(const PdfDictionaryPtr& widget, const std::string& text) {
    auto box = widgetBox(widget);
    float w = box[2], h = box[3];
    if (w <= 0 || h <= 0) return false;
    DaInfo da = parseDefaultAppearance();
    float bw = borderWidthOf(widget);
    float padding = std::max(1.f, bw) + 1;
    bool multiline = isMultiline();
    float fontSize = da.fontSize;
    const auto& metrics = da.font->getFontProgram()->getFontMetrics();
    float asc = static_cast<float>(metrics.getTypoAscender()), desc = static_cast<float>(metrics.getTypoDescender());
    if (asc - desc <= 0) {
        asc = 800;
        desc = -200;
    }
    std::string shown = text;
    if (getFieldFlag(FF_PASSWORD)) shown = std::string(io::TextUtil::utf8ToCodePoints(text).size(), '*');
    if (fontSize <= 0) {
        // auto size (iText caps it at 12 for multiline text)
        fontSize = multiline ? 12 : std::min((h - 2 * padding) * 1000 / (asc - desc), 12.f * 2);
        float textWidth = da.font->getWidth(shown, 1);
        if (!multiline && textWidth > 0) fontSize = std::min(fontSize, (w - 2 * padding) / textWidth);
        fontSize = std::max(4.f, fontSize);
    }
    auto xObject = std::make_shared<PdfFormXObject>(Rectangle(0, 0, w, h));
    PdfCanvas canvas(*xObject, document_);
    drawBackgroundAndBorder(canvas, widget, w, h, bw);
    canvas.beginMarkedContent(PdfName::Tx);
    canvas.saveState().rectangle(padding - 1, padding - 1, w - 2 * (padding - 1), h - 2 * (padding - 1)).clip().endPath();
    std::vector<std::string> lines;
    if (multiline) {
        float maxWidth = w - 2 * padding;
        std::string remaining = shown;
        size_t pos;
        while ((pos = remaining.find('\n')) != std::string::npos) {
            for (const auto& l : da.font->splitString(remaining.substr(0, pos), fontSize, maxWidth)) lines.push_back(l);
            remaining = remaining.substr(pos + 1);
        }
        for (const auto& l : da.font->splitString(remaining, fontSize, maxWidth)) lines.push_back(l);
    } else {
        lines.push_back(shown);
    }
    float lineHeight = (asc - desc) * fontSize / 1000.f;
    float y;
    if (multiline) y = h - padding - asc * fontSize / 1000.f;
    else y = (h - lineHeight) / 2 - desc * fontSize / 1000.f;
    int q = getJustification();
    canvas.beginText().setFontAndSize(da.font, fontSize).setFillColor(da.color);
    for (const auto& line : lines) {
        float lw = da.font->getWidth(line, fontSize);
        float x = q == ALIGN_CENTER ? (w - lw) / 2 : (q == ALIGN_RIGHT ? w - padding - lw : padding);
        canvas.setTextMatrix(x, y);
        canvas.showText(line);
        y -= lineHeight;
    }
    canvas.endText().restoreState().endMarkedContent();
    auto ap = std::make_shared<PdfDictionary>();
    xObject->getPdfObject()->makeIndirect(document_);
    ap->put(PdfName::N, xObject->getPdfObject());
    widget->put(PdfName::AP, ap);
    widget->setModified();
    return true;
}

bool PdfFormField::regenerateCheckBoxWidget(const PdfDictionaryPtr& widget, const std::string& value) {
    auto box = widgetBox(widget);
    float w = box[2], h = box[3];
    if (w <= 0 || h <= 0) return false;
    // keep existing appearances of read documents
    auto existing = widget->getAsDictionary(PdfName::AP);
    if (existing && existing->getAsDictionary(PdfName::N) && !getPdfObject()->isModified() && document_->getReader()) return true;
    std::string onState = "Yes";
    if (existing) {
        auto n = existing->getAsDictionary(PdfName::N);
        if (n)
            for (const auto& k : n->keySet())
                if (k->getValue() != "Off") onState = k->getValue();
    }
    float bw = borderWidthOf(widget);
    auto zapf = PdfFontFactory::createFont(io::StandardFonts::ZAPFDINGBATS);
    auto makeState = [&](bool on) {
        auto x = std::make_shared<PdfFormXObject>(Rectangle(0, 0, w, h));
        PdfCanvas canvas(*x, document_);
        drawBackgroundAndBorder(canvas, widget, w, h, bw);
        if (on) {
            float size = std::min(w, h) * 0.8f;
            // ZapfDingbats code '4' is the check mark (font-specific encoding, as iText's /MK /CA (4))
            std::string check = "4";
            float cw = zapf->getWidth(check, size);
            canvas.beginText().setFontAndSize(zapf, size).setTextMatrix((w - cw) / 2, (h - size * 0.7f) / 2).showText(check).endText();
        }
        x->getPdfObject()->makeIndirect(document_);
        return x->getPdfObject();
    };
    auto n = std::make_shared<PdfDictionary>();
    n->put(PdfName::of(onState), makeState(true));
    n->put(PdfName::of("Off"), makeState(false));
    auto ap = std::make_shared<PdfDictionary>();
    ap->put(PdfName::N, n);
    widget->put(PdfName::AP, ap);
    widget->put(PdfName::AS, value == onState ? PdfName::of(onState) : PdfName::of("Off"));
    widget->setModified();
    return true;
}

bool PdfFormField::regenerateField() {
    if (!form_) return false;  // appearances are generated once the field is added to a form
    if (!form_->isGenerateAppearance()) return false;
    PdfNamePtr type = getFormType();
    bool ok = true;
    if (nameEquals(type, PdfName::Tx) || nameEquals(type, PdfName::Ch)) {
        std::string text = hasDisplayValue_ ? displayValue_ : getValueAsString();
        for (const auto& w : getWidgets()) ok = regenerateTextWidget(w, text) && ok;
    } else if (nameEquals(type, PdfName::Btn) && !getFieldFlag(FF_PUSH_BUTTON)) {
        std::string value = getValueAsString();
        for (const auto& w : getWidgets()) ok = regenerateCheckBoxWidget(w, value) && ok;
    }
    for (const auto& kid : getKids()) {
        kid->setAcroForm(form_);
        ok = kid->regenerateField() && ok;
    }
    return ok;
}

// ============================================================ PdfAcroForm
std::shared_ptr<PdfAcroForm> PdfAcroForm::getAcroForm(PdfDocument& document, bool createIfNotExist) {
    auto acroFormDict = document.getCatalog().getPdfObject()->getAsDictionary(PdfName::AcroForm);
    if (!acroFormDict) {
        if (!createIfNotExist) return nullptr;
        acroFormDict = std::make_shared<PdfDictionary>();
        acroFormDict->put(PdfName::Fields, std::make_shared<PdfArray>());
        if (document.getWriter()) acroFormDict->makeIndirect(&document);
        document.getCatalog().put(PdfName::AcroForm, acroFormDict);
    }
    return std::make_shared<PdfAcroForm>(acroFormDict, &document);
}

PdfAcroForm::PdfAcroForm(PdfDictionaryPtr dict, PdfDocument* document)
    : PdfObjectWrapper<PdfDictionary>(std::move(dict)), document_(document) {}

PdfArrayPtr PdfAcroForm::getFields(bool create) {
    auto fields = getPdfObject()->getAsArray(PdfName::Fields);
    if (!fields && create) {
        fields = std::make_shared<PdfArray>();
        getPdfObject()->put(PdfName::Fields, fields);
    }
    return fields;
}

void PdfAcroForm::addField(const PdfFormFieldPtr& field, PdfPage* page) {
    field->setAcroForm(this);
    if (document_->getWriter()) field->getPdfObject()->makeIndirect(document_);
    getFields(true)->add(field->getPdfObject());
    getPdfObject()->setModified();
    if (page) {
        for (const auto& w : field->getWidgets()) {
            bool present = false;
            if (auto annots = page->getPdfObject()->getAsArray(PdfName::Annots)) present = annots->contains(w);
            if (!present) page->addAnnotation(w);
        }
    }
    field->regenerateField();
}

void PdfAcroForm::collectFields(const PdfDictionaryPtr& dict, std::map<std::string, PdfFormFieldPtr>& out, int depth) {
    if (!dict || depth > 64) return;
    auto field = std::make_shared<PdfFormField>(dict, document_);
    field->setAcroForm(this);
    std::string name = field->getFieldName();
    if (!name.empty()) out[name] = field;
    auto kids = dict->getAsArray(PdfName::Kids);
    if (!kids) return;
    for (int i = 0; i < kids->size(); ++i) {
        auto kid = kids->getAsDictionary(i);
        if (kid && kid->containsKey(PdfName::T)) collectFields(kid, out, depth + 1);
    }
}

std::map<std::string, PdfFormFieldPtr> PdfAcroForm::getAllFormFields() {
    std::map<std::string, PdfFormFieldPtr> out;
    auto fields = getFields(false);
    if (!fields) return out;
    for (int i = 0; i < fields->size(); ++i) collectFields(fields->getAsDictionary(i), out, 0);
    return out;
}

std::map<std::string, PdfFormFieldPtr> PdfAcroForm::getRootFormFields() {
    std::map<std::string, PdfFormFieldPtr> out;
    auto fields = getFields(false);
    if (!fields) return out;
    for (int i = 0; i < fields->size(); ++i) {
        auto dict = fields->getAsDictionary(i);
        if (!dict) continue;
        auto field = std::make_shared<PdfFormField>(dict, document_);
        field->setAcroForm(this);
        out[field->getFieldName()] = field;
    }
    return out;
}

PdfFormFieldPtr PdfAcroForm::getField(const std::string& fieldName) {
    auto all = getAllFormFields();
    auto it = all.find(fieldName);
    return it == all.end() ? nullptr : it->second;
}

PdfAcroForm& PdfAcroForm::setNeedAppearances(bool needAppearances) {
    getPdfObject()->put(PdfName::NeedAppearances, PdfBoolean::valueOf(needAppearances));
    getPdfObject()->setModified();
    return *this;
}

PdfAcroForm& PdfAcroForm::setDefaultAppearance(const std::string& appearance) {
    getPdfObject()->put(PdfName::DA, std::make_shared<PdfString>(appearance));
    getPdfObject()->setModified();
    return *this;
}

std::shared_ptr<PdfFont> PdfAcroForm::getDefaultFont() {
    if (!defaultFont_) defaultFont_ = PdfFontFactory::createFont(io::StandardFonts::HELVETICA, io::PdfEncodings::WINANSI);
    return defaultFont_;
}

std::shared_ptr<PdfFont> PdfAcroForm::resolveFont(const PdfNamePtr& name) {
    auto dr = getDefaultResources();
    auto fonts = dr ? dr->getAsDictionary(PdfName::Font) : nullptr;
    auto fontDict = fonts ? fonts->getAsDictionary(name) : nullptr;
    if (fontDict) {
        auto it = fontCache_.find(fontDict.get());
        if (it != fontCache_.end()) return it->second;
        std::shared_ptr<PdfFont> font;
        // standard 14 fonts referenced by name are re-created so that new text can be encoded
        auto baseFont = fontDict->getAsName(PdfName::BaseFont);
        if (baseFont && io::StandardFonts::isStandardFont(baseFont->getValue()) && !fontDict->containsKey(PdfName::FontDescriptor))
            font = PdfFontFactory::createFont(baseFont->getValue(), io::PdfEncodings::WINANSI);
        else
            font = PdfFontFactory::createFont(fontDict);
        fontCache_[fontDict.get()] = font;
        return font;
    }
    // well-known abbreviations used by Acrobat
    const std::string& n = name->getValue();
    if (n == "Helv") return getDefaultFont();
    if (n == "HeBo") return PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    if (n == "Cour") return PdfFontFactory::createFont(io::StandardFonts::COURIER);
    if (n == "TiRo") return PdfFontFactory::createFont(io::StandardFonts::TIMES_ROMAN);
    if (n == "ZaDb") return PdfFontFactory::createFont(io::StandardFonts::ZAPFDINGBATS);
    return nullptr;
}

PdfNamePtr PdfAcroForm::registerFont(const std::shared_ptr<PdfFont>& font) {
    auto dr = getDefaultResources();
    if (!dr) {
        dr = std::make_shared<PdfDictionary>();
        setDefaultResources(dr);
    }
    auto fonts = dr->getAsDictionary(PdfName::Font);
    if (!fonts) {
        fonts = std::make_shared<PdfDictionary>();
        dr->put(PdfName::Font, fonts);
    }
    for (const auto& kv : fonts->entrySet()) {
        auto d = pdf_cast<PdfDictionary>(kv.second->isIndirectReference() ? fonts->get(kv.first) : kv.second);
        if (d.get() == font->getPdfObject().get()) return kv.first;
    }
    if (document_->getWriter()) document_->addFont(font);
    PdfNamePtr name;
    if (font.get() == defaultFont_.get() && !fonts->containsKey(PdfName::of("Helv"))) {
        name = PdfName::of("Helv");
    } else {
        int i = 0;
        do {
            name = std::make_shared<PdfName>("F" + std::to_string(++i));
        } while (fonts->containsKey(name));
    }
    fonts->put(name, font->getPdfObject());
    fontCache_[font->getPdfObject().get()] = font;
    getPdfObject()->setModified();
    return name;
}

static PdfPage* findWidgetPage(PdfDocument* doc, const PdfDictionaryPtr& widget) {
    auto p = widget->getAsDictionary(PdfName::P);
    if (p) {
        PdfPage* page = doc->getPage(p.get());
        if (page) return page;
    }
    for (int i = 1; i <= doc->getNumberOfPages(); ++i) {
        auto annots = doc->getPage(i)->getPdfObject()->getAsArray(PdfName::Annots);
        if (annots && annots->contains(widget)) return doc->getPage(i);
    }
    return nullptr;
}

void PdfAcroForm::flattenFields() {
    if (!document_->getWriter()) throw PdfException("The document must be opened in writing mode to flatten fields.");
    auto all = getAllFormFields();
    std::vector<PdfFormFieldPtr> toFlatten;
    if (fieldsForFlattening_.empty()) {
        for (auto& kv : all) toFlatten.push_back(kv.second);
    } else {
        for (const auto& name : fieldsForFlattening_) {
            auto it = all.find(name);
            if (it != all.end()) toFlatten.push_back(it->second);
        }
    }
    std::map<PdfPage*, std::unique_ptr<PdfCanvas>> canvases;
    std::set<const PdfObject*> flattenedWidgets;
    for (auto& field : toFlatten) {
        for (const auto& widget : field->getWidgets()) {
            if (flattenedWidgets.count(widget.get())) continue;
            flattenedWidgets.insert(widget.get());
            PdfPage* page = findWidgetPage(document_, widget);
            if (!page) continue;
            auto f = widget->getAsNumber(PdfName::F);
            bool hidden = f && (f->intValue() & 2);
            auto ap = widget->getAsDictionary(PdfName::AP);
            PdfObjectPtr normal = ap ? ap->get(PdfName::N) : nullptr;
            PdfStreamPtr appearance;
            if (normal && normal->isStream()) {
                appearance = std::static_pointer_cast<PdfStream>(normal);
            } else if (normal && normal->isDictionary()) {
                auto as = widget->getAsName(PdfName::AS);
                if (as) appearance = std::static_pointer_cast<PdfDictionary>(normal)->getAsStream(as);
            }
            auto rect = widget->getAsRectangle(PdfName::Rect);
            if (appearance && rect && !hidden) {
                auto& canvas = canvases[page];
                if (!canvas) canvas = std::make_unique<PdfCanvas>(page, true);
                auto bboxArr = appearance->getAsArray(PdfName::BBox);
                Rectangle bbox = bboxArr ? bboxArr->toRectangle() : Rectangle(0, 0, rect->getWidth(), rect->getHeight());
                PdfFormXObject xObject(appearance);
                float sx = bbox.getWidth() != 0 ? rect->getWidth() / bbox.getWidth() : 1;
                float sy = bbox.getHeight() != 0 ? rect->getHeight() / bbox.getHeight() : 1;
                canvas->addXObjectWithTransformationMatrix(xObject, sx, 0, 0, sy, rect->getX() - bbox.getX() * sx,
                                                           rect->getY() - bbox.getY() * sy);
            }
            // remove the widget from the page
            auto annots = page->getPdfObject()->getAsArray(PdfName::Annots);
            if (annots) {
                annots->remove(widget.get());
                if (annots->getIndirectReference()) annots->setModified();
                page->getPdfObject()->setModified();
            }
        }
    }
    // remove flattened fields from the form
    for (auto& field : toFlatten) {
        auto fields = getFields(false);
        if (fields) fields->remove(field->getPdfObject().get());
        auto parent = field->getPdfObject()->getAsDictionary(PdfName::Parent);
        if (parent) {
            auto kids = parent->getAsArray(PdfName::Kids);
            if (kids) kids->remove(field->getPdfObject().get());
        }
    }
    getPdfObject()->setModified();
    auto fields = getFields(false);
    if (!fields || fields->isEmpty()) document_->getCatalog().remove(PdfName::AcroForm);
    fieldsForFlattening_.clear();
}

bool PdfAcroForm::removeField(const std::string& fieldName) {
    auto field = getField(fieldName);
    if (!field) return false;
    for (const auto& widget : field->getWidgets()) {
        PdfPage* page = findWidgetPage(document_, widget);
        if (!page) continue;
        auto annots = page->getPdfObject()->getAsArray(PdfName::Annots);
        if (annots) annots->remove(widget.get());
        page->getPdfObject()->setModified();
    }
    auto parent = field->getPdfObject()->getAsDictionary(PdfName::Parent);
    if (parent) {
        auto kids = parent->getAsArray(PdfName::Kids);
        if (kids) kids->remove(field->getPdfObject().get());
        parent->setModified();
    } else if (auto fields = getFields(false)) {
        fields->remove(field->getPdfObject().get());
    }
    getPdfObject()->setModified();
    return true;
}

void PdfAcroForm::renameField(const std::string& oldName, const std::string& newName) {
    auto field = getField(oldName);
    if (!field) return;
    field->getPdfObject()->put(PdfName::T, std::make_shared<PdfString>(newName, io::PdfEncodings::UNICODE_BIG));
    field->getPdfObject()->setModified();
}

// ============================================================ builders
static PdfFormFieldPtr makeField(PdfDocument* doc, const std::string& name, const PdfNamePtr& type, const Rectangle& rect) {
    auto dict = std::make_shared<PdfDictionary>();
    dict->put(PdfName::FT, type);
    dict->put(PdfName::T, std::make_shared<PdfString>(name, io::PdfEncodings::UNICODE_BIG));
    dict->put(PdfName::Type, PdfName::Annot);
    dict->put(PdfName::Subtype, PdfName::Widget);
    dict->put(PdfName::Rect, std::make_shared<PdfArray>(rect));
    dict->put(PdfName::F, makeNumber(4));  // print
    if (doc->getWriter()) dict->makeIndirect(doc);
    return std::make_shared<PdfFormField>(dict, doc);
}

PdfFormFieldPtr TextFormFieldBuilder::createText() {
    auto field = makeField(document_, name_, PdfName::Tx, rect_);
    field->getPdfObject()->put(PdfName::DA, std::make_shared<PdfString>("/Helv 0 Tf 0 g"));
    field->getPdfObject()->put(PdfName::V, std::make_shared<PdfString>(""));
    return field;
}

PdfFormFieldPtr TextFormFieldBuilder::createMultilineText() {
    auto field = createText();
    field->getPdfObject()->put(PdfName::Ff, makeNumber(PdfFormField::FF_MULTILINE));
    return field;
}

PdfFormFieldPtr CheckBoxFormFieldBuilder::createCheckBox() {
    auto field = makeField(document_, name_, PdfName::Btn, rect_);
    field->getPdfObject()->put(PdfName::V, PdfName::of("Off"));
    field->getPdfObject()->put(PdfName::AS, PdfName::of("Off"));
    auto mkDict = std::make_shared<PdfDictionary>();
    mkDict->put(PdfName::BC, std::make_shared<PdfArray>(std::vector<float>{0}));
    mkDict->put(PdfName::CA, std::make_shared<PdfString>("4"));
    field->getPdfObject()->put(PdfName::MK, mkDict);
    return field;
}

PdfFormFieldPtr ChoiceFormFieldBuilder::create(bool combo) {
    auto field = makeField(document_, name_, PdfName::Ch, rect_);
    if (combo) field->getPdfObject()->put(PdfName::Ff, makeNumber(PdfFormField::FF_COMBO));
    auto opt = std::make_shared<PdfArray>();
    for (const auto& o : options_) opt->add(std::make_shared<PdfString>(o, io::PdfEncodings::UNICODE_BIG));
    field->getPdfObject()->put(PdfName::Opt, opt);
    field->getPdfObject()->put(PdfName::DA, std::make_shared<PdfString>("/Helv 0 Tf 0 g"));
    return field;
}

PdfFormFieldPtr ChoiceFormFieldBuilder::createComboBox() { return create(true); }
PdfFormFieldPtr ChoiceFormFieldBuilder::createList() { return create(false); }

}  // namespace forms
}  // namespace pdfcpp
