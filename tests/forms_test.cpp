#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/forms/Forms.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::forms;
namespace fs = std::filesystem;

static Bytes createForm() {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        PdfPage* page = pdf.addNewPage();
        auto form = PdfAcroForm::getAcroForm(pdf, true);
        auto name = TextFormFieldBuilder(&pdf, "name").setWidgetRectangle(Rectangle(100, 700, 200, 20)).createText();
        name->setValue("John Doe");
        form->addField(name, page);
        auto notes = TextFormFieldBuilder(&pdf, "notes").setWidgetRectangle(Rectangle(100, 550, 200, 100)).createMultilineText();
        form->addField(notes, page);
        notes->setValue("Line one of the notes field that is long enough to wrap\nSecond line");
        auto agree = CheckBoxFormFieldBuilder(&pdf, "agree").setWidgetRectangle(Rectangle(100, 500, 15, 15)).createCheckBox();
        form->addField(agree, page);
        auto color = ChoiceFormFieldBuilder(&pdf, "color").setWidgetRectangle(Rectangle(100, 450, 100, 20)).setOptions({"Red", "Green"}).createComboBox();
        form->addField(color, page);
        pdf.close();
    }
    return baos.toByteArray();
}

TEST(createAndReadForm) {
    Bytes bytes = createForm();
    PdfDocument pdf(PdfReader::fromBytes(bytes));
    auto form = PdfAcroForm::getAcroForm(pdf, false);
    EXPECT_TRUE(form != nullptr);
    auto fields = form->getAllFormFields();
    EXPECT_EQ(static_cast<int>(fields.size()), 4);
    EXPECT_EQ(fields["name"]->getValueAsString(), std::string("John Doe"));
    EXPECT_EQ(fields["agree"]->getValueAsString(), std::string("Off"));
    EXPECT_EQ(fields["color"]->getOptions().size(), size_t(2));
}

TEST(fillAndFlatten) {
    Bytes bytes = createForm();
    std::string out = "form_flattened.pdf";
    {
        PdfDocument pdf(PdfReader::fromBytes(bytes), std::make_unique<PdfWriter>(out));
        auto form = PdfAcroForm::getAcroForm(pdf, false);
        auto fields = form->getAllFormFields();
        fields["name"]->setValue("Jane Roe");
        fields["agree"]->setValue("Yes");
        fields["color"]->setValue("Green");
        form->flattenFields();
        pdf.close();
    }
    PdfDocument pdf(std::make_unique<PdfReader>(out));
    EXPECT_TRUE(PdfAcroForm::getAcroForm(pdf, false) == nullptr);
    EXPECT_EQ(pdf.getPage(1)->getAnnotsSize(), 0);
    std::string text = PdfTextExtractor::getTextFromPage(pdf.getPage(1));
    EXPECT_TRUE(text.find("Jane Roe") != std::string::npos);
    EXPECT_TRUE(text.find("Second line") != std::string::npos);
    EXPECT_TRUE(text.find("Green") != std::string::npos);
}

TEST(fillJavaFormResources) {
    fs::path dir = fs::path(pdfcpp_test::referenceDir()) / "forms/src/test/resources/com/itextpdf/forms/FormFieldFlatteningTest";
    if (!fs::exists(dir)) return;
    int ok = 0, total = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".pdf") continue;
        ++total;
        try {
            io::ByteArrayOutputStream baos;
            PdfDocument pdf(std::make_unique<PdfReader>(entry.path().string()), std::make_unique<PdfWriter>(baos));
            auto form = PdfAcroForm::getAcroForm(pdf, false);
            if (form) {
                for (auto& kv : form->getAllFormFields())
                    if (nameEquals(kv.second->getFormType(), PdfName::Tx)) kv.second->setValue("pdfcpp");
                form->flattenFields();
            }
            pdf.close();
            ++ok;
        } catch (const std::exception& e) {
            std::cout << "    " << entry.path().filename() << ": " << e.what() << "\n";
        }
    }
    std::cout << "    filled+flattened " << ok << "/" << total << "\n";
    EXPECT_EQ(ok, total);
}

TEST_MAIN()
