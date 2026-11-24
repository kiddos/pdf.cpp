#ifndef PDFCPP_KERNEL_PDF_CANVAS_PARSER_PDF_CANVAS_PROCESSOR_H_
#define PDFCPP_KERNEL_PDF_CANVAS_PARSER_PDF_CANVAS_PROCESSOR_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/kernel/geom/geom.h"
#include "pdfcpp/kernel/pdf/canvas/pdf_canvas.h"
#include "pdfcpp/kernel/pdf/pdf_document.h"

namespace pdfcpp {
namespace kernel {

class PdfFont;

/// Port of EventType.
enum class EventType {
  kBeginText,
  kRenderText,
  kEndText,
  kRenderImage,
  kRenderPath,
  kClipPathChanged
};

/// Port of CanvasTag: a marked-content tag with its properties.
class CanvasTag {
 public:
  CanvasTag(PdfNamePtr role, PdfDictionaryPtr properties = nullptr)
      : role_(std::move(role)), properties_(std::move(properties)) {}
  const PdfNamePtr& GetRole() const { return role_; }
  int GetMcid() const;
  bool HasMcid() const { return GetMcid() != -1; }
  const PdfDictionaryPtr& GetProperties() const { return properties_; }
  /// /ActualText of the tag (UTF-8) or empty optional.
  std::optional<std::string> GetActualText() const;
  std::optional<std::string> GetExpansionText() const;

 private:
  PdfNamePtr role_;
  PdfDictionaryPtr properties_;
};

/// Port of ParserGraphicsState.
class ParserGraphicsState : public CanvasGraphicsState {
 public:
  void SetClippingPath(const Path& path) { clipping_path_ = path; }
  const Path& GetClippingPath() const { return clipping_path_; }
  void Clip(const Path& path, int filling_rule);

 private:
  Path clipping_path_;
};

/// Port of IEventData.
class IEventData {
 public:
  virtual ~IEventData() = default;
};

/// Port of AbstractRenderInfo.
class AbstractRenderInfo : public IEventData {
 public:
  explicit AbstractRenderInfo(const ParserGraphicsState& gs) : gs_(gs) {}
  const ParserGraphicsState& GetGraphicsState() const { return gs_; }
  ColorPtr GetFillColor() const { return gs_.GetFillColor(); }
  ColorPtr GetStrokeColor() const { return gs_.GetStrokeColor(); }

 protected:
  ParserGraphicsState gs_;
};

/// Port of TextRenderInfo: information about a shown string.
class TextRenderInfo : public AbstractRenderInfo {
 public:
  TextRenderInfo(PdfStringPtr str, const ParserGraphicsState& gs,
                 const Matrix& text_matrix,
                 const std::vector<CanvasTag>& canvas_tag_hierarchy);
  /// Decoded text (UTF-8).
  std::string GetText() const;
  /// ActualText from enclosing marked content (UTF-8) if any.
  std::optional<std::string> GetActualText() const;
  std::optional<std::string> GetExpansionText() const;
  bool HasMcid(int mcid, bool check_the_topmost_level_only = false) const;
  int GetMcid() const;
  LineSegment GetBaseline() const;
  LineSegment GetUnscaledBaseline() const;
  LineSegment GetAscentLine() const;
  LineSegment GetDescentLine() const;
  PdfFont* GetFont() const { return gs_.GetFont(); }
  float GetRise() const;
  float GetSingleSpaceWidth() const;
  int GetTextRenderMode() const { return gs_.GetTextRenderingMode(); }
  std::vector<TextRenderInfo> GetCharacterRenderInfos() const;
  const PdfStringPtr& GetPdfString() const { return string_; }
  const Matrix& GetTextMatrix() const { return text_matrix_; }
  float GetFontSize() const { return gs_.GetFontSize(); }
  float GetHorizontalScaling() const { return gs_.GetHorizontalScaling(); }
  float GetCharSpacing() const { return gs_.GetCharSpacing(); }
  float GetWordSpacing() const { return gs_.GetWordSpacing(); }
  float GetLeading() const { return gs_.GetLeading(); }
  float GetUnscaledWidth() const;
  const std::vector<CanvasTag>& GetCanvasTagHierarchy() const {
    return canvas_tag_hierarchy_;
  }

