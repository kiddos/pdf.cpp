# Known Bugs & Potential Issues

Audit of `pdfcpp` (`include/pdfcpp/` and `src/`). Issues are grouped by module with severity, exact source location, trigger conditions, and observed impact.

## Status

All issues below have been resolved; line numbers in the entries refer to the code at the time of the audit.
Regression tests live in `tests/io_test.cpp` and the matching module test files (look for `BUGS.md #N` comments).

Resolutions that deliberately keep (or only partly change) Java behaviour:

- **#7** — the missing encoding aliases were added. Unmapped Symbol/ZapfDingbats bytes still decode to U+0000,
  exactly as Java's `SymbolConversion.byteToChar` does.
- **#15** — the close loop already flushed the new object stream (by visiting its xref entry); `close()` now flushes
  it explicitly instead of relying on iteration order.
- **#16** — same single pass as Java and no objects were actually lost (flushing never sets `MODIFIED`); the pass is
  now repeated until nothing new gets flushed.
- **#17** — ported Java's `createType0FontFromTrueTypeFontProgram`: Type0 + TrueType is always embedded and
  licensing restrictions / `FORCE_NOT_EMBEDDED` throw.
- **#32** — like Java, `setValue("CustomOn")` on a checkbox whose only on-state is `Yes` checks nothing; use the
  newly ported `setCheckBoxAppearanceOnStateName("CustomOn")` first. Existing artwork of read checkboxes is kept on
  value changes and redrawn only after appearance setters.
- **#34** — the first row group that does not fit is now split in place (as iText does) instead of moving the whole
  table; it moves only when no cell content fits at all.

Found and fixed while working on these:

- `TableRenderer`: cells that were complete before a row split were laid out and drawn again on the next page
  (they now continue as empty boxes).
- `ParagraphRenderer`: split/overflow parts shared child text renderers whose parent pointer still referred to the
  original renderer, a heap use-after-free when a paragraph split inside a table cell (found with ASan).
- `BlockRenderer`: same problem as #36 for `HEIGHT` on the overflow part (`deleteOwnProperty` left the model's value
  in effect); the overflow now gets the remaining height.
- `SignatureUtil::verifySignature`: a missing `/ByteRange` was dereferenced.

## Summary

| Severity | Count | Highlights |
|----------|------:|------------|
| Critical | 6 | Heap use-after-free in `PdfOutline::removeOutline`, null dereference in `PdfReader::readXrefStream` and `SignatureUtil::verifySignature`, buffer over-reads/OOB accesses in JPEG/LZW/Crypto parsers |
| High | 13 | Infinite loops in CMap/TTF/PdfSplitter, overlapping `rowspan > 1` rows and broken split heuristic in `TableRenderer`, partial split layout bug in `ListItemRenderer`, wrong Type0 embedding policy, unflushed object stream on `PdfDocument::close` |
| Medium | 14 | Case-sensitive encoding gaps, overwritten `thread_local` pointer in `CMapToUnicode::lookup`, `addFixedChild` double-percent width, `ParagraphRenderer` split/wrap edge cases, ignored `TextFormFieldBuilder` setters, `PdfFormField` checkbox/modified gaps |
| Low | 4 | Negative seek in `PdfTokenizer::getNextEof`, `NaN` on 0-extent `bezierArc`, empty-text negative width in `TextRenderInfo`, `FontProgramFactory` 4-byte magic check |

---

## `io` Module

### 1. Out-of-bounds reads and infinite loop in JPEG parser (`Critical`)
- **Location:** `src/io/image/ImageData.cpp:72-93` (`ImageDataFactory::createJpeg`)
- **Details:**
  - `int len = (b[pos] << 8) | b[pos + 1];` does not validate `len >= 2` or `pos + len <= b.size()`.
  - If `len == 0` in a malformed JPEG, `pos += len;` (`line 92`) never advances, causing an **infinite loop** while `pos + 4 <= b.size()`.
  - Marker handlers for `JFIF` (`0xE0`, `line 74`), `Adobe` (`0xEE`, `line 80`), `ICC_PROFILE` (`0xE2`, `line 83`: `b.begin() + pos + len`), and `SOF` (`lines 84-90`) read up to `seg + 14` or `pos + len` without checking bounds against `b.size()`, causing out-of-bounds reads / iterator UB on truncated segments.

