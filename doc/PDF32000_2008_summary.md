# ISO 32000-1:2008 (PDF 1.7): Summary

A summary of `doc/PDF32000_2008.pdf`: 756 pages, Adobe's free copy of ISO 32000-1:2008, *Document management —
Portable document format — Part 1: PDF 1.7*. Its technical content, clause numbers and page numbers match the
official ISO edition. Page numbers below refer to the printed page numbers in the spec.

## Background

- Adobe published PDF in 1993. In 2007 it handed PDF 1.7 (from *PDF Reference, 6th edition*, Nov 2006) to ISO.
  ISO/TC 171/SC 2 adopted it through the fast-track procedure in January 2008.
- The standard is **backward inclusive**: it covers all of PDF 1.0–1.6, except features Adobe had removed.
- PDF's imaging model comes from PostScript, but PDF is *not* a programming language. It is a structured binary file
  format built for random access and fast interactive viewing. It also carries non-page objects such as
  annotations, links, forms, signatures and structure.
- The foreword mentions these subset standards: PDF/X (ISO 15930, prepress), PDF/A (ISO 19005, archiving),
  PDF/E (ISO 24517, engineering), PDF/UA (accessibility).

## Clauses 1–6: Scope, conformance, notation

- **Scope:** a device- and platform-independent digital form for documents. It does **not** specify conversion
  processes, UI or rendering implementation, storage media, how to validate conformance, or hardware/OS
  requirements.
- **Conformance** (clause 2):
  - *Conforming files* must meet every requirement. They don't have to use any optional feature.
  - *Conforming readers* must meet the reader-behaviour requirements and render as the spec defines.
  - *Conforming writers* must meet the file-creation requirements.
  - *Conforming products* do both.
- Clause 3 lists normative references: PostScript LRM, Unicode, ICC, JPEG/JPEG 2000, JBIG2, PKCS, XMP, RFCs, etc.
  Clause 4 defines terms. Clause 5 covers notation: keywords in bold, operators, and "shall/should/may".
- Clause 6 covers version designations: the header `%PDF-1.n`, which a catalog `/Version` entry can override.
  Annex I has the details.

## Clause 7: Syntax (pp. 11–109)

The core file format. A parser or writer implements this first.

### 7.2 Lexical conventions
- Character classes: **white-space** (NUL, HT, LF, FF, CR, SP), **delimiters** `( ) < > [ ] { } / %`, and
  **regular** characters.
- `%` starts a comment that runs to end of line. EOL is CR, LF or CR LF.

### 7.3 Objects
Eight basic types, plus indirect objects:

| Type | Syntax / notes |
|------|----------------|
| Boolean | `true`, `false` |
| Numeric | integers (`123`, `-17`) and reals (`3.14`, `.5`). No exponent notation |
| String | literal `( … )` with escapes `\n \r \t \b \f \( \) \\ \ddd` and balanced parens; hex `< … >` (odd digit count is padded with 0). Text strings are PDFDocEncoding or UTF-16BE with BOM |
| Name | `/Name`, with `#xx` hex escapes. Atomic, case-sensitive, max 127 bytes |
| Array | `[ … ]`, heterogeneous |
| Dictionary | `<< /Key value … >>`. Keys are names. A null value means the key is absent |
| Stream | dictionary + `stream` EOL … `endstream`. Needs `/Length`. Optional `/Filter`, `/DecodeParms`, external `/F` |
| Null | `null` |
| Indirect object | `n g obj … endobj`, referenced by `n g R`. A reference to an undefined object resolves to null |

### 7.4 Filters
Decoders that can be chained via `/Filter` arrays:
- **ASCIIHexDecode**, **ASCII85Decode**
- **LZWDecode** and **FlateDecode** (zlib/deflate), both with PNG/TIFF **predictors** (`/Predictor`, `/Colors`,
  `/BitsPerComponent`, `/Columns`, `/EarlyChange`)
- **RunLengthDecode**
- **CCITTFaxDecode** (Group 3/4)
- **JBIG2Decode** (with `/JBIG2Globals`)
- **DCTDecode** (JPEG, `/ColorTransform`)
- **JPXDecode** (JPEG 2000)
- **Crypt** (per-stream crypt filters)

### 7.5 File structure
- **Header:** `%PDF-1.7`. A second comment line with four or more bytes > 127 is recommended to mark the file as
  binary.
