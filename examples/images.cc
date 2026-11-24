// Places images with the layout API and with a raw PdfCanvas: a generated RGB
// gradient, the same gradient with a soft mask (alpha channel), and any
// JPEG/PNG files passed on the command line.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "pdfcpp/io/image/image_data.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

namespace {

constexpr int kSize = 256;

// A kSize x kSize RGB image: red grows to the right, blue grows upwards.
std::shared_ptr<io::ImageData> MakeGradient() {
  Bytes rgb;
  rgb.reserve(kSize * kSize * 3);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      rgb.push_back(static_cast<uint8_t>(x));
      rgb.push_back(static_cast<uint8_t>(128));
      rgb.push_back(static_cast<uint8_t>(255 - y));
    }
  }
  return io::ImageDataFactory::Create(kSize, kSize, /*components=*/3,
                                      /*bpc=*/8, rgb);
}

// An 8-bit grayscale soft mask: an opaque disc whose edge fades out.
std::shared_ptr<io::ImageData> MakeRadialMask() {
  Bytes alpha;
  alpha.reserve(kSize * kSize);
  const double center = (kSize - 1) / 2.0;
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      double d = std::hypot(x - center, y - center) / center;
      double opacity = std::clamp((1.0 - d) / 0.3, 0.0, 1.0);
      alpha.push_back(static_cast<uint8_t>(255 * opacity));
    }
  }
  auto mask = io::ImageDataFactory::Create(kSize, kSize, 1, 8, alpha);
  mask->MakeMask();
  return mask;
}

}  // namespace

int main(int argc, char** argv) {
  std::string dest = argc > 1 ? argv[1] : "images.pdf";
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>(dest));
  Document document(pdf, kernel::PageSize::kA4, /*immediate_flush=*/false);
  auto bold =
      kernel::PdfFontFactory::CreateFont(io::standard_fonts::kHelveticaBold);

  document.Add(Make<Paragraph>("Images")->SetFont(bold).SetFontSize(20));

  // 1. Layout images: natural size, scaled, rotated and translucent, side by
  //    side in a table.
  auto gradient = MakeGradient();
  auto table = Make<Table>(3);
  table->UseAllAvailableWidth();
  for (const char* caption :
       {"ScaleToFit(150, 150)", "rotated 30 degrees", "opacity 0.4"})
    table->AddHeaderCell(caption);
  table->AddCell(
      Make<Cell>()->Add(Make<Image>(gradient)->ScaleToFit(150, 150)));
  table->AddCell(Make<Cell>()->Add(
      Make<Image>(gradient)->ScaleToFit(110, 110).SetRotationAngle(M_PI / 6)));
  table->AddCell(Make<Cell>()->Add(
      Make<Image>(gradient)->ScaleToFit(150, 150).SetOpacity(0.4f)));
  document.Add(table);

  // 2. Soft mask: the mask image becomes the /SMask of the RGB image.
  auto masked = MakeGradient();
  masked->SetImageMask(MakeRadialMask());
  document.Add(Make<Paragraph>("Soft mask (alpha channel) over a background:")
                   ->SetMarginTop(16));
  auto backdrop = Make<Div>();
  backdrop->SetBackgroundColor(kernel::color_constants::kYellow)
      .SetPadding(10)
      .SetWidth(180);
  backdrop->Add(Make<Image>(masked)->ScaleAbsolute(160, 160));
  document.Add(backdrop);

  // 3. The low-level way: one image XObject drawn several times by PdfCanvas.
  //    The XObject is written to the file only once.
  kernel::PdfPage* page = pdf.GetPage(1);
  kernel::PdfImageXObject xobject(gradient);
  kernel::PdfCanvas canvas(page);
  float x = 240;
  for (int i = 0; i < 5; ++i) {
    float size = 30.f + i * 12;
    canvas.AddXObjectFittedIntoRectangle(xobject,
                                         kernel::Rectangle(x, 90, size, size));
    x += size + 10;
  }
  document.ShowTextAligned("One image XObject, drawn five times:", 36, 100,
                           TextAlignment::kLeft);

  // 4. Image files from the command line, one per page, scaled to the page.
  for (int i = 2; i < argc; ++i) {
    try {
      auto data = io::ImageDataFactory::Create(std::string(argv[i]));
      document.Add(Make<AreaBreak>());
      document.Add(
          Make<Paragraph>(std::filesystem::path(argv[i]).filename().string())
              ->SetFont(bold));
      kernel::Rectangle area =
          document.GetPageEffectiveArea(document.GetPageSize());
      document.Add(Make<Image>(data)
                       ->ScaleToFit(area.GetWidth(), area.GetHeight() - 40)
                       .SetHorizontalAlignment(HorizontalAlignment::kCenter));
      std::cout << argv[i] << ": " << data->GetWidth() << "x"
                << data->GetHeight() << " px\n";
    } catch (const std::exception& e) {
      std::cerr << "Skipping " << argv[i] << ": " << e.what() << "\n";
    }
  }

  document.Close();
  std::cout << "Wrote " << dest << "\n";
  return 0;
}
