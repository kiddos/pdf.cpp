#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.geom.Point (double precision 2D point).
class Point {
public:
    double x = 0;
    double y = 0;
    Point() = default;
    Point(double x_, double y_) : x(x_), y(y_) {}
    double getX() const { return x; }
    double getY() const { return y; }
    void setLocation(double x_, double y_) {
        x = x_;
        y = y_;
    }
    void translate(double dx, double dy) {
        x += dx;
        y += dy;
    }
    double distance(const Point& p) const { return std::hypot(x - p.x, y - p.y); }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    std::string toString() const;
};

/// Port of com.itextpdf.kernel.geom.Matrix: 3x3 affine matrix in PDF row-vector convention.
class Matrix {
public:
    static constexpr int I11 = 0, I12 = 1, I13 = 2, I21 = 3, I22 = 4, I23 = 5, I31 = 6, I32 = 7, I33 = 8;

    Matrix() : vals_{1, 0, 0, 0, 1, 0, 0, 0, 1} {}
    Matrix(float tx, float ty) : vals_{1, 0, 0, 0, 1, 0, tx, ty, 1} {}
    Matrix(float e11, float e12, float e13, float e21, float e22, float e23, float e31, float e32, float e33)
        : vals_{e11, e12, e13, e21, e22, e23, e31, e32, e33} {}
    Matrix(float a, float b, float c, float d, float e, float f) : vals_{a, b, 0, c, d, 0, e, f, 1} {}

    float get(int index) const { return vals_[index]; }
    Matrix multiply(const Matrix& by) const;
    Matrix add(const Matrix& arg) const;
    Matrix subtract(const Matrix& arg) const;
    float getDeterminant() const;
    bool operator==(const Matrix& o) const { return vals_ == o.vals_; }
    bool operator!=(const Matrix& o) const { return !(*this == o); }
    std::string toString() const;

private:
    std::array<float, 9> vals_;
};

/// Port of com.itextpdf.kernel.geom.Vector (3-component vector).
class Vector {
public:
    static constexpr int I1 = 0, I2 = 1, I3 = 2;
    Vector() : vals_{0, 0, 0} {}
    Vector(float x, float y, float z) : vals_{x, y, z} {}
    float get(int index) const { return vals_[index]; }
    Vector cross(const Matrix& by) const;
    Vector subtract(const Vector& v) const { return Vector(vals_[0] - v.vals_[0], vals_[1] - v.vals_[1], vals_[2] - v.vals_[2]); }
    Vector add(const Vector& v) const { return Vector(vals_[0] + v.vals_[0], vals_[1] + v.vals_[1], vals_[2] + v.vals_[2]); }
    Vector cross(const Vector& with) const;
    Vector normalize() const;
    Vector multiply(float by) const { return Vector(vals_[0] * by, vals_[1] * by, vals_[2] * by); }
    float dot(const Vector& with) const { return vals_[0] * with.vals_[0] + vals_[1] * with.vals_[1] + vals_[2] * with.vals_[2]; }
    float length() const { return std::sqrt(lengthSquared()); }
    float lengthSquared() const { return vals_[0] * vals_[0] + vals_[1] * vals_[1] + vals_[2] * vals_[2]; }
    bool operator==(const Vector& o) const { return vals_ == o.vals_; }
    std::string toString() const;

private:
    std::array<float, 3> vals_;
};

class Rectangle;

/// Port of com.itextpdf.kernel.geom.LineSegment.
class LineSegment {
public:
    LineSegment() = default;
    LineSegment(const Vector& startPoint, const Vector& endPoint) : startPoint_(startPoint), endPoint_(endPoint) {}
    const Vector& getStartPoint() const { return startPoint_; }
    const Vector& getEndPoint() const { return endPoint_; }
    float getLength() const { return endPoint_.subtract(startPoint_).length(); }
    Rectangle getBoundingRectangle() const;
    LineSegment transformBy(const Matrix& m) const { return LineSegment(startPoint_.cross(m), endPoint_.cross(m)); }
    bool containsSegment(const LineSegment& other) const { return containsPoint(other.startPoint_) && containsPoint(other.endPoint_); }
    bool containsPoint(const Vector& point) const;

private:
    Vector startPoint_;
    Vector endPoint_;
};

/// Port of com.itextpdf.kernel.geom.AffineTransform.
class AffineTransform {
public:
    static constexpr int TYPE_IDENTITY = 0;
    AffineTransform() = default;
    AffineTransform(double m00, double m10, double m01, double m11, double m02, double m12)
        : m00_(m00), m10_(m10), m01_(m01), m11_(m11), m02_(m02), m12_(m12) {}
    explicit AffineTransform(const std::vector<float>& matrix);
    explicit AffineTransform(const std::vector<double>& matrix);

