#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/Color.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"

namespace pdfcpp {
namespace forms {

using kernel::PdfDictionaryPtr;
using kernel::PdfDocument;
using kernel::PdfPage;
using kernel::Rectangle;

class PdfAcroForm;

/// Port of PdfFormField (with the PdfTextFormField / PdfButtonFormField / PdfChoiceFormField behaviour folded in).
/// A field dictionary may also be its own (single) widget annotation, as in PDF "merged" fields.
class PdfFormField : public kernel::PdfObjectWrapper<kernel::PdfDictionary> {
public:
    // field flags (Ff)
    static constexpr int FF_READ_ONLY = 1, FF_REQUIRED = 2, FF_NO_EXPORT = 4;
    static constexpr int FF_MULTILINE = 1 << 12, FF_PASSWORD = 1 << 13, FF_COMB = 1 << 24;
    static constexpr int FF_NO_TOGGLE_TO_OFF = 1 << 14, FF_RADIO = 1 << 15, FF_PUSH_BUTTON = 1 << 16;
    static constexpr int FF_COMBO = 1 << 17, FF_EDIT = 1 << 18;
    static constexpr int ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2;

    PdfFormField(PdfDictionaryPtr dict, PdfDocument* document);

    /// Fully qualified name ("parent.child").
    std::string getFieldName() const;
    /// Partial name (/T).
    std::string getPartialFieldName() const;
    /// /FT of the field (inherited): Tx, Btn, Ch or Sig.
    kernel::PdfNamePtr getFormType() const;
    std::string getValueAsString() const;
    kernel::PdfObjectPtr getValue() const;
    /// Sets the value and regenerates the widget appearances (port of setValue(String)).
    PdfFormField& setValue(const std::string& value);
    /// Text fields: sets the displayed value separately from the stored one.
    PdfFormField& setValue(const std::string& value, const std::string& display);
    int getFieldFlags() const;
    PdfFormField& setFieldFlags(int flags);
    PdfFormField& setFieldFlag(int flag, bool value = true);
    bool getFieldFlag(int flag) const { return (getFieldFlags() & flag) != 0; }
    bool isReadOnly() const { return getFieldFlag(FF_READ_ONLY); }
    PdfFormField& setReadOnly(bool v) { return setFieldFlag(FF_READ_ONLY, v); }
    bool isRequired() const { return getFieldFlag(FF_REQUIRED); }
    PdfFormField& setRequired(bool v) { return setFieldFlag(FF_REQUIRED, v); }
    bool isMultiline() const { return getFieldFlag(FF_MULTILINE); }
    PdfFormField& setMultiline(bool v) { return setFieldFlag(FF_MULTILINE, v); }
    PdfFormField& setFont(std::shared_ptr<kernel::PdfFont> font);
    PdfFormField& setFontSize(float fontSize);
    PdfFormField& setFontAndSize(std::shared_ptr<kernel::PdfFont> font, float size);
    PdfFormField& setColor(const kernel::ColorPtr& color);
    PdfFormField& setJustification(int justification);
    int getJustification() const;
    PdfFormField& setBackgroundColor(const kernel::ColorPtr& color);
    PdfFormField& setBorderColor(const kernel::ColorPtr& color);
    PdfFormField& setBorderWidth(float width);
    /// Choice fields: /Opt values.
    std::vector<std::string> getOptions() const;
    /// Checkbox/radio: the appearance states other than /Off.
    std::vector<std::string> getAppearanceStates() const;
    std::vector<std::shared_ptr<PdfFormField>> getKids() const;
    /// Widget annotations of this field (the field itself if merged).
    std::vector<PdfDictionaryPtr> getWidgets() const;
    PdfFormField& addKid(const std::shared_ptr<PdfFormField>& kid);
    /// Regenerates the normal appearance of all widgets.
    bool regenerateField();
    PdfDocument* getDocument() const { return document_; }
    void setAcroForm(PdfAcroForm* form) { form_ = form; }

private:
    kernel::PdfObjectPtr getInheritable(const kernel::PdfNamePtr& key) const;
    struct DaInfo {
        std::shared_ptr<kernel::PdfFont> font;
        kernel::PdfNamePtr fontName;
        float fontSize = 0;
        kernel::ColorPtr color;
    };
    DaInfo parseDefaultAppearance() const;
    void updateDefaultAppearance(const DaInfo& da);
    bool regenerateTextWidget(const PdfDictionaryPtr& widget, const std::string& text);
    bool regenerateCheckBoxWidget(const PdfDictionaryPtr& widget, const std::string& value);
    PdfDocument* document_;
    PdfAcroForm* form_ = nullptr;
    std::string displayValue_;
    bool hasDisplayValue_ = false;
};

using PdfFormFieldPtr = std::shared_ptr<PdfFormField>;

/// Port of PdfAcroForm.
class PdfAcroForm : public kernel::PdfObjectWrapper<kernel::PdfDictionary> {
public:
    static std::shared_ptr<PdfAcroForm> getAcroForm(PdfDocument& document, bool createIfNotExist);
    PdfAcroForm(PdfDictionaryPtr dict, PdfDocument* document);

