#include "pdfcpp/kernel/pdf/canvas/parser/pdf_canvas_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pdfcpp/io/source/pdf_tokenizer.h"
#include "pdfcpp/io/source/random_access_source.h"
#include "pdfcpp/io/util/text_util.h"
#include "pdfcpp/kernel/font/pdf_font.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ CanvasTag
int CanvasTag::GetMcid() const {
  if (!properties_) return -1;
  auto mcid = properties_->GetAsNumber(PdfName::kMCID);
  return mcid ? mcid->IntValue() : -1;
}

std::optional<std::string> CanvasTag::GetActualText() const {
  if (!properties_) return std::nullopt;
  auto s = properties_->GetAsString(PdfName::kActualText);
  if (!s) return std::nullopt;
  return s->ToUnicodeString();
}

std::optional<std::string> CanvasTag::GetExpansionText() const {
  if (!properties_) return std::nullopt;
  auto s = properties_->GetAsString(PdfName::kE);
  if (!s) return std::nullopt;
  return s->ToUnicodeString();
}

void ParserGraphicsState::Clip(const Path& path, int) {
  // Exact path intersection (the Java port uses a polygon clipper) is not
  // ported; the latest clip wins.
  Path transformed;
  Matrix ctm = GetCtm();
  for (const auto& sp : path.GetSubpaths()) {
    auto pts = sp.GetPiecewiseLinearApproximation();
    if (pts.empty()) continue;
    Subpath out;
    bool first = true;
    Point prev;
    for (const auto& p : pts) {
      Vector v = Vector(static_cast<float>(p.x), static_cast<float>(p.y), 1)
                     .Cross(ctm);
      Point tp(v.Get(0), v.Get(1));
      if (first) {
        out.SetStartPoint(tp);
        first = false;
      } else {
        out.AddSegment(std::make_shared<Line>(prev, tp));
      }
      prev = tp;
    }
    out.SetClosed(true);
    transformed.AddSubpath(out);
  }
  clipping_path_ = transformed;
}

// ============================================================ TextRenderInfo
TextRenderInfo::TextRenderInfo(
    PdfStringPtr str, const ParserGraphicsState& gs, const Matrix& text_matrix,
    const std::vector<CanvasTag>& canvas_tag_hierarchy)
    : AbstractRenderInfo(gs),
      string_(std::move(str)),
      text_matrix_(text_matrix),
      canvas_tag_hierarchy_(canvas_tag_hierarchy) {
  text_to_user_space_transform_matrix_ = text_matrix.Multiply(gs.GetCtm());
}

std::string TextRenderInfo::GetText() const {
  if (!text_)
    text_ = gs_.GetFont() ? gs_.GetFont()->Decode(*string_) : std::string();
  return *text_;
}

std::optional<std::string> TextRenderInfo::GetActualText() const {
  for (auto it = canvas_tag_hierarchy_.rbegin();
       it != canvas_tag_hierarchy_.rend(); ++it) {
    auto t = it->GetActualText();
    if (t) return t;
  }
  return std::nullopt;
}

std::optional<std::string> TextRenderInfo::GetExpansionText() const {
  for (auto it = canvas_tag_hierarchy_.rbegin();
       it != canvas_tag_hierarchy_.rend(); ++it) {
    auto t = it->GetExpansionText();
    if (t) return t;
  }
  return std::nullopt;
}

bool TextRenderInfo::HasMcid(int mcid,
                             bool check_the_topmost_level_only) const {
  if (check_the_topmost_level_only) {
    for (auto it = canvas_tag_hierarchy_.rbegin();
         it != canvas_tag_hierarchy_.rend(); ++it)
      if (it->HasMcid()) return it->GetMcid() == mcid;
    return false;
  }
  for (const auto& tag : canvas_tag_hierarchy_)
    if (tag.HasMcid() && tag.GetMcid() == mcid) return true;
  return false;
}

int TextRenderInfo::GetMcid() const {
  for (auto it = canvas_tag_hierarchy_.rbegin();
       it != canvas_tag_hierarchy_.rend(); ++it)
    if (it->HasMcid()) return it->GetMcid();
  return -1;
}

float TextRenderInfo::GetRise() const {
  if (gs_.GetTextRise() == 0) return 0;
  return ConvertHeightFromTextSpaceToUserSpace(gs_.GetTextRise());
}

float TextRenderInfo::GetPdfStringWidth(const PdfString& string, bool) const {
  PdfFont* font = gs_.GetFont();
  if (!font) return 0;
  float total = 0;
  float hs = gs_.GetHorizontalScaling() / 100.f;
  for (const auto& c : font->DecodeCodes(string)) {
    float w = c.width * gs_.GetFontSize() / 1000.f;
    total +=
        (w + gs_.GetCharSpacing() + (c.is_space ? gs_.GetWordSpacing() : 0)) *
        hs;
  }
  return total;
}

float TextRenderInfo::GetUnscaledWidth() const {
  if (!unscaled_width_) unscaled_width_ = GetPdfStringWidth(*string_, false);
  return *unscaled_width_;
}

LineSegment TextRenderInfo::GetUnscaledBaselineWithOffset(
    float y_offset) const {
  std::string unicode_str = GetText();
  bool ends_with_space = !unicode_str.empty() && unicode_str.back() == ' ';
  // The width includes the trailing char (and word) spacing of the last glyph,
  // which is removed here. An empty string has no glyphs and hence no spacing
  // to remove (Java would yield a negative width).
  float corrected = string_->GetValueBytes().empty()
                        ? 0.f
                        : GetUnscaledWidth() -
                              (gs_.GetCharSpacing() +
                               (ends_with_space ? gs_.GetWordSpacing() : 0)) *
                                  (gs_.GetHorizontalScaling() / 100.f);
  return LineSegment(Vector(0, y_offset, 1), Vector(corrected, y_offset, 1));
}

LineSegment TextRenderInfo::GetBaseline() const {
  return GetUnscaledBaselineWithOffset(0 + gs_.GetTextRise())
      .TransformBy(text_to_user_space_transform_matrix_);
}