    double getScaleX() const { return m00_; }
    double getScaleY() const { return m11_; }
    double getShearX() const { return m01_; }
    double getShearY() const { return m10_; }
    double getTranslateX() const { return m02_; }
    double getTranslateY() const { return m12_; }
    double getDeterminant() const { return m00_ * m11_ - m01_ * m10_; }
    /// Returns {m00, m10, m01, m11, m02, m12}.
    void getMatrix(float matrix[6]) const;
    void getMatrix(double matrix[6]) const;
    bool isIdentity() const { return m00_ == 1 && m11_ == 1 && m10_ == 0 && m01_ == 0 && m02_ == 0 && m12_ == 0; }

    void setTransform(double m00, double m10, double m01, double m11, double m02, double m12);
    void setToIdentity() { setTransform(1, 0, 0, 1, 0, 0); }
    void setToTranslation(double mx, double my) { setTransform(1, 0, 0, 1, mx, my); }
    void setToScale(double scx, double scy) { setTransform(scx, 0, 0, scy, 0, 0); }
    void setToShear(double shx, double shy) { setTransform(1, shy, shx, 1, 0, 0); }
    void setToRotation(double angle);
    void setToRotation(double angle, double px, double py);

    static AffineTransform getTranslateInstance(double mx, double my);
    static AffineTransform getScaleInstance(double scx, double scY);
    static AffineTransform getShearInstance(double shx, double shy);
    static AffineTransform getRotateInstance(double angle);
    static AffineTransform getRotateInstance(double angle, double x, double y);

    void translate(double mx, double my) { concatenate(getTranslateInstance(mx, my)); }
    void scale(double scx, double scy) { concatenate(getScaleInstance(scx, scy)); }
    void shear(double shx, double shy) { concatenate(getShearInstance(shx, shy)); }
    void rotate(double angle) { concatenate(getRotateInstance(angle)); }
    void rotate(double angle, double px, double py) { concatenate(getRotateInstance(angle, px, py)); }

    void concatenate(const AffineTransform& t);
    void preConcatenate(const AffineTransform& t);
    AffineTransform createInverse() const;

    Point transform(const Point& src) const;
    void transform(const float* src, int srcOff, float* dst, int dstOff, int numPts) const;
    void transform(const double* src, int srcOff, double* dst, int dstOff, int numPts) const;
    Point deltaTransform(const Point& src) const;
    Point inverseTransform(const Point& src) const;

    bool operator==(const AffineTransform& o) const {
        return m00_ == o.m00_ && m01_ == o.m01_ && m02_ == o.m02_ && m10_ == o.m10_ && m11_ == o.m11_ && m12_ == o.m12_;
    }

private:
    static AffineTransform multiply(const AffineTransform& t1, const AffineTransform& t2);
    double m00_ = 1, m10_ = 0, m01_ = 0, m11_ = 1, m02_ = 0, m12_ = 0;
};

class PdfPage;

/// Port of com.itextpdf.kernel.geom.Rectangle (value type).
class Rectangle {
public:
    static constexpr float EPS = 1e-4f;

    Rectangle() = default;
    Rectangle(float x, float y, float width, float height) : x_(x), y_(y), width_(width), height_(height) {}
    Rectangle(float width, float height) : Rectangle(0, 0, width, height) {}
    virtual ~Rectangle() = default;

    static Rectangle getCommonRectangle(const std::vector<Rectangle>& rectangles);
    static Rectangle calculateBBox(const std::vector<Point>& points);
    std::vector<Point> toPointsArray() const;
    /// Returns the intersection or an empty optional-like flag (width/height < 0 => no intersection).
    bool getIntersection(const Rectangle& rect, Rectangle& out) const;
    bool contains(const Rectangle& rect) const;
    bool overlaps(const Rectangle& rect, float epsilon = 0) const;
    Rectangle& setBbox(float llx, float lly, float urx, float ury);

