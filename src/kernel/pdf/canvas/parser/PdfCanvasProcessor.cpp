#include "pdfcpp/kernel/pdf/canvas/parser/PdfCanvasProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pdfcpp/io/source/PdfTokenizer.h"
#include "pdfcpp/io/source/RandomAccessSource.h"
#include "pdfcpp/io/util/TextUtil.h"
#include "pdfcpp/kernel/font/PdfFont.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ CanvasTag
int CanvasTag::getMcid() const {
    if (!properties_) return -1;
    auto mcid = properties_->getAsNumber(PdfName::MCID);
    return mcid ? mcid->intValue() : -1;
}

std::optional<std::string> CanvasTag::getActualText() const {
    if (!properties_) return std::nullopt;
    auto s = properties_->getAsString(PdfName::ActualText);
    if (!s) return std::nullopt;
    return s->toUnicodeString();
}

std::optional<std::string> CanvasTag::getExpansionText() const {
    if (!properties_) return std::nullopt;
    auto s = properties_->getAsString(PdfName::E);
    if (!s) return std::nullopt;
    return s->toUnicodeString();
}

void ParserGraphicsState::clip(const Path& path, int) {
    // Exact path intersection (the Java port uses a polygon clipper) is not ported; the latest clip wins.
    Path transformed;
    Matrix ctm = getCtm();
    for (const auto& sp : path.getSubpaths()) {
        auto pts = sp.getPiecewiseLinearApproximation();
        if (pts.empty()) continue;
        Subpath out;
        bool first = true;
        Point prev;
        for (const auto& p : pts) {
            Vector v = Vector(static_cast<float>(p.x), static_cast<float>(p.y), 1).cross(ctm);
            Point tp(v.get(0), v.get(1));
            if (first) {
                out.setStartPoint(tp);
                first = false;
            } else {
                out.addSegment(std::make_shared<Line>(prev, tp));
            }
            prev = tp;
        }
        out.setClosed(true);
        transformed.addSubpath(out);
    }
    clippingPath_ = transformed;
}

// ============================================================ TextRenderInfo
TextRenderInfo::TextRenderInfo(PdfStringPtr str, const ParserGraphicsState& gs, const Matrix& textMatrix,
                               const std::vector<CanvasTag>& canvasTagHierarchy)
    : AbstractRenderInfo(gs), string_(std::move(str)), textMatrix_(textMatrix), canvasTagHierarchy_(canvasTagHierarchy) {
    textToUserSpaceTransformMatrix_ = textMatrix.multiply(gs.getCtm());
}

std::string TextRenderInfo::getText() const {
    if (!text_) text_ = gs_.getFont() ? gs_.getFont()->decode(*string_) : std::string();
    return *text_;
}

std::optional<std::string> TextRenderInfo::getActualText() const {
    for (auto it = canvasTagHierarchy_.rbegin(); it != canvasTagHierarchy_.rend(); ++it) {
        auto t = it->getActualText();
        if (t) return t;
    }
    return std::nullopt;
}

std::optional<std::string> TextRenderInfo::getExpansionText() const {
    for (auto it = canvasTagHierarchy_.rbegin(); it != canvasTagHierarchy_.rend(); ++it) {
        auto t = it->getExpansionText();
        if (t) return t;
    }
    return std::nullopt;
}

bool TextRenderInfo::hasMcid(int mcid, bool checkTheTopmostLevelOnly) const {
    if (checkTheTopmostLevelOnly) {
        for (auto it = canvasTagHierarchy_.rbegin(); it != canvasTagHierarchy_.rend(); ++it)
            if (it->hasMcid()) return it->getMcid() == mcid;
        return false;
    }
    for (const auto& tag : canvasTagHierarchy_)
        if (tag.hasMcid() && tag.getMcid() == mcid) return true;
    return false;
}

int TextRenderInfo::getMcid() const {
    for (auto it = canvasTagHierarchy_.rbegin(); it != canvasTagHierarchy_.rend(); ++it)
        if (it->hasMcid()) return it->getMcid();
    return -1;
}

float TextRenderInfo::getRise() const {
    if (gs_.getTextRise() == 0) return 0;
    return convertHeightFromTextSpaceToUserSpace(gs_.getTextRise());
}

float TextRenderInfo::getPdfStringWidth(const PdfString& string, bool) const {
    PdfFont* font = gs_.getFont();
    if (!font) return 0;
    float total = 0;
    float hs = gs_.getHorizontalScaling() / 100.f;
    for (const auto& c : font->decodeCodes(string)) {
        float w = c.width * gs_.getFontSize() / 1000.f;
        total += (w + gs_.getCharSpacing() + (c.isSpace ? gs_.getWordSpacing() : 0)) * hs;
    }
    return total;
}

float TextRenderInfo::getUnscaledWidth() const {
    if (!unscaledWidth_) unscaledWidth_ = getPdfStringWidth(*string_, false);
    return *unscaledWidth_;
}

LineSegment TextRenderInfo::getUnscaledBaselineWithOffset(float yOffset) const {
    std::string unicodeStr = getText();
    bool endsWithSpace = !unicodeStr.empty() && unicodeStr.back() == ' ';
    float corrected = getUnscaledWidth() - (gs_.getCharSpacing() + (endsWithSpace ? gs_.getWordSpacing() : 0)) *
                                               (gs_.getHorizontalScaling() / 100.f);
    return LineSegment(Vector(0, yOffset, 1), Vector(corrected, yOffset, 1));
}

LineSegment TextRenderInfo::getBaseline() const {
    return getUnscaledBaselineWithOffset(0 + gs_.getTextRise()).transformBy(textToUserSpaceTransformMatrix_);
}

LineSegment TextRenderInfo::getUnscaledBaseline() const { return getUnscaledBaselineWithOffset(0 + gs_.getTextRise()); }