 private:
  LineSegment GetUnscaledBaselineWithOffset(float y_offset) const;
  float ConvertWidthFromTextSpaceToUserSpace(float width) const;
  float ConvertHeightFromTextSpaceToUserSpace(float height) const;
  float GetUnscaledFontSpaceWidth() const;
  float GetPdfStringWidth(const PdfString& string,
                          bool single_char_string) const;
  std::vector<PdfStringPtr> SplitString(const PdfString& string) const;
  PdfStringPtr string_;
  Matrix text_to_user_space_transform_matrix_;
  Matrix text_matrix_;
  std::vector<CanvasTag> canvas_tag_hierarchy_;
  mutable std::optional<std::string> text_;
  mutable std::optional<float> unscaled_width_;
};

/// Port of ImageRenderInfo.
class ImageRenderInfo : public AbstractRenderInfo {
 public:
  ImageRenderInfo(const std::vector<CanvasTag>& canvas_tag_hierarchy,
                  const ParserGraphicsState& gs, const Matrix& ctm,
                  PdfStreamPtr stream, PdfNamePtr resource_name,
                  PdfDictionaryPtr color_space_dictionary, bool is_inline);
  const PdfStreamPtr& GetImage() const { return stream_; }
  Vector GetStartPoint() const { return Vector(0, 0, 1).Cross(ctm_); }
  const Matrix& GetImageCtm() const { return ctm_; }
  float GetArea() const { return std::fabs(ctm_.GetDeterminant()); }
  const PdfNamePtr& GetImageResourceName() const { return resource_name_; }
  bool IsInline() const { return is_inline_; }
  const std::vector<CanvasTag>& GetCanvasTagHierarchy() const {
    return canvas_tag_hierarchy_;
  }
  int GetMcid() const;

 private:
  std::vector<CanvasTag> canvas_tag_hierarchy_;
  Matrix ctm_;
  PdfStreamPtr stream_;
  PdfNamePtr resource_name_;
  PdfDictionaryPtr color_space_dictionary_;
  bool is_inline_;
};

/// Port of PathRenderInfo.
class PathRenderInfo : public AbstractRenderInfo {
 public:
  static constexpr int kNoOp = 0, kStroke = 1, kFill = 2;
  PathRenderInfo(const std::vector<CanvasTag>& canvas_tag_hierarchy,
                 const ParserGraphicsState& gs, const Path& path, int operation,
                 int rule, bool is_clip, int clip_rule)
      : AbstractRenderInfo(gs),
        canvas_tag_hierarchy_(canvas_tag_hierarchy),
        path_(path),
        operation_(operation),
        rule_(rule),
        is_clip_(is_clip),
        clip_rule_(clip_rule) {}
  const Path& GetPath() const { return path_; }
  int GetOperation() const { return operation_; }
  int GetRule() const { return rule_; }
  bool IsPathModifiesClippingPath() const { return is_clip_; }
  int GetClippingRule() const { return clip_rule_; }
  Matrix GetCtm() const { return gs_.GetCtm(); }
  float GetLineWidth() const { return gs_.GetLineWidth(); }
  const std::vector<CanvasTag>& GetCanvasTagHierarchy() const {
    return canvas_tag_hierarchy_;
  }

 private:
  std::vector<CanvasTag> canvas_tag_hierarchy_;
  Path path_;
  int operation_, rule_;
  bool is_clip_;
  int clip_rule_;
};

/// Port of ClippingPathInfo.
class ClippingPathInfo : public AbstractRenderInfo {
 public:
  ClippingPathInfo(const ParserGraphicsState& gs, const Path& path,
                   const Matrix& ctm)
      : AbstractRenderInfo(gs), path_(path), ctm_(ctm) {}
  const Path& GetClippingPath() const { return path_; }
  const Matrix& GetCtm() const { return ctm_; }

 private:
  Path path_;
  Matrix ctm_;
};

/// Port of IEventListener.
class IEventListener {
 public:
  virtual ~IEventListener() = default;
  virtual void EventOccurred(const IEventData& data, EventType type) = 0;
  /// Empty set = all events.
  virtual std::set<EventType> GetSupportedEvents() const { return {}; }
};

/// Port of IContentOperator.
class PdfCanvasProcessor;
class IContentOperator {
 public:
  virtual ~IContentOperator() = default;
  virtual void Invoke(PdfCanvasProcessor& processor, const PdfLiteral& oper,
                      std::vector<PdfObjectPtr>& operands) = 0;
};

/// Port of PdfCanvasParser: splits a content stream into (operands, operator)
/// groups.
class PdfCanvasParser {
 public:
  PdfCanvasParser(const Bytes& content, PdfResources* resources);
  ~PdfCanvasParser();
  /// Reads the next operator with its operands (the operator literal is the
  /// last element). Returns false at end.
  bool Parse(std::vector<PdfObjectPtr>& ls);
  PdfObjectPtr ReadObject();
  /// Byte offset just past the last token read (pdfcpp extension, used to
  /// splice edited content streams).
  int64_t GetPosition() const;

