# pdfcpp

A C++17 port of the [iText](https://github.com/itext/itext-java) PDF library (`reference/itext-java`),
built with CMake. Everything lives in the `pdfcpp` namespace, with sub-namespaces mirroring the Java
modules:

| Java package                     | C++ namespace / headers                                   |
|----------------------------------|-----------------------------------------------------------|
| `com.itextpdf.io.*`              | `pdfcpp::io` — `include/pdfcpp/io/...`                    |
| `com.itextpdf.kernel.*`          | `pdfcpp::kernel` — `include/pdfcpp/kernel/...`            |
| `com.itextpdf.layout.*`          | `pdfcpp::layout` — `include/pdfcpp/layout/...`            |

Classes mirror the Java API (`PdfDocument`, `PdfReader`, `PdfWriter`, `PdfCanvas`, `Document`, `Paragraph`,
`Table`, ...), with names spelled per the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html):
methods are PascalCase (`createFont` → `PdfFontFactory::CreateFont`, `getTextFromPage` →
`PdfTextExtractor::GetTextFromPage`), constants and enumerators are `kPascalCase`
(`ColorConstants.RED` → `color_constants::kRed`, `PdfName.Type` → `PdfName::kType`), variables and
parameters are snake_case and class data members end in `_`. Files are snake_case (`pdf_reader.h`,
`pdf_reader.cc`).

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
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"

using namespace pdfcpp;
using namespace pdfcpp::layout;