static std::pair<float, float> ascentDescent(const ParserGraphicsState& gs) {
    PdfFont* font = gs.getFont();
    float ascent = 800, descent = -200;
    if (font && font->getFontProgram()) {
        ascent = static_cast<float>(font->getFontProgram()->getFontMetrics().getTypoAscender());
        descent = static_cast<float>(font->getFontProgram()->getFontMetrics().getTypoDescender());
    }
    if (descent > 0) descent = -descent;
    float scale = ascent - descent < 700 ? ascent - descent : 1000;
    if (scale == 0) scale = 1000;
    return {ascent / scale * gs.getFontSize(), descent / scale * gs.getFontSize()};
}

LineSegment TextRenderInfo::getAscentLine() const {
    return getUnscaledBaselineWithOffset(ascentDescent(gs_).first + gs_.getTextRise()).transformBy(textToUserSpaceTransformMatrix_);
}

LineSegment TextRenderInfo::getDescentLine() const {
    return getUnscaledBaselineWithOffset(ascentDescent(gs_).second + gs_.getTextRise()).transformBy(textToUserSpaceTransformMatrix_);
}

float TextRenderInfo::convertWidthFromTextSpaceToUserSpace(float width) const {
    LineSegment textSpace(Vector(0, 0, 1), Vector(width, 0, 1));
    return textSpace.transformBy(textToUserSpaceTransformMatrix_).getLength();
}

float TextRenderInfo::convertHeightFromTextSpaceToUserSpace(float height) const {
    LineSegment textSpace(Vector(0, 0, 1), Vector(0, height, 1));
    return textSpace.transformBy(textToUserSpaceTransformMatrix_).getLength();
}

float TextRenderInfo::getUnscaledFontSpaceWidth() const {
    PdfFont* font = gs_.getFont();
    if (!font) return 0;
    float spaceWidth = static_cast<float>(font->getWidth(' '));
    if (spaceWidth == 0) spaceWidth = static_cast<float>(font->getAverageWidth());
    return (spaceWidth * gs_.getFontSize() / 1000.f + gs_.getCharSpacing() + gs_.getWordSpacing()) * gs_.getHorizontalScaling() / 100.f;
}

float TextRenderInfo::getSingleSpaceWidth() const { return convertWidthFromTextSpaceToUserSpace(getUnscaledFontSpaceWidth()); }

std::vector<PdfStringPtr> TextRenderInfo::splitString(const PdfString& string) const {
    std::vector<PdfStringPtr> out;
    if (!gs_.getFont()) return out;
    for (const auto& c : gs_.getFont()->decodeCodes(string)) out.push_back(std::make_shared<PdfString>(c.code));
    return out;
}

std::vector<TextRenderInfo> TextRenderInfo::getCharacterRenderInfos() const {
    std::vector<TextRenderInfo> result;
    float totalWidth = 0;
    for (const auto& str : splitString(*string_)) {
        TextRenderInfo subInfo(str, gs_, Matrix(totalWidth, 0).multiply(textMatrix_), canvasTagHierarchy_);
        totalWidth += subInfo.getUnscaledWidth();
        result.push_back(std::move(subInfo));
    }
    return result;
}

// ============================================================ ImageRenderInfo
ImageRenderInfo::ImageRenderInfo(const std::vector<CanvasTag>& canvasTagHierarchy, const ParserGraphicsState& gs,
                                 const Matrix& ctm, PdfStreamPtr stream, PdfNamePtr resourceName,
                                 PdfDictionaryPtr colorSpaceDictionary, bool isInline)
    : AbstractRenderInfo(gs), canvasTagHierarchy_(canvasTagHierarchy), ctm_(ctm), stream_(std::move(stream)),
      resourceName_(std::move(resourceName)), colorSpaceDictionary_(std::move(colorSpaceDictionary)), isInline_(isInline) {}

int ImageRenderInfo::getMcid() const {
    for (auto it = canvasTagHierarchy_.rbegin(); it != canvasTagHierarchy_.rend(); ++it)
        if (it->hasMcid()) return it->getMcid();
    return -1;
}

// ============================================================ PdfCanvasParser
struct PdfCanvasParser::Impl {
    explicit Impl(const Bytes& content) : tokeniser(io::RandomAccessSourceFactory().createSource(content)) {}
    io::PdfTokenizer tokeniser;
};

PdfCanvasParser::PdfCanvasParser(const Bytes& content, PdfResources* resources)
    : impl_(std::make_unique<Impl>(content)), resources_(resources) {
    (void)resources_;
}

PdfCanvasParser::~PdfCanvasParser() = default;

int64_t PdfCanvasParser::getPosition() const { return impl_->tokeniser.getPosition(); }

bool PdfCanvasParser::nextValidToken() {
    while (impl_->tokeniser.nextToken())
        if (impl_->tokeniser.getTokenType() != io::PdfTokenizer::TokenType::Comment) return true;
    return false;
}

PdfDictionaryPtr PdfCanvasParser::readDictionary() {
    auto dic = std::make_shared<PdfDictionary>();
    using TT = io::PdfTokenizer::TokenType;
    while (true) {
        if (!nextValidToken()) throw PdfException("Unexpected end of file.");
        if (impl_->tokeniser.getTokenType() == TT::EndDic) break;
        if (impl_->tokeniser.getTokenType() != TT::Name)
            impl_->tokeniser.throwError(format(KernelExceptionMessageConstant::DICTIONARY_KEY_IS_NOT_A_NAME, impl_->tokeniser.getStringValue()));
        auto name = std::make_shared<PdfName>(impl_->tokeniser.getByteContent());
        PdfObjectPtr obj = readObject();
        dic->put(name, obj ? obj : PdfObjectPtr(PdfNull::PDF_NULL));
    }
    return dic;
}

