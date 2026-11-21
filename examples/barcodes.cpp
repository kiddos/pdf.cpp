// Generates 1D and 2D barcodes (QR Code, Code 128, EAN-13, Code 39) inside a layout table.
#include <iostream>

#include "pdfcpp/barcodes/Barcodes.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::barcodes;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
    std::string dest = argc > 1 ? argv[1] : "barcodes.pdf";
    kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
    Document document(pdf, kernel::PageSize::A4);

    auto bold = kernel::PdfFontFactory::createFont(io::StandardFonts::HELVETICA_BOLD);
    document.add(make<Paragraph>("Barcode Gallery")->setFont(bold).setFontSize(20).setMarginBottom(12));

    auto table = make<Table>(UnitValue::createPercentArray({2, 3}));
    table->useAllAvailableWidth();
    table->addHeaderCell(make<Cell>()->add(make<Paragraph>("Type")->setFont(bold)));
    table->addHeaderCell(make<Cell>()->add(make<Paragraph>("Barcode")->setFont(bold)));

    // QR Code (Error Correction Level M)
    BarcodeQRCode qr("https://github.com/itext/itext-java", ErrorCorrectionLevel::M);
    auto qrImg = make<Image>(qr.createFormXObject(&pdf, kernel::ColorConstants::BLACK, 3.0f));
    table->addCell("QR Code (EC-M)");
    table->addCell(make<Cell>()->add(qrImg).setPadding(8));

    // Code 128 (auto code-set switching)
    Barcode128 code128(&pdf);
    code128.setCode("PDFCPP-2026-001");
    code128.setBarHeight(36);
    auto c128Img = make<Image>(code128.createFormXObject());
    table->addCell("Code 128");
    table->addCell(make<Cell>()->add(c128Img).setPadding(8));

    // EAN-13
    BarcodeEAN ean13(&pdf);
    ean13.setCodeType(BarcodeEAN::EAN13);
    ean13.setCode("5901234123457");
    ean13.setBarHeight(36);
    auto eanImg = make<Image>(ean13.createFormXObject());
    table->addCell("EAN-13");
    table->addCell(make<Cell>()->add(eanImg).setPadding(8));

    // Code 39 with checksum
    Barcode39 code39(&pdf);
    code39.setCode("ABC12345");
    code39.setGenerateChecksum(true);
    code39.setChecksumText(true);
    code39.setBarHeight(36);
    auto c39Img = make<Image>(code39.createFormXObject(kernel::ColorConstants::DARK_GRAY, kernel::ColorConstants::BLACK));
    table->addCell("Code 39 (checksum)");
    table->addCell(make<Cell>()->add(c39Img).setPadding(8));

    document.add(table);
    document.close();
    std::cout << "Wrote " << dest << "\n";
    return 0;
}
