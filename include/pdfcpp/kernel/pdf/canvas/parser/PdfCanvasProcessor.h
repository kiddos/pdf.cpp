#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "pdfcpp/kernel/geom/Geom.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"

namespace pdfcpp {
namespace kernel {

class PdfFont;

/// Port of EventType.
enum class EventType { BEGIN_TEXT, RENDER_TEXT, END_TEXT, RENDER_IMAGE, RENDER_PATH, CLIP_PATH_CHANGED };

/// Port of CanvasTag: a marked-content tag with its properties.
class CanvasTag {
public:
    CanvasTag(PdfNamePtr role, PdfDictionaryPtr properties = nullptr) : role_(std::move(role)), properties_(std::move(properties)) {}
    const PdfNamePtr& getRole() const { return role_; }
    int getMcid() const;
    bool hasMcid() const { return getMcid() != -1; }
    const PdfDictionaryPtr& getProperties() const { return properties_; }
    /// /ActualText of the tag (UTF-8) or empty optional.
    std::optional<std::string> getActualText() const;
    std::optional<std::string> getExpansionText() const;

private:
    PdfNamePtr role_;
    PdfDictionaryPtr properties_;
};

/// Port of ParserGraphicsState.
class ParserGraphicsState : public CanvasGraphicsState {
public:
    void setClippingPath(const Path& path) { clippingPath_ = path; }
    const Path& getClippingPath() const { return clippingPath_; }
    void clip(const Path& path, int fillingRule);

private:
    Path clippingPath_;
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
    const ParserGraphicsState& getGraphicsState() const { return gs_; }
    ColorPtr getFillColor() const { return gs_.getFillColor(); }
    ColorPtr getStrokeColor() const { return gs_.getStrokeColor(); }

protected:
    ParserGraphicsState gs_;
};

/// Port of TextRenderInfo: information about a shown string.
class TextRenderInfo : public AbstractRenderInfo {
public:
    TextRenderInfo(PdfStringPtr str, const ParserGraphicsState& gs, const Matrix& textMatrix,
                   const std::vector<CanvasTag>& canvasTagHierarchy);
    /// Decoded text (UTF-8).
    std::string getText() const;
    /// ActualText from enclosing marked content (UTF-8) if any.
    std::optional<std::string> getActualText() const;
    std::optional<std::string> getExpansionText() const;
    bool hasMcid(int mcid, bool checkTheTopmostLevelOnly = false) const;
    int getMcid() const;
    LineSegment getBaseline() const;
    LineSegment getUnscaledBaseline() const;
    LineSegment getAscentLine() const;
    LineSegment getDescentLine() const;
    PdfFont* getFont() const { return gs_.getFont(); }
    float getRise() const;
    float getSingleSpaceWidth() const;
    int getTextRenderMode() const { return gs_.getTextRenderingMode(); }
    std::vector<TextRenderInfo> getCharacterRenderInfos() const;
    const PdfStringPtr& getPdfString() const { return string_; }
    const Matrix& getTextMatrix() const { return textMatrix_; }
    float getFontSize() const { return gs_.getFontSize(); }
    float getHorizontalScaling() const { return gs_.getHorizontalScaling(); }
    float getCharSpacing() const { return gs_.getCharSpacing(); }
    float getWordSpacing() const { return gs_.getWordSpacing(); }
    float getLeading() const { return gs_.getLeading(); }
    float getUnscaledWidth() const;
    const std::vector<CanvasTag>& getCanvasTagHierarchy() const { return canvasTagHierarchy_; }

private:
    LineSegment getUnscaledBaselineWithOffset(float yOffset) const;
    float convertWidthFromTextSpaceToUserSpace(float width) const;
    float convertHeightFromTextSpaceToUserSpace(float height) const;
    float getUnscaledFontSpaceWidth() const;
    float getPdfStringWidth(const PdfString& string, bool singleCharString) const;
    std::vector<PdfStringPtr> splitString(const PdfString& string) const;
    PdfStringPtr string_;
    Matrix textToUserSpaceTransformMatrix_;
    Matrix textMatrix_;
    std::vector<CanvasTag> canvasTagHierarchy_;
    mutable std::optional<std::string> text_;
    mutable std::optional<float> unscaledWidth_;
};

/// Port of ImageRenderInfo.
class ImageRenderInfo : public AbstractRenderInfo {
public:
    ImageRenderInfo(const std::vector<CanvasTag>& canvasTagHierarchy, const ParserGraphicsState& gs, const Matrix& ctm,
                    PdfStreamPtr stream, PdfNamePtr resourceName, PdfDictionaryPtr colorSpaceDictionary, bool isInline);
    const PdfStreamPtr& getImage() const { return stream_; }
    Vector getStartPoint() const { return Vector(0, 0, 1).cross(ctm_); }
    const Matrix& getImageCtm() const { return ctm_; }
    float getArea() const { return std::fabs(ctm_.getDeterminant()); }
    const PdfNamePtr& getImageResourceName() const { return resourceName_; }
    bool isInline() const { return isInline_; }
    const std::vector<CanvasTag>& getCanvasTagHierarchy() const { return canvasTagHierarchy_; }
    int getMcid() const;

private:
    std::vector<CanvasTag> canvasTagHierarchy_;
    Matrix ctm_;
    PdfStreamPtr stream_;
    PdfNamePtr resourceName_;
    PdfDictionaryPtr colorSpaceDictionary_;
    bool isInline_;
};

/// Port of PathRenderInfo.
class PathRenderInfo : public AbstractRenderInfo {
public:
    static constexpr int NO_OP = 0, STROKE = 1, FILL = 2;
    PathRenderInfo(const std::vector<CanvasTag>& canvasTagHierarchy, const ParserGraphicsState& gs, const Path& path,
                   int operation, int rule, bool isClip, int clipRule)
        : AbstractRenderInfo(gs), canvasTagHierarchy_(canvasTagHierarchy), path_(path), operation_(operation), rule_(rule),
          isClip_(isClip), clipRule_(clipRule) {}
    const Path& getPath() const { return path_; }
    int getOperation() const { return operation_; }
    int getRule() const { return rule_; }
    bool isPathModifiesClippingPath() const { return isClip_; }
    int getClippingRule() const { return clipRule_; }
    Matrix getCtm() const { return gs_.getCtm(); }
    float getLineWidth() const { return gs_.getLineWidth(); }
    const std::vector<CanvasTag>& getCanvasTagHierarchy() const { return canvasTagHierarchy_; }

private:
    std::vector<CanvasTag> canvasTagHierarchy_;
    Path path_;
    int operation_, rule_;
    bool isClip_;
    int clipRule_;
};

/// Port of ClippingPathInfo.
class ClippingPathInfo : public AbstractRenderInfo {
public:
    ClippingPathInfo(const ParserGraphicsState& gs, const Path& path, const Matrix& ctm)
        : AbstractRenderInfo(gs), path_(path), ctm_(ctm) {}
    const Path& getClippingPath() const { return path_; }
    const Matrix& getCtm() const { return ctm_; }

private:
    Path path_;
    Matrix ctm_;
};

/// Port of IEventListener.
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void eventOccurred(const IEventData& data, EventType type) = 0;
    /// Empty set = all events.
    virtual std::set<EventType> getSupportedEvents() const { return {}; }
};

/// Port of IContentOperator.
class PdfCanvasProcessor;
class IContentOperator {
public:
    virtual ~IContentOperator() = default;
    virtual void invoke(PdfCanvasProcessor& processor, const PdfLiteral& oper, std::vector<PdfObjectPtr>& operands) = 0;
};

/// Port of PdfCanvasParser: splits a content stream into (operands, operator) groups.
class PdfCanvasParser {
public:
    PdfCanvasParser(const Bytes& content, PdfResources* resources);
    ~PdfCanvasParser();
    /// Reads the next operator with its operands (the operator literal is the last element). Returns false at end.
    bool parse(std::vector<PdfObjectPtr>& ls);
    PdfObjectPtr readObject();
    /// Byte offset just past the last token read (pdfcpp extension, used to splice edited content streams).
    int64_t getPosition() const;

private:
    PdfDictionaryPtr readDictionary();
    PdfArrayPtr readArray();
    bool nextValidToken();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PdfResources* resources_;
};

/// Port of com.itextpdf.kernel.pdf.canvas.parser.PdfCanvasProcessor.
class PdfCanvasProcessor {
public:
    explicit PdfCanvasProcessor(IEventListener& eventListener);
    virtual ~PdfCanvasProcessor();

