// Creates an interactive AcroForm (text, multiline, checkbox, combo box) and
// optionally flattens it.
#include "pdfcpp/forms/forms.h"

#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::forms;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
  std::string dest = "form.pdf";
  bool flatten = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--flatten") {
      flatten = true;
    } else {
      dest = arg;
    }
  }

  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  kernel::PdfPage* page = pdf.AddNewPage(kernel::PageSize::kA4);

  // Draw labels using layout Canvas on the page.
  auto bold =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  auto regular =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelvetica);
  Canvas canvas(page, page->GetPageSize());
  canvas.SetFont(bold).SetFontSize(18);
  canvas.ShowTextAligned("Registration Form", 50, 780, TextAlignment::kLeft);
  canvas.SetFont(regular).SetFontSize(11);
  canvas.ShowTextAligned("Full Name:", 50, 725, TextAlignment::kLeft);
  canvas.ShowTextAligned("Country:", 50, 685, TextAlignment::kLeft);
  canvas.ShowTextAligned("Subscribe to newsletter:", 50, 645,
                         TextAlignment::kLeft);
  canvas.ShowTextAligned("Notes:", 50, 600, TextAlignment::kLeft);
  canvas.Close();

  auto form = PdfAcroForm::GetAcroForm(pdf, true);

  auto name_field =
      TextFormFieldBuilder(&pdf, "fullName")
          .SetWidgetRectangle(kernel::Rectangle(150, 718, 260, 22))
          .CreateText();
  name_field->SetBorderColor(kernel::color_constants::kGray)
      .SetBorderWidth(1)
      .SetBackgroundColor(std::make_shared<kernel::DeviceRgb>(245, 248, 255))
      .SetValue("Ada Lovelace");
  form->AddField(name_field, page);

  auto country_field =
      ChoiceFormFieldBuilder(&pdf, "country")
          .SetWidgetRectangle(kernel::Rectangle(150, 678, 200, 22))
          .SetOptions(
              {"United Kingdom", "United States", "Germany", "Japan", "Other"})
          .CreateComboBox();
  country_field->SetBorderColor(kernel::color_constants::kGray)
      .SetBorderWidth(1)
      .SetValue("United Kingdom");
  form->AddField(country_field, page);

  auto subscribe_field =
      CheckBoxFormFieldBuilder(&pdf, "newsletter")
          .SetWidgetRectangle(kernel::Rectangle(200, 642, 16, 16))
          .CreateCheckBox();
  subscribe_field->SetBorderColor(kernel::color_constants::kBlack)
      .SetBorderWidth(1)
      .SetValue("Yes");
  form->AddField(subscribe_field, page);

  auto notes_field =
      TextFormFieldBuilder(&pdf, "notes")
          .SetWidgetRectangle(kernel::Rectangle(150, 520, 260, 90))
          .CreateMultilineText();
  notes_field->SetBorderColor(kernel::color_constants::kGray)
      .SetBorderWidth(1)
      .SetValue("Interested in C++ PDF generation\nand digital signatures.");
  form->AddField(notes_field, page);

  if (flatten) form->FlattenFields();
  pdf.Close();
  std::cout << "Wrote " << (flatten ? "flattened " : "interactive ") << dest
            << "\n";
  return 0;
}