### 2. Infinite loop on truncated `beginbfrange` array in CMap parser (`High`)
- **Location:** `src/io/font/CMap.cpp:115-117` (`CMapParser::parse`)
- **Details:** When `end < start || end - start > 0xFFFF` and `c.kind == Token::ArrStart`, line 116 executes:
  ```cpp
  while (lex.next().kind != Token::ArrEnd) {}
  ```
  It does not check `Token::End`. On a truncated CMap missing `]`, `lex.next()` keeps returning `Token::End`, hanging forever. (Compare with line 135, which checks both `ArrEnd` and `End`.)

### 3. `CMapToUnicode::lookup` returns pointer to shared `thread_local` buffer (`Medium`)
- **Location:** `src/io/font/CMap.cpp:205-209` (`CMapToUnicode::lookup`)
- **Details:** For `identity_ && length == 2`, `lookup()` stores the result in `static thread_local std::u32string tmp` and returns `&tmp`. Any subsequent call to `lookup()` on the same thread overwrites `tmp`, silently invalidating any previously returned `const std::u32string*`.

### 4. TrueType kerning key signedness mismatch, Format 12 wrap-around, and unchecked table directory (`High`)
- **Location:** `src/io/font/TrueTypeFont.cpp:47-52, 259-263, 325, 334`; `include/pdfcpp/io/font/TrueTypeFont.h:63`
- **Details:**
  - `kerning_` is `std::unordered_map<int64_t, int>`. `readKerning()` (`line 325`) inserts `int pair = static_cast<int>(u32(p));`, which sign-extends negative values when `leftGID >= 32768` (`0xFFFFFFFFxxxxxxxx`). `getKerning()` (`line 334`) computes `(static_cast<int64_t>(first.getCode()) << 16) + second.getCode()` (always non-negative), so kerning pairs with `leftGID >= 32768` never match.
  - `parse()` (`lines 48-51`) reads `&data_[rec]` for `numTables` entries before checking `rec + 16 <= data_.size()`, allowing an out-of-bounds read on malformed `numTables`.
  - `readCmapSubtable()` Format 12 (`lines 259-260`): if `endChar == 0xFFFFFFFF` and `startChar >= 0xFFFF0000`, `endChar - startChar <= 0x10FFFF` passes and `for (uint32_t c = startChar; c <= endChar; ++c)` wraps `uint32_t c` back to `0`, causing an infinite loop.

### 5. PNG parser missing `IHDR` length check and division by zero (`High`)
- **Location:** `src/io/image/ImageData.cpp:189-195, 216, 277` (`ImageDataFactory::createPng`)
- **Details:**
  - `IHDR` parsing (`lines 189-195`) checks `data + len > b.size()`, not `len >= 13`; a short `IHDR` chunk reads out of bounds at `b[data + 12]`.
  - Line 277 computes `v = v * 255 / ((1 << bitDepth) - 1);` without validating `bitDepth` (`1, 2, 4, 8, 16`) or `colorType` (`0, 2, 3, 4, 6`). `bitDepth == 0` causes division by zero.

### 6. `FontEncoding` out-of-bounds indexing on negative codes (`High`)
- **Location:** `src/io/font/FontProgram.cpp:185-205` (`FontEncoding::fillCustomEncoding`); `include/pdfcpp/io/font/FontProgram.h:256` (`FontEncoding::getDifference`)
- **Details:**
  - In `fillCustomEncoding()`, neither `orderK %= 256` (`line 185-186`) nor `k = std::atoi(first.c_str())` (`line 193-195`) checks `< 0`. Negative values write out of bounds in `differences_[...]`.
  - `FontEncoding::getDifference(int index)` (`FontProgram.h:256`) indexes `differences_[index]` without checking `0 <= index < 256` (unlike `getUnicode` and `setDifference`).

