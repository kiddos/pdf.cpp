#include "pdfcpp/kernel/pdf/canvas/PdfCanvas.h"

#include <cmath>

#include "pdfcpp/kernel/font/PdfFont.h"
#include "pdfcpp/kernel/pdf/xobject/PdfXObject.h"

namespace pdfcpp {
namespace kernel {

// ============================================================ CanvasGraphicsState
void CanvasGraphicsState::updateFromExtGState(const PdfDictionary& extGState, PdfDocument* document) {
    if (auto lw = extGState.getAsNumber(PdfName::LW)) lineWidth_ = lw->floatValue();
    if (auto lc = extGState.getAsNumber(PdfName::LC)) lineCapStyle_ = lc->intValue();
    if (auto lj = extGState.getAsNumber(PdfName::LJ)) lineJoinStyle_ = lj->intValue();
    if (auto ml = extGState.getAsNumber(PdfName::ML)) miterLimit_ = ml->floatValue();
    if (auto d = extGState.getAsArray(PdfName::D)) dashPattern_ = d;
    if (auto ri = extGState.getAsName(PdfName::RI)) renderingIntent_ = ri;
    if (auto fl = extGState.getAsNumber(PdfName::FL)) flatnessTolerance_ = fl->floatValue();
    if (auto ca = extGState.getAsNumber(PdfName::ca)) fillOpacity_ = ca->floatValue();
    if (auto CA = extGState.getAsNumber(PdfName::CA)) strokeOpacity_ = CA->floatValue();
    if (auto tk = extGState.getAsBoolean(PdfName::TK)) textKnockout_ = tk->getValue();
    if (auto font = extGState.getAsArray(PdfName::Font)) {
        auto fontDict = font->getAsDictionary(0);
        auto size = font->getAsNumber(1);
        if (fontDict && document) font_ = document->getFont(fontDict);
        if (size) fontSize_ = size->floatValue();
    }
}

// ============================================================ PdfCanvas
static PdfStreamPtr getPageStream(PdfPage* page) {
    PdfStreamPtr stream = page->getLastContentStream();
    if (!stream || stream->getOutputStream() == nullptr || stream->containsKey(PdfName::Filter)) return page->newContentStreamAfter();
    return stream;
}

PdfCanvas::PdfCanvas(PdfStreamPtr contentStream, PdfResources* resources, PdfDocument* document)
    : contentStream_(std::move(contentStream)), resources_(resources), document_(document),
      currentGs_(std::make_shared<CanvasGraphicsState>()) {
    if (contentStream_->getOutputStream() == nullptr) contentStream_->initOutputStream();
}

PdfCanvas::PdfCanvas(PdfPage* page, bool wrapOldContent)
    : PdfCanvas(getPageStream(page), page->getResources(), page->getDocument()) {
    if (wrapOldContent) {
        // Wrap old content in q/Q to protect the new content from state changes (as iText does).
        page->newContentStreamBefore()->getOutputStream()->writeBytes(toBytes("q\n"));
        contentStream_->getOutputStream()->writeBytes(toBytes("Q\n"));
    }
}

PdfCanvas::PdfCanvas(PdfFormXObject& xObj, PdfDocument* document)
    : PdfCanvas(xObj.getPdfObject(), xObj.getResources(), document) {}

PdfOutputStream& PdfCanvas::out() { return *contentStream_->getOutputStream(); }

PdfCanvas& PdfCanvas::saveState() {
    gsStack_.push_back(currentGs_);
    currentGs_ = std::make_shared<CanvasGraphicsState>(*currentGs_);
    out().writeString("q\n");
    return *this;
}

PdfCanvas& PdfCanvas::restoreState() {
    if (gsStack_.empty()) throw PdfException(KernelExceptionMessageConstant::UNBALANCED_SAVE_RESTORE_STATE_OPERATORS);
    currentGs_ = gsStack_.back();
    gsStack_.pop_back();
    out().writeString("Q\n");
    return *this;
}

PdfCanvas& PdfCanvas::concatMatrix(double a, double b, double c, double d, double e, double f) {
    currentGs_->updateCtm(Matrix(static_cast<float>(a), static_cast<float>(b), static_cast<float>(c), static_cast<float>(d),
                                 static_cast<float>(e), static_cast<float>(f)));
    out().writeDouble(a).writeSpace().writeDouble(b).writeSpace().writeDouble(c).writeSpace().writeDouble(d).writeSpace()
        .writeDouble(e).writeSpace().writeDouble(f).writeString(" cm\n");
    return *this;
}

PdfCanvas& PdfCanvas::concatMatrix(const AffineTransform& t) {
    double m[6];
    t.getMatrix(m);
    return concatMatrix(m[0], m[1], m[2], m[3], m[4], m[5]);
}

PdfCanvas& PdfCanvas::beginText() {
    out().writeString("BT\n");
    return *this;
}

PdfCanvas& PdfCanvas::endText() {
    out().writeString("ET\n");
    return *this;
}

PdfCanvas& PdfCanvas::setFontAndSize(PdfFont* font, float size) {
    currentGs_->setFontSize(size);
    PdfNamePtr fontName = resources_->addFont(document_, font);
    currentGs_->setFont(font);
    out().write(*fontName).writeSpace().writeFloat(size).writeString(" Tf\n");
    return *this;
}

PdfCanvas& PdfCanvas::moveText(double x, double y) {
    out().writeDouble(x).writeSpace().writeDouble(y).writeString(" Td\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLeading(float leading) {
    currentGs_->setLeading(leading);
    out().writeFloat(leading).writeString(" TL\n");
    return *this;
}

PdfCanvas& PdfCanvas::moveTextWithLeading(float x, float y) {
    currentGs_->setLeading(-y);
    out().writeFloat(x).writeSpace().writeFloat(y).writeString(" TD\n");
    return *this;
}

PdfCanvas& PdfCanvas::newlineText() {
    out().writeString("T*\n");
    return *this;
}

PdfCanvas& PdfCanvas::newlineShowText(const std::string& text) {
    if (!currentGs_->getFont()) throw PdfException("Font and size must be set before writing any text.");
    currentGs_->getFont()->writeText(text, out());
    out().writeString("'\n");
    return *this;
}

PdfCanvas& PdfCanvas::newlineShowText(float wordSpacing, float charSpacing, const std::string& text) {
    if (!currentGs_->getFont()) throw PdfException("Font and size must be set before writing any text.");
    out().writeFloat(wordSpacing).writeSpace().writeFloat(charSpacing).writeSpace();
    currentGs_->getFont()->writeText(text, out());
    out().writeString("\"\n");
    currentGs_->setCharSpacing(charSpacing);
    currentGs_->setWordSpacing(wordSpacing);
    return *this;
}

PdfCanvas& PdfCanvas::setTextRenderingMode(int textRenderingMode) {
    currentGs_->setTextRenderingMode(textRenderingMode);
    out().writeInteger(textRenderingMode).writeString(" Tr\n");
    return *this;
}

PdfCanvas& PdfCanvas::setTextRise(float textRise) {
    currentGs_->setTextRise(textRise);
    out().writeFloat(textRise).writeString(" Ts\n");
    return *this;
}

PdfCanvas& PdfCanvas::setWordSpacing(float wordSpacing) {
    currentGs_->setWordSpacing(wordSpacing);
    out().writeFloat(wordSpacing).writeString(" Tw\n");
    return *this;
}

PdfCanvas& PdfCanvas::setCharacterSpacing(float charSpacing) {
    currentGs_->setCharSpacing(charSpacing);
    out().writeFloat(charSpacing).writeString(" Tc\n");
    return *this;
}

PdfCanvas& PdfCanvas::setHorizontalScaling(float scale) {
    currentGs_->setHorizontalScaling(scale);
    out().writeFloat(scale).writeString(" Tz\n");
    return *this;
}

PdfCanvas& PdfCanvas::setTextMatrix(float a, float b, float c, float d, float x, float y) {
    out().writeFloat(a).writeSpace().writeFloat(b).writeSpace().writeFloat(c).writeSpace().writeFloat(d).writeSpace()
        .writeFloat(x).writeSpace().writeFloat(y).writeString(" Tm\n");
    return *this;
}

PdfCanvas& PdfCanvas::setTextMatrix(const AffineTransform& t) {
    float m[6];
    t.getMatrix(m);
    return setTextMatrix(m[0], m[1], m[2], m[3], m[4], m[5]);
}

PdfCanvas& PdfCanvas::showText(const std::string& text) { return showText(currentGs_->getFont() ? currentGs_->getFont()->createGlyphLine(text) : io::GlyphLine()); }

float PdfCanvas::getSubrangeWidth(const io::GlyphLine& text, int from, int to) {
    float fontSize = currentGs_->getFontSize() / 1000.f;
    float charSpacing = currentGs_->getCharSpacing();
    float scaling = currentGs_->getHorizontalScaling() / 100.f;
    float width = 0;
    for (int i = from; i <= to; i++) {
        const io::Glyph& glyph = text.get(i);
        if (!glyph.hasPlacement()) {
            width += glyph.getWidth() * fontSize + glyph.getXAdvance() * fontSize + charSpacing;
            if (glyph.hasValidUnicode() && glyph.getUnicode() == ' ') width += currentGs_->getWordSpacing();
        }
    }
    return width * scaling;
}

PdfCanvas& PdfCanvas::showText(const io::GlyphLine& text) {
    PdfFont* font = currentGs_->getFont();
    if (!font) throw PdfException("Font and size must be set before writing any text.");
    bool hasAdvances = false;
    for (int i = text.getStart(); i < text.getEnd(); ++i)
        if (text.get(i).hasAdvance()) hasAdvances = true;
    if (!hasAdvances) {
        if (text.getEnd() > text.getStart()) {
            font->writeText(text, text.getStart(), text.getEnd() - 1, out());
            out().writeString("Tj\n");
        }
        return *this;
    }
    // Use TJ with adjustments for advances (e.g. kerning)
    out().writeByte('[');
    int sub = text.getStart();
    for (int i = text.getStart(); i < text.getEnd(); ++i) {
        const io::Glyph& g = text.get(i);
        if (g.getXAdvance() != 0) {
            font->writeText(text, sub, i, out());
            out().writeFloat(static_cast<float>(-g.getXAdvance()), true);
            sub = i + 1;
        }
    }
    if (sub < text.getEnd()) font->writeText(text, sub, text.getEnd() - 1, out());
    out().writeString("]TJ\n");
    return *this;
}

PdfCanvas& PdfCanvas::showText(const PdfArray& textArray) {
    if (!currentGs_->getFont()) throw PdfException("Font and size must be set before writing any text.");
    out().writeByte('[');
    for (const auto& obj : textArray) {
        if (obj->isString()) {
            const auto& s = static_cast<PdfString&>(*obj);
            currentGs_->getFont()->writeText(s.getValue(), out());
        } else if (obj->isNumber()) {
            out().writeFloat(static_cast<PdfNumber&>(*obj).floatValue());
        }
    }
    out().writeString("]TJ\n");
    return *this;
}

PdfCanvas& PdfCanvas::moveTo(double x, double y) {
    out().writeDouble(x).writeSpace().writeDouble(y).writeString(" m\n");
    return *this;
}

PdfCanvas& PdfCanvas::lineTo(double x, double y) {
    out().writeDouble(x).writeSpace().writeDouble(y).writeString(" l\n");
    return *this;
}

PdfCanvas& PdfCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3) {
    out().writeDouble(x1).writeSpace().writeDouble(y1).writeSpace().writeDouble(x2).writeSpace().writeDouble(y2).writeSpace()
        .writeDouble(x3).writeSpace().writeDouble(y3).writeString(" c\n");
    return *this;
}

PdfCanvas& PdfCanvas::curveTo(double x2, double y2, double x3, double y3) {
    out().writeDouble(x2).writeSpace().writeDouble(y2).writeSpace().writeDouble(x3).writeSpace().writeDouble(y3).writeString(" v\n");
    return *this;
}

PdfCanvas& PdfCanvas::curveFromTo(double x1, double y1, double x3, double y3) {
    out().writeDouble(x1).writeSpace().writeDouble(y1).writeSpace().writeDouble(x3).writeSpace().writeDouble(y3).writeString(" y\n");
    return *this;
}

std::vector<std::array<double, 8>> PdfCanvas::bezierArc(double x1, double y1, double x2, double y2, double startAng, double extent) {
    double tmp;
    if (x1 > x2) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 > y1) {
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    double fragAngle;
    int nFrag;
    if (std::fabs(extent) <= 90.0) {
        fragAngle = extent;
        nFrag = 1;
    } else {
        nFrag = static_cast<int>(std::ceil(std::fabs(extent) / 90.0));
        fragAngle = extent / nFrag;
    }
    double xCen = (x1 + x2) / 2., yCen = (y1 + y2) / 2.;
    double rx = (x2 - x1) / 2., ry = (y2 - y1) / 2.;
    double halfAng = fragAngle * M_PI / 360.;
    // A zero extent is a degenerate arc: all control points coincide (Java divides 0 by 0 here and emits NaN).
    double kappa = halfAng == 0 ? 0. : std::fabs(4. / 3. * (1. - std::cos(halfAng)) / std::sin(halfAng));
    std::vector<std::array<double, 8>> pointList;
    for (int i = 0; i < nFrag; ++i) {
        double theta0 = (startAng + i * fragAngle) * M_PI / 180.;
        double theta1 = (startAng + (i + 1) * fragAngle) * M_PI / 180.;
        double cos0 = std::cos(theta0), cos1 = std::cos(theta1), sin0 = std::sin(theta0), sin1 = std::sin(theta1);
        if (fragAngle > 0) {
            pointList.push_back({xCen + rx * cos0, yCen - ry * sin0, xCen + rx * (cos0 - kappa * sin0),
                                 yCen - ry * (sin0 + kappa * cos0), xCen + rx * (cos1 + kappa * sin1),
                                 yCen - ry * (sin1 - kappa * cos1), xCen + rx * cos1, yCen - ry * sin1});
        } else {
            pointList.push_back({xCen + rx * cos0, yCen - ry * sin0, xCen + rx * (cos0 + kappa * sin0),
                                 yCen - ry * (sin0 - kappa * cos0), xCen + rx * (cos1 - kappa * sin1),
                                 yCen - ry * (sin1 + kappa * cos1), xCen + rx * cos1, yCen - ry * sin1});
        }
    }
    return pointList;
}

PdfCanvas& PdfCanvas::arc(double x1, double y1, double x2, double y2, double startAng, double extent) {
    auto ar = bezierArc(x1, y1, x2, y2, startAng, extent);
    if (ar.empty()) return *this;
    moveTo(ar[0][0], ar[0][1]);
    for (const auto& pt : ar) curveTo(pt[2], pt[3], pt[4], pt[5], pt[6], pt[7]);
    return *this;
}

PdfCanvas& PdfCanvas::arcContinuous(double x1, double y1, double x2, double y2, double startAng, double extent) {
    auto ar = bezierArc(x1, y1, x2, y2, startAng, extent);
    if (ar.empty()) return *this;
    lineTo(ar[0][0], ar[0][1]);
    for (const auto& pt : ar) curveTo(pt[2], pt[3], pt[4], pt[5], pt[6], pt[7]);
    return *this;
}

PdfCanvas& PdfCanvas::rectangle(double x, double y, double width, double height) {
    out().writeDouble(x).writeSpace().writeDouble(y).writeSpace().writeDouble(width).writeSpace().writeDouble(height).writeString(" re\n");
    return *this;
}

PdfCanvas& PdfCanvas::roundRectangle(double x, double y, double width, double height, double radius) {
    if (width < 0) {
        x += width;
        width = -width;
    }
    if (height < 0) {
        y += height;
        height = -height;
    }
    if (radius < 0) radius = -radius;
    const double curv = 0.4477f;
    moveTo(x + radius, y);
    lineTo(x + width - radius, y);
    curveTo(x + width - radius * curv, y, x + width, y + radius * curv, x + width, y + radius);
    lineTo(x + width, y + height - radius);
    curveTo(x + width, y + height - radius * curv, x + width - radius * curv, y + height, x + width - radius, y + height);
    lineTo(x + radius, y + height);
    curveTo(x + radius * curv, y + height, x, y + height - radius * curv, x, y + height - radius);
    lineTo(x, y + radius);
    curveTo(x, y + radius * curv, x + radius * curv, y, x + radius, y);
    return *this;
}

PdfCanvas& PdfCanvas::circle(double x, double y, double r) {
    const double curve = 0.5523f;
    moveTo(x + r, y);
    curveTo(x + r, y + r * curve, x + r * curve, y + r, x, y + r);
    curveTo(x - r * curve, y + r, x - r, y + r * curve, x - r, y);
    curveTo(x - r, y - r * curve, x - r * curve, y - r, x, y - r);
    curveTo(x + r * curve, y - r, x + r, y - r * curve, x + r, y);
    return *this;
}

#define PDFCPP_OP(name, op)                \
    PdfCanvas& PdfCanvas::name() {        \
        out().writeString(op "\n");        \
        return *this;                      \
    }
PDFCPP_OP(closePath, "h")
PDFCPP_OP(closePathEoFillStroke, "b*")
PDFCPP_OP(closePathFillStroke, "b")
PDFCPP_OP(endPath, "n")
PDFCPP_OP(stroke, "S")
PDFCPP_OP(clip, "W")
PDFCPP_OP(eoClip, "W*")
PDFCPP_OP(closePathStroke, "s")
PDFCPP_OP(fill, "f")
PDFCPP_OP(fillStroke, "B")
PDFCPP_OP(eoFill, "f*")
PDFCPP_OP(eoFillStroke, "B*")
#undef PDFCPP_OP

PdfCanvas& PdfCanvas::setLineWidth(float lineWidth) {
    if (currentGs_->getLineWidth() == lineWidth) return *this;
    currentGs_->setLineWidth(lineWidth);
    out().writeFloat(lineWidth).writeString(" w\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineCapStyle(int lineCapStyle) {
    if (currentGs_->getLineCapStyle() == lineCapStyle) return *this;
    currentGs_->setLineCapStyle(lineCapStyle);
    out().writeInteger(lineCapStyle).writeString(" J\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineJoinStyle(int lineJoinStyle) {
    if (currentGs_->getLineJoinStyle() == lineJoinStyle) return *this;
    currentGs_->setLineJoinStyle(lineJoinStyle);
    out().writeInteger(lineJoinStyle).writeString(" j\n");
    return *this;
}

PdfCanvas& PdfCanvas::setMiterLimit(float miterLimit) {
    if (currentGs_->getMiterLimit() == miterLimit) return *this;
    currentGs_->setMiterLimit(miterLimit);
    out().writeFloat(miterLimit).writeString(" M\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineDash(float phase) {
    out().writeString("[] ").writeFloat(phase).writeString(" d\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineDash(float unitsOn, float phase) {
    out().writeByte('[').writeFloat(unitsOn).writeString("] ").writeFloat(phase).writeString(" d\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineDash(float unitsOn, float unitsOff, float phase) {
    out().writeByte('[').writeFloat(unitsOn).writeSpace().writeFloat(unitsOff).writeString("] ").writeFloat(phase).writeString(" d\n");
    return *this;
}

PdfCanvas& PdfCanvas::setLineDash(const std::vector<float>& array, float phase) {
    out().writeByte('[');
    for (size_t i = 0; i < array.size(); ++i) {
        out().writeFloat(array[i]);
        if (i + 1 < array.size()) out().writeSpace();
    }
    out().writeString("] ").writeFloat(phase).writeString(" d\n");
    return *this;
}

PdfCanvas& PdfCanvas::setRenderingIntent(const PdfNamePtr& renderingIntent) {
    currentGs_->setRenderingIntent(renderingIntent);
    out().write(*renderingIntent).writeString(" ri\n");
    return *this;
}

PdfCanvas& PdfCanvas::setFlatnessTolerance(float flatnessTolerance) {
    currentGs_->setFlatnessTolerance(flatnessTolerance);
    out().writeFloat(flatnessTolerance).writeString(" i\n");
    return *this;
}

void PdfCanvas::writeColorValues(const std::vector<float>& values) {
    for (float v : values) out().writeFloat(v).writeSpace();
}

PdfCanvas& PdfCanvas::setColor(const ColorPtr& color, bool fill) {
    if (!color) return *this;
    ColorPtr current = fill ? currentGs_->getFillColor() : currentGs_->getStrokeColor();
    if (current && *current == *color) return *this;
    if (fill) currentGs_->setFillColor(color);
    else currentGs_->setStrokeColor(color);
    auto cs = color->getColorSpace();
    if (cs && cs->isDevice()) {
        writeColorValues(color->getColorValue());
        int n = cs->getNumberOfComponents();
        const char* op = n == 1 ? (fill ? "g" : "G") : n == 3 ? (fill ? "rg" : "RG") : (fill ? "k" : "K");
        out().writeString(op).writeNewLine();
    } else if (cs) {
        PdfNamePtr csName = resources_->addColorSpace(cs->getPdfObject());
        out().write(*csName).writeString(fill ? " cs\n" : " CS\n");
        writeColorValues(color->getColorValue());
        out().writeString(fill ? "scn\n" : "SCN\n");
    }
    return *this;
}

PdfCanvas& PdfCanvas::setExtGState(PdfExtGState& extGState) {
    if (!extGState.isFlushed()) currentGs_->updateFromExtGState(*extGState.getPdfObject(), document_);
    PdfNamePtr name = resources_->addExtGState(extGState);
    out().write(*name).writeString(" gs\n");
    return *this;
}

PdfCanvas& PdfCanvas::beginMarkedContent(const PdfNamePtr& tag, const PdfDictionaryPtr& properties) {
    ++mcDepth_;
    out().write(*tag).writeSpace();
    if (!properties) {
        out().writeString("BMC\n");
    } else if (properties->getIndirectReference() == nullptr) {
        out().write(*properties).writeString(" BDC\n");
    } else {
        out().write(*resources_->addProperties(properties)).writeString(" BDC\n");
    }
    return *this;
}

PdfCanvas& PdfCanvas::endMarkedContent() {
    if (--mcDepth_ < 0) throw PdfException(KernelExceptionMessageConstant::UNBALANCED_BEGIN_END_MARKED_CONTENT_OPERATORS);
    out().writeString("EMC\n");
    return *this;
}

PdfCanvas& PdfCanvas::addXObject(PdfXObject& xObject) {
    PdfNamePtr name;
    if (auto* image = dynamic_cast<PdfImageXObject*>(&xObject)) name = resources_->addImage(*image);
    else if (auto* form = dynamic_cast<PdfFormXObject*>(&xObject)) name = resources_->addForm(*form);
    else return *this;
    out().write(*name).writeString(" Do\n");
    return *this;
}

PdfCanvas& PdfCanvas::addXObjectWithTransformationMatrix(PdfXObject& xObject, float a, float b, float c, float d, float e, float f) {
    saveState();
    concatMatrix(a, b, c, d, e, f);
    addXObject(xObject);
    return restoreState();
}

PdfCanvas& PdfCanvas::addXObjectAt(PdfXObject& xObject, float x, float y) {
    if (xObject.isImage()) return addXObjectWithTransformationMatrix(xObject, xObject.getWidth(), 0, 0, xObject.getHeight(), x, y);
    auto* form = dynamic_cast<PdfFormXObject*>(&xObject);
    float bx = 0, by = 0;
    if (form && form->getBBox()) {
        bx = form->getBBox()->getX();
        by = form->getBBox()->getY();
    }
    return addXObjectWithTransformationMatrix(xObject, 1, 0, 0, 1, x - bx, y - by);
}

PdfCanvas& PdfCanvas::addXObjectFittedIntoRectangle(PdfXObject& xObject, const Rectangle& rect) {
    if (xObject.isImage())
        return addXObjectWithTransformationMatrix(xObject, rect.getWidth(), 0, 0, rect.getHeight(), rect.getX(), rect.getY());
    float w = xObject.getWidth(), h = xObject.getHeight();
    float sx = w != 0 ? rect.getWidth() / w : 1, sy = h != 0 ? rect.getHeight() / h : 1;
    auto* form = dynamic_cast<PdfFormXObject*>(&xObject);
    float bx = 0, by = 0;
    if (form && form->getBBox()) {
        bx = form->getBBox()->getX();
        by = form->getBBox()->getY();
    }
    return addXObjectWithTransformationMatrix(xObject, sx, 0, 0, sy, rect.getX() - bx * sx, rect.getY() - by * sy);
}

std::shared_ptr<PdfImageXObject> PdfCanvas::addImageWithTransformationMatrix(const std::shared_ptr<io::ImageData>& image, float a,
                                                                             float b, float c, float d, float e, float f) {
    auto xObject = std::make_shared<PdfImageXObject>(image);
    addXObjectWithTransformationMatrix(*xObject, a, b, c, d, e, f);
    return xObject;
}

std::shared_ptr<PdfImageXObject> PdfCanvas::addImageAt(const std::shared_ptr<io::ImageData>& image, float x, float y) {
    return addImageWithTransformationMatrix(image, image->getWidth(), 0, 0, image->getHeight(), x, y);
}

std::shared_ptr<PdfImageXObject> PdfCanvas::addImageFittedIntoRectangle(const std::shared_ptr<io::ImageData>& image, const Rectangle& rect) {
    return addImageWithTransformationMatrix(image, rect.getWidth(), 0, 0, rect.getHeight(), rect.getX(), rect.getY());
}

PdfCanvas& PdfCanvas::writeLiteral(const std::string& s) {
    out().writeString(s);
    return *this;
}

PdfCanvas& PdfCanvas::writeLiteral(char c) {
    out().writeByte(c);
    return *this;
}

PdfCanvas& PdfCanvas::writeLiteral(float n) {
    out().writeFloat(n);
    return *this;
}

}  // namespace kernel
}  // namespace pdfcpp