    float getX() const { return x_; }
    Rectangle& setX(float x) {
        x_ = x;
        return *this;
    }
    float getY() const { return y_; }
    Rectangle& setY(float y) {
        y_ = y;
        return *this;
    }
    float getWidth() const { return width_; }
    Rectangle& setWidth(float width) {
        width_ = width;
        return *this;
    }
    float getHeight() const { return height_; }
    Rectangle& setHeight(float height) {
        height_ = height;
        return *this;
    }
    Rectangle& increaseHeight(float extra) {
        height_ += extra;
        return *this;
    }
    Rectangle& decreaseHeight(float extra) {
        height_ -= extra;
        return *this;
    }
    Rectangle& increaseWidth(float extra) {
        width_ += extra;
        return *this;
    }
    Rectangle& decreaseWidth(float extra) {
        width_ -= extra;
        return *this;
    }
    float getLeft() const { return x_; }
    float getRight() const { return x_ + width_; }
    float getTop() const { return y_ + height_; }
    float getBottom() const { return y_; }
    Rectangle& moveDown(float move) {
        y_ -= move;
        return *this;
    }
    Rectangle& moveUp(float move) {
        y_ += move;
        return *this;
    }
    Rectangle& moveRight(float move) {
        x_ += move;
        return *this;
    }
    Rectangle& moveLeft(float move) {
        x_ -= move;
        return *this;
    }
    Rectangle& applyMargins(float topIndent, float rightIndent, float bottomIndent, float leftIndent, bool reverse);
    bool intersectsLine(float x1, float y1, float x2, float y2) const;
    std::string toString() const;
    bool equalsWithEpsilon(const Rectangle& that, float eps = EPS) const;
    bool operator==(const Rectangle& o) const {
        return x_ == o.x_ && y_ == o.y_ && width_ == o.width_ && height_ == o.height_;
    }

protected:
    float x_ = 0, y_ = 0, width_ = 0, height_ = 0;
};

/// Port of com.itextpdf.kernel.geom.PageSize.
class PageSize : public Rectangle {
public:
    static const PageSize A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10;
    static const PageSize B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B10;
    static const PageSize DEFAULT, EXECUTIVE, LEDGER, LEGAL, LETTER, TABLOID;

    PageSize() = default;
    PageSize(float width, float height) : Rectangle(0, 0, width, height) {}
    explicit PageSize(const Rectangle& box) : Rectangle(box) {}
    PageSize rotate() const { return PageSize(height_, width_); }
};

/// Port of IShape/Line/BezierCurve/Subpath/Path (used by canvas parsing and clipping).
class IShape {
public:
    virtual ~IShape() = default;
    virtual std::vector<Point> getBasePoints() const = 0;
};

class Line : public IShape {
public:
    Line(const Point& p1, const Point& p2) : p1_(p1), p2_(p2) {}
    std::vector<Point> getBasePoints() const override { return {p1_, p2_}; }

private:
    Point p1_, p2_;
};

class BezierCurve : public IShape {
public:
    explicit BezierCurve(std::vector<Point> controlPoints) : controlPoints_(std::move(controlPoints)) {}
    std::vector<Point> getBasePoints() const override { return controlPoints_; }

private:
    std::vector<Point> controlPoints_;
};

class Subpath {
public:
    Subpath() = default;
    explicit Subpath(const Point& startPoint) : startPoint_(startPoint) {}
    Subpath(float x, float y) : startPoint_(x, y) {}
    void setStartPoint(const Point& p) { startPoint_ = p; }
    Point getStartPoint() const { return startPoint_; }
    Point getLastPoint() const;
    void addSegment(std::shared_ptr<IShape> segment);
    const std::vector<std::shared_ptr<IShape>>& getSegments() const { return segments_; }
    bool isEmpty() const { return segments_.empty(); }
    bool isSinglePointOpen() const { return segments_.empty() && !closed_; }
    bool isSinglePointClosed() const { return segments_.empty() && closed_; }
    bool isClosed() const { return closed_; }
    void setClosed(bool closed) { closed_ = closed; }
    bool isDegenerate() const;
    std::vector<Point> getPiecewiseLinearApproximation() const;

private:
    Point startPoint_;
    std::vector<std::shared_ptr<IShape>> segments_;
    bool closed_ = false;
};

class Path {
public:
    Path() = default;
    const std::vector<Subpath>& getSubpaths() const { return subpaths_; }
    std::vector<Subpath>& getSubpaths() { return subpaths_; }
    void addSubpath(const Subpath& subpath);
    void addSubpaths(const std::vector<Subpath>& subpaths);
    Point getCurrentPoint() const { return currentPoint_; }
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void curveTo(float x1, float y1, float x2, float y2, float x3, float y3);
    void curveTo(float x2, float y2, float x3, float y3);
    void curveFromTo(float x1, float y1, float x3, float y3);
    void rectangle(const Rectangle& rect);
    void rectangle(float x, float y, float w, float h);
    void closeSubpath();
    void closeAllSubpaths();
    void replaceCloseWithLine();
    bool isEmpty() const { return subpaths_.empty(); }

private:
    Subpath* getLastSubpath() { return subpaths_.empty() ? nullptr : &subpaths_.back(); }
    std::vector<Subpath> subpaths_;
    Point currentPoint_;
};

}  // namespace kernel
}  // namespace pdfcpp
