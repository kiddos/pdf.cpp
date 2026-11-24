#ifndef PDFCPP_KERNEL_GEOM_GEOM_H_
#define PDFCPP_KERNEL_GEOM_GEOM_H_

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
  Point(double x, double y) : x(x), y(y) {}
  double GetX() const { return x; }
  double GetY() const { return y; }
  void SetLocation(double x, double y) {
    this->x = x;
    this->y = y;
  }
  void Translate(double dx, double dy) {
    x += dx;
    y += dy;
  }
  double Distance(const Point& p) const { return std::hypot(x - p.x, y - p.y); }
  bool operator==(const Point& o) const { return x == o.x && y == o.y; }
  std::string ToString() const;
};

/// Port of com.itextpdf.kernel.geom.Matrix: 3x3 affine matrix in PDF row-vector
/// convention.
class Matrix {
 public:
  static constexpr int kI11 = 0, kI12 = 1, kI13 = 2, kI21 = 3, kI22 = 4,
                       kI23 = 5, kI31 = 6, kI32 = 7, kI33 = 8;

  Matrix() : vals_{1, 0, 0, 0, 1, 0, 0, 0, 1} {}
  Matrix(float tx, float ty) : vals_{1, 0, 0, 0, 1, 0, tx, ty, 1} {}
  Matrix(float e11, float e12, float e13, float e21, float e22, float e23,
         float e31, float e32, float e33)
      : vals_{e11, e12, e13, e21, e22, e23, e31, e32, e33} {}
  Matrix(float a, float b, float c, float d, float e, float f)
      : vals_{a, b, 0, c, d, 0, e, f, 1} {}

  float Get(int index) const { return vals_[index]; }
  Matrix Multiply(const Matrix& by) const;
  Matrix Add(const Matrix& arg) const;
  Matrix Subtract(const Matrix& arg) const;
  float GetDeterminant() const;
  bool operator==(const Matrix& o) const { return vals_ == o.vals_; }
  bool operator!=(const Matrix& o) const { return !(*this == o); }
  std::string ToString() const;

 private:
  std::array<float, 9> vals_;
};

/// Port of com.itextpdf.kernel.geom.Vector (3-component vector).
class Vector {
 public:
  static constexpr int kI1 = 0, kI2 = 1, kI3 = 2;
  Vector() : vals_{0, 0, 0} {}
  Vector(float x, float y, float z) : vals_{x, y, z} {}
  float Get(int index) const { return vals_[index]; }
  Vector Cross(const Matrix& by) const;
  Vector Subtract(const Vector& v) const {
    return Vector(vals_[0] - v.vals_[0], vals_[1] - v.vals_[1],
                  vals_[2] - v.vals_[2]);
  }
  Vector Add(const Vector& v) const {
    return Vector(vals_[0] + v.vals_[0], vals_[1] + v.vals_[1],
                  vals_[2] + v.vals_[2]);
  }
  Vector Cross(const Vector& with) const;
  Vector Normalize() const;
  Vector Multiply(float by) const {
    return Vector(vals_[0] * by, vals_[1] * by, vals_[2] * by);
  }
  float Dot(const Vector& with) const {
    return vals_[0] * with.vals_[0] + vals_[1] * with.vals_[1] +
           vals_[2] * with.vals_[2];
  }
  float Length() const { return std::sqrt(LengthSquared()); }
  float LengthSquared() const {
    return vals_[0] * vals_[0] + vals_[1] * vals_[1] + vals_[2] * vals_[2];
  }
  bool operator==(const Vector& o) const { return vals_ == o.vals_; }
  std::string ToString() const;

 private:
  std::array<float, 3> vals_;
};

class Rectangle;

/// Port of com.itextpdf.kernel.geom.LineSegment.
class LineSegment {
 public:
  LineSegment() = default;
  LineSegment(const Vector& start_point, const Vector& end_point)
      : start_point_(start_point), end_point_(end_point) {}
  const Vector& GetStartPoint() const { return start_point_; }
  const Vector& GetEndPoint() const { return end_point_; }
  float GetLength() const { return end_point_.Subtract(start_point_).Length(); }
  Rectangle GetBoundingRectangle() const;
  LineSegment TransformBy(const Matrix& m) const {
    return LineSegment(start_point_.Cross(m), end_point_.Cross(m));
  }
  bool ContainsSegment(const LineSegment& other) const {
    return ContainsPoint(other.start_point_) && ContainsPoint(other.end_point_);
  }
  bool ContainsPoint(const Vector& point) const;

 private:
  Vector start_point_;
  Vector end_point_;
};

/// Port of com.itextpdf.kernel.geom.AffineTransform.
class AffineTransform {
 public:
  static constexpr int kTypeIdentity = 0;
  AffineTransform() = default;
  AffineTransform(double m00, double m10, double m01, double m11, double m02,
                  double m12)
      : m00_(m00), m10_(m10), m01_(m01), m11_(m11), m02_(m02), m12_(m12) {}
  explicit AffineTransform(const std::vector<float>& matrix);
  explicit AffineTransform(const std::vector<double>& matrix);