### 7. `PdfEncodings` case-sensitive alias gaps and NUL emission (`Medium`)
- **Location:** `src/io/text.cpp:264-266, 300, 333-334, 337, 351`
- **Details:** Unlike `byteToUnicode()` (`lines 226-233`), `convertToBytes()` and `convertToString()` match `encoding == WINANSI` (`"Cp1252"`), `encoding == PDF_DOC_ENCODING` (`"PDF"`), and `lower == "macroman"` without recognizing `"WinAnsi"`, `"WinAnsiEncoding"`, `"PDFDocEncoding"`, `"pdf"`, or `"MacRomanEncoding"`. Those fall through to `iconvName()` and throw `IoException: Unsupported encoding`.
- Additionally, `convertToString()` (`line 337`) appends `U+0000` (`'\0'`) to the returned string for unmapped entries (`0`) in `SYMBOL_TABLE` and `ZAPF_TABLE`.

### 8. `WindowRandomAccessSource` and `GroupedRandomAccessSource` accept negative positions (`Medium`)
- **Location:** `src/io/source.cpp:230-239, 251-255`
- **Details:** Unlike `ArrayRandomAccessSource::get`, `WindowRandomAccessSource::get(int64_t position)` only checks `position >= length_`. Negative `position` reads before `offset_` in the underlying source. `GroupedRandomAccessSource::findIndex(int64_t position)` returns `0` for `position < 0`, passing a negative offset to `sources_[0]`.

### 9. `PdfTokenizer::getNextEof()` seeks negative on files `< 4` bytes (`Low`)
- **Location:** `src/io/PdfTokenizer.cpp:144-145`
- **Details:** `file_->seek(file_->getPosition() - 4)` underflows below `0` when the input is shorter than 4 bytes and lacks `"%%EOF"`.

### 10. `FontProgramFactory::createFont(const Bytes&)` checks only 4 bytes of AFM header (`Low`)
- **Location:** `src/io/font/FontProgram.cpp:525`
- **Details:** `std::memcmp(fontProgram.data(), "StartFontMetrics", 4) == 0` only checks `"Star"` instead of all 16 bytes (and assumes `size() > 4`).

---

## `kernel` Module

### 11. `PdfOutline::removeOutline()` heap use-after-free & wrong hierarchical `/Count` (`Critical`)
- **Location:** `src/kernel/pdf/Navigation.cpp:206-236`
- **Details:**
  - `removeOutline()` (`lines 206-212`) erases the `shared_ptr` owning `this` from `parent_->children_` (`lines 209-210`) and then accesses `this->parent_->rebuildLinks()` (`line 211`). If no external `shared_ptr` keeps `this` alive, `this` is destroyed during `erase()`, causing a **heap use-after-free**. Even when kept alive externally, `parent_` is never cleared.
  - `rebuildLinks()` (`lines 222-223`) sets `/Count` to direct `children_.size()` rather than recursively summing visible descendants as required by PDF 1.7 §12.3.3.

### 12. `PdfReader::readXrefStream` null dereference on `/Index` and unchecked `/W` / `/Length` / `/N` (`Critical`)
- **Location:** `src/kernel/pdf/PdfReader.cpp:99, 218-224, 517, 578, 583-601, 775-786`
- **Details:**
  - `readXrefStream()` (`lines 586-587`): `index->getAsNumber(idx)->intValue()` and `index->getAsNumber(idx + 1)->intValue()` dereference the pointer without null checks (unlike `w->getAsNumber(k)` on `line 584`). A non-numeric element in `/Index` crashes with a null pointer dereference.
  - `readXrefStream()` (`lines 583-592`): `/W` entries `wc[k]` are not checked for `< 0` or `> 8` (negative `wc[k]` can defeat `bptr + wc[0] + wc[1] + wc[2] > b.size()`).
  - `readObjectStream()` (`lines 218-224`): `n = nNum->intValue()` has no bounds/memory-limit guard before `std::vector<int> address(n), objNumber(n);` (negative `n` throws `std::length_error`; large `n` causes unbounded allocation). Similarly, `readStreamBytesRaw()` (`line 99`) skips `checkPdfStreamLength` for `/XRef` and `/ObjStm`, so an inflated `/Length` allocates gigabytes before reading (`lines 106, 578`).
  - `readXrefSection()` (`line 517`): `/XRefStm` offset uses `->intValue()` instead of `->longValue()`, truncating 64-bit offsets > 2 GB.
  - `checkPdfStreamLength()` (`lines 775, 783, 785`): seeks to `pos - 16`, `pos - 2`, and `pos - 1` without checking `pos >= 16` / `pos >= 2`.