    void addField(const PdfFormFieldPtr& field, PdfPage* page = nullptr);
    /// All terminal and non-terminal fields keyed by fully qualified name.
    std::map<std::string, PdfFormFieldPtr> getAllFormFields();
    std::map<std::string, PdfFormFieldPtr> getRootFormFields();
    PdfFormFieldPtr getField(const std::string& fieldName);
    PdfAcroForm& setNeedAppearances(bool needAppearances);
    PdfAcroForm& setDefaultAppearance(const std::string& appearance);
    kernel::PdfStringPtr getDefaultAppearance() const { return getPdfObject()->getAsString(kernel::PdfName::DA); }
    PdfDictionaryPtr getDefaultResources() const { return getPdfObject()->getAsDictionary(kernel::PdfName::DR); }
    PdfAcroForm& setDefaultResources(const PdfDictionaryPtr& dr) {
        getPdfObject()->put(kernel::PdfName::DR, dr);
        return *this;
    }
    bool isGenerateAppearance() const { return generateAppearance_; }
    void setGenerateAppearance(bool v) { generateAppearance_ = v; }
    /// Draws every widget's appearance on its page and removes the fields (port of flattenFields).
    void flattenFields();
    /// Marks a field for partial flattening.
    void partialFormFlattening(const std::string& fieldName) { fieldsForFlattening_.push_back(fieldName); }
    bool removeField(const std::string& fieldName);
    void renameField(const std::string& oldName, const std::string& newName);
    PdfDocument* getPdfDocument() const { return document_; }
    /// Font used for generated appearances when /DA does not resolve.
    std::shared_ptr<kernel::PdfFont> getDefaultFont();
    /// Resolves a font name from /DR /Font.
    std::shared_ptr<kernel::PdfFont> resolveFont(const kernel::PdfNamePtr& name);
    kernel::PdfNamePtr registerFont(const std::shared_ptr<kernel::PdfFont>& font);

private:
    void collectFields(const PdfDictionaryPtr& dict, std::map<std::string, PdfFormFieldPtr>& out, int depth);
    kernel::PdfArrayPtr getFields(bool create);
    PdfDocument* document_;
    bool generateAppearance_ = true;
    std::vector<std::string> fieldsForFlattening_;
    std::shared_ptr<kernel::PdfFont> defaultFont_;
    std::map<const kernel::PdfObject*, std::shared_ptr<kernel::PdfFont>> fontCache_;
};

/// Port of TextFormFieldBuilder.
class TextFormFieldBuilder {
public:
    TextFormFieldBuilder(PdfDocument* document, const std::string& formFieldName) : document_(document), name_(formFieldName) {}
    TextFormFieldBuilder& setWidgetRectangle(const Rectangle& rect) {
        rect_ = rect;
        hasRect_ = true;
        return *this;
    }
    TextFormFieldBuilder& setPage(PdfPage* page) {
        page_ = page;
        return *this;
    }
    TextFormFieldBuilder& setFont(std::shared_ptr<kernel::PdfFont> font) {
        font_ = std::move(font);
        return *this;
    }
    PdfFormFieldPtr createText();
    PdfFormFieldPtr createMultilineText();

private:
    PdfDocument* document_;
    std::string name_;
    Rectangle rect_;
    bool hasRect_ = false;
    PdfPage* page_ = nullptr;
    std::shared_ptr<kernel::PdfFont> font_;
};

/// Port of CheckBoxFormFieldBuilder.
class CheckBoxFormFieldBuilder {
public:
    CheckBoxFormFieldBuilder(PdfDocument* document, const std::string& formFieldName) : document_(document), name_(formFieldName) {}
    CheckBoxFormFieldBuilder& setWidgetRectangle(const Rectangle& rect) {
        rect_ = rect;
        return *this;
    }
    PdfFormFieldPtr createCheckBox();

private:
    PdfDocument* document_;
    std::string name_;
    Rectangle rect_;
};

/// Port of ChoiceFormFieldBuilder (combo boxes and list boxes).
class ChoiceFormFieldBuilder {
public:
    ChoiceFormFieldBuilder(PdfDocument* document, const std::string& formFieldName) : document_(document), name_(formFieldName) {}
    ChoiceFormFieldBuilder& setWidgetRectangle(const Rectangle& rect) {
        rect_ = rect;
        return *this;
    }
    ChoiceFormFieldBuilder& setOptions(const std::vector<std::string>& options) {
        options_ = options;
        return *this;
    }
    PdfFormFieldPtr createComboBox();
    PdfFormFieldPtr createList();

private:
    PdfFormFieldPtr create(bool combo);
    PdfDocument* document_;
    std::string name_;
    Rectangle rect_;
    std::vector<std::string> options_;
};

}  // namespace forms
}  // namespace pdfcpp