int main() {
  kernel::PdfDocument pdf(std::make_unique<kernel::PdfWriter>("hello.pdf"));
  Document document(pdf);
  document.Add(Make<Paragraph>("Hello World!")->SetFontSize(20));
  document.Close();
}
```

More in `examples/`: `hello_world`, `report` (lists, multi-page tables with repeating headers,
outlines), `barcodes` (QR Code, Code 128, EAN-13, Code 39), `forms` (AcroForm fields and optional
flattening), `canvas_graphics` (vector paths, FormXObjects, annotations, page footers), `stamp`
(watermark an existing PDF, optional append mode), `merge`, `split`, `encrypt` (AES-256),
`extract_text`, `edit_text` (find and replace text in an existing PDF), `sign`, `verify_signature`,
`layout_showcase` (text styles, alignment, borders, lists, tables with spans, fixed/rotated elements),
`images` (layout and canvas images, soft masks, JPEG/PNG files), `fonts` (standard 14 fonts, embedded
TrueType, Identity-H Unicode text, kerning), `bookmarks` (clickable table of contents, nested outlines,
internal links), `pdf_info` (inspect version, encryption, pages, fonts, bookmarks and form fields),
`content_parser` (custom `IEventListener`: text positions, fonts and image extraction),
`raw_objects` (hand-written content streams and dictionaries, named destinations, object-graph dump)
and `compress` (rewrite with full compression, or decompress for debugging).

### Differences from the Java API

* Objects are reference counted: PDF objects are `std::shared_ptr<PdfObject>` (`PdfDictionaryPtr`,
  `PdfArrayPtr`, ...); predefined names are `PdfName::kType`, `PdfName::kPage`, ... (all 879 generated from
  `PdfName.java`; `PdfName.a`/`PdfName.r` become `kLowerA`/`kLowerR`). Downcast with `PdfCast<T>(obj)`.
  Layout elements are created with `layout::Make<T>(...)` (i.e. `std::make_shared`), and fluent setters
  return references, so Java's `doc.add(new Paragraph("x").setBold())` becomes
  `doc.Add(Make<Paragraph>("x")->SetBold())`.
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
  updates, broken-xref repair (`FixXref` / `RebuildXref`), stream length recovery, memory-limit guards.
* `PdfWriter`: full compression (object + xref streams), compression levels, append mode
  (`StampingProperties::UseAppendMode`), document IDs.
* Filters: Flate (+ PNG/TIFF predictors), LZW, ASCIIHex, ASCII85, RunLength.
* Standard security handler: RC4-40, RC4-128, AES-128, AES-256 (R6) — reading and writing, user/owner
  passwords, permissions.
* `PdfDocument`, `PdfPage`, page tree, `PdfResources`, `PdfDocumentInfo`, `CopyPagesTo`.
* `PdfCanvas`: paths, text, colors (Gray/RGB/CMYK/any color space), ExtGState, XObjects, images,
  marked content; `PdfFormXObject`, `PdfImageXObject`, `PdfExtGState`.
* Fonts: `PdfFontFactory` → simple Type1/TrueType fonts (WinAnsi or custom encodings, embedding,
  subsetting) and Type0 Identity-H/V fonts with embedded subset + ToUnicode; reading of all font types
  (Type1, TrueType, Type3, Type0) including `/Differences`, ToUnicode, predefined Unicode CMaps.
* Content parsing: `PdfCanvasProcessor`, `TextRenderInfo`, `ImageRenderInfo`, `PathRenderInfo`, inline
  images, `SimpleTextExtractionStrategy`, `LocationTextExtractionStrategy` (with diacritic marks
  handling), `PdfTextExtractor`. The Java `PdfTextExtractorTest` / `SimpleFontToUnicodeExtractionTest`
  expectations are reproduced in `tests/kernel_parser_test.cc`.
* Navigation: `PdfOutline`, `PdfDestination`, `PdfAction`, `PdfAnnotation` (link, text, square,
  free text, highlight), `PdfMerger`, `PdfSplitter`.
* Text editing (pdfcpp extension, no iText kernel equivalent): `PdfTextEditor` finds and replaces text in
  existing content streams — literal, regex or custom matching per `BT … ET` block; matches may span
  `TJ` kerning and separately positioned words; the replacement reuses the original font (only glyphs a
  subset font is known to contain) and falls back to a configurable font per character; optional
  layout preservation; edits form XObjects too. Only the changed operators are rewritten.

**barcodes**
* `Barcode128` (code sets A/B/C, auto switching, FNC1), `BarcodeEAN` (EAN-13, EAN-8, UPC-A), `Barcode39`
  (incl. extended + checksum), `BarcodeQRCode` with a native encoder whose output was verified to be
  bit-identical to iText's `qrcode.Encoder` for numeric/alphanumeric/byte data at every EC level.

**forms**
* `PdfAcroForm` (get/create, field lookup by qualified name, add/remove/rename, default resources and
  appearance, `FlattenFields`/`PartialFormFlattening`), `PdfFormField` (values, flags, fonts/sizes/colors,
  justification, borders/backgrounds, appearance regeneration for text, multiline, password, choice and
  checkbox fields), `TextFormFieldBuilder`, `CheckBoxFormFieldBuilder`, `ChoiceFormFieldBuilder`.

**signatures** (OpenSSL)
* `PdfSigner::SignDetached` — incremental-update signing with `adbe.pkcs7.detached` CMS signatures
  (SHA-1/256/384/512), visible or invisible signature fields; `PrivateKey`/`Certificate` loading from
  PEM or PKCS#12 and self-signed test certificates.
* `SignatureUtil` — signature discovery, `/ByteRange` coverage check and CMS integrity verification.
  Results agree with poppler's `pdfsig` on the Java test-suite's signed PDFs.

**layout**
* `Document`, `Canvas`, `ShowTextAligned`.
* Elements: `Paragraph`, `Text`, `Link`, `Div`, `Image`, `List`/`ListItem` (bullets, decimal, roman,
  latin, greek), `Table`/`Cell` (point/percent widths, colspan, rowspan, header/footer rows repeated on
  every page, cell content split across pages), `AreaBreak`, `LineSeparator`.
* Properties: fonts, sizes, colors, bold/italic simulation, underline/line-through, character/word
  spacing, text rise, alignment (left/right/center/justified), leading (fixed/multiplied), first-line
  indent, margins, paddings, borders (solid, dashed, dotted, double), backgrounds, widths/heights,
  keep-together, fixed positioning, rotation, opacity.
* Renderers follow iText's model (`Layout()` → `LayoutResult` `kFull`/`kPartial`/`kNothing` with
  split/overflow renderers, `Draw(DrawContext)`).

## Not (yet) ported

These Java modules/features are not part of this port: XFA and the more exotic `forms` features
(radio groups builder, push buttons, XFDF), the advanced `sign` features (PAdES B-T/LT/LTA, timestamps,
OCSP/CRL, certificate-chain validation, external/two-phase signing, CAdES signing-certificate
attributes), the remaining barcodes (PDF417, DataMatrix, Codabar, Inter25, MSI, Postnet,
EAN supplements), `svg`, `styled-xml-parser`, `pdfa`/`pdfua` validation, `hyph` (hyphenation),
font-asian CMaps (predefined CJK CMaps other than the Unicode ones), XMP metadata modelling (raw XMP
bytes can be set), tagged PDF / structure tree, optional content (layers), public-key encryption,
GSUB/GPOS shaping, CFF subsetting (CFF-based OpenType fonts are embedded whole), flex/grid/float layouts,
and the polygon clipper used for exact clipping paths in the parser.
