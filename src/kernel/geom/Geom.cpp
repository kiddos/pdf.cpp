#include "pdfcpp/kernel/geom/Geom.h"

#include <algorithm>
#include <limits>
#include <sstream>

#include "pdfcpp/io/source/ByteUtils.h"

namespace pdfcpp {
namespace kernel {

static std::string fmt(double v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

std::string Point::toString() const { return "Point: [x=" + fmt(x) + ",y=" + fmt(y) + "]"; }

// ---------------------------------------------------------------- Matrix
Matrix Matrix::multiply(const Matrix& by) const {
    const auto& a = vals_;
    const auto& b = by.vals_;
    Matrix rslt;
    auto& c = rslt.vals_;
    c[I11] = a[I11] * b[I11] + a[I12] * b[I21] + a[I13] * b[I31];
    c[I12] = a[I11] * b[I12] + a[I12] * b[I22] + a[I13] * b[I32];
    c[I13] = a[I11] * b[I13] + a[I12] * b[I23] + a[I13] * b[I33];
    c[I21] = a[I21] * b[I11] + a[I22] * b[I21] + a[I23] * b[I31];
    c[I22] = a[I21] * b[I12] + a[I22] * b[I22] + a[I23] * b[I32];
    c[I23] = a[I21] * b[I13] + a[I22] * b[I23] + a[I23] * b[I33];
    c[I31] = a[I31] * b[I11] + a[I32] * b[I21] + a[I33] * b[I31];
    c[I32] = a[I31] * b[I12] + a[I32] * b[I22] + a[I33] * b[I32];
    c[I33] = a[I31] * b[I13] + a[I32] * b[I23] + a[I33] * b[I33];
    return rslt;
}

Matrix Matrix::add(const Matrix& arg) const {
    Matrix r;
    for (int i = 0; i < 9; ++i) r.vals_[i] = vals_[i] + arg.vals_[i];
    return r;
}

Matrix Matrix::subtract(const Matrix& arg) const {
    Matrix r;
    for (int i = 0; i < 9; ++i) r.vals_[i] = vals_[i] - arg.vals_[i];
    return r;
}

float Matrix::getDeterminant() const {
    return vals_[I11] * vals_[I22] * vals_[I33] + vals_[I12] * vals_[I23] * vals_[I31] +
           vals_[I13] * vals_[I21] * vals_[I32] - vals_[I11] * vals_[I23] * vals_[I32] -
           vals_[I12] * vals_[I21] * vals_[I33] - vals_[I13] * vals_[I22] * vals_[I31];
}

std::string Matrix::toString() const {
    return fmt(vals_[I11]) + "\t" + fmt(vals_[I12]) + "\t" + fmt(vals_[I13]) + "\n" + fmt(vals_[I21]) + "\t" +
           fmt(vals_[I22]) + "\t" + fmt(vals_[I23]) + "\n" + fmt(vals_[I31]) + "\t" + fmt(vals_[I32]) + "\t" +
           fmt(vals_[I33]);
}

// ---------------------------------------------------------------- Vector
Vector Vector::cross(const Matrix& by) const {
    float x = vals_[0] * by.get(Matrix::I11) + vals_[1] * by.get(Matrix::I21) + vals_[2] * by.get(Matrix::I31);
    float y = vals_[0] * by.get(Matrix::I12) + vals_[1] * by.get(Matrix::I22) + vals_[2] * by.get(Matrix::I32);
    float z = vals_[0] * by.get(Matrix::I13) + vals_[1] * by.get(Matrix::I23) + vals_[2] * by.get(Matrix::I33);
    return Vector(x, y, z);
}

Vector Vector::cross(const Vector& with) const {
    float x = vals_[1] * with.vals_[2] - vals_[2] * with.vals_[1];
    float y = vals_[2] * with.vals_[0] - vals_[0] * with.vals_[2];
    float z = vals_[0] * with.vals_[1] - vals_[1] * with.vals_[0];
    return Vector(x, y, z);
}

Vector Vector::normalize() const {
    float l = length();
    return Vector(vals_[0] / l, vals_[1] / l, vals_[2] / l);
}

std::string Vector::toString() const { return fmt(vals_[0]) + "," + fmt(vals_[1]) + "," + fmt(vals_[2]); }

// ---------------------------------------------------------------- LineSegment
Rectangle LineSegment::getBoundingRectangle() const {
    float x1 = startPoint_.get(0), y1 = startPoint_.get(1), x2 = endPoint_.get(0), y2 = endPoint_.get(1);
    return Rectangle(std::min(x1, x2), std::min(y1, y2), std::fabs(x2 - x1), std::fabs(y2 - y1));
}

bool LineSegment::containsPoint(const Vector& point) const {
    Vector diff1 = point.subtract(startPoint_);
    if (diff1.get(0) < 0 || diff1.get(1) < 0 || diff1.get(2) < 0) return false;
    Vector diff2 = endPoint_.subtract(point);
    if (diff2.get(0) < 0 || diff2.get(1) < 0 || diff2.get(2) < 0) return false;
    return true;
}

// ---------------------------------------------------------------- AffineTransform
AffineTransform::AffineTransform(const std::vector<float>& matrix) {
    m00_ = matrix[0];
    m10_ = matrix[1];
    m01_ = matrix[2];
    m11_ = matrix[3];
    if (matrix.size() > 4) {
        m02_ = matrix[4];
        m12_ = matrix[5];
    }
}

AffineTransform::AffineTransform(const std::vector<double>& matrix) {
    m00_ = matrix[0];
    m10_ = matrix[1];
    m01_ = matrix[2];
    m11_ = matrix[3];
    if (matrix.size() > 4) {
        m02_ = matrix[4];
        m12_ = matrix[5];
    }
}

void AffineTransform::getMatrix(float matrix[6]) const {
    matrix[0] = static_cast<float>(m00_);
    matrix[1] = static_cast<float>(m10_);
    matrix[2] = static_cast<float>(m01_);
    matrix[3] = static_cast<float>(m11_);
    matrix[4] = static_cast<float>(m02_);
    matrix[5] = static_cast<float>(m12_);
}

void AffineTransform::getMatrix(double matrix[6]) const {
    matrix[0] = m00_;
    matrix[1] = m10_;
    matrix[2] = m01_;
    matrix[3] = m11_;
    matrix[4] = m02_;
    matrix[5] = m12_;
}

void AffineTransform::setTransform(double m00, double m10, double m01, double m11, double m02, double m12) {
    m00_ = m00;
    m10_ = m10;
    m01_ = m01;
    m11_ = m11;
    m02_ = m02;
    m12_ = m12;
}

void AffineTransform::setToRotation(double angle) {
    double sin = std::sin(angle);
    double cos = std::cos(angle);
    if (std::fabs(cos) < 1E-10) {
        cos = 0.0;
        sin = sin > 0.0 ? 1.0 : -1.0;
    } else if (std::fabs(sin) < 1E-10) {
        sin = 0.0;
        cos = cos > 0.0 ? 1.0 : -1.0;
    }
    setTransform(cos, sin, -sin, cos, 0, 0);
}

void AffineTransform::setToRotation(double angle, double px, double py) {
    setToRotation(angle);
    m02_ = px * (1.0 - m00_) + py * m10_;
    m12_ = py * (1.0 - m00_) - px * m10_;
}

AffineTransform AffineTransform::getTranslateInstance(double mx, double my) {
    AffineTransform t;
    t.setToTranslation(mx, my);
    return t;
}
AffineTransform AffineTransform::getScaleInstance(double scx, double scy) {
    AffineTransform t;
    t.setToScale(scx, scy);
    return t;
}
AffineTransform AffineTransform::getShearInstance(double shx, double shy) {
    AffineTransform t;
    t.setToShear(shx, shy);
    return t;
}
AffineTransform AffineTransform::getRotateInstance(double angle) {
    AffineTransform t;
    t.setToRotation(angle);
    return t;
}
AffineTransform AffineTransform::getRotateInstance(double angle, double x, double y) {
    AffineTransform t;
    t.setToRotation(angle, x, y);
    return t;
}

AffineTransform AffineTransform::multiply(const AffineTransform& t1, const AffineTransform& t2) {
    return AffineTransform(t1.m00_ * t2.m00_ + t1.m10_ * t2.m01_, t1.m00_ * t2.m10_ + t1.m10_ * t2.m11_,
                           t1.m01_ * t2.m00_ + t1.m11_ * t2.m01_, t1.m01_ * t2.m10_ + t1.m11_ * t2.m11_,
                           t1.m02_ * t2.m00_ + t1.m12_ * t2.m01_ + t2.m02_,
                           t1.m02_ * t2.m10_ + t1.m12_ * t2.m11_ + t2.m12_);
}

void AffineTransform::concatenate(const AffineTransform& t) { *this = multiply(t, *this); }
void AffineTransform::preConcatenate(const AffineTransform& t) { *this = multiply(*this, t); }

AffineTransform AffineTransform::createInverse() const {
    double det = getDeterminant();
    if (std::fabs(det) < std::numeric_limits<double>::min() * 2) throw std::runtime_error("Determinant is zero. Cannot invert transformation.");
    return AffineTransform(m11_ / det, -m10_ / det, -m01_ / det, m00_ / det, (m01_ * m12_ - m11_ * m02_) / det,
                           (m10_ * m02_ - m00_ * m12_) / det);
}

Point AffineTransform::transform(const Point& src) const {
    return Point(src.x * m00_ + src.y * m01_ + m02_, src.x * m10_ + src.y * m11_ + m12_);
}

void AffineTransform::transform(const float* src, int srcOff, float* dst, int dstOff, int numPts) const {
    std::vector<float> tmp(src + srcOff, src + srcOff + numPts * 2);
    for (int i = 0; i < numPts; ++i) {
        double x = tmp[2 * i], y = tmp[2 * i + 1];
        dst[dstOff + 2 * i] = static_cast<float>(x * m00_ + y * m01_ + m02_);
        dst[dstOff + 2 * i + 1] = static_cast<float>(x * m10_ + y * m11_ + m12_);
    }
}

void AffineTransform::transform(const double* src, int srcOff, double* dst, int dstOff, int numPts) const {
    std::vector<double> tmp(src + srcOff, src + srcOff + numPts * 2);
    for (int i = 0; i < numPts; ++i) {
        double x = tmp[2 * i], y = tmp[2 * i + 1];
        dst[dstOff + 2 * i] = x * m00_ + y * m01_ + m02_;
        dst[dstOff + 2 * i + 1] = x * m10_ + y * m11_ + m12_;
    }
}

Point AffineTransform::deltaTransform(const Point& src) const {
    return Point(src.x * m00_ + src.y * m01_, src.x * m10_ + src.y * m11_);
}

Point AffineTransform::inverseTransform(const Point& src) const { return createInverse().transform(src); }

// ---------------------------------------------------------------- Rectangle
Rectangle Rectangle::getCommonRectangle(const std::vector<Rectangle>& rectangles) {
    float ury = -std::numeric_limits<float>::max();
    float llx = std::numeric_limits<float>::max();
    float lly = std::numeric_limits<float>::max();
    float urx = -std::numeric_limits<float>::max();
    for (const auto& rectangle : rectangles) {
        Rectangle rec = rectangle;
        if (rec.getY() < lly) lly = rec.getY();
        if (rec.getX() < llx) llx = rec.getX();
        if (rec.getY() + rec.getHeight() > ury) ury = rec.getY() + rec.getHeight();
        if (rec.getX() + rec.getWidth() > urx) urx = rec.getX() + rec.getWidth();
    }
    return Rectangle(llx, lly, urx - llx, ury - lly);
}

Rectangle Rectangle::calculateBBox(const std::vector<Point>& points) {
    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = -std::numeric_limits<double>::max(), maxY = maxX;
    for (const auto& p : points) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    return Rectangle(static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(maxX - minX),
                     static_cast<float>(maxY - minY));
}

std::vector<Point> Rectangle::toPointsArray() const {
    return {Point(x_, y_), Point(x_ + width_, y_), Point(x_ + width_, y_ + height_), Point(x_, y_ + height_)};
}

bool Rectangle::getIntersection(const Rectangle& rect, Rectangle& out) const {
    float llx = std::max(x_, rect.x_);
    float lly = std::max(y_, rect.y_);
    float urx = std::min(getRight(), rect.getRight());
    float ury = std::min(getTop(), rect.getTop());
    float width = urx - llx;
    if (std::fabs(width) < EPS) width = 0;
    float height = ury - lly;
    if (std::fabs(height) < EPS) height = 0;
    if (width >= 0 && height >= 0) {
        out = Rectangle(llx, lly, width, height);
        return true;
    }
    return false;
}

bool Rectangle::contains(const Rectangle& rect) const {
    float llx = getX(), lly = getY(), urx = llx + getWidth(), ury = lly + getHeight();
    float rllx = rect.getX(), rlly = rect.getY(), rurx = rllx + rect.getWidth(), rury = rlly + rect.getHeight();
    return llx - EPS <= rllx && lly - EPS <= rlly && rurx <= urx + EPS && rury <= ury + EPS;
}

bool Rectangle::overlaps(const Rectangle& rect, float epsilon) const {
    if ((getX() + getWidth()) < (rect.getX() + epsilon)) return false;
    if ((getX() + epsilon) > (rect.getX() + rect.getWidth())) return false;
    if ((getY() + getHeight()) < (rect.getY() + epsilon)) return false;
    if ((getY() + epsilon) > (rect.getY() + rect.getHeight())) return false;
    return true;
}

Rectangle& Rectangle::setBbox(float llx, float lly, float urx, float ury) {
    if (llx > urx) std::swap(llx, urx);
    if (lly > ury) std::swap(lly, ury);
    x_ = llx;
    y_ = lly;
    width_ = urx - llx;
    height_ = ury - lly;
    return *this;
}

Rectangle& Rectangle::applyMargins(float topIndent, float rightIndent, float bottomIndent, float leftIndent, bool reverse) {
    float s = reverse ? -1.f : 1.f;
    x_ += leftIndent * s;
    width_ -= (leftIndent + rightIndent) * s;
    y_ += bottomIndent * s;
    height_ -= (topIndent + bottomIndent) * s;
    return *this;
}

static int relativeCCW(double x1, double y1, double x2, double y2, double px, double py) {
    x2 -= x1;
    y2 -= y1;
    px -= x1;
    py -= y1;
    double t = px * y2 - py * x2;
    if (t == 0.0) {
        t = px * x2 + py * y2;
        if (t > 0.0) {
            px -= x2;
            py -= y2;
            t = px * x2 + py * y2;
            if (t < 0.0) t = 0.0;
        }
    }
    return t < 0.0 ? -1 : (t > 0.0 ? 1 : 0);
}

static bool linesIntersect(double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4) {
    return ((relativeCCW(x1, y1, x2, y2, x3, y3) * relativeCCW(x1, y1, x2, y2, x4, y4) <= 0) &&
            (relativeCCW(x3, y3, x4, y4, x1, y1) * relativeCCW(x3, y3, x4, y4, x2, y2) <= 0));
}

bool Rectangle::intersectsLine(float x1, float y1, float x2, float y2) const {
    double rx1 = getX(), ry1 = getY(), rx2 = rx1 + getWidth(), ry2 = ry1 + getHeight();
    return (rx1 <= x1 && x1 <= rx2 && ry1 <= y1 && y1 <= ry2) || (rx1 <= x2 && x2 <= rx2 && ry1 <= y2 && y2 <= ry2) ||
           linesIntersect(rx1, ry1, rx2, ry2, x1, y1, x2, y2) || linesIntersect(rx2, ry1, rx1, ry2, x1, y1, x2, y2);
}

std::string Rectangle::toString() const {
    return "Rectangle: " + fmt(getWidth()) + 'x' + fmt(getHeight());
}

bool Rectangle::equalsWithEpsilon(const Rectangle& that, float eps) const {
    float dx = std::fabs(x_ - that.x_), dy = std::fabs(y_ - that.y_);
    float dw = std::fabs(width_ - that.width_), dh = std::fabs(height_ - that.height_);
    return dx < eps && dy < eps && dw < eps && dh < eps;
}

const PageSize PageSize::A0(2384, 3370), PageSize::A1(1684, 2384), PageSize::A2(1190, 1684), PageSize::A3(842, 1190),
    PageSize::A4(595, 842), PageSize::A5(420, 595), PageSize::A6(298, 420), PageSize::A7(210, 298),
    PageSize::A8(148, 210), PageSize::A9(105, 148), PageSize::A10(74, 105);
const PageSize PageSize::B0(2834, 4008), PageSize::B1(2004, 2834), PageSize::B2(1417, 2004), PageSize::B3(1000, 1417),
    PageSize::B4(708, 1000), PageSize::B5(498, 708), PageSize::B6(354, 498), PageSize::B7(249, 354),
    PageSize::B8(175, 249), PageSize::B9(124, 175), PageSize::B10(88, 124);
const PageSize PageSize::DEFAULT(595, 842), PageSize::EXECUTIVE(522, 756), PageSize::LEDGER(1224, 792),
    PageSize::LEGAL(612, 1008), PageSize::LETTER(612, 792), PageSize::TABLOID(792, 1224);

// ---------------------------------------------------------------- Path
Point Subpath::getLastPoint() const {
    Point lastPoint = startPoint_;
    if (!segments_.empty() && !closed_) {
        auto pts = segments_.back()->getBasePoints();
        lastPoint = pts.back();
    }
    return lastPoint;
}

void Subpath::addSegment(std::shared_ptr<IShape> segment) {
    if (closed_) return;
    if (isSinglePointOpen()) startPoint_ = segment->getBasePoints()[0];
    segments_.push_back(std::move(segment));
}

bool Subpath::isDegenerate() const {
    if (!segments_.empty() && closed_) return false;
    for (const auto& segment : segments_) {
        auto pts = segment->getBasePoints();
        for (const auto& p : pts)
            if (!(p == startPoint_)) return false;
    }
    return true;
}

static std::vector<Point> approximateBezier(const std::vector<Point>& p) {
    std::vector<Point> out;
    const int steps = 16;
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double mt = 1 - t;
        double x = mt * mt * mt * p[0].x + 3 * mt * mt * t * p[1].x + 3 * mt * t * t * p[2].x + t * t * t * p[3].x;
        double y = mt * mt * mt * p[0].y + 3 * mt * mt * t * p[1].y + 3 * mt * t * t * p[2].y + t * t * t * p[3].y;
        out.emplace_back(x, y);
    }
    return out;
}

std::vector<Point> Subpath::getPiecewiseLinearApproximation() const {
    std::vector<Point> result;
    if (segments_.empty()) return result;
    result.push_back(startPoint_);
    for (const auto& seg : segments_) {
        auto pts = seg->getBasePoints();
        if (dynamic_cast<BezierCurve*>(seg.get())) {
            auto approx = approximateBezier(pts);
            result.insert(result.end(), approx.begin(), approx.end());
        } else {
            result.push_back(pts.back());
        }
    }
    return result;
}

void Path::addSubpath(const Subpath& subpath) {
    subpaths_.push_back(subpath);
    currentPoint_ = subpath.getLastPoint();
}

void Path::addSubpaths(const std::vector<Subpath>& subpaths) {
    if (subpaths.empty()) return;
    subpaths_.insert(subpaths_.end(), subpaths.begin(), subpaths.end());
    currentPoint_ = subpaths_.back().getLastPoint();
}

void Path::moveTo(float x, float y) {
    currentPoint_ = Point(x, y);
    Subpath* last = getLastSubpath();
    if (last && last->isSinglePointOpen()) last->setStartPoint(currentPoint_);
    else subpaths_.emplace_back(currentPoint_);
}

void Path::lineTo(float x, float y) {
    if (subpaths_.empty()) throw std::runtime_error("Path shall start with \"re\" or \"m\" operator");
    Point target(x, y);
    getLastSubpath()->addSegment(std::make_shared<Line>(currentPoint_, target));
    currentPoint_ = target;
}

void Path::curveTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    if (subpaths_.empty()) throw std::runtime_error("Path shall start with \"re\" or \"m\" operator");
    Point p2(x1, y1), p3(x2, y2), p4(x3, y3);
    getLastSubpath()->addSegment(std::make_shared<BezierCurve>(std::vector<Point>{currentPoint_, p2, p3, p4}));
    currentPoint_ = p4;
}

void Path::curveTo(float x2, float y2, float x3, float y3) {
    curveTo(static_cast<float>(currentPoint_.x), static_cast<float>(currentPoint_.y), x2, y2, x3, y3);
}

void Path::curveFromTo(float x1, float y1, float x3, float y3) { curveTo(x1, y1, x3, y3, x3, y3); }

void Path::rectangle(const Rectangle& rect) { rectangle(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight()); }

void Path::rectangle(float x, float y, float w, float h) {
    moveTo(x, y);
    lineTo(x + w, y);
    lineTo(x + w, y + h);
    lineTo(x, y + h);
    closeSubpath();
}

void Path::closeSubpath() {
    Subpath* last = getLastSubpath();
    if (last && !last->isClosed()) {
        last->setClosed(true);
        Point startPoint = last->getStartPoint();
        moveTo(static_cast<float>(startPoint.x), static_cast<float>(startPoint.y));
    }
}

void Path::closeAllSubpaths() {
    for (auto& subpath : subpaths_) subpath.setClosed(true);
}

void Path::replaceCloseWithLine() {
    for (auto& subpath : subpaths_) {
        if (subpath.isClosed() && !subpath.isEmpty()) {
            subpath.setClosed(false);
            Point last = subpath.getLastPoint();
            subpath.addSegment(std::make_shared<Line>(last, subpath.getStartPoint()));
        }
    }
}

}  // namespace kernel
}  // namespace pdfcpp