PdfArrayPtr PdfCanvasParser::readArray() {
    auto array = std::make_shared<PdfArray>();
    while (true) {
        PdfObjectPtr obj = readObject();
        if (!obj) {
            if (impl_->tokeniser.getTokenType() == io::PdfTokenizer::TokenType::EndArray) break;
            if (impl_->tokeniser.getTokenType() == io::PdfTokenizer::TokenType::EndDic) continue;
            break;
        }
        array->add(obj);
    }
    return array;
}

PdfObjectPtr PdfCanvasParser::readObject() {
    if (!nextValidToken()) return nullptr;
    using TT = io::PdfTokenizer::TokenType;
    auto& tok = impl_->tokeniser;
    switch (tok.getTokenType()) {
        case TT::StartDic:
            return readDictionary();
        case TT::StartArray:
            return readArray();
        case TT::String:
            return std::make_shared<PdfString>(tok.getByteContent(), tok.isHexString());
        case TT::Name:
            return std::make_shared<PdfName>(tok.getByteContent());
        case TT::Number:
            return std::make_shared<PdfNumber>(tok.getByteContent());
        case TT::EndArray:
        case TT::EndDic:
            return nullptr;
        default: {
            std::string v = tok.getStringValue();
            if (v == "true") return PdfBoolean::TRUE_VALUE;
            if (v == "false") return PdfBoolean::FALSE_VALUE;
            if (v == "null") return std::make_shared<PdfNull>();
            return std::make_shared<PdfLiteral>(tok.getByteContent());
        }
    }
}

bool PdfCanvasParser::parse(std::vector<PdfObjectPtr>& ls) {
    ls.clear();
    PdfObjectPtr ob;
    while ((ob = readObject()) != nullptr) {
        ls.push_back(ob);
        if (ob->isLiteral()) {
            // Inline image: BI <dict entries> ID <data> EI
            if (static_cast<PdfLiteral&>(*ob).toString() == "BI") {
                auto dict = std::make_shared<PdfDictionary>();
                auto& tok = impl_->tokeniser;
                while (true) {
                    PdfObjectPtr key = readObject();
                    if (!key) return true;
                    if (key->isLiteral() && static_cast<PdfLiteral&>(*key).toString() == "ID") break;
                    if (!key->isName()) continue;
                    PdfObjectPtr value = readObject();
                    if (!value) break;
                    dict->put(std::static_pointer_cast<PdfName>(key), value);
                }
                // skip the single whitespace after ID
                int ch = tok.read();
                if (!io::PdfTokenizer::isWhitespace(ch)) tok.backOnePosition(ch);
                Bytes data;
                // find EI surrounded by whitespace
                int prev2 = -1, prev1 = -1;
                while (true) {
                    int c = tok.read();
                    if (c == -1) break;
                    data.push_back(static_cast<uint8_t>(c));
                    size_t n = data.size();
                    if (n >= 3 && data[n - 2] == 'E' && data[n - 1] == 'I' && io::PdfTokenizer::isWhitespace(data[n - 3])) {
                        int next = tok.peek();
                        if (next == -1 || io::PdfTokenizer::isWhitespace(next) || io::PdfTokenizer::isDelimiter(next)) {
                            data.resize(n - 3);
                            break;
                        }
                    }
                    prev2 = prev1;
                    prev1 = c;
                }
                (void)prev2;
                auto stream = std::make_shared<PdfStream>(data);
                // expand abbreviated keys
                static const std::pair<const char*, const char*> abbrev[] = {
                    {"BPC", "BitsPerComponent"}, {"CS", "ColorSpace"}, {"D", "Decode"}, {"DP", "DecodeParms"},
                    {"F", "Filter"}, {"H", "Height"}, {"W", "Width"}, {"I", "Interpolate"}, {"IM", "ImageMask"}};
                for (const auto& kv : dict->entrySet()) {
                    std::string key = kv.first->getValue();
                    for (const auto& a : abbrev)
                        if (key == a.first) key = a.second;
                    PdfObjectPtr value = kv.second;
                    if (value->isName()) {
                        static const std::pair<const char*, const char*> valAbbrev[] = {
                            {"G", "DeviceGray"}, {"RGB", "DeviceRGB"}, {"CMYK", "DeviceCMYK"}, {"I", "Indexed"},
                            {"AHx", "ASCIIHexDecode"}, {"A85", "ASCII85Decode"}, {"LZW", "LZWDecode"},
                            {"Fl", "FlateDecode"}, {"RL", "RunLengthDecode"}, {"CCF", "CCITTFaxDecode"}, {"DCT", "DCTDecode"}};
                        std::string v = static_cast<PdfName&>(*value).getValue();
                        for (const auto& a : valAbbrev)
                            if (v == a.first) value = PdfName::of(a.second);
                    }
                    stream->put(PdfName::of(key), value);
                }
                stream->put(PdfName::Type, PdfName::XObject);
                stream->put(PdfName::Subtype, PdfName::Image);
                ls.clear();
                ls.push_back(stream);
                ls.push_back(ob);
            }
            break;
        }
    }
    return !ls.empty();
}

// ============================================================ operators
namespace {
class LambdaOperator : public IContentOperator {
public:
    using Fn = std::function<void(PdfCanvasProcessor&, const PdfLiteral&, std::vector<PdfObjectPtr>&)>;
    explicit LambdaOperator(Fn fn) : fn_(std::move(fn)) {}
    void invoke(PdfCanvasProcessor& p, const PdfLiteral& oper, std::vector<PdfObjectPtr>& operands) override {
        fn_(p, oper, operands);
    }

private:
    Fn fn_;
};

float num(const std::vector<PdfObjectPtr>& ops, size_t i) {
    if (i >= ops.size() || !ops[i]->isNumber()) return 0;
    return static_cast<PdfNumber&>(*ops[i]).floatValue();
}

std::vector<float> nums(const std::vector<PdfObjectPtr>& ops) {
    std::vector<float> v;
    for (size_t i = 0; i + 1 < ops.size(); ++i)
        if (ops[i]->isNumber()) v.push_back(static_cast<PdfNumber&>(*ops[i]).floatValue());
    return v;
}
}  // namespace

