#include "pdfcpp/forms/forms.h"

#include <filesystem>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::forms;
namespace fs = std::filesystem;

static Bytes CreateForm() {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    PdfPage* page = pdf.AddNewPage();
    auto form = PdfAcroForm::GetAcroForm(pdf, true);
    auto name = TextFormFieldBuilder(&pdf, "name")
                    .SetWidgetRectangle(Rectangle(100, 700, 200, 20))
                    .CreateText();
    name->SetValue("John Doe");
    form->AddField(name, page);
    auto notes = TextFormFieldBuilder(&pdf, "notes")
                     .SetWidgetRectangle(Rectangle(100, 550, 200, 100))
                     .CreateMultilineText();
    form->AddField(notes, page);
    notes->SetValue(
        "Line one of the notes field that is long enough to wrap\nSecond line");
    auto agree = CheckBoxFormFieldBuilder(&pdf, "agree")
                     .SetWidgetRectangle(Rectangle(100, 500, 15, 15))
                     .CreateCheckBox();
    form->AddField(agree, page);
    auto color = ChoiceFormFieldBuilder(&pdf, "color")
                     .SetWidgetRectangle(Rectangle(100, 450, 100, 20))
                     .SetOptions({"Red", "Green"})
                     .CreateComboBox();
    form->AddField(color, page);
    pdf.Close();
  }
  return baos.ToByteArray();
}

TEST(CreateAndReadForm) {
  Bytes bytes = CreateForm();
  PdfDocument pdf(PdfReader::FromBytes(bytes));
  auto form = PdfAcroForm::GetAcroForm(pdf, false);
  EXPECT_TRUE(form != nullptr);
  auto fields = form->GetAllFormFields();
  EXPECT_EQ(static_cast<int>(fields.size()), 4);
  EXPECT_EQ(fields["name"]->GetValueAsString(), std::string("John Doe"));
  EXPECT_EQ(fields["agree"]->GetValueAsString(), std::string("Off"));
  EXPECT_EQ(fields["color"]->GetOptions().size(), size_t(2));
}

TEST(FillAndFlatten) {
  Bytes bytes = CreateForm();
  std::string out = "form_flattened.pdf";
  {
    PdfDocument pdf(PdfReader::FromBytes(bytes),
                    std::make_unique<PdfWriter>(out));
    auto form = PdfAcroForm::GetAcroForm(pdf, false);
    auto fields = form->GetAllFormFields();
    fields["name"]->SetValue("Jane Roe");
    fields["agree"]->SetValue("Yes");
    fields["color"]->SetValue("Green");
    form->FlattenFields();
    pdf.Close();
  }
  PdfDocument pdf(std::make_unique<PdfReader>(out));
  EXPECT_TRUE(PdfAcroForm::GetAcroForm(pdf, false) == nullptr);
  EXPECT_EQ(pdf.GetPage(1)->GetAnnotsSize(), 0);
  std::string text = PdfTextExtractor::GetTextFromPage(pdf.GetPage(1));
  EXPECT_TRUE(text.find("Jane Roe") != std::string::npos);
  EXPECT_TRUE(text.find("Second line") != std::string::npos);
  EXPECT_TRUE(text.find("Green") != std::string::npos);
}

TEST(FillJavaFormResources) {
  fs::path dir =
      fs::path(pdfcpp_test::ReferenceDir()) /
      "forms/src/test/resources/com/itextpdf/forms/FormFieldFlatteningTest";
  if (!fs::exists(dir)) return;
  int ok = 0, total = 0;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".pdf") continue;
    ++total;
    try {
      io::ByteArrayOutputStream baos;
      PdfDocument pdf(std::make_unique<PdfReader>(entry.path().string()),
                      std::make_unique<PdfWriter>(baos));
      auto form = PdfAcroForm::GetAcroForm(pdf, false);
      if (form) {
        for (auto& kv : form->GetAllFormFields())
          if (NameEquals(kv.second->GetFormType(), PdfName::kTx))
            kv.second->SetValue("pdfcpp");
        form->FlattenFields();
      }
      pdf.Close();
      ++ok;
    } catch (const std::exception& e) {
      std::cout << "    " << entry.path().filename() << ": " << e.what()
                << "\n";
    }
  }
  std::cout << "    filled+flattened " << ok << "/" << total << "\n";
  EXPECT_EQ(ok, total);
}