LineSegment TextRenderInfo::GetUnscaledBaseline() const {
  return GetUnscaledBaselineWithOffset(0 + gs_.GetTextRise());
}

static std::pair<float, float> AscentDescent(const ParserGraphicsState& gs) {
  PdfFont* font = gs.GetFont();
  float ascent = 800, descent = -200;
  if (font && font->GetFontProgram()) {
    ascent = static_cast<float>(
        font->GetFontProgram()->GetFontMetrics().GetTypoAscender());
    descent = static_cast<float>(
        font->GetFontProgram()->GetFontMetrics().GetTypoDescender());
  }
  if (descent > 0) descent = -descent;
  float scale = ascent - descent < 700 ? ascent - descent : 1000;
  if (scale == 0) scale = 1000;
  return {ascent / scale * gs.GetFontSize(),
          descent / scale * gs.GetFontSize()};
}

LineSegment TextRenderInfo::GetAscentLine() const {
  return GetUnscaledBaselineWithOffset(AscentDescent(gs_).first +
                                       gs_.GetTextRise())
      .TransformBy(text_to_user_space_transform_matrix_);
}

LineSegment TextRenderInfo::GetDescentLine() const {
  return GetUnscaledBaselineWithOffset(AscentDescent(gs_).second +
                                       gs_.GetTextRise())
      .TransformBy(text_to_user_space_transform_matrix_);
}

float TextRenderInfo::ConvertWidthFromTextSpaceToUserSpace(float width) const {
  LineSegment text_space(Vector(0, 0, 1), Vector(width, 0, 1));
  return text_space.TransformBy(text_to_user_space_transform_matrix_)
      .GetLength();
}

float TextRenderInfo::ConvertHeightFromTextSpaceToUserSpace(
    float height) const {
  LineSegment text_space(Vector(0, 0, 1), Vector(0, height, 1));
  return text_space.TransformBy(text_to_user_space_transform_matrix_)
      .GetLength();
}

float TextRenderInfo::GetUnscaledFontSpaceWidth() const {
  PdfFont* font = gs_.GetFont();
  if (!font) return 0;
  float space_width = static_cast<float>(font->GetWidth(' '));
  if (space_width == 0)
    space_width = static_cast<float>(font->GetAverageWidth());
  return (space_width * gs_.GetFontSize() / 1000.f + gs_.GetCharSpacing() +
          gs_.GetWordSpacing()) *
         gs_.GetHorizontalScaling() / 100.f;
}

float TextRenderInfo::GetSingleSpaceWidth() const {
  return ConvertWidthFromTextSpaceToUserSpace(GetUnscaledFontSpaceWidth());
}

std::vector<PdfStringPtr> TextRenderInfo::SplitString(
    const PdfString& string) const {
  std::vector<PdfStringPtr> out;
  if (!gs_.GetFont()) return out;
  for (const auto& c : gs_.GetFont()->DecodeCodes(string))
    out.push_back(std::make_shared<PdfString>(c.code));
  return out;
}

std::vector<TextRenderInfo> TextRenderInfo::GetCharacterRenderInfos() const {
  std::vector<TextRenderInfo> result;
  float total_width = 0;
  for (const auto& str : SplitString(*string_)) {
    TextRenderInfo sub_info(str, gs_,
                            Matrix(total_width, 0).Multiply(text_matrix_),
                            canvas_tag_hierarchy_);
    total_width += sub_info.GetUnscaledWidth();
    result.push_back(std::move(sub_info));
  }
  return result;
}

// ============================================================ ImageRenderInfo
ImageRenderInfo::ImageRenderInfo(
    const std::vector<CanvasTag>& canvas_tag_hierarchy,
    const ParserGraphicsState& gs, const Matrix& ctm, PdfStreamPtr stream,
    PdfNamePtr resource_name, PdfDictionaryPtr color_space_dictionary,
    bool is_inline)
    : AbstractRenderInfo(gs),
      canvas_tag_hierarchy_(canvas_tag_hierarchy),
      ctm_(ctm),
      stream_(std::move(stream)),
      resource_name_(std::move(resource_name)),
      color_space_dictionary_(std::move(color_space_dictionary)),
      is_inline_(is_inline) {}

int ImageRenderInfo::GetMcid() const {
  for (auto it = canvas_tag_hierarchy_.rbegin();
       it != canvas_tag_hierarchy_.rend(); ++it)
    if (it->HasMcid()) return it->GetMcid();
  return -1;
}

// ============================================================ PdfCanvasParser
struct PdfCanvasParser::Impl {
  explicit Impl(const Bytes& content)
      : tokeniser(io::RandomAccessSourceFactory().CreateSource(content)) {}
  io::PdfTokenizer tokeniser;
};

PdfCanvasParser::PdfCanvasParser(const Bytes& content, PdfResources* resources)
    : impl_(std::make_unique<Impl>(content)), resources_(resources) {
  (void)resources_;
}

PdfCanvasParser::~PdfCanvasParser() = default;

int64_t PdfCanvasParser::GetPosition() const {
  return impl_->tokeniser.GetPosition();
}

bool PdfCanvasParser::NextValidToken() {
  while (impl_->tokeniser.NextToken())
    if (impl_->tokeniser.GetTokenType() !=
        io::PdfTokenizer::TokenType::kComment)
      return true;
  return false;
}

PdfDictionaryPtr PdfCanvasParser::ReadDictionary() {
  auto dic = std::make_shared<PdfDictionary>();
  using TT = io::PdfTokenizer::TokenType;
  while (true) {
    if (!NextValidToken()) throw PdfException("Unexpected end of file.");
    if (impl_->tokeniser.GetTokenType() == TT::kEndDic) break;
    if (impl_->tokeniser.GetTokenType() != TT::kName)
      impl_->tokeniser.ThrowError(
          Format(kernel_exception_message_constant::kDictionaryKeyIsNotAName,
                 impl_->tokeniser.GetStringValue()));
    auto name = std::make_shared<PdfName>(impl_->tokeniser.GetByteContent());
    PdfObjectPtr obj = ReadObject();
    dic->Put(name, obj ? obj : PdfObjectPtr(PdfNull::kPdfNull));
  }
  return dic;
}