- **Body:** a sequence of indirect objects.
- **Cross-reference table:** `xref` sections with subsections (`first count`). Each entry is exactly 20 bytes,
  `nnnnnnnnnn ggggg n|f` plus a 2-byte EOL. Free entries form a linked list headed by object 0 (gen 65535).
- **Trailer:** `trailer << /Size /Root /Prev /Encrypt /Info /ID >>`, then `startxref`, the byte offset, and `%%EOF`.
- **Incremental updates:** new body, xref and trailer sections are appended, with `/Prev` chaining back. The
  original bytes are never rewritten, which is what signatures depend on.
- **Object streams** (`/Type /ObjStm`, `/N`, `/First`, PDF 1.5): compressed containers for non-stream objects.
- **Cross-reference streams** (`/Type /XRef`, `/W`, `/Index`, PDF 1.5): binary xref data in a stream. Entry type
  0 = free, 1 = uncompressed (offset), 2 = compressed (object stream number + index).
- **Hybrid-reference files:** a classic xref plus `/XRefStm` in the trailer, so pre-1.5 readers still work.

### 7.6 Encryption
- Encryption dictionary (`/Filter`, `/SubFilter`, `/V`, `/Length`, `/CF`, `/StmF`, `/StrF`, `/EFF`). Strings and
  streams are encrypted with a per-object key derived from the file key plus the object number and generation.
- **Standard security handler:** owner and user passwords, `/O`, `/U`, `/P` (permission bits: print, modify, copy,
  annotate, fill forms, extract for accessibility, assemble, high-quality print), revisions **R2, R3, R4**.
  Algorithms 1–7 cover key computation, O/U values and authentication. RC4 40–128 bit; **AESV2** (AES-128-CBC)
  through crypt filters (V4).
- **Public-key security handler** (`adbe.pkcs7.s3/s4/s5`): recipients as PKCS#7 enveloped data.
- **Crypt filters** (PDF 1.5): named filters (`/Identity`, `/StdCF`, …) for strings, streams and embedded files.
- `/EncryptMetadata` controls whether the XMP stream is encrypted.

### 7.7 Document structure
- **Catalog** (`/Type /Catalog`), the root of the document. Holds `/Pages`, `/PageLabels`, `/Names`, `/Dests`,
  `/ViewerPreferences`, `/PageLayout`, `/PageMode`, `/Outlines`, `/Threads`, `/OpenAction`, `/AA`, `/URI`,
  `/AcroForm`, `/Metadata`, `/StructTreeRoot`, `/MarkInfo`, `/Lang`, `/OCProperties`, `/Perms`, `/Extensions`,
  and more.
- **Page tree:** `/Pages` nodes (`/Kids`, `/Count`, `/Parent`) and `/Page` leaves. Inheritable attributes:
  `/Resources`, `/MediaBox`, `/CropBox`, `/Rotate`. Page entries include `/Contents` (a stream or an array of
  streams that are concatenated), `/Annots`, `/BleedBox`/`/TrimBox`/`/ArtBox`, `/Group`, `/Thumb`, `/B`, `/Dur`,
  `/Trans`, `/AA`, `/Metadata`, `/StructParents`, `/Tabs`, `/UserUnit` (PDF 1.6) and `/VP`.
- **Name dictionary:** name trees for `/Dests`, `/AP`, `/JavaScript`, `/Pages`, `/Templates`, `/IDS`, `/URLS`,
  `/EmbeddedFiles`, `/AlternatePresentations` and `/Renditions`.

### 7.8 Content streams and resources
- A content stream is a sequence of operands followed by operators, in postfix order (Annex A lists all of them).
- **Resource dictionary** categories: `/ExtGState`, `/ColorSpace`, `/Pattern`, `/Shading`, `/XObject`, `/Font`,
  `/ProcSet`, `/Properties`.

### 7.9 Common data structures
- Text strings, date strings (`D:YYYYMMDDHHmmSSOHH'mm`), rectangles `[llx lly urx ury]`.
- **Name trees** and **number trees** (`/Kids`, `/Names` or `/Nums`, `/Limits`).

### 7.10 Functions
- **Type 0** sampled, **Type 2** exponential interpolation, **Type 3** stitching, **Type 4** PostScript calculator
  (operator subset in Annex B).
- Common keys: `/Domain`, `/Range`.