  double GetScaleX() const { return m00_; }
  double GetScaleY() const { return m11_; }
  double GetShearX() const { return m01_; }
  double GetShearY() const { return m10_; }
  double GetTranslateX() const { return m02_; }
  double GetTranslateY() const { return m12_; }
  double GetDeterminant() const { return m00_ * m11_ - m01_ * m10_; }
  /// Returns {m00, m10, m01, m11, m02, m12}.
  void GetMatrix(float matrix[6]) const;
  void GetMatrix(double matrix[6]) const;
  bool IsIdentity() const {
    return m00_ == 1 && m11_ == 1 && m10_ == 0 && m01_ == 0 && m02_ == 0 &&
           m12_ == 0;
  }

  void SetTransform(double m00, double m10, double m01, double m11, double m02,
                    double m12);
  void SetToIdentity() { SetTransform(1, 0, 0, 1, 0, 0); }
  void SetToTranslation(double mx, double my) {
    SetTransform(1, 0, 0, 1, mx, my);
  }
  void SetToScale(double scx, double scy) {
    SetTransform(scx, 0, 0, scy, 0, 0);
  }
  void SetToShear(double shx, double shy) {
    SetTransform(1, shy, shx, 1, 0, 0);
  }
  void SetToRotation(double angle);
  void SetToRotation(double angle, double px, double py);

  static AffineTransform GetTranslateInstance(double mx, double my);
  static AffineTransform GetScaleInstance(double scx, double sc_y);
  static AffineTransform GetShearInstance(double shx, double shy);
  static AffineTransform GetRotateInstance(double angle);
  static AffineTransform GetRotateInstance(double angle, double x, double y);

  void Translate(double mx, double my) {
    Concatenate(GetTranslateInstance(mx, my));
  }
  void Scale(double scx, double scy) {
    Concatenate(GetScaleInstance(scx, scy));
  }
  void Shear(double shx, double shy) {
    Concatenate(GetShearInstance(shx, shy));
  }
  void Rotate(double angle) { Concatenate(GetRotateInstance(angle)); }
  void Rotate(double angle, double px, double py) {
    Concatenate(GetRotateInstance(angle, px, py));
  }

  void Concatenate(const AffineTransform& t);
  void PreConcatenate(const AffineTransform& t);
  AffineTransform CreateInverse() const;

  Point Transform(const Point& src) const;
  void Transform(const float* src, int src_off, float* dst, int dst_off,
                 int num_pts) const;
  void Transform(const double* src, int src_off, double* dst, int dst_off,
                 int num_pts) const;
  Point DeltaTransform(const Point& src) const;
  Point InverseTransform(const Point& src) const;

  bool operator==(const AffineTransform& o) const {
    return m00_ == o.m00_ && m01_ == o.m01_ && m02_ == o.m02_ &&
           m10_ == o.m10_ && m11_ == o.m11_ && m12_ == o.m12_;
  }

 private:
  static AffineTransform Multiply(const AffineTransform& t1,
                                  const AffineTransform& t2);
  double m00_ = 1, m10_ = 0, m01_ = 0, m11_ = 1, m02_ = 0, m12_ = 0;
};

class PdfPage;

/// Port of com.itextpdf.kernel.geom.Rectangle (value type).
class Rectangle {
 public:
  static constexpr float kEps = 1e-4f;

  Rectangle() = default;
  Rectangle(float x, float y, float width, float height)
      : x_(x), y_(y), width_(width), height_(height) {}
  Rectangle(float width, float height) : Rectangle(0, 0, width, height) {}
  virtual ~Rectangle() = default;

  static Rectangle GetCommonRectangle(const std::vector<Rectangle>& rectangles);
  static Rectangle CalculateBBox(const std::vector<Point>& points);
  std::vector<Point> ToPointsArray() const;
  /// Returns the intersection or an empty optional-like flag (width/height < 0
  /// => no intersection).
  bool GetIntersection(const Rectangle& rect, Rectangle& out) const;
  bool Contains(const Rectangle& rect) const;
  bool Overlaps(const Rectangle& rect, float epsilon = 0) const;
  Rectangle& SetBbox(float llx, float lly, float urx, float ury);

