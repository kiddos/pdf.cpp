#include "pdfcpp/kernel/pdf/canvas/editing/pdf_text_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/io/util/text_util.h"
#include "pdfcpp/kernel/font/pdf_font.h"
#include "pdfcpp/kernel/geom/geom.h"
#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"

namespace pdfcpp {
namespace kernel {

namespace {

// Form XObjects nested deeper than this are not edited (guards against
// recursive forms).
constexpr int kMaxFormDepth = 32;
// Gap along the baseline, in ems, above which two pieces of text count as
// separate words.
constexpr float kWordGapEm = 0.15f;
// Offset across the baseline, in ems, above which two pieces of text count as
// separate lines.
constexpr float kLineGapEm = 0.5f;

PdfStringPtr RawString(const Bytes& bytes) {
  static const char* hex = "0123456789ABCDEF";
  Bytes content;
  content.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    content.push_back(static_cast<uint8_t>(hex[b >> 4]));
    content.push_back(static_cast<uint8_t>(hex[b & 15]));
  }
  return std::make_shared<PdfString>(content, true);
}

void WriteNumber(std::string& out, double v) {
  if (std::fabs(v) < 0.0005) v = 0;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3f", v);
  std::string s = buf;
  s.erase(s.find_last_not_of('0') + 1);
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  out += s;
}

void WriteName(std::string& out, const std::string& raw) {
  out += '/';
  for (unsigned char c : raw) {
    if (c < 0x21 || c > 0x7e || c == '#' || io::PdfTokenizer::IsDelimiter(c)) {
      char buf[4];
      std::snprintf(buf, sizeof buf, "#%02X", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
}

void WriteString(std::string& out, const Bytes& bytes, bool hex) {
  if (hex) {
    static const char* digits = "0123456789ABCDEF";
    out += '<';
    for (uint8_t b : bytes) {
      out += digits[b >> 4];
      out += digits[b & 15];
    }
    out += '>';
    return;
  }
  out += '(';
  for (uint8_t b : bytes) {
    if (b == '(' || b == ')' || b == '\\') {
      out += '\\';
      out += static_cast<char>(b);
    } else if (b < 32 || b > 126) {
      char buf[5];
      std::snprintf(buf, sizeof buf, "\\%03o", b);
      out += buf;
    } else {
      out += static_cast<char>(b);
    }
  }
  out += ')';
}

float NumberOperand(const std::vector<PdfObjectPtr>& operands, size_t i) {
  if (i >= operands.size()) return 0;
  auto n = PdfCast<PdfNumber>(operands[i]);
  return n ? n->FloatValue() : 0;
}

// Subset fonts may lack glyphs for codes the font dictionary still maps, and
// Type 3 fonts only define the glyphs they draw, so for these only codes
// already shown somewhere are trusted.
bool RequiresKnownCodes(const PdfDictionaryPtr& font_dict) {
  if (NameEquals(font_dict->GetAsName(PdfName::kSubtype), PdfName::kType3))
    return true;
  auto base_font = font_dict->GetAsName(PdfName::kBaseFont);
  if (!base_font) return false;
  const std::string& name = base_font->GetValue();
  if (name.size() < 7 || name[6] != '+') return false;
  return std::all_of(name.begin(), name.begin() + 6,
                     [](char c) { return c >= 'A' && c <= 'Z'; });
}

struct TextState {
  PdfDictionaryPtr font_dict;
  PdfFont* font = nullptr;
  PdfNamePtr font_name;  // resource name given to Tf; nullptr when the font
                         // came from an ExtGState
  float size = 0;
  float char_spacing = 0;
  float word_spacing = 0;
  float scaling = 1;  // Tz / 100
  float leading = 0;
};

// A run of inserted character codes, shown with the original font or with the
// fallback font.
struct EncodedRun {
  bool fallback = false;
  Bytes bytes;
};

// One character code or one TJ adjustment of a text-showing operator.
struct Item {
  bool is_code = true;
  Bytes code;
  float width = 0;  // glyph space
  bool is_space = false;
  float adjust = 0;  // TJ number
  bool deleted = false;
  std::vector<EncodedRun> before, after;
};

struct ShowOp {
  size_t op = 0;
  TextState state;
  std::vector<Item> items;
  bool hex = false;
  bool modified = false;
};

// A piece of block text: a character code (show >= 0) or a virtual separator
// (show == -1).
struct Span {
  size_t start, end;
  int show;
  int item;
};

struct Block {
  std::string text;
  std::vector<Span> spans;
};

struct ParsedOp {
  size_t start, end;
  std::string oper;
  std::vector<PdfObjectPtr> operands;
};

struct StreamEdit {
  PdfResources* resources = nullptr;
  std::vector<ParsedOp> ops;
  std::vector<ShowOp> shows;
  std::vector<Block> blocks;
};

}  // namespace

struct PdfTextEditor::Impl {
  explicit Impl(PdfDocument& d) : doc(d) {}

  PdfDocument& doc;
  std::shared_ptr<PdfFont> fallback;
  bool preserve_layout = false;
  bool process_forms = true;
  std::map<const PdfObject*, std::set<Bytes>> used_codes;
  std::map<std::pair<const PdfObject*, uint32_t>, std::optional<Bytes>>
      encode_cache;
  std::vector<std::shared_ptr<PdfResources>> owned_resources;

  // state of the current call
  std::set<const PdfObject*> visited;
  const Finder* finder = nullptr;
  std::vector<std::string>* collected = nullptr;
  TextEditResult result;
  int depth = 0;

  void BeginCall(const Finder* f, std::vector<std::string>* c) {
    visited.clear();
    finder = f;
    collected = c;
    result = TextEditResult();
    depth = 0;
  }

  PdfFont* FallbackFont() {
    if (!fallback) fallback = PdfFontFactory::CreateFont();
    return fallback.get();
  }

  // ------------------------------------------------------------ page / stream
  // processing
  void ProcessPage(PdfPage* page) {
    int count = page->GetContentStreamCount();
    if (count == 0) return;
    PdfObjectPtr contents = page->GetPdfObject()->Get(PdfName::kContents);
    if (!contents || !visited.insert(contents.get()).second) return;
    PdfResources* resources = page->GetResources();
    auto new_bytes =
        ProcessStream(page->GetContentBytes(), resources, TextState());
    if (!new_bytes) return;
    if (count == 1) {
      auto stream = page->GetContentStream(0);
      stream->SetData(*new_bytes);
      stream->SetModified();
    } else {
      auto stream = std::make_shared<PdfStream>(*new_bytes);
      stream->MakeIndirect(&doc);
      page->Put(PdfName::kContents, stream);
    }
    page->GetPdfObject()->SetModified();
    resources->GetPdfObject()->SetModified();
  }

  std::optional<Bytes> ProcessStream(const Bytes& content,
                                     PdfResources* resources,
                                     const TextState& initial) {
    StreamEdit se;
    se.resources = resources;
    Scan(se, content, initial);
    if (collected)
      for (const auto& b : se.blocks) collected->push_back(b.text);
    if (!finder) return std::nullopt;
    for (auto& b : se.blocks) {
      if (b.text.empty()) continue;
      ApplyReplacements(se, b, (*finder)(b.text));
    }
    bool modified = std::any_of(se.shows.begin(), se.shows.end(),
                                [](const ShowOp& s) { return s.modified; });
    if (!modified) return std::nullopt;
    Bytes out;
    size_t pos = 0;
    for (const auto& show : se.shows) {
      if (!show.modified) continue;
      const ParsedOp& op = se.ops[show.op];
      out.insert(out.end(), content.begin() + static_cast<std::ptrdiff_t>(pos),
                 content.begin() + static_cast<std::ptrdiff_t>(op.start));
      std::string text = "\n" + Serialize(se, show);
      out.insert(out.end(), text.begin(), text.end());
      pos = op.end;
    }
    out.insert(out.end(), content.begin() + static_cast<std::ptrdiff_t>(pos),
               content.end());
    return out;
  }

  // ------------------------------------------------------------ phase 1: parse
  // and decode
  void Scan(StreamEdit& se, const Bytes& content, const TextState& initial) {
    PdfCanvasParser parser(content, se.resources);
    std::vector<PdfObjectPtr> operands;
    std::vector<TextState> stack;
    TextState state = initial;
    bool in_text = false;
    Matrix tm, tlm;
    // end of the last shown text, for virtual separators
    bool has_last = false;
    float last_x = 0, last_y = 0;

    auto move_line = [&](float tx, float ty) {
      tlm = Matrix(tx, ty).Multiply(tlm);
      tm = tlm;
    };
    auto separator = [&](Block& b) {
      if (!has_last || b.text.empty()) return;
      float dx = tm.Get(Matrix::kI31) - last_x,
            dy = tm.Get(Matrix::kI32) - last_y;
      float ax = tm.Get(Matrix::kI11), ay = tm.Get(Matrix::kI12);
      float len = std::sqrt(ax * ax + ay * ay);
      if (len == 0) return;
      ax /= len;
      ay /= len;
      float along = dx * ax + dy * ay, across = dx * ay - dy * ax;
      float em = std::fabs(state.size) *
                 std::sqrt(tm.Get(Matrix::kI21) * tm.Get(Matrix::kI21) +
                           tm.Get(Matrix::kI22) * tm.Get(Matrix::kI22));
      if (em == 0) em = 1;
      const char* sep = nullptr;
      if (std::fabs(across) > kLineGapEm * em)
        sep = "\n";
      else if (std::fabs(along) > kWordGapEm * em)
        sep = " ";  // a gap, or a jump back over earlier text
      if (!sep) return;
      char last = b.text.back();
      if (last == ' ' || last == '\n' || last == '\t') return;
      size_t start = b.text.size();
      b.text += sep;
      b.spans.push_back({start, b.text.size(), -1, -1});
    };
    auto advance = [&](float tx) { tm = Matrix(tx, 0).Multiply(tm); };

    auto show = [&](size_t op_index,
                    const std::vector<PdfObjectPtr>& elements) {
      if (!in_text || !state.font || se.blocks.empty()) return;
      Block& b = se.blocks.back();
      ShowOp so;
      so.op = op_index;
      so.state = state;
      int show_index = static_cast<int>(se.shows.size());
      auto& used = used_codes[state.font_dict.get()];
      for (const auto& el : elements) {
        if (auto str = PdfCast<PdfString>(el)) {
          if (str->IsHexWriting()) so.hex = true;
          auto codes = state.font->DecodeCodes(*str);
          if (codes.empty()) continue;
          separator(b);
          for (auto& c : codes) {
            Item item;
            item.code = c.code;
            item.width = c.width;
            item.is_space = c.is_space;
            used.insert(c.code);
            size_t start = b.text.size();
            b.text += c.text;
            b.spans.push_back({start, b.text.size(), show_index,
                               static_cast<int>(so.items.size())});
            so.items.push_back(std::move(item));
            advance((c.width / 1000.f * state.size + state.char_spacing +
                     (c.is_space ? state.word_spacing : 0)) *
                    state.scaling);
          }
          has_last = true;
          last_x = tm.Get(Matrix::kI31);
          last_y = tm.Get(Matrix::kI32);
        } else if (auto n = PdfCast<PdfNumber>(el)) {
          Item item;
          item.is_code = false;
          item.adjust = n->FloatValue();
          advance(-item.adjust / 1000.f * state.size * state.scaling);
          so.items.push_back(std::move(item));
        }
      }
      if (dynamic_cast<PdfType0Font*>(state.font)) so.hex = true;
      se.shows.push_back(std::move(so));
    };

    while (parser.Parse(operands)) {
      size_t end = static_cast<size_t>(parser.GetPosition());
      size_t start = se.ops.empty() ? 0 : se.ops.back().end;
      if (operands.empty() || !operands.back()->IsLiteral()) continue;
      ParsedOp op{
          start, end, static_cast<PdfLiteral&>(*operands.back()).ToString(),
          std::vector<PdfObjectPtr>(operands.begin(), operands.end() - 1)};
      se.ops.push_back(op);
      size_t index = se.ops.size() - 1;
      const auto& o = se.ops.back().operands;
      const std::string& name = se.ops.back().oper;

      if (name == "q") {
        stack.push_back(state);
      } else if (name == "Q") {
        if (!stack.empty()) {
          state = stack.back();
          stack.pop_back();
        }
      } else if (name == "Tf") {
        if (o.size() < 2) continue;
        state.size = NumberOperand(o, 1);
        state.font = nullptr;
        state.font_dict = nullptr;
        state.font_name = PdfCast<PdfName>(o[0]);
        auto fonts = se.resources && state.font_name
                         ? se.resources->GetResource(PdfName::kFont)
                         : nullptr;
        auto font_dict =
            fonts ? PdfCast<PdfDictionary>(fonts->Get(state.font_name))
                  : nullptr;
        if (font_dict) {
          state.font_dict = font_dict;
          state.font = doc.GetFont(font_dict);
        }
      } else if (name == "gs") {
        auto gs_name = o.empty() ? nullptr : PdfCast<PdfName>(o[0]);
        auto ext = gs_name && se.resources
                       ? se.resources->GetResource(PdfName::kExtGState)
                       : nullptr;
        auto dict = ext ? ext->GetAsDictionary(gs_name) : nullptr;
        auto font_arr = dict ? dict->GetAsArray(PdfName::kFont) : nullptr;
        if (font_arr) {
          if (auto font_dict = font_arr->GetAsDictionary(0)) {
            state.font_dict = font_dict;
            state.font = doc.GetFont(font_dict);
            state.font_name = nullptr;
          }
          if (auto size = font_arr->GetAsNumber(1))
            state.size = size->FloatValue();
        }
      } else if (name == "Tc") {
        state.char_spacing = NumberOperand(o, 0);
      } else if (name == "Tw") {
        state.word_spacing = NumberOperand(o, 0);
      } else if (name == "Tz") {
        state.scaling = NumberOperand(o, 0) / 100.f;
      } else if (name == "TL") {
        state.leading = NumberOperand(o, 0);
      } else if (name == "BT") {
        in_text = true;
        tm = tlm = Matrix();
        has_last = false;
        se.blocks.emplace_back();
      } else if (name == "ET") {
        in_text = false;
      } else if (name == "Td") {
        move_line(NumberOperand(o, 0), NumberOperand(o, 1));
      } else if (name == "TD") {
        state.leading = -NumberOperand(o, 1);
        move_line(NumberOperand(o, 0), NumberOperand(o, 1));
      } else if (name == "Tm") {
        tlm = Matrix(NumberOperand(o, 0), NumberOperand(o, 1),
                     NumberOperand(o, 2), NumberOperand(o, 3),
                     NumberOperand(o, 4), NumberOperand(o, 5));
        tm = tlm;
      } else if (name == "T*") {
        move_line(0, -state.leading);
      } else if (name == "Tj") {
        show(index, o);
      } else if (name == "TJ") {
        auto arr = o.empty() ? nullptr : PdfCast<PdfArray>(o[0]);
        if (!arr) continue;
        std::vector<PdfObjectPtr> elements;
        for (int i = 0; i < arr->size(); ++i) elements.push_back(arr->Get(i));
        show(index, elements);
      } else if (name == "'") {
        move_line(0, -state.leading);
        show(index, o);
      } else if (name == "\"") {
        state.word_spacing = NumberOperand(o, 0);
        state.char_spacing = NumberOperand(o, 1);
        move_line(0, -state.leading);
        if (o.size() >= 3) show(index, {o[2]});
      } else if (name == "Do") {
        ProcessForm(se, o, state);
      }
    }
  }

  void ProcessForm(StreamEdit& se, const std::vector<PdfObjectPtr>& o,
                   const TextState& state) {
    if (!process_forms || !se.resources || o.empty() || depth >= kMaxFormDepth)
      return;
    auto name = PdfCast<PdfName>(o[0]);
    auto xobjects =
        name ? se.resources->GetResource(PdfName::kXObject) : nullptr;
    auto form = xobjects ? xobjects->GetAsStream(name) : nullptr;
    if (!form ||
        !NameEquals(form->GetAsName(PdfName::kSubtype), PdfName::kForm))
      return;
    if (!visited.insert(form.get()).second) return;
    PdfResources* resources = se.resources;
    if (auto res_dict = form->GetAsDictionary(PdfName::kResources)) {
      owned_resources.push_back(std::make_shared<PdfResources>(res_dict));
      resources = owned_resources.back().get();
    }
    ++depth;
    std::optional<Bytes> new_bytes;
    try {
      new_bytes = ProcessStream(form->GetBytes(), resources, state);
    } catch (...) {
      --depth;
      throw;
    }
    --depth;
    if (new_bytes) {
      form->SetData(*new_bytes);
      form->SetModified();
      resources->GetPdfObject()->SetModified();
    }
  }

  // ------------------------------------------------------------ phase 2: apply
  // edits
  std::optional<Bytes> EncodeWithFont(const TextState& state, uint32_t cp) {
    auto key = std::make_pair(
        static_cast<const PdfObject*>(state.font_dict.get()), cp);
    auto it = encode_cache.find(key);
    if (it == encode_cache.end()) {
      std::optional<Bytes> found;
      const io::Glyph* glyph = state.font->GetGlyph(static_cast<int>(cp));
      int code = glyph ? glyph->GetCode() : -1;
      if (code >= 0) {
        std::string expected = io::TextUtil::CodePointToUtf8(cp);
        std::vector<int> lengths = dynamic_cast<PdfType0Font*>(state.font)
                                       ? std::vector<int>{2, 1, 3, 4}
                                       : std::vector<int>{1};
        for (int len : lengths) {
          if (len < 4 && code >= (1 << (8 * len))) continue;
          Bytes bytes(static_cast<size_t>(len));
          for (int k = 0; k < len; ++k)
            bytes[static_cast<size_t>(k)] =
                static_cast<uint8_t>(code >> (8 * (len - 1 - k)));
          auto decoded = state.font->DecodeCodes(*RawString(bytes));
          if (decoded.size() == 1 && decoded[0].code == bytes &&
              decoded[0].text == expected) {
            found = bytes;
            break;
          }
        }
      }
      it = encode_cache.emplace(key, found).first;
    }
    if (!it->second) return std::nullopt;
    if (RequiresKnownCodes(state.font_dict) &&
        !used_codes[state.font_dict.get()].count(*it->second))
      return std::nullopt;
    return it->second;
  }

  std::optional<Bytes> EncodeWithFallback(uint32_t cp) {
    PdfFont* font = FallbackFont();
    if (!font->ContainsGlyph(static_cast<int>(cp))) return std::nullopt;
    Bytes bytes = font->ConvertToBytes(io::TextUtil::CodePointToUtf8(cp));
    if (bytes.empty()) return std::nullopt;
    return bytes;
  }

  std::optional<std::vector<EncodedRun>> Encode(const TextState& state,
                                                const std::string& text,
                                                bool& used_fallback) {
    std::vector<EncodedRun> runs;
    used_fallback = false;
    for (uint32_t cp : io::TextUtil::Utf8ToCodePoints(text)) {
      bool fb = false;
      std::optional<Bytes> bytes = EncodeWithFont(state, cp);
      if (!bytes) {
        bytes = EncodeWithFallback(cp);
        fb = true;
      }
      if (!bytes) return std::nullopt;
      used_fallback = used_fallback || fb;
      if (runs.empty() || runs.back().fallback != fb)
        runs.push_back({fb, Bytes()});
      runs.back().bytes.insert(runs.back().bytes.end(), bytes->begin(),
                               bytes->end());
    }
    return runs;
  }

  void ApplyReplacements(StreamEdit& se, Block& b,
                         std::vector<Replacement> reps) {
    std::stable_sort(reps.begin(), reps.end(),
                     [](const Replacement& x, const Replacement& y) {
                       return x.start < y.start;
                     });
    size_t consumed = 0;
    bool any = false;
    for (const auto& rep : reps) {
      if (rep.start > rep.end || rep.end > b.text.size()) continue;
      if (any && rep.start < consumed) continue;  // overlaps the previous edit
      any = true;
      consumed = rep.end;
      ApplyReplacement(se, b, rep);
    }
  }

  void ApplyReplacement(StreamEdit& se, Block& b, const Replacement& rep) {
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(b.spans.size()); ++i) {
      const Span& sp = b.spans[static_cast<size_t>(i)];
      if (sp.show < 0) continue;
      bool covered = sp.start == sp.end
                         ? (sp.start >= rep.start && sp.start < rep.end)
                         : (sp.start < rep.end && sp.end > rep.start);
      if (!covered) continue;
      if (first < 0) first = i;
      last = i;
    }
    std::string text = rep.text;
    int anchor_span = -1;
    bool insert_before = true;
    if (first >= 0) {
      const Span& f = b.spans[static_cast<size_t>(first)];
      const Span& l = b.spans[static_cast<size_t>(last)];
      if (f.start < rep.start)
        text = b.text.substr(f.start, rep.start - f.start) + text;
      if (l.end > rep.end) text += b.text.substr(rep.end, l.end - rep.end);
      anchor_span = first;
    } else {
      // Only virtual separators matched: insert after the preceding glyph, else
      // before the following one.
      for (int i = static_cast<int>(b.spans.size()) - 1;
           i >= 0 && anchor_span < 0; --i) {
        const Span& sp = b.spans[static_cast<size_t>(i)];
        if (sp.show >= 0 && sp.end <= rep.start) {
          anchor_span = i;
          insert_before = false;
        }
      }
      for (int i = 0; i < static_cast<int>(b.spans.size()) && anchor_span < 0;
           ++i) {
        const Span& sp = b.spans[static_cast<size_t>(i)];
        if (sp.show >= 0 && sp.start >= rep.end) anchor_span = i;
      }
    }
    if (anchor_span < 0) {
      ++result.skipped;
      return;
    }
    // A glyph shared with an earlier match (e.g. a ligature) cannot be edited
    // twice.
    for (int i = std::max(first, 0); first >= 0 && i <= last; ++i) {
      const Span& sp = b.spans[static_cast<size_t>(i)];
      if (sp.show >= 0 && se.shows[static_cast<size_t>(sp.show)]
                              .items[static_cast<size_t>(sp.item)]
                              .deleted) {
        ++result.skipped;
        return;
      }
    }
    const Span& anchor = b.spans[static_cast<size_t>(anchor_span)];
    ShowOp& anchor_show = se.shows[static_cast<size_t>(anchor.show)];
    bool used_fallback = false;
    auto runs = Encode(anchor_show.state, text, used_fallback);
    if (!runs) {
      ++result.skipped;
      return;
    }
    // delete the matched codes; without preserve_layout also drop the TJ
    // adjustments between them
    std::map<int, std::pair<int, int>>
        deleted_range;  // show -> [first item, last item]
    for (int i = std::max(first, 0); first >= 0 && i <= last; ++i) {
      const Span& sp = b.spans[static_cast<size_t>(i)];
      if (sp.show < 0) continue;
      ShowOp& show = se.shows[static_cast<size_t>(sp.show)];
      show.items[static_cast<size_t>(sp.item)].deleted = true;
      show.modified = true;
      auto ins =
          deleted_range.emplace(sp.show, std::make_pair(sp.item, sp.item));
      if (!ins.second) {
        ins.first->second.first = std::min(ins.first->second.first, sp.item);
        ins.first->second.second = std::max(ins.first->second.second, sp.item);
      }
    }
    if (!preserve_layout) {
      for (const auto& kv : deleted_range) {
        auto& items = se.shows[static_cast<size_t>(kv.first)].items;
        for (int k = kv.second.first; k <= kv.second.second; ++k)
          if (!items[static_cast<size_t>(k)].is_code)
            items[static_cast<size_t>(k)].deleted = true;
      }
    }
    Item& anchor_item = anchor_show.items[static_cast<size_t>(anchor.item)];
    auto& target = insert_before ? anchor_item.before : anchor_item.after;
    target.insert(target.end(), runs->begin(), runs->end());
    anchor_show.modified = true;
    ++result.replaced;
    if (used_fallback) ++result.used_fallback_font;
  }

  // ------------------------------------------------------------ serialization
  PdfNamePtr FontResourceName(PdfResources* resources, const TextState& state) {
    auto fonts = resources->GetResource(PdfName::kFont);
    if (state.font_name && fonts &&
        fonts->Get(state.font_name) == state.font_dict)
      return state.font_name;
    PdfNamePtr name =
        resources->AddResource(state.font_dict, PdfName::kFont, "F");
    resources->GetPdfObject()->SetModified();
    return name;
  }

  // Displacement of `bytes` expressed in TJ units (thousandths of a text space
  // unit at the current font size).
  float TjDisplacement(PdfFont* font, const TextState& state,
                       const Bytes& bytes) {
    if (state.size == 0) return 0;
    float total = 0;
    for (const auto& c : font->DecodeCodes(*RawString(bytes)))
      total += c.width +
               (state.char_spacing + (c.is_space ? state.word_spacing : 0)) *
                   1000.f / state.size;
    return total;
  }

  std::string Serialize(StreamEdit& se, const ShowOp& show) {
    const ParsedOp& op = se.ops[show.op];
    const TextState& state = show.state;
    std::string out;
    if (op.oper == "'") {
      out += "T*\n";
    } else if (op.oper == "\"") {
      WriteNumber(out, NumberOperand(op.operands, 0));
      out += " Tw ";
      WriteNumber(out, NumberOperand(op.operands, 1));
      out += " Tc T*\n";
    }

    struct Piece {
      bool fallback;
      bool is_number;
      Bytes bytes;
      float number;
    };
    std::vector<Piece> pieces;
    auto add_codes = [&](bool fb, const Bytes& bytes) {
      if (bytes.empty()) return;
      if (!pieces.empty() && !pieces.back().is_number &&
          pieces.back().fallback == fb)
        pieces.back().bytes.insert(pieces.back().bytes.end(), bytes.begin(),
                                   bytes.end());
      else
        pieces.push_back({fb, false, bytes, 0});
    };
    auto add_number = [&](float n) {
      if (n == 0) return;
      bool fb = !pieces.empty() && pieces.back().fallback;
      if (!pieces.empty() && pieces.back().is_number)
        pieces.back().number += n;
      else
        pieces.push_back({fb, true, Bytes(), n});
    };
    auto add_runs = [&](const std::vector<EncodedRun>& runs) {
      for (const auto& r : runs) {
        add_codes(r.fallback, r.bytes);
        if (preserve_layout)
          add_number(TjDisplacement(r.fallback ? FallbackFont() : state.font,
                                    state, r.bytes));
      }
    };
    for (const auto& item : show.items) {
      add_runs(item.before);
      if (item.is_code) {
        if (!item.deleted)
          add_codes(false, item.code);
        else if (preserve_layout && state.size != 0)
          add_number(-(item.width + (state.char_spacing +
                                     (item.is_space ? state.word_spacing : 0)) *
                                        1000.f / state.size));
      } else if (!item.deleted) {
        add_number(item.adjust);
      }
      add_runs(item.after);
    }

    PdfNamePtr fallback_name;
    PdfNamePtr original_name;
    size_t i = 0;
    bool first_segment = true;
    while (i < pieces.size()) {
      bool fb = pieces[i].fallback;
      size_t j = i;
      while (j < pieces.size() && pieces[j].fallback == fb) ++j;
      if (!first_segment) out += "\n";
      first_segment = false;
      if (fb) {
        if (!fallback_name) {
          fallback_name = se.resources->AddFont(&doc, FallbackFont());
          se.resources->GetPdfObject()->SetModified();
        }
        WriteName(out, fallback_name->GetValue());
        out += ' ';
        WriteNumber(out, state.size);
        out += " Tf ";
      }
      bool fb_hex =
          fb && dynamic_cast<PdfType0Font*>(FallbackFont()) != nullptr;
      bool hex = fb ? fb_hex : show.hex;
      if (j - i == 1 && !pieces[i].is_number) {
        WriteString(out, pieces[i].bytes, hex);
        out += " Tj";
      } else {
        out += '[';
        for (size_t k = i; k < j; ++k) {
          if (k > i) out += ' ';
          if (pieces[k].is_number)
            WriteNumber(out, pieces[k].number);
          else
            WriteString(out, pieces[k].bytes, hex);
        }
        out += "] TJ";
      }
      if (fb) {
        if (!original_name)
          original_name = FontResourceName(se.resources, state);
        out += ' ';
        WriteName(out, original_name->GetValue());
        out += ' ';
        WriteNumber(out, state.size);
        out += " Tf";
      }
      i = j;
    }
    return out;
  }
};

// ============================================================ PdfTextEditor
PdfTextEditor::PdfTextEditor(PdfDocument& document)
    : impl_(std::make_unique<Impl>(document)) {}

PdfTextEditor::~PdfTextEditor() = default;

PdfTextEditor& PdfTextEditor::SetFallbackFont(std::shared_ptr<PdfFont> font) {
  impl_->fallback = std::move(font);
  return *this;
}

PdfTextEditor& PdfTextEditor::SetPreserveLayout(bool preserve_layout) {
  impl_->preserve_layout = preserve_layout;
  return *this;
}

PdfTextEditor& PdfTextEditor::SetProcessFormXObjects(
    bool process_form_x_objects) {
  impl_->process_forms = process_form_x_objects;
  return *this;
}

static PdfTextEditor::Finder LiteralFinder(const std::string& search,
                                           const std::string& replacement) {
  return [search, replacement](const std::string& text) {
    std::vector<PdfTextEditor::Replacement> reps;
    if (search.empty()) return reps;
    for (size_t pos = text.find(search); pos != std::string::npos;
         pos = text.find(search, pos + search.size()))
      reps.push_back({pos, pos + search.size(), replacement});
    return reps;
  };
}

static PdfTextEditor::Finder RegexFinder(const std::regex& pattern,
                                         const std::string& format) {
  return [pattern, format](const std::string& text) {
    std::vector<PdfTextEditor::Replacement> reps;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
         it != std::sregex_iterator(); ++it) {
      const std::smatch& m = *it;
      if (m.length(0) == 0) continue;
      size_t start = static_cast<size_t>(m.position(0));
      reps.push_back(
          {start, start + static_cast<size_t>(m.length(0)), m.format(format)});
    }
    return reps;
  };
}

TextEditResult PdfTextEditor::ReplaceText(PdfPage* page,
                                          const std::string& search,
                                          const std::string& replacement) {
  return Edit(page, LiteralFinder(search, replacement));
}

TextEditResult PdfTextEditor::ReplaceText(const std::string& search,
                                          const std::string& replacement) {
  return Edit(LiteralFinder(search, replacement));
}

TextEditResult PdfTextEditor::ReplaceRegex(PdfPage* page,
                                           const std::regex& pattern,
                                           const std::string& format) {
  return Edit(page, RegexFinder(pattern, format));
}

TextEditResult PdfTextEditor::ReplaceRegex(const std::regex& pattern,
                                           const std::string& format) {
  return Edit(RegexFinder(pattern, format));
}

TextEditResult PdfTextEditor::Edit(PdfPage* page, const Finder& finder) {
  impl_->BeginCall(&finder, nullptr);
  impl_->ProcessPage(page);
  return impl_->result;
}

TextEditResult PdfTextEditor::Edit(const Finder& finder) {
  impl_->BeginCall(&finder, nullptr);
  PdfDocument& doc = impl_->doc;
  for (int i = 1; i <= doc.GetNumberOfPages(); ++i)
    impl_->ProcessPage(doc.GetPage(i));
  return impl_->result;
}

std::vector<std::string> PdfTextEditor::GetTextBlocks(PdfPage* page) {
  std::vector<std::string> blocks;
  impl_->BeginCall(nullptr, &blocks);
  impl_->ProcessPage(page);
  return blocks;
}

}  // namespace kernel
}  // namespace pdfcpp