### 7.11 File specifications
- File spec strings and dictionaries (`/F`, `/UF`, `/FS`, `/EF`, `/RF`, `/Desc`).
- Embedded file streams (`/Type /EmbeddedFile`, `/Params`), related files, collections (portfolios, PDF 1.7).

### 7.12 Extensions dictionary
Developer extensions (`/BaseVersion`, `/ExtensionLevel`) mark proprietary extensions to a base version.

## Clause 8: Graphics (pp. 110–236)

### 8.2–8.3 Graphics objects and coordinate systems
- The five graphics objects are **path**, **text**, **external (XObject)**, **inline image** and **shading**. The
  spec gives a state diagram of which operators are allowed where (page level, path object, text object, …).
- Coordinate spaces: **device**, **user** (default 1/72 inch, or `/UserUnit`), **text**, **glyph**, **image**,
  **form** and **pattern** space. Mapped by the **CTM**, which `cm` concatenates with a 3×3 affine matrix
  `[a b c d e f]`.

### 8.4 Graphics state
- **Device-independent parameters:** CTM, clipping path, colour space/colour, text state, line width, cap, join,
  miter limit, dash pattern, rendering intent, stroke adjustment, blend mode, soft mask, alpha constants, alpha
  source.
- **Device-dependent parameters:** overprint, overprint mode, black generation, undercolour removal, transfer,
  halftone, flatness, smoothness.
- `q`/`Q` save and restore the state. `w J j M d ri i gs` set parameters, and `gs` applies an **ExtGState**
  dictionary.

### 8.5 Paths
- Construction: `m l c v y h re`.
- Painting: `S s f F f* B B* b b* n`.
- Clipping: `W W*`, which takes effect after the following painting operator.
- Fill rules: **nonzero winding** and **even-odd**.

### 8.6 Colour spaces
- **Device:** DeviceGray, DeviceRGB, DeviceCMYK.
- **CIE-based:** CalGray, CalRGB, Lab, **ICCBased**.
- **Special:** Indexed, Pattern, **Separation**, **DeviceN** (including NChannel attributes).
- Operators: `CS cs SC sc SCN scn G g RG rg K k`.
- Rendering intents and overprint control.

### 8.7 Patterns
- **Tiling patterns** (Type 1): coloured or uncoloured, with `/BBox`, `/XStep`, `/YStep`.
- **Shading patterns** (Type 2) and the `sh` operator. Shading types:
  1. function-based
  2. axial
  3. radial
  4. free-form Gouraud triangle mesh
  5. lattice-form Gouraud mesh
  6. Coons patch mesh
  7. tensor-product patch mesh

### 8.8–8.10 XObjects, images, forms
- `Do` paints an XObject.
- **Image XObjects:** `/Width`, `/Height`, `/ColorSpace`, `/BitsPerComponent`, `/Decode`, `/Interpolate`,
  `/ImageMask` (stencil masks), `/Mask` (explicit or colour-key masking), `/SMask` (soft mask), `/Alternates`,
  `/Intent`.
- **Inline images:** `BI … ID … EI` with abbreviated keys.
- **Form XObjects:** reusable content with `/BBox`, `/Matrix`, `/Resources`. Group XObjects, reference XObjects
  (import a page from another PDF), PostScript XObjects (deprecated).

### 8.11 Optional content (layers)
- **OCGs** (`/Type /OCG`) and **membership dictionaries** (`/OCMD`, `/OCGs`, `/P` policy, `/VE` visibility
  expressions).
- `/OCProperties` in the catalog: the default config `/D`, alternative `/Configs`, `/Order`, `/ON`/`/OFF`,
  `/RBGroups`, `/Locked`, `/AS` auto-state by usage (`/View`, `/Print`, `/Export`, zoom, language).
- Content is marked optional with `BDC /OC /name … EMC`, or with `/OC` on XObjects and annotations.

## Clause 9: Text (pp. 237–295)

### 9.2–9.4 Text state and objects
- Text objects are `BT … ET`, which hold the text matrix `Tm` and the text line matrix `Tlm`.
- **Text state:** `Tc` char spacing, `Tw` word spacing (applies only to the single-byte code 32), `Tz` horizontal
  scaling, `TL` leading, `Tf` font and size, `Tr` render mode (0–7: fill, stroke, clip and their combinations;
  3 = invisible), `Ts` rise, and text knockout.