### 13. `PdfEncryption` buffer over-reads and broken `EMBEDDED_FILES_ONLY` writing (`Critical`)
- **Location:** `src/kernel/crypto/PdfEncryption.cpp:140-204, 448-450, 471, 499-501, 532-538`
- **Details:**
  - `computeUserPassword()` (`lines 499-501`): `std::equal(userPad.begin() + i, userPad.end(), PAD)` reads `userPad.size() - i` bytes from the 32-byte `PAD` array without checking `userPad.size() == 32` (buffer over-read past `PAD` if `/O` > 32 bytes; invalid iterator if `< 32` bytes).
  - `readAes256()` (`lines 448-471`): if `/OE` or `/UE` is shorter than 32 bytes, `mkey_` is shorter than 32 bytes (or empty if `< 16` bytes), and `aesEcbDecrypt(mkey_, ...)` passes a short/empty `key.data()` to OpenSSL `EVP_aes_128_ecb()`, reading past the buffer.
  - `decryptByteArray()` (`lines 532-538`): the AES branch does not guard `if (key_.empty())` (unlike the RC4 branch on `line 539`).
  - `PdfEncryption` constructor (`lines 140-204`): when `EMBEDDED_FILES_ONLY` is combined with `STANDARD_ENCRYPTION_128` / `ENCRYPTION_AES_128`, `setupByUserPassword40or128()` still writes `/StrF /StdCF` and `/StmF /StdCF` (instead of `/Identity` + `/EFF /StdCF`) while `PdfOutputStream` and `PdfString` skip encrypting strings and streams, producing corrupt PDFs.

### 14. `LZWDecodeFilter::LZWDecode` out-of-bounds access on malformed streams (`Critical`)
- **Location:** `src/kernel/pdf/filters/Filters.cpp:221-251`
- **Details:**
  - When a malformed stream has `code >= table.size()` (`lines 239-244`), it assumes `code == table.size()` and indexes `table[oldCode][0]`. If `oldCode == 256` (whose entry in `table` is an empty `Bytes{}`) or `code > table.size()`, `table[oldCode][0]` accesses out of bounds.
  - If a stream starts without clear code `256` or exceeds 4096 entries without a `256` reset, `table.push_back(n)` grows past 4096 while `bitsToRead` remains 12.

### 15. `PdfDocument::close()` leaves unused-object `PdfObjectStream` unflushed under full compression (`High`)
- **Location:** `src/kernel/pdf/PdfDocument.cpp:1048-1059`
- **Details:** Line 1048 calls `writer_->flushWaitingObjects(forbiddenToFlush)`, which flushes and clears `objectStream_`. Lines 1049-1058 then iterate over remaining un-flushed references and, when `isFlushUnusedObjects()` is true, call `object->flush()` (`canBeInObjStm = true`). Under full compression, `flushObject` adds these objects to a **new** `objectStream_` that is never flushed before `writeXrefTableAndTrailer`, leaving xref entries pointing to a missing object stream.

### 16. `PdfWriter::flushModifiedWaitingObjects` single-pass ignores `MUST_BE_FLUSHED` (`High`)
- **Location:** `src/kernel/pdf/PdfWriter.cpp:251-266`
- **Details:** Unlike `flushWaitingObjects()`, `flushModifiedWaitingObjects()` makes only one pass `1..xref->size()-1` and checks only `PdfObject::MODIFIED`, ignoring `PdfObject::MUST_BE_FLUSHED`. Objects marked `MUST_BE_FLUSHED` (or lower-numbered objects marked `MODIFIED`) during flushing are skipped and then freed by `PdfDocument::close()` (`PdfDocument.cpp:1032`).

### 17. `PdfFontFactory::createFont` ignores `EmbeddingStrategy` / `allowEmbedding` for `PdfType0Font` (`High`)
- **Location:** `src/kernel/font/PdfFont.cpp:984-989, 911`
- **Details:**
  - Line 989 constructs `std::make_shared<PdfType0Font>(ttf, encoding, true)` with `embedded = true` hardcoded, ignoring both `EmbeddingStrategy::PREFER_NOT_EMBEDDED` / `FORCE_NOT_EMBEDDED` and `!ttf->getFontNames().allowEmbedding()` (even after lines 984-987 set `embedded = false` to honor embedding restrictions).
  - `PdfType0Font::flushFontData()` (`line 911`) dereferences `dict.getIndirectReference()->getDocument()` without a null check (unlike `PdfSimpleFont::flushFontData` on `line 569`).