 private:
  PdfDictionaryPtr ReadDictionary();
  PdfArrayPtr ReadArray();
  bool NextValidToken();
  struct Impl;
  std::unique_ptr<Impl> impl_;
  PdfResources* resources_;
};

/// Port of com.itextpdf.kernel.pdf.canvas.parser.PdfCanvasProcessor.
class PdfCanvasProcessor {
 public:
  explicit PdfCanvasProcessor(IEventListener& event_listener);
  virtual ~PdfCanvasProcessor();

  /// Registers a custom operator; returns the previous one.
  std::shared_ptr<IContentOperator> RegisterContentOperator(
      const std::string& operator_string, std::shared_ptr<IContentOperator> op);
  std::vector<std::string> GetRegisteredContentOperatorStrings() const;
  void Reset();
  void ProcessContent(const Bytes& content_bytes, PdfResources* resources);
  void ProcessPageContent(PdfPage* page);
  IEventListener& GetEventListener() { return event_listener_; }

  // State accessed by operators
  ParserGraphicsState& GetGraphicsState() { return gs_stack_.back(); }
  std::vector<ParserGraphicsState>& GetGsStack() { return gs_stack_; }
  Matrix& GetTextMatrix() { return text_matrix_; }
  Matrix& GetTextLineMatrix() { return text_line_matrix_; }
  PdfResources* GetResources() {
    return resources_stack_.empty() ? nullptr : resources_stack_.back();
  }
  Path& GetCurrentPath() { return current_path_; }
  PdfDocument* GetDocument() const { return document_; }
  void SetDocument(PdfDocument* d) { document_ = d; }

  void BeginText();
  void EndText();
  void DisplayPdfString(const PdfStringPtr& string);
  void ApplyTextAdjust(float tj);
  void DisplayImage(const std::vector<CanvasTag>& canvas_tag_hierarchy,
                    const PdfStreamPtr& image_stream,
                    const PdfNamePtr& resource_name, bool is_inline);
  void DisplayXObject(const PdfNamePtr& resource_name);
  void BeginMarkedContent(const PdfNamePtr& tag, const PdfDictionaryPtr& dict);
  void EndMarkedContent();
  void PaintPath(int operation, int rule);
  void SetClippingPath(int rule) {
    is_clip_ = true;
    clipping_rule_ = rule;
  }
  void EventOccurred(const IEventData& data, EventType type);
  PdfFont* GetFont(const PdfDictionaryPtr& font_dict);
  const std::vector<CanvasTag>& GetMarkedContentStack() const {
    return marked_content_stack_;
  }
  void ProcessInlineImage(PdfCanvasParser& parser,
                          const PdfDictionaryPtr& image_dict);

 protected:
  void PopulateOperators();
  void InvokeOperator(const PdfLiteral& oper,
                      std::vector<PdfObjectPtr>& operands);

  IEventListener& event_listener_;
  std::set<EventType> supported_events_;
  std::map<std::string, std::shared_ptr<IContentOperator>> operators_;
  std::vector<PdfResources*> resources_stack_;
  std::vector<std::shared_ptr<PdfResources>> owned_resources_;
  std::vector<ParserGraphicsState> gs_stack_;
  Matrix text_matrix_;
  Matrix text_line_matrix_;
  Path current_path_;
  bool is_clip_ = false;
  int clipping_rule_ = 0;
  std::vector<CanvasTag> marked_content_stack_;
  std::map<const PdfObject*, std::shared_ptr<PdfFont>> cached_fonts_;
  PdfDocument* document_ = nullptr;
  int xobject_depth_ = 0;
  friend class PdfCanvasParser;
};

// ------------------------------------------------------------------ listeners
/// Port of ITextExtractionStrategy.
class ITextExtractionStrategy : public IEventListener {
 public:
  virtual std::string GetResultantText() = 0;
};

/// Port of SimpleTextExtractionStrategy.
class SimpleTextExtractionStrategy : public ITextExtractionStrategy {
 public:
  void EventOccurred(const IEventData& data, EventType type) override;
  std::set<EventType> GetSupportedEvents() const override {
    return {EventType::kRenderText};
  }
  std::string GetResultantText() override { return result_; }

