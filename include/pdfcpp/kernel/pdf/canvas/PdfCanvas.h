#pragma once

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include "pdfcpp/io/font/FontProgram.h"
#include "pdfcpp/io/image/ImageData.h"
#include "pdfcpp/kernel/colors/Color.h"
#include "pdfcpp/kernel/geom/Geom.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"

namespace pdfcpp {
namespace kernel {

class PdfFont;
class PdfXObject;
class PdfImageXObject;
class PdfFormXObject;
class PdfExtGState;

/// Port of PdfCanvasConstants.
namespace PdfCanvasConstants {
namespace TextRenderingMode {
inline constexpr int FILL = 0, STROKE = 1, FILL_STROKE = 2, INVISIBLE = 3, FILL_CLIP = 4, STROKE_CLIP = 5,
                     FILL_STROKE_CLIP = 6, CLIP = 7;
}
namespace LineCapStyle {
inline constexpr int BUTT = 0, ROUND = 1, PROJECTING_SQUARE = 2;
}
namespace LineJoinStyle {
inline constexpr int MITER = 0, ROUND = 1, BEVEL = 2;
}
namespace FillingRule {
inline constexpr int NONZERO_WINDING = 1, EVEN_ODD = 2;
}
}  // namespace PdfCanvasConstants

/// Port of CanvasGraphicsState (writer-side subset).
class CanvasGraphicsState {
public:
    Matrix getCtm() const { return ctm_; }
    void updateCtm(const Matrix& newCtm) { ctm_ = newCtm.multiply(ctm_); }
    void setCtm(const Matrix& m) { ctm_ = m; }
    ColorPtr getFillColor() const { return fillColor_; }
    void setFillColor(ColorPtr c) { fillColor_ = std::move(c); }
    ColorPtr getStrokeColor() const { return strokeColor_; }
    void setStrokeColor(ColorPtr c) { strokeColor_ = std::move(c); }
    float getLineWidth() const { return lineWidth_; }
    void setLineWidth(float v) { lineWidth_ = v; }
    int getLineCapStyle() const { return lineCapStyle_; }
    void setLineCapStyle(int v) { lineCapStyle_ = v; }
    int getLineJoinStyle() const { return lineJoinStyle_; }
    void setLineJoinStyle(int v) { lineJoinStyle_ = v; }
    float getMiterLimit() const { return miterLimit_; }
    void setMiterLimit(float v) { miterLimit_ = v; }
    float getCharSpacing() const { return charSpacing_; }
    void setCharSpacing(float v) { charSpacing_ = v; }
    float getWordSpacing() const { return wordSpacing_; }
    void setWordSpacing(float v) { wordSpacing_ = v; }
    float getHorizontalScaling() const { return horizontalScaling_; }
    void setHorizontalScaling(float v) { horizontalScaling_ = v; }
    float getLeading() const { return leading_; }
    void setLeading(float v) { leading_ = v; }
    PdfFont* getFont() const { return font_; }
    void setFont(PdfFont* f) { font_ = f; }
    float getFontSize() const { return fontSize_; }
    void setFontSize(float v) { fontSize_ = v; }
    int getTextRenderingMode() const { return textRenderingMode_; }
    void setTextRenderingMode(int v) { textRenderingMode_ = v; }
    float getTextRise() const { return textRise_; }
    void setTextRise(float v) { textRise_ = v; }
    bool getTextKnockout() const { return textKnockout_; }
    void setTextKnockout(bool v) { textKnockout_ = v; }
    float getFlatnessTolerance() const { return flatnessTolerance_; }
    void setFlatnessTolerance(float v) { flatnessTolerance_ = v; }
    const PdfArrayPtr& getDashPattern() const { return dashPattern_; }
    void setDashPattern(PdfArrayPtr d) { dashPattern_ = std::move(d); }
    const PdfNamePtr& getRenderingIntent() const { return renderingIntent_; }
    void setRenderingIntent(PdfNamePtr r) { renderingIntent_ = std::move(r); }
    std::optional<float> getFillOpacity() const { return fillOpacity_; }
    void setFillOpacity(float v) { fillOpacity_ = v; }
    std::optional<float> getStrokeOpacity() const { return strokeOpacity_; }
    void setStrokeOpacity(float v) { strokeOpacity_ = v; }
    /// Applies the entries of an ExtGState dictionary (LW, LC, LJ, ML, D, Font, ca, CA ...).
    void updateFromExtGState(const PdfDictionary& extGState, PdfDocument* document);

private:
    Matrix ctm_;
    ColorPtr fillColor_ = DeviceGray::BLACK;
    ColorPtr strokeColor_ = DeviceGray::BLACK;
    float lineWidth_ = 1;
    int lineCapStyle_ = 0;
    int lineJoinStyle_ = 0;
    float miterLimit_ = 10;
    float charSpacing_ = 0;
    float wordSpacing_ = 0;
    float horizontalScaling_ = 100;
    float leading_ = 0;
    PdfFont* font_ = nullptr;
    float fontSize_ = 0;
    int textRenderingMode_ = 0;
    float textRise_ = 0;
    bool textKnockout_ = true;
    float flatnessTolerance_ = 1;
    PdfArrayPtr dashPattern_;
    PdfNamePtr renderingIntent_;
    std::optional<float> fillOpacity_, strokeOpacity_;
};

/// Port of com.itextpdf.kernel.pdf.canvas.PdfCanvas: writes PDF content stream operators.
class PdfCanvas {
public:
    PdfCanvas(PdfStreamPtr contentStream, PdfResources* resources, PdfDocument* document);
    explicit PdfCanvas(PdfPage* page, bool wrapOldContent = false);
    PdfCanvas(PdfDocument* doc, int pageNum) : PdfCanvas(doc->getPage(pageNum)) {}
    PdfCanvas(PdfFormXObject& xObj, PdfDocument* document);
    virtual ~PdfCanvas() = default;

