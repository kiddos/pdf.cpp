// Generates 1D and 2D barcodes (QR Code, Code 128, EAN-13, Code 39) inside a
// layout table.
#include "pdfcpp/barcodes/barcodes.h"

#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::barcodes;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "barcodes.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  Document document(pdf, kernel::PageSize::kA4);

  auto bold =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  document.Add(Make<Paragraph>("Barcode Gallery")
                   ->SetFont(bold)
                   .SetFontSize(20)
                   .SetMarginBottom(12));

  auto table = Make<Table>(UnitValue::CreatePercentArray({2, 3}));
  table->UseAllAvailableWidth();
  table->AddHeaderCell(
      Make<Cell>()->Add(Make<Paragraph>("Type")->SetFont(bold)));
  table->AddHeaderCell(
      Make<Cell>()->Add(Make<Paragraph>("Barcode")->SetFont(bold)));

  // QR Code (Error Correction Level M)
  BarcodeQRCode qr("https://github.com/itext/itext-java",
                   ErrorCorrectionLevel::kM);
  auto qr_img = Make<Image>(
      qr.CreateFormXObject(&pdf, kernel::color_constants::kBlack, 3.0f));
  table->AddCell("QR Code (EC-M)");
  table->AddCell(Make<Cell>()->Add(qr_img).SetPadding(8));

  // Code 128 (auto code-set switching)
  Barcode128 code128(&pdf);
  code128.SetCode("PDFCPP-2026-001");
  code128.SetBarHeight(36);
  auto c128_img = Make<Image>(code128.CreateFormXObject());
  table->AddCell("Code 128");
  table->AddCell(Make<Cell>()->Add(c128_img).SetPadding(8));

  // EAN-13
  BarcodeEAN ean13(&pdf);
  ean13.SetCodeType(BarcodeEAN::kEan13);
  ean13.SetCode("5901234123457");
  ean13.SetBarHeight(36);
  auto ean_img = Make<Image>(ean13.CreateFormXObject());
  table->AddCell("EAN-13");
  table->AddCell(Make<Cell>()->Add(ean_img).SetPadding(8));

  // Code 39 with checksum
  Barcode39 code39(&pdf);
  code39.SetCode("ABC12345");
  code39.SetGenerateChecksum(true);
  code39.SetChecksumText(true);
  code39.SetBarHeight(36);
  auto c39_img = Make<Image>(code39.CreateFormXObject(
      kernel::color_constants::kDarkGray, kernel::color_constants::kBlack));
  table->AddCell("Code 39 (checksum)");
  table->AddCell(Make<Cell>()->Add(c39_img).SetPadding(8));

  document.Add(table);
  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
