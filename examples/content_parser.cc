// Parses page content streams with a custom IEventListener. Prints every text
// run with its position, font, size and color, lists images and counts vector
// paths. With --images it also saves the images: JPEGs as .jpg, and 8-bit
// RGB/gray images as .ppm/.pgm.
//
// usage: content_parser <file.pdf> [page] [--images]
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/xobject/pdf_xobject.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

namespace {

std::string ColorToString(const ColorPtr& color) {
  if (!color) return "-";
  std::ostringstream out;
  out << std::setprecision(2);
  const char* sep = "";
  for (float v : color->GetColorValue()) {
    out << sep << v;
    sep = ",";
  }
  return out.str();
}

std::string FontName(PdfFont* font) {
  if (!font) return "?";
  auto name = font->GetPdfObject()->GetAsName(PdfName::kBaseFont);
  std::string s = name ? name->GetValue() : "(Type3)";
  // Drop the subset tag, e.g. "ABCDEF+DejaVuSans" -> "DejaVuSans".
  if (s.size() > 7 && s[6] == '+') s = s.substr(7);
  return s;
}

// Writes an 8-bit gray or RGB image (decoded samples) as PGM/PPM. Returns the
// file name, or "" if the image has another format.
std::string WritePnm(PdfStream& stream, Bytes pixels, const std::string& base) {
  int width = stream.GetAsInt(PdfName::kWidth).value_or(0);
  int height = stream.GetAsInt(PdfName::kHeight).value_or(0);
  int bpc = stream.GetAsInt(PdfName::kBitsPerComponent).value_or(0);
  auto cs = stream.GetAsName(PdfName::kColorSpace);
  int components = !cs                              ? 0
                   : cs->GetValue() == "DeviceRGB"  ? 3
                   : cs->GetValue() == "DeviceGray" ? 1
                                                    : 0;
  if (bpc != 8 || components == 0 ||
      pixels.size() < static_cast<size_t>(width) * height * components)
    return "";
  std::string name = base + (components == 3 ? ".ppm" : ".pgm");
  std::ofstream out(name, std::ios::binary);
  out << (components == 3 ? "P6" : "P5") << "\n"
      << width << " " << height << "\n255\n";
  out.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(width) * height * components);
  return name;
}

class ContentListener : public IEventListener {
 public:
  ContentListener(int page, bool save_images)
      : page_(page), save_images_(save_images) {}

  void EventOccurred(const IEventData& data, EventType type) override {
    switch (type) {
      case EventType::kRenderText:
        OnText(static_cast<const TextRenderInfo&>(data));
        break;
      case EventType::kRenderImage:
        OnImage(static_cast<const ImageRenderInfo&>(data));
        break;
      case EventType::kRenderPath:
        if (static_cast<const PathRenderInfo&>(data).GetOperation() !=
            PathRenderInfo::kNoOp)
          ++paths_;
        break;
      default:
        break;
    }
  }

  std::set<EventType> GetSupportedEvents() const override {
    return {EventType::kRenderText, EventType::kRenderImage,
            EventType::kRenderPath};
  }

  int Paths() const { return paths_; }

 private:
  void OnText(const TextRenderInfo& info) {
    std::string text = info.GetText();
    if (text.find_first_not_of(' ') == std::string::npos) return;
    const Vector& start = info.GetBaseline().GetStartPoint();
    // The effective size includes the text matrix and CTM scaling, so it is
    // right even for producers that use "1 Tf" and scale with Tm.
    float size = info.GetAscentLine()
                     .GetStartPoint()
                     .Subtract(info.GetDescentLine().GetStartPoint())
                     .Length();
    std::cout << "  text  " << std::fixed << std::setprecision(1)
              << std::setw(6) << start.Get(Vector::kI1) << std::setw(7)
              << start.Get(Vector::kI2) << "  h=" << std::setw(5) << size
              << "  " << std::left << std::setw(22) << FontName(info.GetFont())
              << std::right << " rgb=" << std::setw(9)
              << ColorToString(info.GetFillColor()) << "  \"" << text << "\"\n";
  }

  void OnImage(const ImageRenderInfo& info) {
    const Matrix& ctm = info.GetImageCtm();
    float w = std::hypot(ctm.Get(Matrix::kI11), ctm.Get(Matrix::kI12));
    float h = std::hypot(ctm.Get(Matrix::kI21), ctm.Get(Matrix::kI22));
    PdfStreamPtr stream = info.GetImage();
    std::cout << "  image " << std::fixed << std::setprecision(1)
              << std::setw(6) << ctm.Get(Matrix::kI31) << std::setw(7)
              << ctm.Get(Matrix::kI32) << "  " << w << " x " << h << " pt, "
              << stream->GetAsInt(PdfName::kWidth).value_or(0) << " x "
              << stream->GetAsInt(PdfName::kHeight).value_or(0) << " px"
              << (info.IsInline() ? " (inline)" : "") << "\n";
    if (!save_images_ || info.IsInline()) return;

    std::string base =
        "page" + std::to_string(page_) + "_img" + std::to_string(++images_);
    PdfImageXObject image(stream);
    Bytes bytes = image.GetImageBytes(/*decoded=*/true);
    if (image.IdentifyImageFileExtension() == "jpg") {
      std::ofstream(base + ".jpg", std::ios::binary)
          .write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
      std::cout << "        saved " << base << ".jpg\n";
    } else if (std::string name = WritePnm(*stream, std::move(bytes), base);
               !name.empty()) {
      std::cout << "        saved " << name << "\n";
    } else {
      std::cout << "        (format not supported for saving)\n";
    }
  }

  int page_;
  bool save_images_;
  int images_ = 0;
  int paths_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: content_parser <file.pdf> [page] [--images]\n";
    return 1;
  }
  int only_page = 0;
  bool save_images = false;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--images")
      save_images = true;
    else
      only_page = std::stoi(arg);
  }

  PdfDocument pdf(std::make_unique<PdfReader>(argv[1]));
  for (int i = 1; i <= pdf.GetNumberOfPages(); ++i) {
    if (only_page && i != only_page) continue;
    std::cout << "--- page " << i << " ---\n";
    ContentListener listener(i, save_images);
    PdfCanvasProcessor processor(listener);
    processor.ProcessPageContent(pdf.GetPage(i));
    std::cout << "  " << listener.Paths() << " painted path(s)\n";
  }
  return 0;
}