PdfCanvasProcessor::PdfCanvasProcessor(IEventListener& eventListener) : eventListener_(eventListener) {
    supportedEvents_ = eventListener.getSupportedEvents();
    populateOperators();
    reset();
}

PdfCanvasProcessor::~PdfCanvasProcessor() = default;

std::shared_ptr<IContentOperator> PdfCanvasProcessor::registerContentOperator(const std::string& operatorString,
                                                                              std::shared_ptr<IContentOperator> op) {
    auto it = operators_.find(operatorString);
    std::shared_ptr<IContentOperator> old = it == operators_.end() ? nullptr : it->second;
    operators_[operatorString] = std::move(op);
    return old;
}

std::vector<std::string> PdfCanvasProcessor::getRegisteredContentOperatorStrings() const {
    std::vector<std::string> v;
    for (const auto& kv : operators_) v.push_back(kv.first);
    return v;
}

void PdfCanvasProcessor::reset() {
    gsStack_.clear();
    gsStack_.emplace_back();
    textMatrix_ = Matrix();
    textLineMatrix_ = Matrix();
    resourcesStack_.clear();
    ownedResources_.clear();
    isClip_ = false;
    currentPath_ = Path();
    markedContentStack_.clear();
}

void PdfCanvasProcessor::eventOccurred(const IEventData& data, EventType type) {
    if (supportedEvents_.empty() || supportedEvents_.count(type)) eventListener_.eventOccurred(data, type);
}