PdfArrayPtr PdfCanvasParser::ReadArray() {
  auto array = std::make_shared<PdfArray>();
  while (true) {
    PdfObjectPtr obj = ReadObject();
    if (!obj) {
      if (impl_->tokeniser.GetTokenType() ==
          io::PdfTokenizer::TokenType::kEndArray)
        break;
      if (impl_->tokeniser.GetTokenType() ==
          io::PdfTokenizer::TokenType::kEndDic)
        continue;
      break;
    }
    array->Add(obj);
  }
  return array;
}

PdfObjectPtr PdfCanvasParser::ReadObject() {
  if (!NextValidToken()) return nullptr;
  using TT = io::PdfTokenizer::TokenType;
  auto& tok = impl_->tokeniser;
  switch (tok.GetTokenType()) {
    case TT::kStartDic:
      return ReadDictionary();
    case TT::kStartArray:
      return ReadArray();
    case TT::kString:
      return std::make_shared<PdfString>(tok.GetByteContent(),
                                         tok.IsHexString());
    case TT::kName:
      return std::make_shared<PdfName>(tok.GetByteContent());
    case TT::kNumber:
      return std::make_shared<PdfNumber>(tok.GetByteContent());
    case TT::kEndArray:
    case TT::kEndDic:
      return nullptr;
    default: {
      std::string v = tok.GetStringValue();
      if (v == "true") return PdfBoolean::kTrueValue;
      if (v == "false") return PdfBoolean::kFalseValue;
      if (v == "null") return std::make_shared<PdfNull>();
      return std::make_shared<PdfLiteral>(tok.GetByteContent());
    }
  }
}

