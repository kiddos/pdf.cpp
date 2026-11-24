// Rewrites a PDF with different compression settings.
//
// By default it writes the smallest file it can: Flate at the best level for
// every uncompressed stream, plus full compression (objects packed in object
// streams and a cross-reference stream). With --decompress it does the
// opposite and writes a file whose content streams and objects are readable
// in a text editor, which helps when debugging.
//
// usage: compress <in.pdf> <out.pdf> [--decompress]
#include <filesystem>
#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

namespace {

// True if every filter on the stream is a lossless general-purpose one that
// pdfcpp can decode (images in DCT/JPX/JBIG2/CCITT are left alone).
bool HasOnlyGenericFilters(const PdfStream& stream) {
  PdfObjectPtr filter = stream.Get(PdfName::kFilter);
  if (!filter) return false;
  std::vector<PdfNamePtr> filters;
  if (auto name = PdfCast<PdfName>(filter)) filters.push_back(name);
  if (auto array = PdfCast<PdfArray>(filter))
    for (int i = 0; i < array->size(); ++i)
      filters.push_back(array->GetAsName(i));
  for (const auto& f : filters) {
    if (!f) return false;
    const std::string& v = f->GetValue();
    if (v != "FlateDecode" && v != "LZWDecode" && v != "ASCIIHexDecode" &&
        v != "ASCII85Decode" && v != "RunLengthDecode")
      return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: compress <in.pdf> <out.pdf> [--decompress]\n";
    return 1;
  }
  bool decompress = argc > 3 && std::string(argv[3]) == "--decompress";

  WriterProperties props;
  if (decompress) {
    props.SetCompressionLevel(compression_constants::kNoCompression)
        .SetFullCompressionMode(false);
  } else {
    props.SetCompressionLevel(compression_constants::kBestCompression)
        .SetFullCompressionMode(true);
  }

  {
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1]),
                    std::make_unique<PdfWriter>(argv[2], props));
    int decoded = 0, recompressed = 0;
    for (int i = 1; i < pdf.GetNumberOfPdfObjects(); ++i) {
      auto stream = PdfCast<PdfStream>(pdf.GetPdfObject(i));
      if (!stream) continue;
      if (decompress && HasOnlyGenericFilters(*stream)) {
        // Replace the encoded bytes by the decoded ones and drop the filters.
        Bytes data = stream->GetBytes(/*decoded=*/true);
        stream->Remove(PdfName::kFilter);
        stream->Remove(PdfName::kDecodeParms);
        stream->SetData(data);
        stream->SetCompressionLevel(compression_constants::kNoCompression);
        ++decoded;
      } else if (!decompress && !stream->ContainsKey(PdfName::kFilter)) {
        // Unfiltered streams get Flate-compressed when written.
        stream->SetData(stream->GetBytes(false));
        stream->SetCompressionLevel(compression_constants::kBestCompression);
        ++recompressed;
      }
    }
    pdf.Close();
    if (decompress)
      std::cout << "Decoded " << decoded << " stream(s)\n";
    else
      std::cout << "Compressed " << recompressed << " stream(s)\n";
  }

  auto before = std::filesystem::file_size(argv[1]);
  auto after = std::filesystem::file_size(argv[2]);
  std::cout << argv[1] << ": " << before << " bytes\n"
            << argv[2] << ": " << after << " bytes ("
            << (after * 100 / std::max<decltype(before)>(before, 1)) << "%)\n";
  return 0;
}