void PdfCanvasProcessor::populateOperators() {
    auto reg = [this](const std::string& name, LambdaOperator::Fn fn) {
        operators_[name] = std::make_shared<LambdaOperator>(std::move(fn));
    };
    using Ops = std::vector<PdfObjectPtr>;
    // graphics state
    reg("q", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.gsStack_.push_back(p.gsStack_.back()); });
    reg("Q", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
        if (p.gsStack_.size() > 1) p.gsStack_.pop_back();
        // clipping path changes are reported (as in iText)
        ClippingPathInfo info(p.getGraphicsState(), p.getGraphicsState().getClippingPath(), p.getGraphicsState().getCtm());
        p.eventOccurred(info, EventType::CLIP_PATH_CHANGED);
    });
    reg("cm", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        Matrix m(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5));
        p.getGraphicsState().updateCtm(m);
    });
    reg("w", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setLineWidth(num(o, 0)); });
    reg("J", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setLineCapStyle(static_cast<int>(num(o, 0))); });
    reg("j", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setLineJoinStyle(static_cast<int>(num(o, 0))); });
    reg("M", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setMiterLimit(num(o, 0)); });
    reg("d", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        auto arr = std::make_shared<PdfArray>();
        if (!o.empty() && o[0]->isArray()) arr->add(o[0]);
        if (o.size() > 1) arr->add(o[1]);
        p.getGraphicsState().setDashPattern(arr);
    });
    reg("ri", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (!o.empty() && o[0]->isName()) p.getGraphicsState().setRenderingIntent(std::static_pointer_cast<PdfName>(o[0]));
    });
    reg("i", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setFlatnessTolerance(num(o, 0)); });
    reg("gs", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (o.empty() || !o[0]->isName() || !p.getResources()) return;
        auto ext = p.getResources()->getResource(PdfName::ExtGState);
        auto dict = ext ? ext->getAsDictionary(std::static_pointer_cast<PdfName>(o[0])) : nullptr;
        if (!dict) return;
        auto fontArr = dict->getAsArray(PdfName::Font);
        if (fontArr) {
            auto fontDict = fontArr->getAsDictionary(0);
            if (fontDict) p.getGraphicsState().setFont(p.getFont(fontDict));
            if (auto size = fontArr->getAsNumber(1)) p.getGraphicsState().setFontSize(size->floatValue());
        }
        PdfDictionary copy(dict.get());
        copy.remove(PdfName::Font);
        p.getGraphicsState().updateFromExtGState(copy, nullptr);
    });
    // text
    reg("BT", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.beginText(); });
    reg("ET", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.endText(); });
    reg("Tc", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setCharSpacing(num(o, 0)); });
    reg("Tw", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setWordSpacing(num(o, 0)); });
    reg("Tz", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setHorizontalScaling(num(o, 0)); });
    reg("TL", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setLeading(num(o, 0)); });
    reg("Tr", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setTextRenderingMode(static_cast<int>(num(o, 0))); });
    reg("Ts", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.getGraphicsState().setTextRise(num(o, 0)); });
    reg("Tf", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (o.size() < 2) return;
        float size = num(o, 1);
        if (o[0]->isName() && p.getResources()) {
            auto fonts = p.getResources()->getResource(PdfName::Font);
            PdfObjectPtr fontObj = fonts ? fonts->get(std::static_pointer_cast<PdfName>(o[0])) : nullptr;
            auto fontDict = pdf_cast<PdfDictionary>(fontObj);
            if (fontDict) p.getGraphicsState().setFont(p.getFont(fontDict));
        }
        p.getGraphicsState().setFontSize(size);
    });
    auto td = [](PdfCanvasProcessor& p, float tx, float ty) {
        Matrix translationMatrix(tx, ty);
        p.textMatrix_ = translationMatrix.multiply(p.textLineMatrix_);
        p.textLineMatrix_ = p.textMatrix_;
    };
    reg("Td", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { td(p, num(o, 0), num(o, 1)); });
    reg("TD", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        p.getGraphicsState().setLeading(-num(o, 1));
        td(p, num(o, 0), num(o, 1));
    });
    reg("Tm", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        p.textLineMatrix_ = Matrix(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5));
        p.textMatrix_ = p.textLineMatrix_;
    });
    reg("T*", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { td(p, 0, -p.getGraphicsState().getLeading()); });
    reg("Tj", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (!o.empty() && o[0]->isString()) p.displayPdfString(std::static_pointer_cast<PdfString>(o[0]));
    });
    reg("'", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        td(p, 0, -p.getGraphicsState().getLeading());
        if (!o.empty() && o[0]->isString()) p.displayPdfString(std::static_pointer_cast<PdfString>(o[0]));
    });
    reg("\"", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (o.size() < 3) return;
        p.getGraphicsState().setWordSpacing(num(o, 0));
        p.getGraphicsState().setCharSpacing(num(o, 1));
        td(p, 0, -p.getGraphicsState().getLeading());
        if (o[2]->isString()) p.displayPdfString(std::static_pointer_cast<PdfString>(o[2]));
    });
    reg("TJ", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (o.empty() || !o[0]->isArray()) return;
        for (const auto& entry : *std::static_pointer_cast<PdfArray>(o[0])) {
            if (entry->isString()) p.displayPdfString(std::static_pointer_cast<PdfString>(entry));
            else if (entry->isNumber()) p.applyTextAdjust(static_cast<PdfNumber&>(*entry).floatValue());
        }
    });
    // path construction
    reg("m", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.currentPath_.moveTo(num(o, 0), num(o, 1)); });
    reg("l", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        try {
            p.currentPath_.lineTo(num(o, 0), num(o, 1));
        } catch (...) {
        }
    });
    reg("c", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        try {
            p.currentPath_.curveTo(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5));
        } catch (...) {
        }
    });
    reg("v", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        try {
            p.currentPath_.curveTo(num(o, 0), num(o, 1), num(o, 2), num(o, 3));
        } catch (...) {
        }
    });
    reg("y", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        try {
            p.currentPath_.curveFromTo(num(o, 0), num(o, 1), num(o, 2), num(o, 3));
        } catch (...) {
        }
    });
    reg("h", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.currentPath_.closeSubpath(); });
    reg("re", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { p.currentPath_.rectangle(num(o, 0), num(o, 1), num(o, 2), num(o, 3)); });
    // path painting
    using PRI = PathRenderInfo;
    const int NZ = PdfCanvasConstants::FillingRule::NONZERO_WINDING, EO = PdfCanvasConstants::FillingRule::EVEN_ODD;
    auto paint = [this](const std::string& name, int op, int rule, bool close) {
        operators_[name] = std::make_shared<LambdaOperator>([op, rule, close](PdfCanvasProcessor& p, const PdfLiteral&, std::vector<PdfObjectPtr>&) {
            if (close) p.currentPath_.closeSubpath();
            p.paintPath(op, rule);
        });
    };
    paint("S", PRI::STROKE, NZ, false);
    paint("s", PRI::STROKE, NZ, true);
    paint("f", PRI::FILL, NZ, false);
    paint("F", PRI::FILL, NZ, false);
    paint("f*", PRI::FILL, EO, false);
    paint("B", PRI::FILL | PRI::STROKE, NZ, false);
    paint("B*", PRI::FILL | PRI::STROKE, EO, false);
    paint("b", PRI::FILL | PRI::STROKE, NZ, true);
    paint("b*", PRI::FILL | PRI::STROKE, EO, true);
    paint("n", PRI::NO_OP, NZ, false);
    reg("W", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.setClippingPath(PdfCanvasConstants::FillingRule::NONZERO_WINDING); });
    reg("W*", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.setClippingPath(PdfCanvasConstants::FillingRule::EVEN_ODD); });
    // colors
    auto setColor = [](PdfCanvasProcessor& p, bool fill, ColorPtr c) {
        if (fill) p.getGraphicsState().setFillColor(std::move(c));
        else p.getGraphicsState().setStrokeColor(std::move(c));
    };
    reg("g", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, true, std::make_shared<DeviceGray>(num(o, 0))); });
    reg("G", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, false, std::make_shared<DeviceGray>(num(o, 0))); });
    reg("rg", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, true, std::make_shared<DeviceRgb>(num(o, 0), num(o, 1), num(o, 2))); });
    reg("RG", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, false, std::make_shared<DeviceRgb>(num(o, 0), num(o, 1), num(o, 2))); });
    reg("k", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, true, std::make_shared<DeviceCmyk>(num(o, 0), num(o, 1), num(o, 2), num(o, 3))); });
    reg("K", [setColor](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setColor(p, false, std::make_shared<DeviceCmyk>(num(o, 0), num(o, 1), num(o, 2), num(o, 3))); });
    auto setCs = [setColor](PdfCanvasProcessor& p, bool fill, Ops& o) {
        if (o.empty() || !o[0]->isName()) return;
        PdfObjectPtr csObj = o[0];
        auto res = p.getResources();
        if (res) {
            auto csDict = res->getResource(PdfName::ColorSpace);
            if (csDict) {
                PdfObjectPtr named = csDict->get(std::static_pointer_cast<PdfName>(o[0]));
                if (named) csObj = named;
            }
        }
        auto cs = PdfColorSpace::makeColorSpace(csObj);
        if (cs) setColor(p, fill, Color::makeColor(cs));
    };
    reg("cs", [setCs](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setCs(p, true, o); });
    reg("CS", [setCs](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setCs(p, false, o); });
    auto setScn = [setColor](PdfCanvasProcessor& p, bool fill, Ops& o) {
        ColorPtr current = fill ? p.getGraphicsState().getFillColor() : p.getGraphicsState().getStrokeColor();
        auto cs = current ? current->getColorSpace() : nullptr;
        setColor(p, fill, Color::makeColor(cs, nums(o)));
    };
    reg("sc", [setScn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setScn(p, true, o); });
    reg("scn", [setScn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setScn(p, true, o); });
    reg("SC", [setScn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setScn(p, false, o); });
    reg("SCN", [setScn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) { setScn(p, false, o); });
    // XObjects / images / marked content
    reg("Do", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (!o.empty() && o[0]->isName()) p.displayXObject(std::static_pointer_cast<PdfName>(o[0]));
    });
    reg("BI", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (!o.empty() && o[0]->isStream()) p.displayImage(p.getMarkedContentStack(), std::static_pointer_cast<PdfStream>(o[0]), nullptr, true);
    });
    reg("BMC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (!o.empty() && o[0]->isName()) p.beginMarkedContent(std::static_pointer_cast<PdfName>(o[0]), nullptr);
    });
    reg("BDC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
        if (o.size() < 3 || !o[0]->isName()) return;
        PdfDictionaryPtr props;
        if (o[1]->isDictionary()) {
            props = std::static_pointer_cast<PdfDictionary>(o[1]);
        } else if (o[1]->isName() && p.getResources()) {
            props = pdf_cast<PdfDictionary>(p.getResources()->getProperties(std::static_pointer_cast<PdfName>(o[1])));
        }
        p.beginMarkedContent(std::static_pointer_cast<PdfName>(o[0]), props);
    });
    reg("EMC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.endMarkedContent(); });
    // ignored operators
    for (const char* ignored : {"d0", "d1", "sh", "MP", "DP", "BX", "EX"}) reg(ignored, [](PdfCanvasProcessor&, const PdfLiteral&, Ops&) {});
}