bool PdfCanvasParser::Parse(std::vector<PdfObjectPtr>& ls) {
  ls.clear();
  PdfObjectPtr ob;
  while ((ob = ReadObject()) != nullptr) {
    ls.push_back(ob);
    if (ob->IsLiteral()) {
      // Inline image: BI <dict entries> ID <data> EI
      if (static_cast<PdfLiteral&>(*ob).ToString() == "BI") {
        auto dict = std::make_shared<PdfDictionary>();
        auto& tok = impl_->tokeniser;
        while (true) {
          PdfObjectPtr key = ReadObject();
          if (!key) return true;
          if (key->IsLiteral() &&
              static_cast<PdfLiteral&>(*key).ToString() == "ID")
            break;
          if (!key->IsName()) continue;
          PdfObjectPtr value = ReadObject();
          if (!value) break;
          dict->Put(std::static_pointer_cast<PdfName>(key), value);
        }
        // skip the single whitespace after ID
        int ch = tok.Read();
        if (!io::PdfTokenizer::IsWhitespace(ch)) tok.BackOnePosition(ch);
        Bytes data;
        // find EI surrounded by whitespace
        int prev2 = -1, prev1 = -1;
        while (true) {
          int c = tok.Read();
          if (c == -1) break;
          data.push_back(static_cast<uint8_t>(c));
          size_t n = data.size();
          if (n >= 3 && data[n - 2] == 'E' && data[n - 1] == 'I' &&
              io::PdfTokenizer::IsWhitespace(data[n - 3])) {
            int next = tok.Peek();
            if (next == -1 || io::PdfTokenizer::IsWhitespace(next) ||
                io::PdfTokenizer::IsDelimiter(next)) {
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
        static const std::pair<const char*, const char*> kAbbrev[] = {
            {"BPC", "BitsPerComponent"},
            {"CS", "ColorSpace"},
            {"D", "Decode"},
            {"DP", "DecodeParms"},
            {"F", "Filter"},
            {"H", "Height"},
            {"W", "Width"},
            {"I", "Interpolate"},
            {"IM", "ImageMask"}};
        for (const auto& kv : dict->EntrySet()) {
          std::string key = kv.first->GetValue();
          for (const auto& a : kAbbrev)
            if (key == a.first) key = a.second;
          PdfObjectPtr value = kv.second;
          if (value->IsName()) {
            static const std::pair<const char*, const char*> kValAbbrev[] = {
                {"G", "DeviceGray"},       {"RGB", "DeviceRGB"},
                {"CMYK", "DeviceCMYK"},    {"I", "Indexed"},
                {"AHx", "ASCIIHexDecode"}, {"A85", "ASCII85Decode"},
                {"LZW", "LZWDecode"},      {"Fl", "FlateDecode"},
                {"RL", "RunLengthDecode"}, {"CCF", "CCITTFaxDecode"},
                {"DCT", "DCTDecode"}};
            std::string v = static_cast<PdfName&>(*value).GetValue();
            for (const auto& a : kValAbbrev)
              if (v == a.first) value = PdfName::Of(a.second);
          }
          stream->Put(PdfName::Of(key), value);
        }
        stream->Put(PdfName::kType, PdfName::kXObject);
        stream->Put(PdfName::kSubtype, PdfName::kImage);
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
  using Fn = std::function<void(PdfCanvasProcessor&, const PdfLiteral&,
                                std::vector<PdfObjectPtr>&)>;
  explicit LambdaOperator(Fn fn) : fn_(std::move(fn)) {}
  void Invoke(PdfCanvasProcessor& p, const PdfLiteral& oper,
              std::vector<PdfObjectPtr>& operands) override {
    fn_(p, oper, operands);
  }

 private:
  Fn fn_;
};

float Num(const std::vector<PdfObjectPtr>& ops, size_t i) {
  if (i >= ops.size() || !ops[i]->IsNumber()) return 0;
  return static_cast<PdfNumber&>(*ops[i]).FloatValue();
}

std::vector<float> Nums(const std::vector<PdfObjectPtr>& ops) {
  std::vector<float> v;
  for (size_t i = 0; i + 1 < ops.size(); ++i)
    if (ops[i]->IsNumber())
      v.push_back(static_cast<PdfNumber&>(*ops[i]).FloatValue());
  return v;
}
}  // namespace

PdfCanvasProcessor::PdfCanvasProcessor(IEventListener& event_listener)
    : event_listener_(event_listener) {
  supported_events_ = event_listener.GetSupportedEvents();
  PopulateOperators();
  Reset();
}

PdfCanvasProcessor::~PdfCanvasProcessor() = default;

std::shared_ptr<IContentOperator> PdfCanvasProcessor::RegisterContentOperator(
    const std::string& operator_string, std::shared_ptr<IContentOperator> op) {
  auto it = operators_.find(operator_string);
  std::shared_ptr<IContentOperator> old =
      it == operators_.end() ? nullptr : it->second;
  operators_[operator_string] = std::move(op);
  return old;
}

std::vector<std::string>
PdfCanvasProcessor::GetRegisteredContentOperatorStrings() const {
  std::vector<std::string> v;
  for (const auto& kv : operators_) v.push_back(kv.first);
  return v;
}

void PdfCanvasProcessor::Reset() {
  gs_stack_.clear();
  gs_stack_.emplace_back();
  text_matrix_ = Matrix();
  text_line_matrix_ = Matrix();
  resources_stack_.clear();
  owned_resources_.clear();
  is_clip_ = false;
  current_path_ = Path();
  marked_content_stack_.clear();
}

void PdfCanvasProcessor::EventOccurred(const IEventData& data, EventType type) {
  if (supported_events_.empty() || supported_events_.count(type))
    event_listener_.EventOccurred(data, type);
}

void PdfCanvasProcessor::PopulateOperators() {
  auto reg = [this](const std::string& name, LambdaOperator::Fn fn) {
    operators_[name] = std::make_shared<LambdaOperator>(std::move(fn));
  };
  using Ops = std::vector<PdfObjectPtr>;
  // graphics state
  reg("q", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    p.gs_stack_.push_back(p.gs_stack_.back());
  });
  reg("Q", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    if (p.gs_stack_.size() > 1) p.gs_stack_.pop_back();
    // clipping path changes are reported (as in iText)
    ClippingPathInfo info(p.GetGraphicsState(),
                          p.GetGraphicsState().GetClippingPath(),
                          p.GetGraphicsState().GetCtm());
    p.EventOccurred(info, EventType::kClipPathChanged);
  });
  reg("cm", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    Matrix m(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3), Num(o, 4), Num(o, 5));
    p.GetGraphicsState().UpdateCtm(m);
  });
  reg("w", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetLineWidth(Num(o, 0));
  });
  reg("J", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetLineCapStyle(static_cast<int>(Num(o, 0)));
  });
  reg("j", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetLineJoinStyle(static_cast<int>(Num(o, 0)));
  });
  reg("M", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetMiterLimit(Num(o, 0));
  });
  reg("d", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    auto arr = std::make_shared<PdfArray>();
    if (!o.empty() && o[0]->IsArray()) arr->Add(o[0]);
    if (o.size() > 1) arr->Add(o[1]);
    p.GetGraphicsState().SetDashPattern(arr);
  });
  reg("ri", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (!o.empty() && o[0]->IsName())
      p.GetGraphicsState().SetRenderingIntent(
          std::static_pointer_cast<PdfName>(o[0]));
  });
  reg("i", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetFlatnessTolerance(Num(o, 0));
  });
  reg("gs", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (o.empty() || !o[0]->IsName() || !p.GetResources()) return;
    auto ext = p.GetResources()->GetResource(PdfName::kExtGState);
    auto dict =
        ext ? ext->GetAsDictionary(std::static_pointer_cast<PdfName>(o[0]))
            : nullptr;
    if (!dict) return;
    auto font_arr = dict->GetAsArray(PdfName::kFont);
    if (font_arr) {
      auto font_dict = font_arr->GetAsDictionary(0);
      if (font_dict) p.GetGraphicsState().SetFont(p.GetFont(font_dict));
      if (auto size = font_arr->GetAsNumber(1))
        p.GetGraphicsState().SetFontSize(size->FloatValue());
    }
    PdfDictionary copy(dict.get());
    copy.Remove(PdfName::kFont);
    p.GetGraphicsState().UpdateFromExtGState(copy, nullptr);
  });
  // text
  reg("BT",
      [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.BeginText(); });
  reg("ET",
      [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) { p.EndText(); });
  reg("Tc", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetCharSpacing(Num(o, 0));
  });
  reg("Tw", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetWordSpacing(Num(o, 0));
  });
  reg("Tz", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetHorizontalScaling(Num(o, 0));
  });
  reg("TL", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetLeading(Num(o, 0));
  });
  reg("Tr", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetTextRenderingMode(static_cast<int>(Num(o, 0)));
  });
  reg("Ts", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetTextRise(Num(o, 0));
  });
  reg("Tf", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (o.size() < 2) return;
    float size = Num(o, 1);
    if (o[0]->IsName() && p.GetResources()) {
      auto fonts = p.GetResources()->GetResource(PdfName::kFont);
      PdfObjectPtr font_obj =
          fonts ? fonts->Get(std::static_pointer_cast<PdfName>(o[0])) : nullptr;
      auto font_dict = PdfCast<PdfDictionary>(font_obj);
      if (font_dict) p.GetGraphicsState().SetFont(p.GetFont(font_dict));
    }
    p.GetGraphicsState().SetFontSize(size);
  });
  auto td = [](PdfCanvasProcessor& p, float tx, float ty) {
    Matrix translation_matrix(tx, ty);
    p.text_matrix_ = translation_matrix.Multiply(p.text_line_matrix_);
    p.text_line_matrix_ = p.text_matrix_;
  };
  reg("Td", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    td(p, Num(o, 0), Num(o, 1));
  });
  reg("TD", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.GetGraphicsState().SetLeading(-Num(o, 1));
    td(p, Num(o, 0), Num(o, 1));
  });
  reg("Tm", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.text_line_matrix_ = Matrix(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3),
                                 Num(o, 4), Num(o, 5));
    p.text_matrix_ = p.text_line_matrix_;
  });
  reg("T*", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    td(p, 0, -p.GetGraphicsState().GetLeading());
  });
  reg("Tj", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (!o.empty() && o[0]->IsString())
      p.DisplayPdfString(std::static_pointer_cast<PdfString>(o[0]));
  });
  reg("'", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    td(p, 0, -p.GetGraphicsState().GetLeading());
    if (!o.empty() && o[0]->IsString())
      p.DisplayPdfString(std::static_pointer_cast<PdfString>(o[0]));
  });
  reg("\"", [td](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (o.size() < 3) return;
    p.GetGraphicsState().SetWordSpacing(Num(o, 0));
    p.GetGraphicsState().SetCharSpacing(Num(o, 1));
    td(p, 0, -p.GetGraphicsState().GetLeading());
    if (o[2]->IsString())
      p.DisplayPdfString(std::static_pointer_cast<PdfString>(o[2]));
  });
  reg("TJ", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (o.empty() || !o[0]->IsArray()) return;
    for (const auto& entry : *std::static_pointer_cast<PdfArray>(o[0])) {
      if (entry->IsString())
        p.DisplayPdfString(std::static_pointer_cast<PdfString>(entry));
      else if (entry->IsNumber())
        p.ApplyTextAdjust(static_cast<PdfNumber&>(*entry).FloatValue());
    }
  });
  // path construction
  reg("m", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.current_path_.MoveTo(Num(o, 0), Num(o, 1));
  });
  reg("l", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    try {
      p.current_path_.LineTo(Num(o, 0), Num(o, 1));
    } catch (...) {
    }
  });
  reg("c", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    try {
      p.current_path_.CurveTo(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3),
                              Num(o, 4), Num(o, 5));
    } catch (...) {
    }
  });
  reg("v", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    try {
      p.current_path_.CurveTo(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3));
    } catch (...) {
    }
  });
  reg("y", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    try {
      p.current_path_.CurveFromTo(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3));
    } catch (...) {
    }
  });
  reg("h", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    p.current_path_.CloseSubpath();
  });
  reg("re", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    p.current_path_.Rectangle(Num(o, 0), Num(o, 1), Num(o, 2), Num(o, 3));
  });
  // path painting
  using PRI = PathRenderInfo;
  const int nz = pdf_canvas_constants::filling_rule::kNonzeroWinding,
            eo = pdf_canvas_constants::filling_rule::kEvenOdd;
  auto paint = [this](const std::string& name, int op, int rule, bool close) {
    operators_[name] = std::make_shared<LambdaOperator>(
        [op, rule, close](PdfCanvasProcessor& p, const PdfLiteral&,
                          std::vector<PdfObjectPtr>&) {
          if (close) p.current_path_.CloseSubpath();
          p.PaintPath(op, rule);
        });
  };
  paint("S", PRI::kStroke, nz, false);
  paint("s", PRI::kStroke, nz, true);
  paint("f", PRI::kFill, nz, false);
  paint("F", PRI::kFill, nz, false);
  paint("f*", PRI::kFill, eo, false);
  paint("B", PRI::kFill | PRI::kStroke, nz, false);
  paint("B*", PRI::kFill | PRI::kStroke, eo, false);
  paint("b", PRI::kFill | PRI::kStroke, nz, true);
  paint("b*", PRI::kFill | PRI::kStroke, eo, true);
  paint("n", PRI::kNoOp, nz, false);
  reg("W", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    p.SetClippingPath(pdf_canvas_constants::filling_rule::kNonzeroWinding);
  });
  reg("W*", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    p.SetClippingPath(pdf_canvas_constants::filling_rule::kEvenOdd);
  });
  // colors
  auto set_color = [](PdfCanvasProcessor& p, bool fill, ColorPtr c) {
    if (fill)
      p.GetGraphicsState().SetFillColor(std::move(c));
    else
      p.GetGraphicsState().SetStrokeColor(std::move(c));
  };
  reg("g", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, true, std::make_shared<DeviceGray>(Num(o, 0)));
  });
  reg("G", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, false, std::make_shared<DeviceGray>(Num(o, 0)));
  });
  reg("rg", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, true,
              std::make_shared<DeviceRgb>(Num(o, 0), Num(o, 1), Num(o, 2)));
  });
  reg("RG", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, false,
              std::make_shared<DeviceRgb>(Num(o, 0), Num(o, 1), Num(o, 2)));
  });
  reg("k", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, true,
              std::make_shared<DeviceCmyk>(Num(o, 0), Num(o, 1), Num(o, 2),
                                           Num(o, 3)));
  });
  reg("K", [set_color](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_color(p, false,
              std::make_shared<DeviceCmyk>(Num(o, 0), Num(o, 1), Num(o, 2),
                                           Num(o, 3)));
  });
  auto set_cs = [set_color](PdfCanvasProcessor& p, bool fill, Ops& o) {
    if (o.empty() || !o[0]->IsName()) return;
    PdfObjectPtr cs_obj = o[0];
    auto res = p.GetResources();
    if (res) {
      auto cs_dict = res->GetResource(PdfName::kColorSpace);
      if (cs_dict) {
        PdfObjectPtr named =
            cs_dict->Get(std::static_pointer_cast<PdfName>(o[0]));
        if (named) cs_obj = named;
      }
    }
    auto cs = PdfColorSpace::MakeColorSpace(cs_obj);
    if (cs) set_color(p, fill, Color::MakeColor(cs));
  };
  reg("cs", [set_cs](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_cs(p, true, o);
  });
  reg("CS", [set_cs](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_cs(p, false, o);
  });
  auto set_scn = [set_color](PdfCanvasProcessor& p, bool fill, Ops& o) {
    ColorPtr current = fill ? p.GetGraphicsState().GetFillColor()
                            : p.GetGraphicsState().GetStrokeColor();
    auto cs = current ? current->GetColorSpace() : nullptr;
    set_color(p, fill, Color::MakeColor(cs, Nums(o)));
  };
  reg("sc", [set_scn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_scn(p, true, o);
  });
  reg("scn", [set_scn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_scn(p, true, o);
  });
  reg("SC", [set_scn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_scn(p, false, o);
  });
  reg("SCN", [set_scn](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    set_scn(p, false, o);
  });
  // XObjects / images / marked content
  reg("Do", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (!o.empty() && o[0]->IsName())
      p.DisplayXObject(std::static_pointer_cast<PdfName>(o[0]));
  });
  reg("BI", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (!o.empty() && o[0]->IsStream())
      p.DisplayImage(p.GetMarkedContentStack(),
                     std::static_pointer_cast<PdfStream>(o[0]), nullptr, true);
  });
  reg("BMC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (!o.empty() && o[0]->IsName())
      p.BeginMarkedContent(std::static_pointer_cast<PdfName>(o[0]), nullptr);
  });
  reg("BDC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops& o) {
    if (o.size() < 3 || !o[0]->IsName()) return;
    PdfDictionaryPtr props;
    if (o[1]->IsDictionary()) {
      props = std::static_pointer_cast<PdfDictionary>(o[1]);
    } else if (o[1]->IsName() && p.GetResources()) {
      props = PdfCast<PdfDictionary>(p.GetResources()->GetProperties(
          std::static_pointer_cast<PdfName>(o[1])));
    }
    p.BeginMarkedContent(std::static_pointer_cast<PdfName>(o[0]), props);
  });
  reg("EMC", [](PdfCanvasProcessor& p, const PdfLiteral&, Ops&) {
    p.EndMarkedContent();
  });
  // ignored operators
  for (const char* ignored : {"d0", "d1", "sh", "MP", "DP", "BX", "EX"})
    reg(ignored, [](PdfCanvasProcessor&, const PdfLiteral&, Ops&) {});
}