  float GetX() const { return x_; }
  Rectangle& SetX(float x) {
    x_ = x;
    return *this;
  }
  float GetY() const { return y_; }
  Rectangle& SetY(float y) {
    y_ = y;
    return *this;
  }
  float GetWidth() const { return width_; }
  Rectangle& SetWidth(float width) {
    width_ = width;
    return *this;
  }
  float GetHeight() const { return height_; }
  Rectangle& SetHeight(float height) {
    height_ = height;
    return *this;
  }
  Rectangle& IncreaseHeight(float extra) {
    height_ += extra;
    return *this;
  }
  Rectangle& DecreaseHeight(float extra) {
    height_ -= extra;
    return *this;
  }
  Rectangle& IncreaseWidth(float extra) {
    width_ += extra;
    return *this;
  }
  Rectangle& DecreaseWidth(float extra) {
    width_ -= extra;
    return *this;
  }
  float GetLeft() const { return x_; }
  float GetRight() const { return x_ + width_; }
  float GetTop() const { return y_ + height_; }
  float GetBottom() const { return y_; }
  Rectangle& MoveDown(float move) {
    y_ -= move;
    return *this;
  }
  Rectangle& MoveUp(float move) {
    y_ += move;
    return *this;
  }
  Rectangle& MoveRight(float move) {
    x_ += move;
    return *this;
  }
  Rectangle& MoveLeft(float move) {
    x_ -= move;
    return *this;
  }
  Rectangle& ApplyMargins(float top_indent, float right_indent,
                          float bottom_indent, float left_indent, bool reverse);
  bool IntersectsLine(float x1, float y1, float x2, float y2) const;
  std::string ToString() const;
  bool EqualsWithEpsilon(const Rectangle& that, float eps = kEps) const;
  bool operator==(const Rectangle& o) const {
    return x_ == o.x_ && y_ == o.y_ && width_ == o.width_ &&
           height_ == o.height_;
  }

 protected:
  float x_ = 0, y_ = 0, width_ = 0, height_ = 0;
};

/// Port of com.itextpdf.kernel.geom.PageSize.
class PageSize : public Rectangle {
 public:
  static const PageSize kA0, kA1, kA2, kA3, kA4, kA5, kA6, kA7, kA8, kA9, kA10;
  static const PageSize kB0, kB1, kB2, kB3, kB4, kB5, kB6, kB7, kB8, kB9, kB10;
  static const PageSize kDefault, kExecutive, kLedger, kLegal, kLetter,
      kTabloid;

  PageSize() = default;
  PageSize(float width, float height) : Rectangle(0, 0, width, height) {}
  explicit PageSize(const Rectangle& box) : Rectangle(box) {}
  PageSize Rotate() const { return PageSize(height_, width_); }
};

/// Port of IShape/Line/BezierCurve/Subpath/Path (used by canvas parsing and
/// clipping).
class IShape {
 public:
  virtual ~IShape() = default;
  virtual std::vector<Point> GetBasePoints() const = 0;
};

class Line : public IShape {
 public:
  Line(const Point& p1, const Point& p2) : p1_(p1), p2_(p2) {}
  std::vector<Point> GetBasePoints() const override { return {p1_, p2_}; }

 private:
  Point p1_, p2_;
};

class BezierCurve : public IShape {
 public:
  explicit BezierCurve(std::vector<Point> control_points)
      : control_points_(std::move(control_points)) {}
  std::vector<Point> GetBasePoints() const override { return control_points_; }

 private:
  std::vector<Point> control_points_;
};

class Subpath {
 public:
  Subpath() = default;
  explicit Subpath(const Point& start_point) : start_point_(start_point) {}
  Subpath(float x, float y) : start_point_(x, y) {}
  void SetStartPoint(const Point& p) { start_point_ = p; }
  Point GetStartPoint() const { return start_point_; }
  Point GetLastPoint() const;
  void AddSegment(std::shared_ptr<IShape> segment);
  const std::vector<std::shared_ptr<IShape>>& GetSegments() const {
    return segments_;
  }
  bool IsEmpty() const { return segments_.empty(); }
  bool IsSinglePointOpen() const { return segments_.empty() && !closed_; }
  bool IsSinglePointClosed() const { return segments_.empty() && closed_; }
  bool IsClosed() const { return closed_; }
  void SetClosed(bool closed) { closed_ = closed; }
  bool IsDegenerate() const;
  std::vector<Point> GetPiecewiseLinearApproximation() const;

 private:
  Point start_point_;
  std::vector<std::shared_ptr<IShape>> segments_;
  bool closed_ = false;
};

class Path {
 public:
  Path() = default;
  const std::vector<Subpath>& GetSubpaths() const { return subpaths_; }
  std::vector<Subpath>& GetSubpaths() { return subpaths_; }
  void AddSubpath(const Subpath& subpath);
  void AddSubpaths(const std::vector<Subpath>& subpaths);
  Point GetCurrentPoint() const { return current_point_; }
  void MoveTo(float x, float y);
  void LineTo(float x, float y);
  void CurveTo(float x1, float y1, float x2, float y2, float x3, float y3);
  void CurveTo(float x2, float y2, float x3, float y3);
  void CurveFromTo(float x1, float y1, float x3, float y3);
  void Rectangle(const Rectangle& rect);
  void Rectangle(float x, float y, float w, float h);
  void CloseSubpath();
  void CloseAllSubpaths();
  void ReplaceCloseWithLine();
  bool IsEmpty() const { return subpaths_.empty(); }

 private:
  Subpath* GetLastSubpath() {
    return subpaths_.empty() ? nullptr : &subpaths_.back();
  }
  std::vector<Subpath> subpaths_;
  Point current_point_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_GEOM_GEOM_H_
