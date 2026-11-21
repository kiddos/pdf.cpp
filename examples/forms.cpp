// Creates an interactive AcroForm (text, multiline, checkbox, combo box) and optionally flattens it.
#include <iostream>

#include "pdfcpp/forms/Forms.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"

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
    kernel::PdfPage* page = pdf.addNewPage(kernel::PageSize::A4);

    // Draw labels using layout Canvas on the page.
    auto bold = kernel::PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    auto regular = kernel::PdfFontFactory::createFont(io::StandardFonts::HELVETICA);
    Canvas canvas(page, page->getPageSize());
    canvas.setFont(bold).setFontSize(18);
    canvas.showTextAligned("Registration Form", 50, 780, TextAlignment::LEFT);
    canvas.setFont(regular).setFontSize(11);
    canvas.showTextAligned("Full Name:", 50, 725, TextAlignment::LEFT);
    canvas.showTextAligned("Country:", 50, 685, TextAlignment::LEFT);
    canvas.showTextAligned("Subscribe to newsletter:", 50, 645, TextAlignment::LEFT);
    canvas.showTextAligned("Notes:", 50, 600, TextAlignment::LEFT);
    canvas.close();

    auto form = PdfAcroForm::getAcroForm(pdf, true);

    auto nameField = TextFormFieldBuilder(&pdf, "fullName")
                         .setWidgetRectangle(kernel::Rectangle(150, 718, 260, 22))
                         .createText();
    nameField->setBorderColor(kernel::ColorConstants::GRAY)
        .setBorderWidth(1)
        .setBackgroundColor(std::make_shared<kernel::DeviceRgb>(245, 248, 255))
        .setValue("Ada Lovelace");
    form->addField(nameField, page);

    auto countryField = ChoiceFormFieldBuilder(&pdf, "country")
                            .setWidgetRectangle(kernel::Rectangle(150, 678, 200, 22))
                            .setOptions({"United Kingdom", "United States", "Germany", "Japan", "Other"})
                            .createComboBox();
    countryField->setBorderColor(kernel::ColorConstants::GRAY).setBorderWidth(1).setValue("United Kingdom");
    form->addField(countryField, page);

    auto subscribeField = CheckBoxFormFieldBuilder(&pdf, "newsletter")
                              .setWidgetRectangle(kernel::Rectangle(200, 642, 16, 16))
                              .createCheckBox();
    subscribeField->setBorderColor(kernel::ColorConstants::BLACK).setBorderWidth(1).setValue("Yes");
    form->addField(subscribeField, page);

    auto notesField = TextFormFieldBuilder(&pdf, "notes")
                          .setWidgetRectangle(kernel::Rectangle(150, 520, 260, 90))
                          .createMultilineText();
    notesField->setBorderColor(kernel::ColorConstants::GRAY)
        .setBorderWidth(1)
        .setValue("Interested in C++ PDF generation\nand digital signatures.");
    form->addField(notesField, page);

    if (flatten) form->flattenFields();
    pdf.close();
    std::cout << "Wrote " << (flatten ? "flattened " : "interactive ") << dest << "\n";
    return 0;
}