void PdfCanvasProcessor::InvokeOperator(const PdfLiteral& oper,
                                        std::vector<PdfObjectPtr>& operands) {
  auto it = operators_.find(oper.ToString());
  if (it == operators_.end())
    return;  // unknown operators are ignored (DefaultOperator)
  it->second->Invoke(*this, oper, operands);
}

void PdfCanvasProcessor::ProcessContent(const Bytes& content_bytes,
                                        PdfResources* resources) {
  resources_stack_.push_back(resources);
  PdfCanvasParser parser(content_bytes, resources);
  std::vector<PdfObjectPtr> operands;
  try {
    while (parser.Parse(operands)) {
      auto oper = std::static_pointer_cast<PdfLiteral>(operands.back());
      if (!oper->IsLiteral()) continue;
      InvokeOperator(*oper, operands);
    }
  } catch (const std::exception&) {
    resources_stack_.pop_back();
    throw;
  }
  resources_stack_.pop_back();
}

void PdfCanvasProcessor::ProcessPageContent(PdfPage* page) {
  document_ = page->GetDocument();
  PdfResources* res = page->GetResources();
  ProcessContent(page->GetContentBytes(), res);
}

PdfFont* PdfCanvasProcessor::GetFont(const PdfDictionaryPtr& font_dict) {
  if (!font_dict) return nullptr;
  if (document_) return document_->GetFont(font_dict);
  auto it = cached_fonts_.find(font_dict.get());
  if (it != cached_fonts_.end()) return it->second.get();
  auto font = PdfFontFactory::CreateFont(font_dict);
  PdfFont* raw = font.get();
  cached_fonts_[font_dict.get()] = font;
  return raw;
}

