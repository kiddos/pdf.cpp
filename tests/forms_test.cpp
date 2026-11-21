#include <filesystem>

#include "TestHarness.h"
#include "pdfcpp/forms/Forms.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

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


/// A non-terminal field "parent" with two text kids "parent.a" / "parent.b" whose widgets are on page 1.
static Bytes createHierarchicalForm() {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        PdfPage* page = pdf.addNewPage();
        auto form = PdfAcroForm::getAcroForm(pdf, true);
        auto parentDict = std::make_shared<PdfDictionary>();
        parentDict->put(PdfName::T, std::make_shared<PdfString>("parent"));
        parentDict->makeIndirect(&pdf);
        auto parent = std::make_shared<PdfFormField>(parentDict, &pdf);
        auto a = TextFormFieldBuilder(&pdf, "a").setWidgetRectangle(Rectangle(100, 700, 200, 20)).createText();
        auto b = TextFormFieldBuilder(&pdf, "b").setWidgetRectangle(Rectangle(100, 650, 200, 20)).createText();
        parent->addKid(a);
        parent->addKid(b);
        form->addField(parent, page);
        page->addAnnotation(a->getPdfObject());
        page->addAnnotation(b->getPdfObject());
        a->setAcroForm(form.get());
        b->setAcroForm(form.get());
        a->setValue("KIDA");
        b->setValue("KIDB");
        pdf.close();
    }
    return baos.toByteArray();
}

static Bytes flattenOnly(const Bytes& in, const std::vector<std::string>& names) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        auto reader = PdfReader::fromBytes(in);
        PdfDocument pdf(*reader, writer);
        auto form = PdfAcroForm::getAcroForm(pdf, false);
        for (const auto& n : names) form->partialFormFlattening(n);
        form->flattenFields();
        pdf.close();
    }
    return baos.toByteArray();
}

// BUGS.md #30: partially flattening a parent flattens its kids' widgets; flattening every kid prunes the parent.
TEST(partialFlatteningOfHierarchicalFields) {
    Bytes form = createHierarchicalForm();
    for (const auto& names : {std::vector<std::string>{"parent"}, std::vector<std::string>{"parent.a", "parent.b"}}) {
        Bytes out = flattenOnly(form, names);
        PdfDocument pdf(PdfReader::fromBytes(out));
        auto annots = pdf.getPage(1)->getPdfObject()->getAsArray(PdfName::Annots);
        EXPECT_TRUE(!annots || annots->isEmpty());
        // the AcroForm (whose /Fields only held "parent") is gone entirely
        EXPECT_TRUE(PdfAcroForm::getAcroForm(pdf, false) == nullptr);
        std::string text = PdfTextExtractor::getTextFromPage(pdf.getPage(1));
        EXPECT_TRUE(text.find("KIDA") != std::string::npos);
        EXPECT_TRUE(text.find("KIDB") != std::string::npos);
        pdf.close();
    }
}

// BUGS.md #30: the appearance's /Matrix is taken into account when flattening.
TEST(flatteningHonoursAppearanceMatrix) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        PdfPage* page = pdf.addNewPage();
        auto form = PdfAcroForm::getAcroForm(pdf, true);
        form->setGenerateAppearance(false);
        auto field = TextFormFieldBuilder(&pdf, "rotated").setWidgetRectangle(Rectangle(100, 700, 200, 20)).createText();
        form->addField(field, page);
        // a 20x200 appearance rotated by 90 degrees covers 200x20, exactly the widget rectangle
        auto xobject = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 20, 200));
        xobject->getPdfObject()->put(PdfName::Matrix, std::make_shared<PdfArray>(std::vector<float>{0, 1, -1, 0, 0, 0}));
        auto ap = std::make_shared<PdfDictionary>();
        ap->put(PdfName::N, xobject->getPdfObject());
        field->getPdfObject()->put(PdfName::AP, ap);
        form->flattenFields();
        pdf.close();
    }
    PdfDocument pdf(PdfReader::fromBytes(baos.toByteArray()));
    Bytes content = pdf.getPage(1)->getContentBytes();
    std::string s(content.begin(), content.end());
    // translate(200, 0) to bring the rotated box to the origin, then place it at (100, 700) without scaling
    EXPECT_TRUE(s.find("1 0 0 1 300 700 cm") != std::string::npos);
    pdf.close();
}