/// A non-terminal field "parent" with two text kids "parent.a" / "parent.b"
/// whose widgets are on page 1.
static Bytes CreateHierarchicalForm() {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    PdfPage* page = pdf.AddNewPage();
    auto form = PdfAcroForm::GetAcroForm(pdf, true);
    auto parent_dict = std::make_shared<PdfDictionary>();
    parent_dict->Put(PdfName::kT, std::make_shared<PdfString>("parent"));
    parent_dict->MakeIndirect(&pdf);
    auto parent = std::make_shared<PdfFormField>(parent_dict, &pdf);
    auto a = TextFormFieldBuilder(&pdf, "a")
                 .SetWidgetRectangle(Rectangle(100, 700, 200, 20))
                 .CreateText();
    auto b = TextFormFieldBuilder(&pdf, "b")
                 .SetWidgetRectangle(Rectangle(100, 650, 200, 20))
                 .CreateText();
    parent->AddKid(a);
    parent->AddKid(b);
    form->AddField(parent, page);
    page->AddAnnotation(a->GetPdfObject());
    page->AddAnnotation(b->GetPdfObject());
    a->SetAcroForm(form.get());
    b->SetAcroForm(form.get());
    a->SetValue("KIDA");
    b->SetValue("KIDB");
    pdf.Close();
  }
  return baos.ToByteArray();
}

static Bytes FlattenOnly(const Bytes& in,
                         const std::vector<std::string>& names) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    auto reader = PdfReader::FromBytes(in);
    PdfDocument pdf(*reader, writer);
    auto form = PdfAcroForm::GetAcroForm(pdf, false);
    for (const auto& n : names) form->PartialFormFlattening(n);
    form->FlattenFields();
    pdf.Close();
  }
  return baos.ToByteArray();
}

// BUGS.md #30: partially flattening a parent flattens its kids' widgets;
// flattening every kid prunes the parent.
TEST(PartialFlatteningOfHierarchicalFields) {
  Bytes form = CreateHierarchicalForm();
  for (const auto& names : {std::vector<std::string>{"parent"},
                            std::vector<std::string>{"parent.a", "parent.b"}}) {
    Bytes out = FlattenOnly(form, names);
    PdfDocument pdf(PdfReader::FromBytes(out));
    auto annots = pdf.GetPage(1)->GetPdfObject()->GetAsArray(PdfName::kAnnots);
    EXPECT_TRUE(!annots || annots->IsEmpty());
    // the AcroForm (whose /Fields only held "parent") is gone entirely
    EXPECT_TRUE(PdfAcroForm::GetAcroForm(pdf, false) == nullptr);
    std::string text = PdfTextExtractor::GetTextFromPage(pdf.GetPage(1));
    EXPECT_TRUE(text.find("KIDA") != std::string::npos);
    EXPECT_TRUE(text.find("KIDB") != std::string::npos);
    pdf.Close();
  }
}

// BUGS.md #30: the appearance's /Matrix is taken into account when flattening.
TEST(FlatteningHonoursAppearanceMatrix) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    PdfPage* page = pdf.AddNewPage();
    auto form = PdfAcroForm::GetAcroForm(pdf, true);
    form->SetGenerateAppearance(false);
    auto field = TextFormFieldBuilder(&pdf, "rotated")
                     .SetWidgetRectangle(Rectangle(100, 700, 200, 20))
                     .CreateText();
    form->AddField(field, page);
    // a 20x200 appearance rotated by 90 degrees covers 200x20, exactly the
    // widget rectangle
    auto xobject = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 20, 200));
    xobject->GetPdfObject()->Put(
        PdfName::kMatrix,
        std::make_shared<PdfArray>(std::vector<float>{0, 1, -1, 0, 0, 0}));
    auto ap = std::make_shared<PdfDictionary>();
    ap->Put(PdfName::kN, xobject->GetPdfObject());
    field->GetPdfObject()->Put(PdfName::kAP, ap);
    form->FlattenFields();
    pdf.Close();
  }
  PdfDocument pdf(PdfReader::FromBytes(baos.ToByteArray()));
  Bytes content = pdf.GetPage(1)->GetContentBytes();
  std::string s(content.begin(), content.end());
  // translate(200, 0) to bring the rotated box to the origin, then place it at
  // (100, 700) without scaling
  EXPECT_TRUE(s.find("1 0 0 1 300 700 cm") != std::string::npos);
  pdf.Close();
}