    PdfResources* getResources() const { return resources_; }
    PdfDocument* getDocument() const { return document_; }
    const PdfStreamPtr& getContentStream() const { return contentStream_; }
    CanvasGraphicsState& getGraphicsState() { return *currentGs_; }
    void attachContentStream(PdfStreamPtr contentStream) { contentStream_ = std::move(contentStream); }

    PdfCanvas& saveState();
    PdfCanvas& restoreState();
    PdfCanvas& concatMatrix(double a, double b, double c, double d, double e, double f);
    PdfCanvas& concatMatrix(const AffineTransform& transform);
    PdfCanvas& beginText();
    PdfCanvas& endText();
    PdfCanvas& beginVariableText() { return beginMarkedContent(PdfName::Tx); }
    PdfCanvas& endVariableText() { return endMarkedContent(); }
    PdfCanvas& setFontAndSize(PdfFont* font, float size);
    PdfCanvas& setFontAndSize(const std::shared_ptr<PdfFont>& font, float size) { return setFontAndSize(font.get(), size); }
    PdfCanvas& moveText(double x, double y);
    PdfCanvas& setLeading(float leading);
    PdfCanvas& moveTextWithLeading(float x, float y);
    PdfCanvas& newlineText();
    PdfCanvas& newlineShowText(const std::string& text);
    PdfCanvas& newlineShowText(float wordSpacing, float charSpacing, const std::string& text);
    PdfCanvas& setTextRenderingMode(int textRenderingMode);
    PdfCanvas& setTextRise(float textRise);
    PdfCanvas& setWordSpacing(float wordSpacing);
    PdfCanvas& setCharacterSpacing(float charSpacing);
    PdfCanvas& setHorizontalScaling(float scale);
    PdfCanvas& setTextMatrix(float a, float b, float c, float d, float x, float y);
    PdfCanvas& setTextMatrix(const AffineTransform& transform);
    PdfCanvas& setTextMatrix(float x, float y) { return setTextMatrix(1, 0, 0, 1, x, y); }
    /// Shows text (UTF-8) with the current font.
    PdfCanvas& showText(const std::string& text);
    /// Shows a glyph line, applying glyph advances (kerning) via TJ.
    PdfCanvas& showText(const io::GlyphLine& text);
    PdfCanvas& showText(const PdfArray& textArray);

    PdfCanvas& moveTo(double x, double y);
    PdfCanvas& lineTo(double x, double y);
    PdfCanvas& curveTo(double x1, double y1, double x2, double y2, double x3, double y3);
    PdfCanvas& curveTo(double x2, double y2, double x3, double y3);
    PdfCanvas& curveFromTo(double x1, double y1, double x3, double y3);
    PdfCanvas& arc(double x1, double y1, double x2, double y2, double startAng, double extent);
    PdfCanvas& arcContinuous(double x1, double y1, double x2, double y2, double startAng, double extent);
    PdfCanvas& ellipse(double x1, double y1, double x2, double y2) { return arc(x1, y1, x2, y2, 0, 360); }
    static std::vector<std::array<double, 8>> bezierArc(double x1, double y1, double x2, double y2, double startAng, double extent);
    PdfCanvas& rectangle(double x, double y, double width, double height);
    PdfCanvas& rectangle(const Rectangle& rectangle) {
        return this->rectangle(rectangle.getX(), rectangle.getY(), rectangle.getWidth(), rectangle.getHeight());
    }
    PdfCanvas& roundRectangle(double x, double y, double width, double height, double radius);
    PdfCanvas& circle(double x, double y, double r);
    PdfCanvas& closePath();
    PdfCanvas& closePathEoFillStroke();
    PdfCanvas& closePathFillStroke();
    PdfCanvas& endPath();
    PdfCanvas& stroke();
    PdfCanvas& clip();
    PdfCanvas& eoClip();
    PdfCanvas& closePathStroke();
    PdfCanvas& fill();
    PdfCanvas& fillStroke();
    PdfCanvas& eoFill();
    PdfCanvas& eoFillStroke();