// BUGS.md #31: TextFormFieldBuilder::setPage / setFont are honoured.
TEST(textBuilderPageAndFont) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        pdf.addNewPage();
        PdfPage* second = pdf.addNewPage();
        auto form = PdfAcroForm::getAcroForm(pdf, true);
        auto courier = PdfFontFactory::createFont(io::StandardFonts::COURIER);
        auto field = TextFormFieldBuilder(&pdf, "t")
                         .setWidgetRectangle(Rectangle(100, 700, 200, 20))
                         .setPage(second)
                         .setFont(courier)
                         .createText();
        form->addField(field);
        field->setValue("typed");
        pdf.close();
    }
    PdfDocument pdf(PdfReader::fromBytes(baos.toByteArray()));
    auto annots2 = pdf.getPage(2)->getPdfObject()->getAsArray(PdfName::Annots);
    EXPECT_TRUE(annots2 && annots2->size() == 1);
    auto annots1 = pdf.getPage(1)->getPdfObject()->getAsArray(PdfName::Annots);
    EXPECT_TRUE(!annots1 || annots1->isEmpty());
    auto form = PdfAcroForm::getAcroForm(pdf, false);
    auto field = form->getField("t");
    std::string da = field->getPdfObject()->getAsString(PdfName::DA)->toUnicodeString();
    std::string fontName = da.substr(1, da.find(' ') - 1);
    auto drFonts = form->getPdfObject()->getAsDictionary(PdfName::DR)->getAsDictionary(PdfName::Font);
    auto fontDict = drFonts ? drFonts->getAsDictionary(PdfName::of(fontName)) : nullptr;
    EXPECT_TRUE(fontDict != nullptr);
    if (fontDict) EXPECT_EQ(fontDict->getAsName(PdfName::BaseFont)->getValue(), std::string("Courier"));
    pdf.close();
}

// BUGS.md #32: custom on-states and existing checkbox artwork.
TEST(checkBoxOnStatesAndExistingArtwork) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos);
        PdfDocument pdf(writer);
        PdfPage* page = pdf.addNewPage();
        auto form = PdfAcroForm::getAcroForm(pdf, true);
        auto custom = CheckBoxFormFieldBuilder(&pdf, "custom").setWidgetRectangle(Rectangle(100, 600, 15, 15)).createCheckBox();
        form->addField(custom, page);
        custom->setCheckBoxAppearanceOnStateName("CustomOn");
        custom->setValue("CustomOn");
        EXPECT_EQ(custom->getPdfObject()->getAsName(PdfName::AS)->getValue(), std::string("CustomOn"));
        // a checkbox whose appearances are hand-made artwork
        auto art = CheckBoxFormFieldBuilder(&pdf, "art").setWidgetRectangle(Rectangle(100, 500, 15, 15)).createCheckBox();
        form->addField(art, page);
        auto makeArt = [&](const std::string& marker) {
            auto x = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 15, 15));
            x->getPdfObject()->getOutputStream()->writeString("% " + marker + "\n0 0 15 15 re f\n");
            return x->getPdfObject();
        };
        auto n = std::make_shared<PdfDictionary>();
        n->put(PdfName::of("Yes"), makeArt("custom-on"));
        n->put(PdfName::of("Off"), makeArt("custom-off"));
        auto ap = std::make_shared<PdfDictionary>();
        ap->put(PdfName::N, n);
        art->getPdfObject()->put(PdfName::AP, ap);
        pdf.close();
    }
    io::ByteArrayOutputStream filled;
    {
        PdfWriter writer(filled);
        auto reader = PdfReader::fromBytes(baos.toByteArray());
        PdfDocument pdf(*reader, writer);
        auto form = PdfAcroForm::getAcroForm(pdf, false);
        form->getField("art")->setValue("Yes");
        pdf.close();
    }
    PdfDocument pdf(PdfReader::fromBytes(filled.toByteArray()));
    auto art = PdfAcroForm::getAcroForm(pdf, false)->getField("art");
    EXPECT_EQ(art->getPdfObject()->getAsName(PdfName::AS)->getValue(), std::string("Yes"));
    auto on = art->getPdfObject()->getAsDictionary(PdfName::AP)->getAsDictionary(PdfName::N)->getAsStream(PdfName::of("Yes"));
    Bytes onBytes = on->getBytes();
    EXPECT_TRUE(std::string(onBytes.begin(), onBytes.end()).find("custom-on") != std::string::npos);
    pdf.close();
}

TEST_MAIN()