### 18. `PdfSplitter::splitByPageCount` infinite loop for `pageCount <= 0` (`High`)
- **Location:** `src/kernel/pdf/Navigation.cpp:262-267`
- **Details:** `for (int start = 1; start <= n; start += pageCount)` loops indefinitely if `pageCount <= 0` and `n >= 1`.

### 19. `PdfDocument::addPage(int, std::shared_ptr<PdfPage>)` skips foreign-document check (`Medium`)
- **Location:** `src/kernel/pdf/PdfDocument.cpp:898-904`
- **Details:** Unlike `addPage(std::shared_ptr<PdfPage>)` (`line 891`), the indexed overload does not check `page->getDocument() != nullptr && page->getDocument() != this`, allowing pages owned by another document to be inserted directly.

### 20. `PdfResources::addForm(PdfFormXObject&, const PdfNamePtr&)` skips resource map & `setModified()` (`Medium`)
- **Location:** `src/kernel/pdf/PdfDocumentFonts.cpp:64-75`
- **Details:** Writes directly to `/XObject` without updating `resourceToName_` (so `getResourceName()` / subsequent `addForm()` calls cannot find it) and sets `setModifiedFlag(true)` without calling `getPdfObject()->setModified()`, so changes are lost in append mode.

### 21. `PdfPage::copyTo` leaves dangling `/Popup` refs and filters all `/P` keys (`Medium`)
- **Location:** `src/kernel/pdf/PdfDocument.cpp:658-663, 692-706`
- **Details:** `copyTo()` skips copying `/Popup` annotations into `/Annots` (`line 693`), yet `PageCopyFilter` does not filter `/Popup` keys inside markup annotations, copying an orphaned `/Popup` object. Conversely, `PageCopyFilter` (`line 661`) drops `/P` from *any* dictionary in the copied subtree, not just direct annotation entries.

### 22. `PdfDictionary::clone(excludeKeys)` loses keys if `clone()` throws (`Medium`)
- **Location:** `src/kernel/pdf/PdfObject.cpp:804-817`
- **Details:** Mutates `this->map_` via `const_cast` to temporarily erase `excludeKeys`, but lacks the `try/catch` restoration used in `copyTo(excludeKeys)` (`lines 831-837`). If `PdfObject::clone()` throws (e.g. on a flushed stream), `excludeKeys` remain permanently erased from the source dictionary. Also `PdfArray::toString` (`line 600`) and `PdfDictionary::toString` (`line 782`) dereference entries without null guards.

### 23. `FlateDecodeFilter::decodePredictor` lacks validation for negative/overflowing parameters (`Medium`)
- **Location:** `src/kernel/pdf/filters/Filters.cpp:51-60`
- **Details:** `int bytesPerRow = (colors * width * bpc + 7) / 8;` can overflow signed `int` or accept negative `colors`/`width` pairs whose product is positive (`colors = -1, width = -10`).

### 24. `Color::makeColor` missing length check for 3-component `DeviceRGB` (`Medium`)
- **Location:** `src/kernel/colors/Color.cpp:68`
- **Details:** Checks `n == 3` without `v.size() >= 3` (unlike `n == 4 && v.size() >= 4` on `line 69`), causing out-of-bounds reads `v[1]` / `v[2]` if a 1- or 2-element `colorValue` is passed with `DeviceRGB`.

### 25. `PdfCanvasProcessor::displayXObject` leaks `xobjectDepth_` on exception (`Medium`)
- **Location:** `src/kernel/pdf/canvas/parser/PdfCanvasProcessor.cpp:725-746`
- **Details:** `++xobjectDepth_` (`line 725`) is executed before `xobjectStream->getBytes()` (`line 732`), which sits outside the `try/catch` block (`lines 740-746`). If stream decoding throws, `xobjectDepth_` is never decremented.

### 26. `PdfCanvas::bezierArc` emits `NaN` when `extent == 0` (`Low`)
- **Location:** `src/kernel/pdf/canvas/PdfCanvas.cpp:297`
- **Details:** `std::sin(halfAng)` is `0` when `extent == 0`, producing `0.0 / 0.0` (`NaN`) for `kappa` and writing `NaN` into the content stream.