    /// Registers a custom operator; returns the previous one.
    std::shared_ptr<IContentOperator> registerContentOperator(const std::string& operatorString,
                                                              std::shared_ptr<IContentOperator> op);
    std::vector<std::string> getRegisteredContentOperatorStrings() const;
    void reset();
    void processContent(const Bytes& contentBytes, PdfResources* resources);
    void processPageContent(PdfPage* page);
    IEventListener& getEventListener() { return eventListener_; }

    // State accessed by operators
    ParserGraphicsState& getGraphicsState() { return gsStack_.back(); }
    std::vector<ParserGraphicsState>& getGsStack() { return gsStack_; }
    Matrix& getTextMatrix() { return textMatrix_; }
    Matrix& getTextLineMatrix() { return textLineMatrix_; }
    PdfResources* getResources() { return resourcesStack_.empty() ? nullptr : resourcesStack_.back(); }
    Path& getCurrentPath() { return currentPath_; }
    PdfDocument* getDocument() const { return document_; }
    void setDocument(PdfDocument* d) { document_ = d; }

    void beginText();
    void endText();
    void displayPdfString(const PdfStringPtr& string);
    void applyTextAdjust(float tj);
    void displayImage(const std::vector<CanvasTag>& canvasTagHierarchy, const PdfStreamPtr& imageStream,
                      const PdfNamePtr& resourceName, bool isInline);
    void displayXObject(const PdfNamePtr& resourceName);
    void beginMarkedContent(const PdfNamePtr& tag, const PdfDictionaryPtr& dict);
    void endMarkedContent();
    void paintPath(int operation, int rule);
    void setClippingPath(int rule) {
        isClip_ = true;
        clippingRule_ = rule;
    }
    void eventOccurred(const IEventData& data, EventType type);
    PdfFont* getFont(const PdfDictionaryPtr& fontDict);
    const std::vector<CanvasTag>& getMarkedContentStack() const { return markedContentStack_; }
    void processInlineImage(PdfCanvasParser& parser, const PdfDictionaryPtr& imageDict);

protected:
    void populateOperators();
    void invokeOperator(const PdfLiteral& oper, std::vector<PdfObjectPtr>& operands);