void PdfCanvasProcessor::BeginText() {
  text_matrix_ = Matrix();
  text_line_matrix_ = text_matrix_;
  struct Empty : IEventData {
  } e;
  EventOccurred(e, EventType::kBeginText);
}

void PdfCanvasProcessor::EndText() {
  struct Empty : IEventData {
  } e;
  EventOccurred(e, EventType::kEndText);
}

void PdfCanvasProcessor::DisplayPdfString(const PdfStringPtr& string) {
  if (!GetGraphicsState().GetFont()) return;
  TextRenderInfo render_info(string, GetGraphicsState(), text_matrix_,
                             marked_content_stack_);
  text_matrix_ =
      Matrix(render_info.GetUnscaledWidth(), 0).Multiply(text_matrix_);
  EventOccurred(render_info, EventType::kRenderText);
}

void PdfCanvasProcessor::ApplyTextAdjust(float tj) {
  float adjust_by = -tj / 1000.f * GetGraphicsState().GetFontSize() *
                    (GetGraphicsState().GetHorizontalScaling() / 100.f);
  text_matrix_ = Matrix(adjust_by, 0).Multiply(text_matrix_);
}

void PdfCanvasProcessor::DisplayImage(
    const std::vector<CanvasTag>& canvas_tag_hierarchy,
    const PdfStreamPtr& image_stream, const PdfNamePtr& resource_name,
    bool is_inline) {
  PdfDictionaryPtr color_space_dic =
      GetResources() ? GetResources()->GetResource(PdfName::kColorSpace)
                     : nullptr;
  ImageRenderInfo render_info(canvas_tag_hierarchy, GetGraphicsState(),
                              GetGraphicsState().GetCtm(), image_stream,
                              resource_name, color_space_dic, is_inline);
  EventOccurred(render_info, EventType::kRenderImage);
}

void PdfCanvasProcessor::DisplayXObject(const PdfNamePtr& resource_name) {
  if (!GetResources()) return;
  auto xobjects = GetResources()->GetResource(PdfName::kXObject);
  auto xobject_stream =
      xobjects ? xobjects->GetAsStream(resource_name) : nullptr;
  if (!xobject_stream) return;
  auto subtype = xobject_stream->GetAsName(PdfName::kSubtype);
  if (NameEquals(subtype, PdfName::kImage)) {
    DisplayImage(marked_content_stack_, xobject_stream, resource_name, false);
  } else if (NameEquals(subtype, PdfName::kForm)) {
    if (xobject_depth_ > 32) return;  // guard against recursive forms
    PdfResources* resources = GetResources();
    auto res_dict = xobject_stream->GetAsDictionary(PdfName::kResources);
    if (res_dict) {
      owned_resources_.push_back(std::make_shared<PdfResources>(res_dict));
      resources = owned_resources_.back().get();
    }
    // Decode before bumping the depth: a throwing filter must not leave
    // xobject_depth_ incremented.
    Bytes content_bytes = xobject_stream->GetBytes();
    ++xobject_depth_;
    auto matrix = xobject_stream->GetAsArray(PdfName::kMatrix);
    gs_stack_.push_back(gs_stack_.back());
    if (matrix && matrix->size() == 6) {
      auto m = matrix->ToFloatArray();
      GetGraphicsState().UpdateCtm(Matrix(m[0], m[1], m[2], m[3], m[4], m[5]));
    }
    Matrix saved_tm = text_matrix_, saved_tlm = text_line_matrix_;
    try {
      ProcessContent(content_bytes, resources);
    } catch (...) {
      gs_stack_.pop_back();
      --xobject_depth_;
      throw;
    }
    text_matrix_ = saved_tm;
    text_line_matrix_ = saved_tlm;
    gs_stack_.pop_back();
    --xobject_depth_;
  }
}