- **Positioning:** `Td TD Tm T*`.
- **Showing:** `Tj TJ ' "`. Numbers in a `TJ` array adjust the position by −n/1000 text-space units.
- Glyph displacement formula:
  `tx = ((w0 − Tj/1000)·Tfs + Tc + Tw)·Th`, `ty = (w1 − Tj/1000)·Tfs + Tc + Tw`.

### 9.5–9.6 Simple fonts
- Single-byte codes, up to 256 glyphs.
- **Type 1:** the **standard 14 fonts**, which need no embedding: Times, Helvetica and Courier in four styles
  each, plus Symbol and ZapfDingbats. Also multiple-master fonts.
- **TrueType:** cmap selection rules for symbolic and non-symbolic fonts.
- **Type 3:** glyphs defined by content streams (`/CharProcs`, `/FontMatrix`, `d0`/`d1`).
- **Encodings:** `/BaseEncoding` (Standard, MacRoman, WinAnsi, MacExpert) plus a `/Differences` array. Symbolic
  fonts use their built-in encoding.
- Widths: `/FirstChar`, `/LastChar`, `/Widths`.

### 9.7 Composite fonts
- **Type 0** font = **CMap** (character code → CID) + one **CIDFont**: `CIDFontType0` (CFF-based) or
  `CIDFontType2` (TrueType-based, with `/CIDToGIDMap`).
- `/CIDSystemInfo` (Registry–Ordering–Supplement).
- Metrics: `/W`, `/DW` and the vertical metrics `/W2`, `/DW2`.
- Predefined CMaps: Adobe-GB1, -CNS1, -Japan1, -Korea1, the Identity-H/V CMaps, and the UCS2/UTF16 CMaps. Embedded
  CMaps use `begincodespacerange`, `begincidrange`, and so on.

### 9.8–9.9 Font descriptors and embedding
- **Font descriptor** keys: `/Flags` (FixedPitch, Serif, Symbolic, Script, Nonsymbolic, Italic, AllCap, SmallCap,
  ForceBold), `/FontBBox`, `/ItalicAngle`, `/Ascent`, `/Descent`, `/CapHeight`, `/StemV`, …
- Embedded programs: `/FontFile` (Type 1), `/FontFile2` (TrueType), `/FontFile3` (`/Subtype` Type1C, CIDFontType0C,
  OpenType).
- **Subset fonts** are named with a six-uppercase-letter tag, as in `ABCDEF+Name`.

### 9.10 Text extraction
How to map character codes to Unicode, in priority order:
1. the **ToUnicode** CMap
2. standard or predefined encodings with glyph names (Adobe Glyph List)
3. the CIDSystemInfo Registry-Ordering

`/ActualText` (14.9.4) can override the result.

## Clause 10: Rendering

- CIE-based colour → device colour via CRDs or ICC.
- Conversions between DeviceGray, RGB and CMYK, including **black generation** and **undercolour removal**.
- **Transfer functions** and **halftones**: types 1, 5, 6, 10 and 16, plus threshold arrays.
- **Scan conversion** rules: the pixel-touching rule for fills and strokes, stroke adjustment, and smoothness and
  flatness tolerances.

## Clause 11: Transparency

- **Compositing model:** the result is a function of backdrop, source colour, source shape and opacity, and blend
  mode.
- **Blend modes:**
  - *separable:* Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight,
    Difference, Exclusion
  - *non-separable:* Hue, Saturation, Color, Luminosity
- **Transparency groups:** form XObjects with `/Group << /S /Transparency /CS /I /K >>`. **Isolated** groups
  composite onto a transparent backdrop. **Knockout** groups composite each element against the group's initial
  backdrop.
- **Soft masks:** `/SMask` in ExtGState (Alpha or Luminosity type, `/G` group, `/BC`, `/TR`) or on images.
- **ExtGState keys:** `/CA` (stroke alpha), `/ca` (fill alpha), `/BM`, `/SMask`, `/AIS`, `/TK`.
- Colour space and rendering issues: the group colour space, the page group, overprinting with transparency, and
  how spot colours behave.

## Clause 12: Interactive features (pp. 362–485)

### 12.2 Viewer preferences
`/HideToolbar`, `/FitWindow`, `/DisplayDocTitle`, `/Direction`, `/PrintScaling`, `/Duplex`, `/PrintPageRange`,
`/NumCopies`, …