// BUGS.md #31: TextFormFieldBuilder::SetPage / SetFont are honoured.
TEST(TextBuilderPageAndFont) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    pdf.AddNewPage();
    PdfPage* second = pdf.AddNewPage();
    auto form = PdfAcroForm::GetAcroForm(pdf, true);
    auto courier = PdfFontFactory::CreateFont(io::standard_fonts::kCourier);
    auto field = TextFormFieldBuilder(&pdf, "t")
                     .SetWidgetRectangle(Rectangle(100, 700, 200, 20))
                     .SetPage(second)
                     .SetFont(courier)
                     .CreateText();
    form->AddField(field);
    field->SetValue("typed");
    pdf.Close();
  }
  PdfDocument pdf(PdfReader::FromBytes(baos.ToByteArray()));
  auto annots2 = pdf.GetPage(2)->GetPdfObject()->GetAsArray(PdfName::kAnnots);
  EXPECT_TRUE(annots2 && annots2->size() == 1);
  auto annots1 = pdf.GetPage(1)->GetPdfObject()->GetAsArray(PdfName::kAnnots);
  EXPECT_TRUE(!annots1 || annots1->IsEmpty());
  auto form = PdfAcroForm::GetAcroForm(pdf, false);
  auto field = form->GetField("t");
  std::string da =
      field->GetPdfObject()->GetAsString(PdfName::kDA)->ToUnicodeString();
  std::string font_name = da.substr(1, da.find(' ') - 1);
  auto dr_fonts = form->GetPdfObject()
                      ->GetAsDictionary(PdfName::kDR)
                      ->GetAsDictionary(PdfName::kFont);
  auto font_dict =
      dr_fonts ? dr_fonts->GetAsDictionary(PdfName::Of(font_name)) : nullptr;
  EXPECT_TRUE(font_dict != nullptr);
  if (font_dict)
    EXPECT_EQ(font_dict->GetAsName(PdfName::kBaseFont)->GetValue(),
              std::string("Courier"));
  pdf.Close();
}

// BUGS.md #32: custom on-states and existing checkbox artwork.
TEST(CheckBoxOnStatesAndExistingArtwork) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(baos);
    PdfDocument pdf(writer);
    PdfPage* page = pdf.AddNewPage();
    auto form = PdfAcroForm::GetAcroForm(pdf, true);
    auto custom = CheckBoxFormFieldBuilder(&pdf, "custom")
                      .SetWidgetRectangle(Rectangle(100, 600, 15, 15))
                      .CreateCheckBox();
    form->AddField(custom, page);
    custom->SetCheckBoxAppearanceOnStateName("CustomOn");
    custom->SetValue("CustomOn");
    EXPECT_EQ(custom->GetPdfObject()->GetAsName(PdfName::kAS)->GetValue(),
              std::string("CustomOn"));
    // a checkbox whose appearances are hand-made artwork
    auto art = CheckBoxFormFieldBuilder(&pdf, "art")
                   .SetWidgetRectangle(Rectangle(100, 500, 15, 15))
                   .CreateCheckBox();
    form->AddField(art, page);
    auto make_art = [&](const std::string& marker) {
      auto x = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 15, 15));
      x->GetPdfObject()->GetOutputStream()->WriteString("% " + marker +
                                                        "\n0 0 15 15 re f\n");
      return x->GetPdfObject();
    };
    auto n = std::make_shared<PdfDictionary>();
    n->Put(PdfName::Of("Yes"), make_art("custom-on"));
    n->Put(PdfName::Of("Off"), make_art("custom-off"));
    auto ap = std::make_shared<PdfDictionary>();
    ap->Put(PdfName::kN, n);
    art->GetPdfObject()->Put(PdfName::kAP, ap);
    pdf.Close();
  }
  io::ByteArrayOutputStream filled;
  {
    PdfWriter writer(filled);
    auto reader = PdfReader::FromBytes(baos.ToByteArray());
    PdfDocument pdf(*reader, writer);
    auto form = PdfAcroForm::GetAcroForm(pdf, false);
    form->GetField("art")->SetValue("Yes");
    pdf.Close();
  }
  PdfDocument pdf(PdfReader::FromBytes(filled.ToByteArray()));
  auto art = PdfAcroForm::GetAcroForm(pdf, false)->GetField("art");
  EXPECT_EQ(art->GetPdfObject()->GetAsName(PdfName::kAS)->GetValue(),
            std::string("Yes"));
  auto on = art->GetPdfObject()
                ->GetAsDictionary(PdfName::kAP)
                ->GetAsDictionary(PdfName::kN)
                ->GetAsStream(PdfName::Of("Yes"));
  Bytes on_bytes = on->GetBytes();
  EXPECT_TRUE(std::string(on_bytes.begin(), on_bytes.end()).find("custom-on") !=
              std::string::npos);
  pdf.Close();
}

TEST_MAIN()