void PdfCanvasProcessor::BeginMarkedContent(const PdfNamePtr& tag,
                                            const PdfDictionaryPtr& dict) {
  marked_content_stack_.emplace_back(tag, dict);
}

void PdfCanvasProcessor::EndMarkedContent() {
  if (!marked_content_stack_.empty()) marked_content_stack_.pop_back();
}

void PdfCanvasProcessor::PaintPath(int operation, int rule) {
  ParserGraphicsState& gs = GetGraphicsState();
  PathRenderInfo render_info(marked_content_stack_, gs, current_path_,
                             operation, rule, is_clip_, clipping_rule_);
  EventOccurred(render_info, EventType::kRenderPath);
  if (is_clip_) {
    is_clip_ = false;
    gs.Clip(current_path_, clipping_rule_);
    ClippingPathInfo clipping_info(gs, gs.GetClippingPath(), gs.GetCtm());
    EventOccurred(clipping_info, EventType::kClipPathChanged);
  }
  current_path_ = Path();
}

// ============================================================
// SimpleTextExtractionStrategy
void SimpleTextExtractionStrategy::EventOccurred(const IEventData& data,
                                                 EventType type) {
  if (type != EventType::kRenderText) return;
  const auto& render_info = static_cast<const TextRenderInfo&>(data);
  bool first_render = result_.empty();
  bool hard_return = false;
  LineSegment segment = render_info.GetBaseline();
  Vector start = segment.GetStartPoint();
  Vector end = segment.GetEndPoint();
  std::string text = render_info.GetText();
  if (!first_render && last_start_ && last_end_) {
    Vector x1 = *last_start_, x2 = *last_end_;
    float denom = x2.Subtract(x1).LengthSquared();
    float dist =
        denom == 0
            ? 0
            : x2.Subtract(x1).Cross(x1.Subtract(start)).LengthSquared() / denom;
    if (dist > 1.f) hard_return = true;
  }
  if (hard_return) {
    AppendTextChunk("\n");
  } else if (!first_render) {
    if (result_.back() != ' ' && !text.empty() && text[0] != ' ') {
      float spacing = last_end_->Subtract(start).Length();
      if (spacing > render_info.GetSingleSpaceWidth() / 2.f)
        AppendTextChunk(" ");
    }
  }
  AppendTextChunk(text);
  last_start_ = start;
  last_end_ = end;
}

// ============================================================
// TextChunkLocation
TextChunkLocation::TextChunkLocation(const Vector& start_location,
                                     const Vector& end_location,
                                     float char_space_width)
    : start_location_(start_location),
      end_location_(end_location),
      char_space_width_(char_space_width) {
  Vector o_vector = end_location.Subtract(start_location);
  if (o_vector.Length() == 0) o_vector = Vector(1, 0, 0);
  orientation_vector_ = o_vector.Normalize();
  orientation_magnitude_ =
      static_cast<int>(std::atan2(orientation_vector_.Get(Vector::kI2),
                                  orientation_vector_.Get(Vector::kI1)) *
                       1000);
  Vector origin(0, 0, 1);
  dist_perpendicular_ = static_cast<int>(start_location.Subtract(origin)
                                             .Cross(orientation_vector_)
                                             .Get(Vector::kI3));
  dist_parallel_start_ = orientation_vector_.Dot(start_location);
  dist_parallel_end_ = orientation_vector_.Dot(end_location);
}

bool TextChunkLocation::SameLine(const TextChunkLocation& as) const {
  if (OrientationMagnitude() != as.OrientationMagnitude()) return false;
  float diff = static_cast<float>(DistPerpendicular() - as.DistPerpendicular());
  if (diff == 0) return true;
  LineSegment mine(start_location_, end_location_),
      other(as.start_location_, as.end_location_);
  return std::fabs(diff) <= 2 &&
         (mine.GetLength() == 0 || other.GetLength() == 0);
}

bool TextChunkLocation::IsAtWordBoundary(
    const TextChunkLocation& previous) const {
  if (start_location_ == end_location_ ||
      previous.end_location_ == previous.start_location_)
    return false;
  float dist = DistanceFromEndOf(previous);
  if (dist < 0) {
    dist = previous.DistanceFromEndOf(*this);
    if (dist < 0) return false;
  }
  return dist > GetCharSpaceWidth() / 2.0f;
}

bool TextChunkLocation::ContainsMark(const TextChunkLocation& base_location,
                                     const TextChunkLocation& mark_location) {
  return base_location.GetStartLocation().Get(Vector::kI1) <=
             mark_location.GetStartLocation().Get(Vector::kI1) &&
         base_location.GetEndLocation().Get(Vector::kI1) >=
             mark_location.GetEndLocation().Get(Vector::kI1) &&
         std::abs(base_location.DistPerpendicular() -
                  mark_location.DistPerpendicular()) <= 2;
}