### 12.3 Document-level navigation
- **Destinations:** `[page /XYZ left top zoom]`, `/Fit`, `/FitH`, `/FitV`, `/FitR`, `/FitB`, `/FitBH`, `/FitBV`.
  Named destinations go in the `/Dests` name tree.
- **Outlines (bookmarks):** `/First`, `/Last`, `/Next`, `/Prev`, `/Parent`, `/Count` (negative when closed),
  `/Title`, `/Dest` or `/A`, `/C` colour, `/F` style flags.
- Collections (portable collections or portfolios).

### 12.4 Page-level navigation
Page labels (number tree with `/S` style `D R r A a`, `/P` prefix, `/St` start), articles and threads (beads),
presentations and transitions (`/Trans`, `/Dur`), navigation nodes.

### 12.5 Annotations
- **Common keys:** `/Subtype`, `/Rect`, `/Contents`, `/P`, `/NM`, `/M`, `/F` flags (Invisible, Hidden, Print,
  NoZoom, NoRotate, NoView, ReadOnly, Locked, ToggleNoView, LockedContents), `/AP` appearance streams (`/N`, `/R`,
  `/D`, with on/off states selected by `/AS`), `/Border`, `/BS`, `/C`, `/StructParent`, `/OC`.
- **Markup annotations** add `/T`, `/Popup`, `/CA`, `/RC`, `/CreationDate`, `/IRT`, `/Subj`, `/RT`, `/IT`, `/ExData`.
- **Subtypes:** Text, Link, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight, Underline, Squiggly,
  StrikeOut, Stamp, Caret, Ink, Popup, FileAttachment, Sound, Movie, Widget, Screen, PrinterMark, TrapNet,
  Watermark, 3D.

### 12.6 Actions
- Action dictionaries (`/S`, `/Next` chains). Triggers come from `/OpenAction` and the **additional-actions** `/AA`
  of annotations, pages, the document and form fields (keystroke, format, validate, calculate).
- **Types:** GoTo, GoToR, GoToE, Launch, Thread, URI, Sound, Movie, Hide, Named, SubmitForm, ResetForm, ImportData,
  JavaScript, SetOCGState, Rendition, Trans, GoTo3DView.

### 12.7 Interactive forms (AcroForm)
- **Interactive form dictionary:** `/Fields`, `/NeedAppearances`, `/SigFlags`, `/CO` calculation order, `/DR`
  default resources, `/DA`, `/Q`, `/XFA`.
- **Field tree:** fields can be inherited through `/Parent`/`/Kids`; terminal fields merge with widget annotations.
  Field keys: `/FT`, `/T` (partial name; full names joined by `.`), `/TU`, `/TM`, `/Ff` flags, `/V`, `/DV`, `/AA`.
- **Field types:**
  - **Button** (`/Btn`): push button, check box, radio button (`/Opt`, NoToggleToOff, RadiosInUnison).
  - **Text** (`/Tx`): Multiline, Password, FileSelect, DoNotSpellCheck, DoNotScroll, Comb with `/MaxLen`,
    RichText (`/RV`).
  - **Choice** (`/Ch`): Combo, Edit, Sort, MultiSelect, CommitOnSelChange, `/Opt`, `/I`.
  - **Signature** (`/Sig`).
- **Variable text:** `/DA` default appearance string, `/Q` quadding, `/DS`, `/RV`. Appearance-stream generation
  inside `/Tx BMC … EMC`.
- **Form actions:** SubmitForm (HTML, FDF, XFDF or PDF formats, with flags), ResetForm, ImportData.
- **FDF** (Forms Data Format) file structure, and named pages.
- **XFA** forms: the `/XFA` stream or packet array.

### 12.8 Digital signatures
- **Signature dictionary:** `/Filter` (e.g. Adobe.PPKLite), `/SubFilter` (`adbe.x509.rsa_sha1`,
  `adbe.pkcs7.detached`, `adbe.pkcs7.sha1`), `/Contents` (hex DER signature, padded), `/Cert`, **`/ByteRange`**
  (the signed byte ranges, which exclude the `/Contents` value), `/Reference`, `/Changes`, `/Name`, `/M`,
  `/Location`, `/Reason`, `/ContactInfo`, `/R`, `/V`, `/Prop_Build`.
- **Signature references:** transform methods **DocMDP** (certification signature; `/P` 1 = no changes, 2 = form
  fill and sign, 3 = also annotate), **UR/UR3** (usage rights), **FieldMDP** (`/Action` All/Include/Exclude,
  `/Fields`).