void PdfCanvasProcessor::invokeOperator(const PdfLiteral& oper, std::vector<PdfObjectPtr>& operands) {
    auto it = operators_.find(oper.toString());
    if (it == operators_.end()) return;  // unknown operators are ignored (DefaultOperator)
    it->second->invoke(*this, oper, operands);
}

void PdfCanvasProcessor::processContent(const Bytes& contentBytes, PdfResources* resources) {
    resourcesStack_.push_back(resources);
    PdfCanvasParser parser(contentBytes, resources);
    std::vector<PdfObjectPtr> operands;
    try {
        while (parser.parse(operands)) {
            auto oper = std::static_pointer_cast<PdfLiteral>(operands.back());
            if (!oper->isLiteral()) continue;
            invokeOperator(*oper, operands);
        }
    } catch (const std::exception&) {
        resourcesStack_.pop_back();
        throw;
    }
    resourcesStack_.pop_back();
}

void PdfCanvasProcessor::processPageContent(PdfPage* page) {
    document_ = page->getDocument();
    PdfResources* res = page->getResources();
    processContent(page->getContentBytes(), res);
}

PdfFont* PdfCanvasProcessor::getFont(const PdfDictionaryPtr& fontDict) {
    if (!fontDict) return nullptr;
    if (document_) return document_->getFont(fontDict);
    auto it = cachedFonts_.find(fontDict.get());
    if (it != cachedFonts_.end()) return it->second.get();
    auto font = PdfFontFactory::createFont(fontDict);
    PdfFont* raw = font.get();
    cachedFonts_[fontDict.get()] = font;
    return raw;
}

void PdfCanvasProcessor::beginText() {
    textMatrix_ = Matrix();
    textLineMatrix_ = textMatrix_;
    struct Empty : IEventData {} e;
    eventOccurred(e, EventType::BEGIN_TEXT);
}

void PdfCanvasProcessor::endText() {
    struct Empty : IEventData {} e;
    eventOccurred(e, EventType::END_TEXT);
}

void PdfCanvasProcessor::displayPdfString(const PdfStringPtr& string) {
    if (!getGraphicsState().getFont()) return;
    TextRenderInfo renderInfo(string, getGraphicsState(), textMatrix_, markedContentStack_);
    textMatrix_ = Matrix(renderInfo.getUnscaledWidth(), 0).multiply(textMatrix_);
    eventOccurred(renderInfo, EventType::RENDER_TEXT);
}

void PdfCanvasProcessor::applyTextAdjust(float tj) {
    float adjustBy = -tj / 1000.f * getGraphicsState().getFontSize() * (getGraphicsState().getHorizontalScaling() / 100.f);
    textMatrix_ = Matrix(adjustBy, 0).multiply(textMatrix_);
}

void PdfCanvasProcessor::displayImage(const std::vector<CanvasTag>& canvasTagHierarchy, const PdfStreamPtr& imageStream,
                                      const PdfNamePtr& resourceName, bool isInline) {
    PdfDictionaryPtr colorSpaceDic = getResources() ? getResources()->getResource(PdfName::ColorSpace) : nullptr;
    ImageRenderInfo renderInfo(canvasTagHierarchy, getGraphicsState(), getGraphicsState().getCtm(), imageStream, resourceName,
                               colorSpaceDic, isInline);
    eventOccurred(renderInfo, EventType::RENDER_IMAGE);
}

