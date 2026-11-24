// Demonstrates low-level PdfCanvas vector graphics, colors/opacity,
// FormXObjects, annotations, and page footers.
#include <iostream>

#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/navigation.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::layout;

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "canvas_graphics.pdf";
  PdfDocument pdf(std::make_unique<PdfWriter>(dest));
  Document document(pdf, PageSize::kA4, /*immediateFlush=*/false);

  auto bold = PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);
  document.Add(Make<Paragraph>("Canvas Graphics & Annotations")
                   ->SetFont(bold)
                   .SetFontSize(20)
                   .SetMarginBottom(8));

  // Hyperlink inside a Paragraph.
  auto link_para = Make<Paragraph>("Visit ");
  link_para->Add(
      Make<Link>("the iText repository", "https://github.com/itext/itext-java")
          ->SetFontColor(color_constants::kBlue)
          .SetUnderline());
  link_para->Add(" for the Java reference.");
  document.Add(link_para);

  // Draw vector shapes directly on page 1 using PdfCanvas.
  PdfPage* page = pdf.GetPage(1);
  PdfCanvas canvas(page);

  // Reusable vector badge as a PdfFormXObject.
  auto badge = std::make_shared<PdfFormXObject>(Rectangle(0, 0, 80, 80));
  PdfCanvas badge_canvas(*badge, &pdf);
  badge_canvas
      .SetFillColor(std::make_shared<DeviceCmyk>(0.85f, 0.25f, 0.0f, 0.0f))
      .Circle(40, 40, 36)
      .Fill()
      .SetStrokeColor(color_constants::kWhite)
      .SetLineWidth(3)
      .MoveTo(22, 40)
      .LineTo(35, 27)
      .LineTo(58, 52)
      .Stroke();

  // Place the badge twice at different scales/positions.
  canvas.AddXObjectAt(*badge, 60, 560);
  canvas.SaveState()
      .ConcatMatrix(0.6, 0, 0, 0.6, 160, 576)
      .AddXObject(*badge)
      .RestoreState();

  // Dashed Bezier curve + translucent overlapping rectangles.
  canvas.SaveState()
      .SetStrokeColor(color_constants::kDarkGray)
      .SetLineWidth(2)
      .SetLineDash({6, 4}, 0)
      .MoveTo(260, 580)
      .CurveTo(320, 660, 420, 520, 500, 620)
      .Stroke()
      .RestoreState();

  PdfExtGState translucent;
  translucent.SetFillOpacity(0.55f);
  canvas.SaveState().SetExtGState(translucent);
  canvas.SetFillColor(std::make_shared<DeviceRgb>(230, 80, 60))
      .Rectangle(60, 440, 110, 70)
      .Fill();
  canvas.SetFillColor(std::make_shared<DeviceRgb>(50, 140, 220))
      .Rectangle(120, 415, 110, 70)
      .Fill();
  canvas.RestoreState();

  // Add a sticky-note annotation and a square annotation on page 1.
  auto note = PdfAnnotation::CreateText(Rectangle(260, 470, 24, 24));
  note->SetTitle("Reviewer")
      .SetContents("Vector shapes drawn with PdfCanvas.")
      .SetColor(color_constants::kYellow);
  page->AddAnnotation(note->GetPdfObject());

  auto box = PdfAnnotation::CreateSquare(Rectangle(55, 410, 180, 105));
  box->SetColor(color_constants::kGray).SetBorder(0, 0, 1);
  page->AddAnnotation(box->GetPdfObject());

  // Add a second page and stamp "Page X of Y" footers across all pages
  // (possible because immediate_flush=false).
  document.Add(Make<AreaBreak>());
  document.Add(Make<Paragraph>("Second Page")->SetFont(bold).SetFontSize(16));
  document.Add(
      Make<Paragraph>("Both pages share a dynamically numbered footer."));

  int total_pages = pdf.GetNumberOfPages();
  for (int i = 1; i <= total_pages; ++i) {
    Rectangle ps = pdf.GetPage(i)->GetPageSize();
    auto footer = std::make_shared<Paragraph>(
        "Page " + std::to_string(i) + " of " + std::to_string(total_pages));
    footer->SetFontSize(9).SetFontColor(color_constants::kGray);
    document.ShowTextAligned(footer, ps.GetWidth() / 2, 24, i,
                             TextAlignment::kCenter, VerticalAlignment::kBottom,
                             0);
  }

  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
