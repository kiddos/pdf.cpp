#ifndef PDFCPP_KERNEL_PDF_CANVAS_EDITING_PDF_TEXT_EDITOR_H_
#define PDFCPP_KERNEL_PDF_CANVAS_EDITING_PDF_TEXT_EDITOR_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace kernel {

class PdfFont;

/// Outcome of a PdfTextEditor operation.
struct TextEditResult {
  /// Matches that were rewritten.
  int replaced = 0;
  /// Matches left untouched: neither the original font nor the fallback font
  /// can show the replacement, or the match shares a glyph (e.g. a ligature)
  /// with another match.
  int skipped = 0;
  /// Rewritten matches that needed the fallback font for at least one
  /// character.
  int used_fallback_font = 0;

  TextEditResult& operator+=(const TextEditResult& o) {
    replaced += o.replaced;
    skipped += o.skipped;
    used_fallback_font += o.used_fallback_font;
    return *this;
  }
};

/// Edits the text of an existing document by rewriting the text-showing
/// operators of its content streams (pdfcpp extension; iText has no kernel
/// equivalent).
///
/// Matching works on the text of one `BT … ET` block at a time, decoded to
/// Unicode the same way text extraction does. Where glyphs are positioned apart
/// (a word gap in a `TJ` array, or a `Td` move along the same line) the block
/// text has a virtual space, and a move to another line gives a virtual `\n`.
/// So `"Hello World"` matches even if the two words are shown by separate
/// operators.
///
/// The replacement is written with the font of the first matched glyph, at the
/// position of that glyph. Characters that font cannot show fall back to the
/// fallback font (Helvetica unless set). Subset and Type 3 fonts only count as
/// able to show a character if that character's code is already used somewhere
/// in the processed content, because the glyph may have been dropped from the
/// subset.
///
/// Only operators that change are rewritten; the rest of each content stream is
/// kept byte for byte. Text inside form XObjects is edited in place, which
/// affects every page that draws the same form. Annotation appearance streams
/// (e.g. form fields) are not edited.
///
/// Typical use:
/// ```
/// PdfDocument doc(std::make_unique<PdfReader>("in.pdf"),
/// std::make_unique<PdfWriter>("out.pdf")); PdfTextEditor editor(doc);
/// editor.ReplaceText("2023", "2024");
/// doc.close();
/// ```
class PdfTextEditor {
 public:
  /// One edit within a block's text: bytes [start, end) of the UTF-8 block text
  /// are replaced by `text`.
  struct Replacement {
    size_t start;
    size_t end;
    std::string text;
  };
  /// Called with the text of each `BT … ET` block. Returns the edits to make in
  /// it (overlapping or out-of-range entries are ignored).
  using Finder =
      std::function<std::vector<Replacement>(const std::string& block_text)>;

  explicit PdfTextEditor(PdfDocument& document);
  ~PdfTextEditor();
  PdfTextEditor(const PdfTextEditor&) = delete;
  PdfTextEditor& operator=(const PdfTextEditor&) = delete;

  /// Font for characters the original font cannot show. Must belong to (or be
  /// usable in) the edited document. Defaults to Helvetica with WinAnsi
  /// encoding.
  PdfTextEditor& SetFallbackFont(std::shared_ptr<PdfFont> font);
  /// When true, compensates the width difference of each edit with a `TJ`
  /// adjustment, so text that follows in the same operator keeps its original
  /// position. Default false: following text moves along with the edit.
  PdfTextEditor& SetPreserveLayout(bool preserve_layout);
  /// Whether text inside form XObjects drawn by a page is edited too. Default
  /// true.
  PdfTextEditor& SetProcessFormXObjects(bool process_form_x_objects);

  /// Replaces every occurrence of `search` (UTF-8) on the page.
  TextEditResult ReplaceText(PdfPage* page, const std::string& search,
                             const std::string& replacement);
  /// Replaces every occurrence of `search` on every page.
  TextEditResult ReplaceText(const std::string& search,
                             const std::string& replacement);
  /// Replaces every match of `pattern` on the page; `format` uses
  /// std::regex_replace syntax ($1, $&, ...).
  TextEditResult ReplaceRegex(PdfPage* page, const std::regex& pattern,
                              const std::string& format);
  /// Replaces every match of `pattern` on every page.
  TextEditResult ReplaceRegex(const std::regex& pattern,
                              const std::string& format);
  /// General form: lets `finder` choose the edits for each block on the page.
  TextEditResult Edit(PdfPage* page, const Finder& finder);
  /// General form over every page. A form XObject shared by several pages is
  /// edited once.
  TextEditResult Edit(const Finder& finder);

  /// The text of each `BT … ET` block on the page (form XObjects included), as
  /// the finder sees it.
  std::vector<std::string> GetTextBlocks(PdfPage* page);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_CANVAS_EDITING_PDF_TEXT_EDITOR_H_