// ============================================================
// LocationTextExtractionStrategy
void LocationTextExtractionStrategy::EventOccurred(const IEventData& data,
                                                   EventType type) {
  if (type != EventType::kRenderText) return;
  const auto& render_info = static_cast<const TextRenderInfo&>(data);
  LineSegment segment = render_info.GetBaseline();
  if (render_info.GetRise() != 0)
    segment = segment.TransformBy(Matrix(0, -render_info.GetRise()));
  if (use_actual_text_) {
    auto actual_text = render_info.GetActualText();
    if (actual_text && has_last_ && last_actual_text_key_ &&
        *last_actual_text_key_ == *actual_text && !locational_result_.empty()) {
      const TextChunk& last = locational_result_.back();
      const Vector& ls = last.GetLocation().GetStartLocation();
      const Vector& le = last.GetLocation().GetEndLocation();
      Vector merged_start(std::min(ls.Get(0), segment.GetStartPoint().Get(0)),
                          std::min(ls.Get(1), segment.GetStartPoint().Get(1)),
                          std::min(ls.Get(2), segment.GetStartPoint().Get(2)));
      Vector merged_end(std::max(le.Get(0), segment.GetEndPoint().Get(0)),
                        std::max(le.Get(1), segment.GetEndPoint().Get(1)),
                        std::max(le.Get(2), segment.GetEndPoint().Get(2)));
      locational_result_.back() = TextChunk(
          last.GetText(), TextChunkLocation(merged_start, merged_end,
                                            render_info.GetSingleSpaceWidth()));
    } else {
      locational_result_.emplace_back(
          actual_text ? *actual_text : render_info.GetText(),
          TextChunkLocation(segment.GetStartPoint(), segment.GetEndPoint(),
                            render_info.GetSingleSpaceWidth()));
    }
    last_actual_text_key_ = actual_text;
  } else {
    locational_result_.emplace_back(
        render_info.GetText(),
        TextChunkLocation(segment.GetStartPoint(), segment.GetEndPoint(),
                          render_info.GetSingleSpaceWidth()));
  }
  has_last_ = true;
}

void LocationTextExtractionStrategy::SortTextChunks(
    std::vector<TextChunk>& text_chunks) {
  // marks (zero-length chunks) attach to the chunk they overlap (port of
  // sortWithMarks)
  struct Marks {
    std::vector<size_t> preceding, succeeding;
  };
  std::map<size_t, Marks> marks;
  std::vector<size_t> to_sort;
  for (size_t mark_ind = 0; mark_ind < text_chunks.size(); ++mark_ind) {
    const auto& location = text_chunks[mark_ind].GetLocation();
    if (location.GetStartLocation() == location.GetEndLocation()) {
      bool found = false;
      for (size_t base_ind = 0; base_ind < text_chunks.size(); ++base_ind) {
        if (mark_ind == base_ind) continue;
        const auto& base_location = text_chunks[base_ind].GetLocation();
        if (!(base_location.GetStartLocation() ==
              base_location.GetEndLocation()) &&
            TextChunkLocation::ContainsMark(base_location, location)) {
          if (mark_ind < base_ind)
            marks[base_ind].preceding.push_back(mark_ind);
          else
            marks[base_ind].succeeding.push_back(mark_ind);
          found = true;
          break;
        }
      }
      if (!found) to_sort.push_back(mark_ind);
    } else {
      to_sort.push_back(mark_ind);
    }
  }
  bool ltr = !right_to_left_run_direction_;
  std::stable_sort(to_sort.begin(), to_sort.end(), [&](size_t a, size_t b) {
    const auto& first = text_chunks[a].GetLocation();
    const auto& second = text_chunks[b].GetLocation();
    if (first.OrientationMagnitude() != second.OrientationMagnitude())
      return first.OrientationMagnitude() < second.OrientationMagnitude();
    if (first.DistPerpendicular() != second.DistPerpendicular())
      return first.DistPerpendicular() < second.DistPerpendicular();
    return ltr ? first.DistParallelStart() < second.DistParallelStart()
               : first.DistParallelEnd() > second.DistParallelEnd();
  });
  std::vector<TextChunk> result;
  for (size_t current : to_sort) {
    auto it = marks.find(current);
    if (it != marks.end()) {
      if (ltr)
        for (size_t j : it->second.preceding) result.push_back(text_chunks[j]);
      else
        for (auto r = it->second.succeeding.rbegin();
             r != it->second.succeeding.rend(); ++r)
          result.push_back(text_chunks[*r]);
    }
    result.push_back(text_chunks[current]);
    if (it != marks.end()) {
      if (ltr)
        for (size_t j : it->second.succeeding) result.push_back(text_chunks[j]);
      else
        for (auto r = it->second.preceding.rbegin();
             r != it->second.preceding.rend(); ++r)
          result.push_back(text_chunks[*r]);
    }
  }
  text_chunks.swap(result);
}

std::string LocationTextExtractionStrategy::GetResultantText() {
  std::vector<TextChunk> text_chunks(locational_result_);
  SortTextChunks(text_chunks);
  std::string sb;
  const TextChunk* last_chunk = nullptr;
  for (const auto& chunk : text_chunks) {
    if (!last_chunk) {
      sb += chunk.GetText();
    } else if (chunk.SameLine(*last_chunk)) {
      bool starts_with_space =
          !chunk.GetText().empty() && chunk.GetText()[0] == ' ';
      bool ends_with_space =
          !last_chunk->GetText().empty() && last_chunk->GetText().back() == ' ';
      if (IsChunkAtWordBoundary(chunk, *last_chunk) && !starts_with_space &&
          !ends_with_space)
        sb += output_chunk_separator_;
      sb += chunk.GetText();
    } else {
      sb += output_newline_;
      sb += chunk.GetText();
    }
    last_chunk = &chunk;
  }
  return sb;
}

// ============================================================ PdfTextExtractor
std::string PdfTextExtractor::GetTextFromPage(
    PdfPage* page, ITextExtractionStrategy& strategy) {
  PdfCanvasProcessor parser(strategy);
  parser.ProcessPageContent(page);
  return strategy.GetResultantText();
}

std::string PdfTextExtractor::GetTextFromPage(PdfPage* page) {
  LocationTextExtractionStrategy strategy;
  return GetTextFromPage(page, strategy);
}

}  // namespace kernel
}  // namespace pdfcpp