void PdfCanvasProcessor::displayXObject(const PdfNamePtr& resourceName) {
    if (!getResources()) return;
    auto xobjects = getResources()->getResource(PdfName::XObject);
    auto xobjectStream = xobjects ? xobjects->getAsStream(resourceName) : nullptr;
    if (!xobjectStream) return;
    auto subtype = xobjectStream->getAsName(PdfName::Subtype);
    if (nameEquals(subtype, PdfName::Image)) {
        displayImage(markedContentStack_, xobjectStream, resourceName, false);
    } else if (nameEquals(subtype, PdfName::Form)) {
        if (xobjectDepth_ > 32) return;  // guard against recursive forms
        ++xobjectDepth_;
        PdfResources* resources = getResources();
        auto resDict = xobjectStream->getAsDictionary(PdfName::Resources);
        if (resDict) {
            ownedResources_.push_back(std::make_shared<PdfResources>(resDict));
            resources = ownedResources_.back().get();
        }
        Bytes contentBytes = xobjectStream->getBytes();
        auto matrix = xobjectStream->getAsArray(PdfName::Matrix);
        gsStack_.push_back(gsStack_.back());
        if (matrix && matrix->size() == 6) {
            auto m = matrix->toFloatArray();
            getGraphicsState().updateCtm(Matrix(m[0], m[1], m[2], m[3], m[4], m[5]));
        }
        Matrix savedTm = textMatrix_, savedTlm = textLineMatrix_;
        try {
            processContent(contentBytes, resources);
        } catch (...) {
            gsStack_.pop_back();
            --xobjectDepth_;
            throw;
        }
        textMatrix_ = savedTm;
        textLineMatrix_ = savedTlm;
        gsStack_.pop_back();
        --xobjectDepth_;
    }
}

void PdfCanvasProcessor::beginMarkedContent(const PdfNamePtr& tag, const PdfDictionaryPtr& dict) {
    markedContentStack_.emplace_back(tag, dict);
}

void PdfCanvasProcessor::endMarkedContent() {
    if (!markedContentStack_.empty()) markedContentStack_.pop_back();
}

void PdfCanvasProcessor::paintPath(int operation, int rule) {
    ParserGraphicsState& gs = getGraphicsState();
    PathRenderInfo renderInfo(markedContentStack_, gs, currentPath_, operation, rule, isClip_, clippingRule_);
    eventOccurred(renderInfo, EventType::RENDER_PATH);
    if (isClip_) {
        isClip_ = false;
        gs.clip(currentPath_, clippingRule_);
        ClippingPathInfo clippingInfo(gs, gs.getClippingPath(), gs.getCtm());
        eventOccurred(clippingInfo, EventType::CLIP_PATH_CHANGED);
    }
    currentPath_ = Path();
}

// ============================================================ SimpleTextExtractionStrategy
void SimpleTextExtractionStrategy::eventOccurred(const IEventData& data, EventType type) {
    if (type != EventType::RENDER_TEXT) return;
    const auto& renderInfo = static_cast<const TextRenderInfo&>(data);
    bool firstRender = result_.empty();
    bool hardReturn = false;
    LineSegment segment = renderInfo.getBaseline();
    Vector start = segment.getStartPoint();
    Vector end = segment.getEndPoint();
    std::string text = renderInfo.getText();
    if (!firstRender && lastStart_ && lastEnd_) {
        Vector x1 = *lastStart_, x2 = *lastEnd_;
        float denom = x2.subtract(x1).lengthSquared();
        float dist = denom == 0 ? 0 : x2.subtract(x1).cross(x1.subtract(start)).lengthSquared() / denom;
        if (dist > 1.f) hardReturn = true;
    }
    if (hardReturn) {
        appendTextChunk("\n");
    } else if (!firstRender) {
        if (result_.back() != ' ' && !text.empty() && text[0] != ' ') {
            float spacing = lastEnd_->subtract(start).length();
            if (spacing > renderInfo.getSingleSpaceWidth() / 2.f) appendTextChunk(" ");
        }
    }
    appendTextChunk(text);
    lastStart_ = start;
    lastEnd_ = end;
}

// ============================================================ TextChunkLocation
TextChunkLocation::TextChunkLocation(const Vector& startLocation, const Vector& endLocation, float charSpaceWidth)
    : startLocation_(startLocation), endLocation_(endLocation), charSpaceWidth_(charSpaceWidth) {
    Vector oVector = endLocation.subtract(startLocation);
    if (oVector.length() == 0) oVector = Vector(1, 0, 0);
    orientationVector_ = oVector.normalize();
    orientationMagnitude_ = static_cast<int>(std::atan2(orientationVector_.get(Vector::I2), orientationVector_.get(Vector::I1)) * 1000);
    Vector origin(0, 0, 1);
    distPerpendicular_ = static_cast<int>(startLocation.subtract(origin).cross(orientationVector_).get(Vector::I3));
    distParallelStart_ = orientationVector_.dot(startLocation);
    distParallelEnd_ = orientationVector_.dot(endLocation);
}

bool TextChunkLocation::sameLine(const TextChunkLocation& as) const {
    if (orientationMagnitude() != as.orientationMagnitude()) return false;
    float diff = static_cast<float>(distPerpendicular() - as.distPerpendicular());
    if (diff == 0) return true;
    LineSegment mine(startLocation_, endLocation_), other(as.startLocation_, as.endLocation_);
    return std::fabs(diff) <= 2 && (mine.getLength() == 0 || other.getLength() == 0);
}

bool TextChunkLocation::isAtWordBoundary(const TextChunkLocation& previous) const {
    if (startLocation_ == endLocation_ || previous.endLocation_ == previous.startLocation_) return false;
    float dist = distanceFromEndOf(previous);
    if (dist < 0) {
        dist = previous.distanceFromEndOf(*this);
        if (dist < 0) return false;
    }
    return dist > getCharSpaceWidth() / 2.0f;
}

bool TextChunkLocation::containsMark(const TextChunkLocation& baseLocation, const TextChunkLocation& markLocation) {
    return baseLocation.getStartLocation().get(Vector::I1) <= markLocation.getStartLocation().get(Vector::I1) &&
           baseLocation.getEndLocation().get(Vector::I1) >= markLocation.getEndLocation().get(Vector::I1) &&
           std::abs(baseLocation.distPerpendicular() - markLocation.distPerpendicular()) <= 2;
}

