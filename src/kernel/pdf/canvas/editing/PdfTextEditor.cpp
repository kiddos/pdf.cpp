#include "pdfcpp/kernel/pdf/canvas/editing/PdfTextEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/io/util/TextUtil.h"
#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/geom/Geom.h"
#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"

namespace pdfcpp {
namespace kernel {

namespace {

// Form XObjects nested deeper than this are not edited (guards against recursive forms).
constexpr int MAX_FORM_DEPTH = 32;
// Gap along the baseline, in ems, above which two pieces of text count as separate words.
constexpr float WORD_GAP_EM = 0.15f;
// Offset across the baseline, in ems, above which two pieces of text count as separate lines.
constexpr float LINE_GAP_EM = 0.5f;

PdfStringPtr rawString(const Bytes& bytes) {
    static const char* hex = "0123456789ABCDEF";
    Bytes content;
    content.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        content.push_back(static_cast<uint8_t>(hex[b >> 4]));
        content.push_back(static_cast<uint8_t>(hex[b & 15]));
    }
    return std::make_shared<PdfString>(content, true);
}

void writeNumber(std::string& out, double v) {
    if (std::fabs(v) < 0.0005) v = 0;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    out += s;
}

void writeName(std::string& out, const std::string& raw) {
    out += '/';
    for (unsigned char c : raw) {
        if (c < 0x21 || c > 0x7e || c == '#' || io::PdfTokenizer::isDelimiter(c)) {
            char buf[4];
            std::snprintf(buf, sizeof buf, "#%02X", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
}

void writeString(std::string& out, const Bytes& bytes, bool hex) {
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

float numberOperand(const std::vector<PdfObjectPtr>& operands, size_t i) {
    if (i >= operands.size()) return 0;
    auto n = pdf_cast<PdfNumber>(operands[i]);
    return n ? n->floatValue() : 0;
}

// Subset fonts may lack glyphs for codes the font dictionary still maps, and Type 3 fonts only define the glyphs
// they draw, so for these only codes already shown somewhere are trusted.
bool requiresKnownCodes(const PdfDictionaryPtr& fontDict) {
    if (nameEquals(fontDict->getAsName(PdfName::Subtype), PdfName::Type3)) return true;
    auto baseFont = fontDict->getAsName(PdfName::BaseFont);
    if (!baseFont) return false;
    const std::string& name = baseFont->getValue();
    if (name.size() < 7 || name[6] != '+') return false;
    return std::all_of(name.begin(), name.begin() + 6, [](char c) { return c >= 'A' && c <= 'Z'; });
}

struct TextState {
    PdfDictionaryPtr fontDict;
    PdfFont* font = nullptr;
    PdfNamePtr fontName;  // resource name given to Tf; nullptr when the font came from an ExtGState
    float size = 0;
    float charSpacing = 0;
    float wordSpacing = 0;
    float scaling = 1;  // Tz / 100
    float leading = 0;
};

// A run of inserted character codes, shown with the original font or with the fallback font.
struct EncodedRun {
    bool fallback = false;
    Bytes bytes;
};

// One character code or one TJ adjustment of a text-showing operator.
struct Item {
    bool isCode = true;
    Bytes code;
    float width = 0;  // glyph space
    bool isSpace = false;
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

// A piece of block text: a character code (show >= 0) or a virtual separator (show == -1).
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
    bool preserveLayout = false;
    bool processForms = true;
    std::map<const PdfObject*, std::set<Bytes>> usedCodes;
    std::map<std::pair<const PdfObject*, uint32_t>, std::optional<Bytes>> encodeCache;
    std::vector<std::shared_ptr<PdfResources>> ownedResources;

    // state of the current call
    std::set<const PdfObject*> visited;
    const Finder* finder = nullptr;
    std::vector<std::string>* collected = nullptr;
    TextEditResult result;
    int depth = 0;

    void beginCall(const Finder* f, std::vector<std::string>* c) {
        visited.clear();
        finder = f;
        collected = c;
        result = TextEditResult();
        depth = 0;
    }

    PdfFont* fallbackFont() {
        if (!fallback) fallback = PdfFontFactory::createFont();
        return fallback.get();
    }

    // ------------------------------------------------------------ page / stream processing
    void processPage(PdfPage* page) {
        int count = page->getContentStreamCount();
        if (count == 0) return;
        PdfObjectPtr contents = page->getPdfObject()->get(PdfName::Contents);
        if (!contents || !visited.insert(contents.get()).second) return;
        PdfResources* resources = page->getResources();
        auto newBytes = processStream(page->getContentBytes(), resources, TextState());
        if (!newBytes) return;
        if (count == 1) {
            auto stream = page->getContentStream(0);
            stream->setData(*newBytes);
            stream->setModified();
        } else {
            auto stream = std::make_shared<PdfStream>(*newBytes);
            stream->makeIndirect(&doc);
            page->put(PdfName::Contents, stream);
        }
        page->getPdfObject()->setModified();
        resources->getPdfObject()->setModified();
    }

    std::optional<Bytes> processStream(const Bytes& content, PdfResources* resources, const TextState& initial) {
        StreamEdit se;
        se.resources = resources;
        scan(se, content, initial);
        if (collected)
            for (const auto& b : se.blocks) collected->push_back(b.text);
        if (!finder) return std::nullopt;
        for (auto& b : se.blocks) {
            if (b.text.empty()) continue;
            applyReplacements(se, b, (*finder)(b.text));
        }
        bool modified = std::any_of(se.shows.begin(), se.shows.end(), [](const ShowOp& s) { return s.modified; });
        if (!modified) return std::nullopt;
        Bytes out;
        size_t pos = 0;
        for (const auto& show : se.shows) {
            if (!show.modified) continue;
            const ParsedOp& op = se.ops[show.op];
            out.insert(out.end(), content.begin() + static_cast<std::ptrdiff_t>(pos),
                       content.begin() + static_cast<std::ptrdiff_t>(op.start));
            std::string text = "\n" + serialize(se, show);
            out.insert(out.end(), text.begin(), text.end());
            pos = op.end;
        }
        out.insert(out.end(), content.begin() + static_cast<std::ptrdiff_t>(pos), content.end());
        return out;
    }

    // ------------------------------------------------------------ phase 1: parse and decode
    void scan(StreamEdit& se, const Bytes& content, const TextState& initial) {
        PdfCanvasParser parser(content, se.resources);
        std::vector<PdfObjectPtr> operands;
        std::vector<TextState> stack;
        TextState state = initial;
        bool inText = false;
        Matrix tm, tlm;
        // end of the last shown text, for virtual separators
        bool hasLast = false;
        float lastX = 0, lastY = 0;

        auto moveLine = [&](float tx, float ty) {
            tlm = Matrix(tx, ty).multiply(tlm);
            tm = tlm;
        };
        auto separator = [&](Block& b) {
            if (!hasLast || b.text.empty()) return;
            float dx = tm.get(Matrix::I31) - lastX, dy = tm.get(Matrix::I32) - lastY;
            float ax = tm.get(Matrix::I11), ay = tm.get(Matrix::I12);
            float len = std::sqrt(ax * ax + ay * ay);
            if (len == 0) return;
            ax /= len;
            ay /= len;
            float along = dx * ax + dy * ay, across = dx * ay - dy * ax;
            float em = std::fabs(state.size) * std::sqrt(tm.get(Matrix::I21) * tm.get(Matrix::I21) + tm.get(Matrix::I22) * tm.get(Matrix::I22));
            if (em == 0) em = 1;
            const char* sep = nullptr;
            if (std::fabs(across) > LINE_GAP_EM * em) sep = "\n";
            else if (std::fabs(along) > WORD_GAP_EM * em) sep = " ";  // a gap, or a jump back over earlier text
            if (!sep) return;
            char last = b.text.back();
            if (last == ' ' || last == '\n' || last == '\t') return;
            size_t start = b.text.size();
            b.text += sep;
            b.spans.push_back({start, b.text.size(), -1, -1});
        };
        auto advance = [&](float tx) { tm = Matrix(tx, 0).multiply(tm); };

        auto show = [&](size_t opIndex, const std::vector<PdfObjectPtr>& elements) {
            if (!inText || !state.font || se.blocks.empty()) return;
            Block& b = se.blocks.back();
            ShowOp so;
            so.op = opIndex;
            so.state = state;
            int showIndex = static_cast<int>(se.shows.size());
            auto& used = usedCodes[state.fontDict.get()];
            for (const auto& el : elements) {
                if (auto str = pdf_cast<PdfString>(el)) {
                    if (str->isHexWriting()) so.hex = true;
                    auto codes = state.font->decodeCodes(*str);
                    if (codes.empty()) continue;
                    separator(b);
                    for (auto& c : codes) {
                        Item item;
                        item.code = c.code;
                        item.width = c.width;
                        item.isSpace = c.isSpace;
                        used.insert(c.code);
                        size_t start = b.text.size();
                        b.text += c.text;
                        b.spans.push_back({start, b.text.size(), showIndex, static_cast<int>(so.items.size())});
                        so.items.push_back(std::move(item));
                        advance((c.width / 1000.f * state.size + state.charSpacing + (c.isSpace ? state.wordSpacing : 0)) *
                                state.scaling);
                    }
                    hasLast = true;
                    lastX = tm.get(Matrix::I31);
                    lastY = tm.get(Matrix::I32);
                } else if (auto n = pdf_cast<PdfNumber>(el)) {
                    Item item;
                    item.isCode = false;
                    item.adjust = n->floatValue();
                    advance(-item.adjust / 1000.f * state.size * state.scaling);
                    so.items.push_back(std::move(item));
                }
            }
            if (dynamic_cast<PdfType0Font*>(state.font)) so.hex = true;
            se.shows.push_back(std::move(so));
        };

        while (parser.parse(operands)) {
            size_t end = static_cast<size_t>(parser.getPosition());
            size_t start = se.ops.empty() ? 0 : se.ops.back().end;
            if (operands.empty() || !operands.back()->isLiteral()) continue;
            ParsedOp op{start, end, static_cast<PdfLiteral&>(*operands.back()).toString(),
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
                state.size = numberOperand(o, 1);
                state.font = nullptr;
                state.fontDict = nullptr;
                state.fontName = pdf_cast<PdfName>(o[0]);
                auto fonts = se.resources && state.fontName ? se.resources->getResource(PdfName::Font) : nullptr;
                auto fontDict = fonts ? pdf_cast<PdfDictionary>(fonts->get(state.fontName)) : nullptr;
                if (fontDict) {
                    state.fontDict = fontDict;
                    state.font = doc.getFont(fontDict);
                }
            } else if (name == "gs") {
                auto gsName = o.empty() ? nullptr : pdf_cast<PdfName>(o[0]);
                auto ext = gsName && se.resources ? se.resources->getResource(PdfName::ExtGState) : nullptr;
                auto dict = ext ? ext->getAsDictionary(gsName) : nullptr;
                auto fontArr = dict ? dict->getAsArray(PdfName::Font) : nullptr;
                if (fontArr) {
                    if (auto fontDict = fontArr->getAsDictionary(0)) {
                        state.fontDict = fontDict;
                        state.font = doc.getFont(fontDict);
                        state.fontName = nullptr;
                    }
                    if (auto size = fontArr->getAsNumber(1)) state.size = size->floatValue();
                }
            } else if (name == "Tc") {
                state.charSpacing = numberOperand(o, 0);
            } else if (name == "Tw") {
                state.wordSpacing = numberOperand(o, 0);
            } else if (name == "Tz") {
                state.scaling = numberOperand(o, 0) / 100.f;
            } else if (name == "TL") {
                state.leading = numberOperand(o, 0);
            } else if (name == "BT") {
                inText = true;
                tm = tlm = Matrix();
                hasLast = false;
                se.blocks.emplace_back();
            } else if (name == "ET") {
                inText = false;
            } else if (name == "Td") {
                moveLine(numberOperand(o, 0), numberOperand(o, 1));
            } else if (name == "TD") {
                state.leading = -numberOperand(o, 1);
                moveLine(numberOperand(o, 0), numberOperand(o, 1));
            } else if (name == "Tm") {
                tlm = Matrix(numberOperand(o, 0), numberOperand(o, 1), numberOperand(o, 2), numberOperand(o, 3),
                             numberOperand(o, 4), numberOperand(o, 5));
                tm = tlm;
            } else if (name == "T*") {
                moveLine(0, -state.leading);
            } else if (name == "Tj") {
                show(index, o);
            } else if (name == "TJ") {
                auto arr = o.empty() ? nullptr : pdf_cast<PdfArray>(o[0]);
                if (!arr) continue;
                std::vector<PdfObjectPtr> elements;
                for (int i = 0; i < arr->size(); ++i) elements.push_back(arr->get(i));
                show(index, elements);
            } else if (name == "'") {
                moveLine(0, -state.leading);
                show(index, o);
            } else if (name == "\"") {
                state.wordSpacing = numberOperand(o, 0);
                state.charSpacing = numberOperand(o, 1);
                moveLine(0, -state.leading);
                if (o.size() >= 3) show(index, {o[2]});
            } else if (name == "Do") {
                processForm(se, o, state);
            }
        }
    }

    void processForm(StreamEdit& se, const std::vector<PdfObjectPtr>& o, const TextState& state) {
        if (!processForms || !se.resources || o.empty() || depth >= MAX_FORM_DEPTH) return;
        auto name = pdf_cast<PdfName>(o[0]);
        auto xobjects = name ? se.resources->getResource(PdfName::XObject) : nullptr;
        auto form = xobjects ? xobjects->getAsStream(name) : nullptr;
        if (!form || !nameEquals(form->getAsName(PdfName::Subtype), PdfName::Form)) return;
        if (!visited.insert(form.get()).second) return;
        PdfResources* resources = se.resources;
        if (auto resDict = form->getAsDictionary(PdfName::Resources)) {
            ownedResources.push_back(std::make_shared<PdfResources>(resDict));
            resources = ownedResources.back().get();
        }
        ++depth;
        std::optional<Bytes> newBytes;
        try {
            newBytes = processStream(form->getBytes(), resources, state);
        } catch (...) {
            --depth;
            throw;
        }
        --depth;
        if (newBytes) {
            form->setData(*newBytes);
            form->setModified();
            resources->getPdfObject()->setModified();
        }
    }

    // ------------------------------------------------------------ phase 2: apply edits
    std::optional<Bytes> encodeWithFont(const TextState& state, uint32_t cp) {
        auto key = std::make_pair(static_cast<const PdfObject*>(state.fontDict.get()), cp);
        auto it = encodeCache.find(key);
        if (it == encodeCache.end()) {
            std::optional<Bytes> found;
            const io::Glyph* glyph = state.font->getGlyph(static_cast<int>(cp));
            int code = glyph ? glyph->getCode() : -1;
            if (code >= 0) {
                std::string expected = io::TextUtil::codePointToUtf8(cp);
                std::vector<int> lengths = dynamic_cast<PdfType0Font*>(state.font) ? std::vector<int>{2, 1, 3, 4}
                                                                                    : std::vector<int>{1};
                for (int len : lengths) {
                    if (len < 4 && code >= (1 << (8 * len))) continue;
                    Bytes bytes(static_cast<size_t>(len));
                    for (int k = 0; k < len; ++k) bytes[static_cast<size_t>(k)] = static_cast<uint8_t>(code >> (8 * (len - 1 - k)));
                    auto decoded = state.font->decodeCodes(*rawString(bytes));
                    if (decoded.size() == 1 && decoded[0].code == bytes && decoded[0].text == expected) {
                        found = bytes;
                        break;
                    }
                }
            }
            it = encodeCache.emplace(key, found).first;
        }
        if (!it->second) return std::nullopt;
        if (requiresKnownCodes(state.fontDict) && !usedCodes[state.fontDict.get()].count(*it->second)) return std::nullopt;
        return it->second;
    }

    std::optional<Bytes> encodeWithFallback(uint32_t cp) {
        PdfFont* font = fallbackFont();
        if (!font->containsGlyph(static_cast<int>(cp))) return std::nullopt;
        Bytes bytes = font->convertToBytes(io::TextUtil::codePointToUtf8(cp));
        if (bytes.empty()) return std::nullopt;
        return bytes;
    }

    std::optional<std::vector<EncodedRun>> encode(const TextState& state, const std::string& text, bool& usedFallback) {
        std::vector<EncodedRun> runs;
        usedFallback = false;
        for (uint32_t cp : io::TextUtil::utf8ToCodePoints(text)) {
            bool fb = false;
            std::optional<Bytes> bytes = encodeWithFont(state, cp);
            if (!bytes) {
                bytes = encodeWithFallback(cp);
                fb = true;
            }
            if (!bytes) return std::nullopt;
            usedFallback = usedFallback || fb;
            if (runs.empty() || runs.back().fallback != fb) runs.push_back({fb, Bytes()});
            runs.back().bytes.insert(runs.back().bytes.end(), bytes->begin(), bytes->end());
        }
        return runs;
    }

    void applyReplacements(StreamEdit& se, Block& b, std::vector<Replacement> reps) {
        std::stable_sort(reps.begin(), reps.end(), [](const Replacement& x, const Replacement& y) { return x.start < y.start; });
        size_t consumed = 0;
        bool any = false;
        for (const auto& rep : reps) {
            if (rep.start > rep.end || rep.end > b.text.size()) continue;
            if (any && rep.start < consumed) continue;  // overlaps the previous edit
            any = true;
            consumed = rep.end;
            applyReplacement(se, b, rep);
        }
    }

    void applyReplacement(StreamEdit& se, Block& b, const Replacement& rep) {
        int first = -1, last = -1;
        for (int i = 0; i < static_cast<int>(b.spans.size()); ++i) {
            const Span& sp = b.spans[static_cast<size_t>(i)];
            if (sp.show < 0) continue;
            bool covered = sp.start == sp.end ? (sp.start >= rep.start && sp.start < rep.end)
                                              : (sp.start < rep.end && sp.end > rep.start);
            if (!covered) continue;
            if (first < 0) first = i;
            last = i;
        }
        std::string text = rep.text;
        int anchorSpan = -1;
        bool insertBefore = true;
        if (first >= 0) {
            const Span& f = b.spans[static_cast<size_t>(first)];
            const Span& l = b.spans[static_cast<size_t>(last)];
            if (f.start < rep.start) text = b.text.substr(f.start, rep.start - f.start) + text;
            if (l.end > rep.end) text += b.text.substr(rep.end, l.end - rep.end);
            anchorSpan = first;
        } else {
            // Only virtual separators matched: insert after the preceding glyph, else before the following one.
            for (int i = static_cast<int>(b.spans.size()) - 1; i >= 0 && anchorSpan < 0; --i) {
                const Span& sp = b.spans[static_cast<size_t>(i)];
                if (sp.show >= 0 && sp.end <= rep.start) {
                    anchorSpan = i;
                    insertBefore = false;
                }
            }
            for (int i = 0; i < static_cast<int>(b.spans.size()) && anchorSpan < 0; ++i) {
                const Span& sp = b.spans[static_cast<size_t>(i)];
                if (sp.show >= 0 && sp.start >= rep.end) anchorSpan = i;
            }
        }
        if (anchorSpan < 0) {
            ++result.skipped;
            return;
        }
        // A glyph shared with an earlier match (e.g. a ligature) cannot be edited twice.
        for (int i = std::max(first, 0); first >= 0 && i <= last; ++i) {
            const Span& sp = b.spans[static_cast<size_t>(i)];
            if (sp.show >= 0 && se.shows[static_cast<size_t>(sp.show)].items[static_cast<size_t>(sp.item)].deleted) {
                ++result.skipped;
                return;
            }
        }
        const Span& anchor = b.spans[static_cast<size_t>(anchorSpan)];
        ShowOp& anchorShow = se.shows[static_cast<size_t>(anchor.show)];
        bool usedFallback = false;
        auto runs = encode(anchorShow.state, text, usedFallback);
        if (!runs) {
            ++result.skipped;
            return;
        }
        // delete the matched codes; without preserveLayout also drop the TJ adjustments between them
        std::map<int, std::pair<int, int>> deletedRange;  // show -> [first item, last item]
        for (int i = std::max(first, 0); first >= 0 && i <= last; ++i) {
            const Span& sp = b.spans[static_cast<size_t>(i)];
            if (sp.show < 0) continue;
            ShowOp& show = se.shows[static_cast<size_t>(sp.show)];
            show.items[static_cast<size_t>(sp.item)].deleted = true;
            show.modified = true;
            auto ins = deletedRange.emplace(sp.show, std::make_pair(sp.item, sp.item));
            if (!ins.second) {
                ins.first->second.first = std::min(ins.first->second.first, sp.item);
                ins.first->second.second = std::max(ins.first->second.second, sp.item);
            }
        }
        if (!preserveLayout) {
            for (const auto& kv : deletedRange) {
                auto& items = se.shows[static_cast<size_t>(kv.first)].items;
                for (int k = kv.second.first; k <= kv.second.second; ++k)
                    if (!items[static_cast<size_t>(k)].isCode) items[static_cast<size_t>(k)].deleted = true;
            }
        }
        Item& anchorItem = anchorShow.items[static_cast<size_t>(anchor.item)];
        auto& target = insertBefore ? anchorItem.before : anchorItem.after;
        target.insert(target.end(), runs->begin(), runs->end());
        anchorShow.modified = true;
        ++result.replaced;
        if (usedFallback) ++result.usedFallbackFont;
    }

    // ------------------------------------------------------------ serialization
    PdfNamePtr fontResourceName(PdfResources* resources, const TextState& state) {
        auto fonts = resources->getResource(PdfName::Font);
        if (state.fontName && fonts && fonts->get(state.fontName) == state.fontDict) return state.fontName;
        PdfNamePtr name = resources->addResource(state.fontDict, PdfName::Font, "F");
        resources->getPdfObject()->setModified();
        return name;
    }

    // Displacement of `bytes` expressed in TJ units (thousandths of a text space unit at the current font size).
    float tjDisplacement(PdfFont* font, const TextState& state, const Bytes& bytes) {
        if (state.size == 0) return 0;
        float total = 0;
        for (const auto& c : font->decodeCodes(*rawString(bytes)))
            total += c.width + (state.charSpacing + (c.isSpace ? state.wordSpacing : 0)) * 1000.f / state.size;
        return total;
    }

    std::string serialize(StreamEdit& se, const ShowOp& show) {
        const ParsedOp& op = se.ops[show.op];
        const TextState& state = show.state;
        std::string out;
        if (op.oper == "'") {
            out += "T*\n";
        } else if (op.oper == "\"") {
            writeNumber(out, numberOperand(op.operands, 0));
            out += " Tw ";
            writeNumber(out, numberOperand(op.operands, 1));
            out += " Tc T*\n";
        }

        struct Piece {
            bool fallback;
            bool isNumber;
            Bytes bytes;
            float number;
        };
        std::vector<Piece> pieces;
        auto addCodes = [&](bool fb, const Bytes& bytes) {
            if (bytes.empty()) return;
            if (!pieces.empty() && !pieces.back().isNumber && pieces.back().fallback == fb)
                pieces.back().bytes.insert(pieces.back().bytes.end(), bytes.begin(), bytes.end());
            else
                pieces.push_back({fb, false, bytes, 0});
        };
        auto addNumber = [&](float n) {
            if (n == 0) return;
            bool fb = !pieces.empty() && pieces.back().fallback;
            if (!pieces.empty() && pieces.back().isNumber) pieces.back().number += n;
            else pieces.push_back({fb, true, Bytes(), n});
        };
        auto addRuns = [&](const std::vector<EncodedRun>& runs) {
            for (const auto& r : runs) {
                addCodes(r.fallback, r.bytes);
                if (preserveLayout) addNumber(tjDisplacement(r.fallback ? fallbackFont() : state.font, state, r.bytes));
            }
        };
        for (const auto& item : show.items) {
            addRuns(item.before);
            if (item.isCode) {
                if (!item.deleted) addCodes(false, item.code);
                else if (preserveLayout && state.size != 0)
                    addNumber(-(item.width + (state.charSpacing + (item.isSpace ? state.wordSpacing : 0)) * 1000.f / state.size));
            } else if (!item.deleted) {
                addNumber(item.adjust);
            }
            addRuns(item.after);
        }

        PdfNamePtr fallbackName;
        PdfNamePtr originalName;
        size_t i = 0;
        bool firstSegment = true;
        while (i < pieces.size()) {
            bool fb = pieces[i].fallback;
            size_t j = i;
            while (j < pieces.size() && pieces[j].fallback == fb) ++j;
            if (!firstSegment) out += "\n";
            firstSegment = false;
            if (fb) {
                if (!fallbackName) {
                    fallbackName = se.resources->addFont(&doc, fallbackFont());
                    se.resources->getPdfObject()->setModified();
                }
                writeName(out, fallbackName->getValue());
                out += ' ';
                writeNumber(out, state.size);
                out += " Tf ";
            }
            bool fbHex = fb && dynamic_cast<PdfType0Font*>(fallbackFont()) != nullptr;
            bool hex = fb ? fbHex : show.hex;
            if (j - i == 1 && !pieces[i].isNumber) {
                writeString(out, pieces[i].bytes, hex);
                out += " Tj";
            } else {
                out += '[';
                for (size_t k = i; k < j; ++k) {
                    if (k > i) out += ' ';
                    if (pieces[k].isNumber) writeNumber(out, pieces[k].number);
                    else writeString(out, pieces[k].bytes, hex);
                }
                out += "] TJ";
            }
            if (fb) {
                if (!originalName) originalName = fontResourceName(se.resources, state);
                out += ' ';
                writeName(out, originalName->getValue());
                out += ' ';
                writeNumber(out, state.size);
                out += " Tf";
            }
            i = j;
        }
        return out;
    }
};

// ============================================================ PdfTextEditor
PdfTextEditor::PdfTextEditor(PdfDocument& document) : impl_(std::make_unique<Impl>(document)) {}

PdfTextEditor::~PdfTextEditor() = default;

PdfTextEditor& PdfTextEditor::setFallbackFont(std::shared_ptr<PdfFont> font) {
    impl_->fallback = std::move(font);
    return *this;
}

PdfTextEditor& PdfTextEditor::setPreserveLayout(bool preserveLayout) {
    impl_->preserveLayout = preserveLayout;
    return *this;
}

PdfTextEditor& PdfTextEditor::setProcessFormXObjects(bool processFormXObjects) {
    impl_->processForms = processFormXObjects;
    return *this;
}

static PdfTextEditor::Finder literalFinder(const std::string& search, const std::string& replacement) {
    return [search, replacement](const std::string& text) {
        std::vector<PdfTextEditor::Replacement> reps;
        if (search.empty()) return reps;
        for (size_t pos = text.find(search); pos != std::string::npos; pos = text.find(search, pos + search.size()))
            reps.push_back({pos, pos + search.size(), replacement});
        return reps;
    };
}

static PdfTextEditor::Finder regexFinder(const std::regex& pattern, const std::string& format) {
    return [pattern, format](const std::string& text) {
        std::vector<PdfTextEditor::Replacement> reps;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            if (m.length(0) == 0) continue;
            size_t start = static_cast<size_t>(m.position(0));
            reps.push_back({start, start + static_cast<size_t>(m.length(0)), m.format(format)});
        }
        return reps;
    };
}

TextEditResult PdfTextEditor::replaceText(PdfPage* page, const std::string& search, const std::string& replacement) {
    return edit(page, literalFinder(search, replacement));
}

TextEditResult PdfTextEditor::replaceText(const std::string& search, const std::string& replacement) {
    return edit(literalFinder(search, replacement));
}

TextEditResult PdfTextEditor::replaceRegex(PdfPage* page, const std::regex& pattern, const std::string& format) {
    return edit(page, regexFinder(pattern, format));
}

TextEditResult PdfTextEditor::replaceRegex(const std::regex& pattern, const std::string& format) {
    return edit(regexFinder(pattern, format));
}

TextEditResult PdfTextEditor::edit(PdfPage* page, const Finder& finder) {
    impl_->beginCall(&finder, nullptr);
    impl_->processPage(page);
    return impl_->result;
}

TextEditResult PdfTextEditor::edit(const Finder& finder) {
    impl_->beginCall(&finder, nullptr);
    PdfDocument& doc = impl_->doc;
    for (int i = 1; i <= doc.getNumberOfPages(); ++i) impl_->processPage(doc.getPage(i));
    return impl_->result;
}

std::vector<std::string> PdfTextEditor::getTextBlocks(PdfPage* page) {
    std::vector<std::string> blocks;
    impl_->beginCall(nullptr, &blocks);
    impl_->processPage(page);
    return blocks;
}

}  // namespace kernel
}  // namespace pdfcpp
