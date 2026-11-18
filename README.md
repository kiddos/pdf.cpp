# pdfcpp

A C++17 port of the [iText](https://github.com/itext/itext-java) PDF library (`reference/itext-java`),
built with CMake. Everything lives in the `pdfcpp` namespace, with sub-namespaces mirroring the Java
modules:

| Java package                     | C++ namespace / headers                                   |
|----------------------------------|-----------------------------------------------------------|
| `com.itextpdf.io.*`              | `pdfcpp::io` — `include/pdfcpp/io/...`                    |
| `com.itextpdf.kernel.*`          | `pdfcpp::kernel` — `include/pdfcpp/kernel/...`            |
| `com.itextpdf.layout.*`          | `pdfcpp::layout` — `include/pdfcpp/layout/...`            |

Class and method names follow the Java API (`PdfDocument`, `PdfReader`, `PdfWriter`, `PdfCanvas`,
`PdfFontFactory::createFont`, `PdfTextExtractor::getTextFromPage`, `Document`, `Paragraph`, `Table`, ...).

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, zlib and OpenSSL (libcrypto). libjpeg/libpng are
detected but optional (images are parsed natively).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build        # runs the unit tests
```

Options: `-DPDFCPP_BUILD_TESTS=OFF`, `-DPDFCPP_BUILD_EXAMPLES=OFF`. The library is installable
(`cmake --install build`) and exports the `pdfcpp::pdfcpp` target.

Several tests reuse the Java project's test resources (PDFs, fonts, images) from
`reference/itext-java`; they are skipped when that directory is missing. Set `PDFCPP_REFERENCE_DIR` to
point elsewhere.

## Example

```cpp
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

int main() {
    kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>("hello.pdf"));
    Document document(pdf);
    document.add(make<Paragraph>("Hello World!")->setFontSize(20));
    document.close();
}
```

More in `examples/`: `hello_world`, `report` (lists, multi-page tables with repeating headers,
outlines), `stamp` (watermark an existing PDF, optional append mode), `merge`, `extract_text`.

### Differences from the Java API

* Objects are reference counted: PDF objects are `std::shared_ptr<PdfObject>` (`PdfDictionaryPtr`,
  `PdfArrayPtr`, ...); predefined names are `PdfName::Type`, `PdfName::Page`, ... (all 879 generated from
  `PdfName.java`). Layout elements are created with `layout::make<T>(...)` (i.e. `std::make_shared`), and
  fluent setters return references, so Java's `doc.add(new Paragraph("x").setBold())` becomes
  `doc.add(make<Paragraph>("x")->setBold())`.
* Java `String` is UTF-8 `std::string`; `byte[]` is `pdfcpp::Bytes` (`std::vector<uint8_t>`).
* Nullable return values are `nullptr` / `std::optional`.
* `PdfDocument` can own its reader/writer (`PdfDocument(std::make_unique<PdfWriter>("out.pdf"))`) or
  borrow them by reference, as in Java.

## What is ported

**io**
* Byte sources, `RandomAccessFileOrArray`, `PdfTokenizer`, `ByteBuffer`/`ByteUtils` number formatting,
  output streams (Deflater, ASCIIHex, ASCII85, RunLength), `StreamUtil`.
* `PdfEncodings` (WinAnsi, PDFDocEncoding, MacRoman, Symbol, ZapfDingbats, UTF-16, any iconv encoding).
* Fonts: `FontProgram`, `Glyph`/`GlyphLine`, `FontEncoding`, `AdobeGlyphList`, `Type1Font` (the 14
  standard fonts' AFM metrics are embedded, plus AFM/PFB files), `TrueTypeFont` (TTF/OTF/TTC; cmap
  formats 0/4/6/12, kerning, glyf subsetting), CMap parsing (`CMapToUnicode`, `CMapEncoding`).
* Images: `ImageDataFactory` for JPEG (DCT pass-through) and PNG (predictor pass-through, alpha →
  SMask, palettes, tRNS), raw images.

**kernel**
* The whole object model (`PdfObject`, `PdfName`, `PdfString`, `PdfNumber`, `PdfArray`, `PdfDictionary`,
  `PdfStream`, `PdfIndirectReference`, ...), copying between documents, smart mode.
* `PdfReader`: classic and cross-reference-stream xrefs, hybrid files, object streams, incremental
  updates, broken-xref repair (`fixXref` / `rebuildXref`), stream length recovery, memory-limit guards.
* `PdfWriter`: full compression (object + xref streams), compression levels, append mode
  (`StampingProperties::useAppendMode`), document IDs.
* Filters: Flate (+ PNG/TIFF predictors), LZW, ASCIIHex, ASCII85, RunLength.
* Standard security handler: RC4-40, RC4-128, AES-128, AES-256 (R6) — reading and writing, user/owner
  passwords, permissions.
* `PdfDocument`, `PdfPage`, page tree, `PdfResources`, `PdfDocumentInfo`, `copyPagesTo`.
* `PdfCanvas`: paths, text, colors (Gray/RGB/CMYK/any color space), ExtGState, XObjects, images,
  marked content; `PdfFormXObject`, `PdfImageXObject`, `PdfExtGState`.
* Fonts: `PdfFontFactory` → simple Type1/TrueType fonts (WinAnsi or custom encodings, embedding,
  subsetting) and Type0 Identity-H/V fonts with embedded subset + ToUnicode; reading of all font types
  (Type1, TrueType, Type3, Type0) including `/Differences`, ToUnicode, predefined Unicode CMaps.
* Content parsing: `PdfCanvasProcessor`, `TextRenderInfo`, `ImageRenderInfo`, `PathRenderInfo`, inline
  images, `SimpleTextExtractionStrategy`, `LocationTextExtractionStrategy` (with diacritic marks
  handling), `PdfTextExtractor`. The Java `PdfTextExtractorTest` / `SimpleFontToUnicodeExtractionTest`
  expectations are reproduced in `tests/kernel_parser_test.cpp`.
* Navigation: `PdfOutline`, `PdfDestination`, `PdfAction`, `PdfAnnotation` (link, text, square,
  free text, highlight), `PdfMerger`, `PdfSplitter`.

**layout**
* `Document`, `Canvas`, `showTextAligned`.
* Elements: `Paragraph`, `Text`, `Link`, `Div`, `Image`, `List`/`ListItem` (bullets, decimal, roman,
  latin, greek), `Table`/`Cell` (point/percent widths, colspan, rowspan, header/footer rows repeated on
  every page, cell content split across pages), `AreaBreak`, `LineSeparator`.
* Properties: fonts, sizes, colors, bold/italic simulation, underline/line-through, character/word
  spacing, text rise, alignment (left/right/center/justified), leading (fixed/multiplied), first-line
  indent, margins, paddings, borders (solid, dashed, dotted, double), backgrounds, widths/heights,
  keep-together, fixed positioning, rotation, opacity.
* Renderers follow iText's model (`layout()` → `LayoutResult` FULL/PARTIAL/NOTHING with split/overflow
  renderers, `draw(DrawContext)`).

## Not (yet) ported

These Java modules/features are not part of this port: `forms` (AcroForms), `sign` (digital
signatures), `barcodes`, `svg`, `styled-xml-parser`, `pdfa`/`pdfua` validation, `hyph` (hyphenation),
font-asian CMaps (predefined CJK CMaps other than the Unicode ones), XMP metadata modelling (raw XMP
bytes can be set), tagged PDF / structure tree, optional content (layers), public-key encryption,
GSUB/GPOS shaping, CFF subsetting (CFF-based OpenType fonts are embedded whole), flex/grid/float layouts,
and the polygon clipper used for exact clipping paths in the parser.