- The catalog `/Perms` dictionary holds `/DocMDP` and `/UR3`. The legal attestation dictionary lists content that
  could change a signed document's appearance.
- How to verify a signature, and how incremental updates keep earlier signatures valid.

### 12.9–12.10 Measurement and requirements
Viewports (`/VP`), rectilinear measure dictionaries (`/RL` scale, number formats), geospatial notes, and document
requirements (`/Requirements`, handlers).

## Clause 13: Multimedia (pp. 486–546)

- **Multimedia (13.2):** rendition actions, rendition trees (media and selector renditions), media clips (data or
  section), play parameters, screen parameters, media criteria (bandwidth, screen depth, language, viewer version),
  media players, and the MH/BE ("must honour" / "best effort") semantics.
- **Sounds and movies:** sound objects (`/R`, `/C`, `/B`, `/E`), movie annotations and activation. Both are older
  mechanisms that clause 13.2 supersedes.
- **Alternate presentations:** slide shows.
- **3D artwork (13.6):** 3D annotations, 3D streams (**U3D**; PRC came in later versions), 3D views (`/3DV`, camera
  `/C2W`, render modes, lighting, cross sections, node parameters), 3D activation, and the JavaScript-to-3D bridge.

## Clause 14: Document interchange (pp. 547–642)

- **Procedure sets (14.2):** `/ProcSet` (PDF, Text, ImageB/C/I). Obsolete, kept for compatibility.
- **Metadata (14.3):** the document information dictionary (`/Title`, `/Author`, `/Subject`, `/Keywords`,
  `/Creator`, `/Producer`, `/CreationDate`, `/ModDate`, `/Trapped`) and **XMP metadata streams**
  (`/Type /Metadata /Subtype /XML`) on the catalog or on individual components.
- **File identifiers (14.4):** trailer `/ID [permanent changing]`, usually MD5 based.
- **Page-piece dictionaries (14.5):** `/PieceInfo` holds private application data, with `/LastModified`.
- **Marked content (14.6):**
  - operators `MP`, `DP` (points) and `BMC`, `BDC`, `EMC` (sequences)
  - tags and property lists, either inline or named in `/Properties`
  - uses: structure, optional content, artifacts, alternate text
- **Logical structure (14.7):**
  - the **structure tree** `/StructTreeRoot` (`/K`, `/ParentTree` number tree, `/RoleMap`, `/ClassMap`,
    `/IDTree`, `/ParentTreeNextKey`)
  - **structure elements** (`/S`, `/P`, `/K`, `/Pg`, `/A` attributes, `/C`, `/Alt`, `/ActualText`, `/Lang`, `/E`)
  - kids can be marked-content references (`MCID`, `/MCR`) or object references (`/OBJR`)
  - user properties and attributes
- **Tagged PDF (14.8):**
  - `/MarkInfo << /Marked true >>`
  - **artifacts** (pagination, layout, page, background) vs. real content
  - reading order and word breaks; Unicode mapping is required
  - **standard structure types:**
    - grouping: `Document`, `Part`, `Art`, `Sect`, `Div`, `BlockQuote`, `Caption`, `TOC`/`TOCI`, `Index`,
      `NonStruct`, `Private`
    - block-level: `P`, `H`, `H1`–`H6`, `L`/`LI`/`Lbl`/`LBody`, `Table`/`TR`/`TH`/`TD`/`THead`/`TBody`/`TFoot`
    - inline-level: `Span`, `Quote`, `Note`, `Reference`, `BibEntry`, `Code`, `Link`, `Annot`, `Ruby`, `Warichu`
    - illustrations: `Figure`, `Formula`, `Form`
  - **standard attributes:** layout (placement, writing mode, colours, borders, padding, space before/after,
    text align, BBox, width/height, line height, …), list, print-field, and table attributes (`RowSpan`,
    `ColSpan`, `Headers`, `Scope`)
- **Accessibility (14.9):** natural language (`/Lang`), alternate descriptions (`/Alt`), replacement text
  (`/ActualText`), expansions of abbreviations (`/E`).
- **Web Capture (14.10):** the web-capture information dictionary, content database, content sets (page and image
  sets), source information, command settings.