// ============================================================ LocationTextExtractionStrategy
void LocationTextExtractionStrategy::eventOccurred(const IEventData& data, EventType type) {
    if (type != EventType::RENDER_TEXT) return;
    const auto& renderInfo = static_cast<const TextRenderInfo&>(data);
    LineSegment segment = renderInfo.getBaseline();
    if (renderInfo.getRise() != 0) segment = segment.transformBy(Matrix(0, -renderInfo.getRise()));
    if (useActualText_) {
        auto actualText = renderInfo.getActualText();
        if (actualText && hasLast_ && lastActualTextKey_ && *lastActualTextKey_ == *actualText && !locationalResult_.empty()) {
            const TextChunk& last = locationalResult_.back();
            const Vector& ls = last.getLocation().getStartLocation();
            const Vector& le = last.getLocation().getEndLocation();
            Vector mergedStart(std::min(ls.get(0), segment.getStartPoint().get(0)), std::min(ls.get(1), segment.getStartPoint().get(1)),
                               std::min(ls.get(2), segment.getStartPoint().get(2)));
            Vector mergedEnd(std::max(le.get(0), segment.getEndPoint().get(0)), std::max(le.get(1), segment.getEndPoint().get(1)),
                             std::max(le.get(2), segment.getEndPoint().get(2)));
            locationalResult_.back() = TextChunk(last.getText(), TextChunkLocation(mergedStart, mergedEnd, renderInfo.getSingleSpaceWidth()));
        } else {
            locationalResult_.emplace_back(actualText ? *actualText : renderInfo.getText(),
                                           TextChunkLocation(segment.getStartPoint(), segment.getEndPoint(), renderInfo.getSingleSpaceWidth()));
        }
        lastActualTextKey_ = actualText;
    } else {
        locationalResult_.emplace_back(renderInfo.getText(),
                                       TextChunkLocation(segment.getStartPoint(), segment.getEndPoint(), renderInfo.getSingleSpaceWidth()));
    }
    hasLast_ = true;
}

void LocationTextExtractionStrategy::sortTextChunks(std::vector<TextChunk>& textChunks) {
    // marks (zero-length chunks) attach to the chunk they overlap (port of sortWithMarks)
    struct Marks {
        std::vector<size_t> preceding, succeeding;
    };
    std::map<size_t, Marks> marks;
    std::vector<size_t> toSort;
    for (size_t markInd = 0; markInd < textChunks.size(); ++markInd) {
        const auto& location = textChunks[markInd].getLocation();
        if (location.getStartLocation() == location.getEndLocation()) {
            bool found = false;
            for (size_t baseInd = 0; baseInd < textChunks.size(); ++baseInd) {
                if (markInd == baseInd) continue;
                const auto& baseLocation = textChunks[baseInd].getLocation();
                if (!(baseLocation.getStartLocation() == baseLocation.getEndLocation()) &&
                    TextChunkLocation::containsMark(baseLocation, location)) {
                    if (markInd < baseInd) marks[baseInd].preceding.push_back(markInd);
                    else marks[baseInd].succeeding.push_back(markInd);
                    found = true;
                    break;
                }
            }
            if (!found) toSort.push_back(markInd);
        } else {
            toSort.push_back(markInd);
        }
    }
    bool ltr = !rightToLeftRunDirection_;
    std::stable_sort(toSort.begin(), toSort.end(), [&](size_t a, size_t b) {
        const auto& first = textChunks[a].getLocation();
        const auto& second = textChunks[b].getLocation();
        if (first.orientationMagnitude() != second.orientationMagnitude())
            return first.orientationMagnitude() < second.orientationMagnitude();
        if (first.distPerpendicular() != second.distPerpendicular()) return first.distPerpendicular() < second.distPerpendicular();
        return ltr ? first.distParallelStart() < second.distParallelStart() : first.distParallelEnd() > second.distParallelEnd();
    });
    std::vector<TextChunk> result;
    for (size_t current : toSort) {
        auto it = marks.find(current);
        if (it != marks.end()) {
            if (ltr) for (size_t j : it->second.preceding) result.push_back(textChunks[j]);
            else for (auto r = it->second.succeeding.rbegin(); r != it->second.succeeding.rend(); ++r) result.push_back(textChunks[*r]);
        }
        result.push_back(textChunks[current]);
        if (it != marks.end()) {
            if (ltr) for (size_t j : it->second.succeeding) result.push_back(textChunks[j]);
            else for (auto r = it->second.preceding.rbegin(); r != it->second.preceding.rend(); ++r) result.push_back(textChunks[*r]);
        }
    }
    textChunks.swap(result);
}

std::string LocationTextExtractionStrategy::getResultantText() {
    std::vector<TextChunk> textChunks(locationalResult_);
    sortTextChunks(textChunks);
    std::string sb;
    const TextChunk* lastChunk = nullptr;
    for (const auto& chunk : textChunks) {
        if (!lastChunk) {
            sb += chunk.getText();
        } else if (chunk.sameLine(*lastChunk)) {
            bool startsWithSpace = !chunk.getText().empty() && chunk.getText()[0] == ' ';
            bool endsWithSpace = !lastChunk->getText().empty() && lastChunk->getText().back() == ' ';
            if (isChunkAtWordBoundary(chunk, *lastChunk) && !startsWithSpace && !endsWithSpace) sb += outputChunkSeparator_;
            sb += chunk.getText();
        } else {
            sb += outputNewline_;
            sb += chunk.getText();
        }
        lastChunk = &chunk;
    }
    return sb;
}

// ============================================================ PdfTextExtractor
std::string PdfTextExtractor::getTextFromPage(PdfPage* page, ITextExtractionStrategy& strategy) {
    PdfCanvasProcessor parser(strategy);
    parser.processPageContent(page);
    return strategy.getResultantText();
}

std::string PdfTextExtractor::getTextFromPage(PdfPage* page) {
    LocationTextExtractionStrategy strategy;
    return getTextFromPage(page, strategy);
}

}  // namespace kernel
}  // namespace pdfcpp