### 27. `TextRenderInfo::getUnscaledBaselineWithOffset` negative width on empty string (`Low`)
- **Location:** `src/kernel/pdf/canvas/parser/PdfCanvasProcessor.cpp:130-135`
- **Details:** When `getUnscaledWidth()` is `0` (e.g. empty string `()` or unmapped glyph) and `charSpacing > 0`, subtracting `gs_.getCharSpacing() * hs` produces a negative baseline width.

---

## `barcodes`, `forms`, and `signatures` Modules

### 28. `SignatureUtil::verifySignature` missing reader null check and `/ByteRange` bounds validation (`Critical`)
- **Location:** `src/signatures/Signatures.cpp:421-429`
- **Details:**
  - `document_.getReader()->getSafeFile()` (`line 422`) does not check `document_.getReader() != nullptr` (unlike `signatureCoversWholeDocument()` on `line 396`).
  - `Bytes part(static_cast<size_t>(br[i + 1]));` (`line 425`) does not validate `br[i] >= 0`, `br[i + 1] >= 0`, or `br[i] + br[i + 1] <= file->length()`. Negative or forged huge `/ByteRange` lengths trigger massive allocations / `std::length_error`.

### 29. `BarcodeEAN` out-of-bounds reads on short or non-digit codes (`High`)
- **Location:** `src/barcodes/Barcodes.cpp:281-311, 328, 348, 394-413`
- **Details:** `getBarsEAN13`, `getBarsEAN8`, `getBarcodeSize` (`UPCA` `line 328`), and `placeBarcode` index `code_` (`code_[0]`, `code_[11]`, `code_.back()`, `code_[k] - '0'`) without verifying exact expected length (`13`, `8`, or `12`) or ASCII digits `'0'..'9'`, risking out-of-bounds reads in `code_`, `PARITY13`, and `BARS_EAN`.

### 30. `PdfAcroForm::flattenFields` ignores `/Matrix` and mishandles hierarchical partial flattening (`High`)
- **Location:** `src/forms/Forms.cpp:672-701`
- **Details:**
  - `flattenFields()` (`lines 673-678`) computes appearance scale/translation solely from `/BBox` and `/Rect`, ignoring the appearance Form XObject's `/Matrix` entry and misplacing rotated or transformed appearances.
  - Partial flattening (`partialFormFlattening`) of all leaf kids of a non-terminal field leaves an empty parent dictionary (`/Kids []`) in `/AcroForm /Fields`; conversely, passing a parent field's name removes it from `/Fields` without flattening its kids' widgets, leaving orphaned annotations on pages.

### 31. `TextFormFieldBuilder::setPage` and `setFont` are ignored (`Medium`)
- **Location:** `include/pdfcpp/forms/Forms.h:152-160`; `src/forms/Forms.cpp:747-758`
- **Details:** `setPage(PdfPage*)` and `setFont(std::shared_ptr<PdfFont>)` store `page_` and `font_`, but `createText()` and `createMultilineText()` never use either member (and hardcode `/DA` to `"/Helv 0 Tf 0 g"`).

### 32. `PdfFormField` checkbox custom on-states, appearance overwriting, and missing `setModified()` (`Medium`)
- **Location:** `src/forms/Forms.cpp:236-276, 281-298, 412-444`
- **Details:**
  - Calling `setValue("CustomOn")` on a newly built checkbox whose `/AP /N` only has `"Yes"`/`"Off"` sets `/AS` to `/Off` because `regenerateCheckBoxWidget` hardcodes `onState = "Yes"`.
  - On an existing non-merged checkbox read from a PDF, `setValue()` sets `getPdfObject()->setModified()`, causing `regenerateCheckBoxWidget()` (`line 413`) to overwrite existing custom `/AP /N` artwork with a ZapfDingbats `"4"` checkmark.
  - `setJustification`, `setBackgroundColor`, `setBorderColor`, and `setBorderWidth` (`lines 236-276`) do not call `setModified()` on the field/widgets when appearance regeneration is skipped or deferred.

---

## `layout` Module