- **Prepress (14.11):**
  - page boundaries: MediaBox ⊇ CropBox ⊇ BleedBox/TrimBox/ArtBox, plus box colour info
  - printer's marks, separation dictionaries, trapping support (TrapNet annotations)
  - **output intents** (`/OutputIntents`, `/GTS_PDFX`, `/GTS_PDFA1` with an ICC `/DestOutputProfile`)
  - OPI dictionaries

## Annexes

| Annex | Kind | Contents |
|-------|------|----------|
| A | informative | **Operator summary**: every content-stream operator with its PostScript equivalent and spec reference. A good checklist for a content-stream parser |
| B | normative | Operators allowed in Type 4 (PostScript calculator) functions: arithmetic, relational/boolean/bitwise, conditional (`if`, `ifelse`), stack |
| C | normative | **Implementation limits** (see below) |
| D | normative | Character sets and encodings: tables for StandardEncoding, MacRomanEncoding, WinAnsiEncoding, PDFDocEncoding, MacExpertEncoding, Symbol and ZapfDingbats |
| E | normative | PDF name registry: how second-class names and developer prefixes are registered |
| F | normative | **Linearized PDF** ("fast web view"): linearization parameter dictionary, first-page xref, primary/overflow hint streams (page offset and shared object hint tables) |
| G | informative | Access strategies for linearized files: opening, random page access, outlines, threads |
| H | informative | Example PDF files: minimal file, simple text, graphics, page tree, outline, incremental update |
| I | normative | PDF versions and compatibility: how to handle features newer than the declared version |
| J | informative | FDF rename flag implementation example |
| K | informative | PostScript compatibility for the transparent imaging model |
| L | informative | Colour plates (figures for blend modes, shading, etc.) |

### Annex C limits (32-bit readers)

| Quantity | Limit |
|----------|-------|
| Integer | −2³¹ … 2³¹−1 |
| Real | ±3.403×10³⁸. Nonzero values closest to 0 are ±1.175×10⁻³⁸, smaller ones become 0. About 5 significant fractional digits |
| String in a content stream | 32,767 bytes (no limit elsewhere) |
| Name | 127 bytes |
| Indirect objects | 8,388,607 |
| `q`/`Q` nesting | 28 |
| DeviceN components | 32 |
| CID | 65,535 |
| File size with a classic xref table | 10¹⁰ bytes (10-digit offsets). No limit with xref streams |

Also recommended:
- Page size between 3×3 and 14,400×14,400 user units.
- Thumbnails at most 106×106 samples.
- Zoom between about 8% and 6400%.
- `obj`, `endobj` and `endstream` at the start of a line, so a reader can rebuild a damaged xref.

## How pdfcpp maps to the spec

| Spec area | pdfcpp module | Status (per README) |
|-----------|---------------|---------------------|
| 7.2–7.5 objects, filters, file structure, object/xref streams | `io`, `kernel/pdf` | Ported |
| 7.6 standard security handler | `kernel/crypto` | Ported; **public-key handler not ported** |
| 7.7 catalog, page tree, name/number trees | `kernel/pdf` | Ported |
| 8 graphics, images, form XObjects | `kernel/pdf/canvas`, `kernel/pdf/xobject`, `io/image` | Ported |
| 8.11 optional content | — | **Not ported** |
| 9 text and fonts (simple, Type 0, embedding) | `io/font`, `kernel/font` | Ported; predefined CJK CMaps, GSUB/GPOS shaping and CFF subsetting **not ported** |
| 9.10 text extraction | `kernel/pdf/canvas/parser` | Ported |
| 12.3–12.6 navigation, annotations, actions | `kernel` | Ported |
| 12.7 AcroForm | `forms` | Ported (XFA and XFDF not) |
| 12.8 signatures | `signatures` | Detached CMS signing and verification ported; PAdES LTV, timestamps and OCSP/CRL not |
| 14.3 XMP metadata | `kernel` | Raw bytes only; **no XMP model** |
| 14.6–14.8 marked content, structure tree, Tagged PDF | — | **Not ported** |
| Annex F linearization | — | Not listed in README; not ported |

Clause numbers to read when porting the remaining items:

| Feature | Clause |
|---------|--------|
| Optional content | 8.11 |
| Public-key encryption | 7.6.4 |
| Tagged PDF | 14.6–14.8 |
| XMP | 14.3.2 |
| CJK CMaps | 9.7.5 |
| Linearization | Annex F |