    IEventListener& eventListener_;
    std::set<EventType> supportedEvents_;
    std::map<std::string, std::shared_ptr<IContentOperator>> operators_;
    std::vector<PdfResources*> resourcesStack_;
    std::vector<std::shared_ptr<PdfResources>> ownedResources_;
    std::vector<ParserGraphicsState> gsStack_;
    Matrix textMatrix_;
    Matrix textLineMatrix_;
    Path currentPath_;
    bool isClip_ = false;
    int clippingRule_ = 0;
    std::vector<CanvasTag> markedContentStack_;
    std::map<const PdfObject*, std::shared_ptr<PdfFont>> cachedFonts_;
    PdfDocument* document_ = nullptr;
    int xobjectDepth_ = 0;
    friend class PdfCanvasParser;
};

// ------------------------------------------------------------------ listeners
/// Port of ITextExtractionStrategy.
class ITextExtractionStrategy : public IEventListener {
public:
    virtual std::string getResultantText() = 0;
};

/// Port of SimpleTextExtractionStrategy.
class SimpleTextExtractionStrategy : public ITextExtractionStrategy {
public:
    void eventOccurred(const IEventData& data, EventType type) override;
    std::set<EventType> getSupportedEvents() const override { return {EventType::RENDER_TEXT}; }
    std::string getResultantText() override { return result_; }

protected:
    void appendTextChunk(const std::string& text) { result_ += text; }

private:
    std::optional<Vector> lastStart_, lastEnd_;
    std::string result_;
};

/// Port of ITextChunkLocation / TextChunkLocationDefaultImp.
class TextChunkLocation {
public:
    TextChunkLocation(const Vector& startLocation, const Vector& endLocation, float charSpaceWidth);
    int orientationMagnitude() const { return orientationMagnitude_; }
    int distPerpendicular() const { return distPerpendicular_; }
    float distParallelStart() const { return distParallelStart_; }
    float distParallelEnd() const { return distParallelEnd_; }
    const Vector& getStartLocation() const { return startLocation_; }
    const Vector& getEndLocation() const { return endLocation_; }
    float getCharSpaceWidth() const { return charSpaceWidth_; }
    bool sameLine(const TextChunkLocation& as) const;
    float distanceFromEndOf(const TextChunkLocation& other) const { return distParallelStart() - other.distParallelEnd(); }
    bool isAtWordBoundary(const TextChunkLocation& previous) const;
    static bool containsMark(const TextChunkLocation& baseLocation, const TextChunkLocation& markLocation);

private:
    Vector startLocation_, endLocation_, orientationVector_;
    int orientationMagnitude_;
    int distPerpendicular_;
    float distParallelStart_, distParallelEnd_;
    float charSpaceWidth_;
};

/// Port of TextChunk.
class TextChunk {
public:
    TextChunk(std::string text, TextChunkLocation location) : text_(std::move(text)), location_(std::move(location)) {}
    const std::string& getText() const { return text_; }
    const TextChunkLocation& getLocation() const { return location_; }
    bool sameLine(const TextChunk& lastChunk) const { return location_.sameLine(lastChunk.location_); }

private:
    std::string text_;
    TextChunkLocation location_;
};

/// Port of LocationTextExtractionStrategy.
class LocationTextExtractionStrategy : public ITextExtractionStrategy {
public:
    LocationTextExtractionStrategy& setUseActualText(bool v) {
        useActualText_ = v;
        return *this;
    }
    LocationTextExtractionStrategy& setRightToLeftRunDirection(bool v) {
        rightToLeftRunDirection_ = v;
        return *this;
    }
    LocationTextExtractionStrategy& setOutputChunkSeparator(const std::string& s) {
        outputChunkSeparator_ = s;
        return *this;
    }
    LocationTextExtractionStrategy& setOutputNewline(const std::string& s) {
        outputNewline_ = s;
        return *this;
    }
    bool isUseActualText() const { return useActualText_; }
    void eventOccurred(const IEventData& data, EventType type) override;
    std::string getResultantText() override;
    const std::vector<TextChunk>& getTextChunks() const { return locationalResult_; }

protected:
    virtual void sortTextChunks(std::vector<TextChunk>& textChunks);
    virtual bool isChunkAtWordBoundary(const TextChunk& chunk, const TextChunk& previousChunk) const {
        return chunk.getLocation().isAtWordBoundary(previousChunk.getLocation());
    }

private:
    std::vector<TextChunk> locationalResult_;
    bool useActualText_ = false;
    bool rightToLeftRunDirection_ = false;
    std::optional<std::string> lastActualTextKey_;
    bool hasLast_ = false;
    std::string outputChunkSeparator_ = " ";
    std::string outputNewline_ = "\n";
};

/// Port of PdfTextExtractor.
class PdfTextExtractor {
public:
    static std::string getTextFromPage(PdfPage* page, ITextExtractionStrategy& strategy);
    /// Uses LocationTextExtractionStrategy.
    static std::string getTextFromPage(PdfPage* page);
};

}  // namespace kernel
}  // namespace pdfcpp