### 33. `TableRenderer::layoutRowGroup` overlaps all rows in `rowspan > 1` row groups (`High`)
- **Location:** `src/layout/Renderers.cpp:1274-1324`
- **Details:** When a cell has `rowspan > 1`, `groupEnd > r + 1`. The loop `for (int rr = r; rr < groupEnd; ++rr)` lays out every row's cells at the **exact same `top` coordinate** (`line 1281`) and assigns every cell `Rectangle cellBox(colX[c], top - groupHeight, ..., groupHeight)` (`line 1310`). All rows `r + 1 .. groupEnd - 1` overlap directly on top of row `r`.

### 34. `TableRenderer::layout` broken split threshold, reversed header draw order, and silent drop on `avail <= 0` (`High`)
- **Location:** `src/layout/Renderers.cpp:1395, 1400-1404, 1417`
- **Details:**
  - Line 1395 checks `avail < 0.5f * (bodyTop - bottomLimit + 1e6f)`. The `+ 1e6f` term makes the threshold `> 500,000` pt (always true on normal pages), preventing tables from splitting on partially used pages and forcing the entire table to the next page.
  - When `area.isEmptyArea()` is true and `avail <= 0` (`partRow->height <= 0`, e.g., header + footer taller than page), `lines 1401-1403` `break` out of the row loop without setting `splitAtRow = true`, returning `LayoutResult::FULL` and silently dropping the remaining rows.
  - Line 1417 inserts header cells via `placed_.insert(placed_.begin(), hc)`, reversing the column draw order (`last..first`).

### 35. `ListItemRenderer::layout` omits symbol offset on `PARTIAL` split & `makeSymbol` negative index (`High`)
- **Location:** `src/layout/Renderers.cpp:1088-1090, 1142-1154`
- **Details:**
  - `ListItemRenderer::layout` shifts `ctx` right by `symbolAreaWidth_` before calling `BlockRenderer::layout(ctx)`, and only restores `-symbolAreaWidth_` / `+symbolAreaWidth_` when `status == FULL` (`lines 1148-1151`). On `PARTIAL` split, `split->occupiedArea_` remains shifted right by `symbolAreaWidth_`, causing `draw()` to paint the bullet/number and left border/background shifted over the item text.
  - In `ListRenderer::makeSymbol` (`lines 1088-1090`), if `LIST_START <= 0`, `(index - 1) % 24` / `% 10` is negative, producing invalid Unicode code points for Greek and ZapfDingbats numbering.

### 36. `RootRenderer::addFixedChild` fails to clear `Property::WIDTH` on `modelElement_` (`Medium`)
- **Location:** `src/layout/Document.cpp:104`
- **Details:** `ar->deleteOwnProperty(Property::WIDTH)` removes `WIDTH` from the renderer's own map only, leaving it intact on `ar->modelElement_` (where `setFixedPosition` sets it). `AbstractRenderer::getPropertyAny` (`Renderers.cpp:27`) still finds `WIDTH` on `modelElement_`, applying percentage widths twice (e.g., `50%` becomes `25%`).

### 37. `ParagraphRenderer` wrap-erase at `x == 0`, inline `Image` borders/padding, fixed-height split, and `0`-size `Image` (`Medium`)
- **Location:** `src/layout/Renderers.cpp:528, 531, 773-774, 790, 896, 975`; `src/layout/Elements.cpp:87-88`
- **Details:**
  - `ParagraphRenderer::layout` (`line 790`): `it.x >= widthAtBreak - EPS && it.x > 0` skips erasing items at `it.x == 0`. If `pieces_[lineStart]` has zero width (`widthAtBreak == 0`), items at `x == 0` are not erased on wrap and are rendered twice.
  - `ParagraphRenderer` (`lines 773-774, 975`): inline `Image` sizing/positioning adds `margins` only, omitting `borders` and `paddings` applied by `ImageRenderer::draw` (`getInnerAreaBBox()`).
  - `ParagraphRenderer` (`line 896`): overflow split does not `deleteOwnProperty(Property::HEIGHT)` (unlike `BlockRenderer` at `line 332`), re-applying fixed `HEIGHT` on overflow pages.
  - `ImageRenderer::calculateImageSize` (`Renderers.cpp:528, 531`) and `Image::scaleToFit` (`Elements.cpp:87-88`) divide by `w` or `h` without checking `!= 0`, producing `NaN`/`Inf` for zero-sized `PdfFormXObject`s.