    PdfCanvas& setLineWidth(float lineWidth);
    PdfCanvas& setLineCapStyle(int lineCapStyle);
    PdfCanvas& setLineJoinStyle(int lineJoinStyle);
    PdfCanvas& setMiterLimit(float miterLimit);
    PdfCanvas& setLineDash(float phase);
    PdfCanvas& setLineDash(float unitsOn, float phase);
    PdfCanvas& setLineDash(float unitsOn, float unitsOff, float phase);
    PdfCanvas& setLineDash(const std::vector<float>& array, float phase);
    PdfCanvas& setRenderingIntent(const PdfNamePtr& renderingIntent);
    PdfCanvas& setFlatnessTolerance(float flatnessTolerance);

    PdfCanvas& setFillColor(const ColorPtr& color) { return setColor(color, true); }
    PdfCanvas& setStrokeColor(const ColorPtr& color) { return setColor(color, false); }
    PdfCanvas& setColor(const ColorPtr& color, bool fill);
    PdfCanvas& setFillColorGray(float g) { return setColor(std::make_shared<DeviceGray>(g), true); }
    PdfCanvas& setStrokeColorGray(float g) { return setColor(std::make_shared<DeviceGray>(g), false); }
    PdfCanvas& resetFillColorGray() { return setFillColorGray(0); }
    PdfCanvas& resetStrokeColorGray() { return setStrokeColorGray(0); }
    PdfCanvas& setFillColorRgb(float r, float g, float b) { return setColor(std::make_shared<DeviceRgb>(r, g, b), true); }
    PdfCanvas& setStrokeColorRgb(float r, float g, float b) { return setColor(std::make_shared<DeviceRgb>(r, g, b), false); }
    PdfCanvas& resetFillColorRgb() { return setFillColorRgb(0, 0, 0); }
    PdfCanvas& resetStrokeColorRgb() { return setStrokeColorRgb(0, 0, 0); }
    PdfCanvas& setFillColorCmyk(float c, float m, float y, float k) { return setColor(std::make_shared<DeviceCmyk>(c, m, y, k), true); }
    PdfCanvas& setStrokeColorCmyk(float c, float m, float y, float k) { return setColor(std::make_shared<DeviceCmyk>(c, m, y, k), false); }

    PdfCanvas& setExtGState(PdfExtGState& extGState);
    PdfCanvas& beginMarkedContent(const PdfNamePtr& tag, const PdfDictionaryPtr& properties = nullptr);
    PdfCanvas& endMarkedContent();

    PdfCanvas& addXObject(PdfXObject& xObject);
    PdfCanvas& addXObjectAt(PdfXObject& xObject, float x, float y);
    PdfCanvas& addXObjectFittedIntoRectangle(PdfXObject& xObject, const Rectangle& rect);
    PdfCanvas& addXObjectWithTransformationMatrix(PdfXObject& xObject, float a, float b, float c, float d, float e, float f);
    /// Adds an image; returns the created image XObject.
    std::shared_ptr<PdfImageXObject> addImageAt(const std::shared_ptr<io::ImageData>& image, float x, float y);
    std::shared_ptr<PdfImageXObject> addImageFittedIntoRectangle(const std::shared_ptr<io::ImageData>& image, const Rectangle& rect);
    std::shared_ptr<PdfImageXObject> addImageWithTransformationMatrix(const std::shared_ptr<io::ImageData>& image, float a,
                                                                      float b, float c, float d, float e, float f);

    PdfCanvas& writeLiteral(const std::string& s);
    PdfCanvas& writeLiteral(char c);
    PdfCanvas& writeLiteral(float n);
    void release() {}

protected:
    PdfOutputStream& out();
    PdfStreamPtr contentStream_;
    PdfResources* resources_;
    PdfDocument* document_;
    std::shared_ptr<CanvasGraphicsState> currentGs_;
    std::vector<std::shared_ptr<CanvasGraphicsState>> gsStack_;
    int mcDepth_ = 0;

private:
    void writeColorValues(const std::vector<float>& values);
    float getSubrangeWidth(const io::GlyphLine& text, int from, int to);
};

}  // namespace kernel
}  // namespace pdfcpp
