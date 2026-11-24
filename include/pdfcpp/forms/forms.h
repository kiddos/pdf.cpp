#ifndef PDFCPP_FORMS_FORMS_H_
#define PDFCPP_FORMS_FORMS_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pdfcpp/kernel/colors/color.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace forms {

using kernel::PdfDictionaryPtr;
using kernel::PdfDocument;
using kernel::PdfPage;
using kernel::Rectangle;

class PdfAcroForm;

/// Port of PdfFormField (with the PdfTextFormField / PdfButtonFormField /
/// PdfChoiceFormField behaviour folded in). A field dictionary may also be its
/// own (single) widget annotation, as in PDF "merged" fields.
class PdfFormField : public kernel::PdfObjectWrapper<kernel::PdfDictionary> {
 public:
  // field flags (Ff)
  static constexpr int kFfReadOnly = 1, kFfRequired = 2, kFfNoExport = 4;
  static constexpr int kFfMultiline = 1 << 12, kFfPassword = 1 << 13,
                       kFfComb = 1 << 24;
  static constexpr int kFfNoToggleToOff = 1 << 14, kFfRadio = 1 << 15,
                       kFfPushButton = 1 << 16;
  static constexpr int kFfCombo = 1 << 17, kFfEdit = 1 << 18;
  static constexpr int kAlignLeft = 0, kAlignCenter = 1, kAlignRight = 2;

  PdfFormField(PdfDictionaryPtr dict, PdfDocument* document);

  /// Fully qualified name ("parent.child").
  std::string GetFieldName() const;
  /// Partial name (/T).
  std::string GetPartialFieldName() const;
  /// /FT of the field (inherited): Tx, Btn, Ch or Sig.
  kernel::PdfNamePtr GetFormType() const;
  std::string GetValueAsString() const;
  kernel::PdfObjectPtr GetValue() const;
  /// Sets the value and regenerates the widget appearances (port of
  /// setValue(String)).
  PdfFormField& SetValue(const std::string& value);
  /// Text fields: sets the displayed value separately from the stored one.
  PdfFormField& SetValue(const std::string& value, const std::string& display);
  int GetFieldFlags() const;
  PdfFormField& SetFieldFlags(int flags);
  PdfFormField& SetFieldFlag(int flag, bool value = true);
  bool GetFieldFlag(int flag) const { return (GetFieldFlags() & flag) != 0; }
  bool IsReadOnly() const { return GetFieldFlag(kFfReadOnly); }
  PdfFormField& SetReadOnly(bool v) { return SetFieldFlag(kFfReadOnly, v); }
  bool IsRequired() const { return GetFieldFlag(kFfRequired); }
  PdfFormField& SetRequired(bool v) { return SetFieldFlag(kFfRequired, v); }
  bool IsMultiline() const { return GetFieldFlag(kFfMultiline); }
  PdfFormField& SetMultiline(bool v) { return SetFieldFlag(kFfMultiline, v); }
  PdfFormField& SetFont(std::shared_ptr<kernel::PdfFont> font);
  PdfFormField& SetFontSize(float font_size);
  PdfFormField& SetFontAndSize(std::shared_ptr<kernel::PdfFont> font,
                               float size);
  PdfFormField& SetColor(const kernel::ColorPtr& color);
  PdfFormField& SetJustification(int justification);
  int GetJustification() const;
  PdfFormField& SetBackgroundColor(const kernel::ColorPtr& color);
  PdfFormField& SetBorderColor(const kernel::ColorPtr& color);
  PdfFormField& SetBorderWidth(float width);
  /// Checkboxes: (re)draws the appearances with `on_state_name` as the "on"
  /// state (port of PdfFormAnnotation.setCheckBoxAppearanceOnStateName);
  /// SetValue(on_state_name) then checks the box.
  PdfFormField& SetCheckBoxAppearanceOnStateName(
      const std::string& on_state_name);
  /// Choice fields: /Opt values.
  std::vector<std::string> GetOptions() const;
  /// Checkbox/radio: the appearance states other than /Off.
  std::vector<std::string> GetAppearanceStates() const;
  std::vector<std::shared_ptr<PdfFormField>> GetKids() const;
  /// Widget annotations of this field (the field itself if merged).
  std::vector<PdfDictionaryPtr> GetWidgets() const;
  PdfFormField& AddKid(const std::shared_ptr<PdfFormField>& kid);
  /// Regenerates the normal appearance of all widgets.
  bool RegenerateField();
  PdfDocument* GetDocument() const { return document_; }
  /// Attaches the field to a form; a font set while detached is registered in
  /// the form's /DR now.
  void SetAcroForm(PdfAcroForm* form);

 private:
  kernel::PdfObjectPtr GetInheritable(const kernel::PdfNamePtr& key) const;
  struct DaInfo {
    std::shared_ptr<kernel::PdfFont> font;
    kernel::PdfNamePtr font_name;
    float font_size = 0;
    kernel::ColorPtr color;
  };
  DaInfo ParseDefaultAppearance() const;
  void UpdateDefaultAppearance(const DaInfo& da);
  bool RegenerateTextWidget(const PdfDictionaryPtr& widget,
                            const std::string& text);
  bool RegenerateCheckBoxWidget(
      const PdfDictionaryPtr& widget, const std::string& value,
      const std::string& on_state_override = std::string());
  PdfDocument* document_;
  PdfAcroForm* form_ = nullptr;
  /// Font set before the field was attached to a form (e.g. by
  /// TextFormFieldBuilder::setFont).
  std::shared_ptr<kernel::PdfFont> pending_font_;
  /// Set when appearance-affecting properties change: existing checkbox artwork
  /// of read documents is then redrawn.
  bool appearance_dirty_ = false;
  std::string display_value_;
  bool has_display_value_ = false;
};