 protected:
  void AppendTextChunk(const std::string& text) { result_ += text; }

 private:
  std::optional<Vector> last_start_, last_end_;
  std::string result_;
};

/// Port of ITextChunkLocation / TextChunkLocationDefaultImp.
class TextChunkLocation {
 public:
  TextChunkLocation(const Vector& start_location, const Vector& end_location,
                    float char_space_width);
  int OrientationMagnitude() const { return orientation_magnitude_; }
  int DistPerpendicular() const { return dist_perpendicular_; }
  float DistParallelStart() const { return dist_parallel_start_; }
  float DistParallelEnd() const { return dist_parallel_end_; }
  const Vector& GetStartLocation() const { return start_location_; }
  const Vector& GetEndLocation() const { return end_location_; }
  float GetCharSpaceWidth() const { return char_space_width_; }
  bool SameLine(const TextChunkLocation& as) const;
  float DistanceFromEndOf(const TextChunkLocation& other) const {
    return DistParallelStart() - other.DistParallelEnd();
  }
  bool IsAtWordBoundary(const TextChunkLocation& previous) const;
  static bool ContainsMark(const TextChunkLocation& base_location,
                           const TextChunkLocation& mark_location);

 private:
  Vector start_location_, end_location_, orientation_vector_;
  int orientation_magnitude_;
  int dist_perpendicular_;
  float dist_parallel_start_, dist_parallel_end_;
  float char_space_width_;
};

/// Port of TextChunk.
class TextChunk {
 public:
  TextChunk(std::string text, TextChunkLocation location)
      : text_(std::move(text)), location_(std::move(location)) {}
  const std::string& GetText() const { return text_; }
  const TextChunkLocation& GetLocation() const { return location_; }
  bool SameLine(const TextChunk& last_chunk) const {
    return location_.SameLine(last_chunk.location_);
  }

 private:
  std::string text_;
  TextChunkLocation location_;
};

/// Port of LocationTextExtractionStrategy.
class LocationTextExtractionStrategy : public ITextExtractionStrategy {
 public:
  LocationTextExtractionStrategy& SetUseActualText(bool v) {
    use_actual_text_ = v;
    return *this;
  }
  LocationTextExtractionStrategy& SetRightToLeftRunDirection(bool v) {
    right_to_left_run_direction_ = v;
    return *this;
  }
  LocationTextExtractionStrategy& SetOutputChunkSeparator(
      const std::string& s) {
    output_chunk_separator_ = s;
    return *this;
  }
  LocationTextExtractionStrategy& SetOutputNewline(const std::string& s) {
    output_newline_ = s;
    return *this;
  }
  bool IsUseActualText() const { return use_actual_text_; }
  void EventOccurred(const IEventData& data, EventType type) override;
  std::string GetResultantText() override;
  const std::vector<TextChunk>& GetTextChunks() const {
    return locational_result_;
  }

 protected:
  virtual void SortTextChunks(std::vector<TextChunk>& text_chunks);
  virtual bool IsChunkAtWordBoundary(const TextChunk& chunk,
                                     const TextChunk& previous_chunk) const {
    return chunk.GetLocation().IsAtWordBoundary(previous_chunk.GetLocation());
  }

 private:
  std::vector<TextChunk> locational_result_;
  bool use_actual_text_ = false;
  bool right_to_left_run_direction_ = false;
  std::optional<std::string> last_actual_text_key_;
  bool has_last_ = false;
  std::string output_chunk_separator_ = " ";
  std::string output_newline_ = "\n";
};

/// Port of PdfTextExtractor.
class PdfTextExtractor {
 public:
  static std::string GetTextFromPage(PdfPage* page,
                                     ITextExtractionStrategy& strategy);
  /// Uses LocationTextExtractionStrategy.
  static std::string GetTextFromPage(PdfPage* page);
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_CANVAS_PARSER_PDF_CANVAS_PROCESSOR_H_