using PdfFormFieldPtr = std::shared_ptr<PdfFormField>;

/// Port of PdfAcroForm.
class PdfAcroForm : public kernel::PdfObjectWrapper<kernel::PdfDictionary> {
 public:
  static std::shared_ptr<PdfAcroForm> GetAcroForm(PdfDocument& document,
                                                  bool create_if_not_exist);
  PdfAcroForm(PdfDictionaryPtr dict, PdfDocument* document);

  void AddField(const PdfFormFieldPtr& field, PdfPage* page = nullptr);
  /// All terminal and non-terminal fields keyed by fully qualified name.
  std::map<std::string, PdfFormFieldPtr> GetAllFormFields();
  std::map<std::string, PdfFormFieldPtr> GetRootFormFields();
  PdfFormFieldPtr GetField(const std::string& field_name);
  PdfAcroForm& SetNeedAppearances(bool need_appearances);
  PdfAcroForm& SetDefaultAppearance(const std::string& appearance);
  kernel::PdfStringPtr GetDefaultAppearance() const {
    return GetPdfObject()->GetAsString(kernel::PdfName::kDA);
  }
  PdfDictionaryPtr GetDefaultResources() const {
    return GetPdfObject()->GetAsDictionary(kernel::PdfName::kDR);
  }
  PdfAcroForm& SetDefaultResources(const PdfDictionaryPtr& dr) {
    GetPdfObject()->Put(kernel::PdfName::kDR, dr);
    return *this;
  }
  bool IsGenerateAppearance() const { return generate_appearance_; }
  void SetGenerateAppearance(bool v) { generate_appearance_ = v; }
  /// Draws every widget's appearance on its page and removes the fields (port
  /// of flattenFields).
  void FlattenFields();
  /// Marks a field for partial flattening.
  void PartialFormFlattening(const std::string& field_name) {
    fields_for_flattening_.push_back(field_name);
  }
  bool RemoveField(const std::string& field_name);
  void RenameField(const std::string& old_name, const std::string& new_name);
  PdfDocument* GetPdfDocument() const { return document_; }
  /// Font used for generated appearances when /DA does not resolve.
  std::shared_ptr<kernel::PdfFont> GetDefaultFont();
  /// Resolves a font name from /DR /Font.
  std::shared_ptr<kernel::PdfFont> ResolveFont(const kernel::PdfNamePtr& name);
  kernel::PdfNamePtr RegisterFont(const std::shared_ptr<kernel::PdfFont>& font);

 private:
  void CollectFields(const PdfDictionaryPtr& dict,
                     std::map<std::string, PdfFormFieldPtr>& out, int depth);
  kernel::PdfArrayPtr GetFields(bool create);
  PdfDocument* document_;
  bool generate_appearance_ = true;
  std::vector<std::string> fields_for_flattening_;
  std::shared_ptr<kernel::PdfFont> default_font_;
  std::map<const kernel::PdfObject*, std::shared_ptr<kernel::PdfFont>>
      font_cache_;
};

/// Port of TextFormFieldBuilder.
class TextFormFieldBuilder {
 public:
  TextFormFieldBuilder(PdfDocument* document,
                       const std::string& form_field_name)
      : document_(document), name_(form_field_name) {}
  TextFormFieldBuilder& SetWidgetRectangle(const Rectangle& rect) {
    rect_ = rect;
    has_rect_ = true;
    return *this;
  }
  TextFormFieldBuilder& SetPage(PdfPage* page) {
    page_ = page;
    return *this;
  }
  TextFormFieldBuilder& SetFont(std::shared_ptr<kernel::PdfFont> font) {
    font_ = std::move(font);
    return *this;
  }
  PdfFormFieldPtr CreateText();
  PdfFormFieldPtr CreateMultilineText();

 private:
  PdfDocument* document_;
  std::string name_;
  Rectangle rect_;
  bool has_rect_ = false;
  PdfPage* page_ = nullptr;
  std::shared_ptr<kernel::PdfFont> font_;
};

/// Port of CheckBoxFormFieldBuilder.
class CheckBoxFormFieldBuilder {
 public:
  CheckBoxFormFieldBuilder(PdfDocument* document,
                           const std::string& form_field_name)
      : document_(document), name_(form_field_name) {}
  CheckBoxFormFieldBuilder& SetWidgetRectangle(const Rectangle& rect) {
    rect_ = rect;
    return *this;
  }
  PdfFormFieldPtr CreateCheckBox();

 private:
  PdfDocument* document_;
  std::string name_;
  Rectangle rect_;
};

/// Port of ChoiceFormFieldBuilder (combo boxes and list boxes).
class ChoiceFormFieldBuilder {
 public:
  ChoiceFormFieldBuilder(PdfDocument* document,
                         const std::string& form_field_name)
      : document_(document), name_(form_field_name) {}
  ChoiceFormFieldBuilder& SetWidgetRectangle(const Rectangle& rect) {
    rect_ = rect;
    return *this;
  }
  ChoiceFormFieldBuilder& SetOptions(const std::vector<std::string>& options) {
    options_ = options;
    return *this;
  }
  PdfFormFieldPtr CreateComboBox();
  PdfFormFieldPtr CreateList();

 private:
  PdfFormFieldPtr Create(bool combo);
  PdfDocument* document_;
  std::string name_;
  Rectangle rect_;
  std::vector<std::string> options_;
};

}  // namespace forms
}  // namespace pdfcpp

#endif  // PDFCPP_FORMS_FORMS_H_
