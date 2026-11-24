#include "pdfcpp/kernel/geom/geom.h"

#include <algorithm>
#include <limits>
#include <sstream>

#include "pdfcpp/io/source/byte_utils.h"

namespace pdfcpp {
namespace kernel {

static std::string Fmt(double v) {
  std::ostringstream ss;
  ss << v;
  return ss.str();
}

std::string Point::ToString() const {
  return "Point: [x=" + Fmt(x) + ",y=" + Fmt(y) + "]";
}

// ---------------------------------------------------------------- Matrix
Matrix Matrix::Multiply(const Matrix& by) const {
  const auto& a = vals_;
  const auto& b = by.vals_;
  Matrix rslt;
  auto& c = rslt.vals_;
  c[kI11] = a[kI11] * b[kI11] + a[kI12] * b[kI21] + a[kI13] * b[kI31];
  c[kI12] = a[kI11] * b[kI12] + a[kI12] * b[kI22] + a[kI13] * b[kI32];
  c[kI13] = a[kI11] * b[kI13] + a[kI12] * b[kI23] + a[kI13] * b[kI33];
  c[kI21] = a[kI21] * b[kI11] + a[kI22] * b[kI21] + a[kI23] * b[kI31];
  c[kI22] = a[kI21] * b[kI12] + a[kI22] * b[kI22] + a[kI23] * b[kI32];
  c[kI23] = a[kI21] * b[kI13] + a[kI22] * b[kI23] + a[kI23] * b[kI33];
  c[kI31] = a[kI31] * b[kI11] + a[kI32] * b[kI21] + a[kI33] * b[kI31];
  c[kI32] = a[kI31] * b[kI12] + a[kI32] * b[kI22] + a[kI33] * b[kI32];
  c[kI33] = a[kI31] * b[kI13] + a[kI32] * b[kI23] + a[kI33] * b[kI33];
  return rslt;
}

Matrix Matrix::Add(const Matrix& arg) const {
  Matrix r;
  for (int i = 0; i < 9; ++i) r.vals_[i] = vals_[i] + arg.vals_[i];
  return r;
}

Matrix Matrix::Subtract(const Matrix& arg) const {
  Matrix r;
  for (int i = 0; i < 9; ++i) r.vals_[i] = vals_[i] - arg.vals_[i];
  return r;
}

float Matrix::GetDeterminant() const {
  return vals_[kI11] * vals_[kI22] * vals_[kI33] +
         vals_[kI12] * vals_[kI23] * vals_[kI31] +
         vals_[kI13] * vals_[kI21] * vals_[kI32] -
         vals_[kI11] * vals_[kI23] * vals_[kI32] -
         vals_[kI12] * vals_[kI21] * vals_[kI33] -
         vals_[kI13] * vals_[kI22] * vals_[kI31];
}

std::string Matrix::ToString() const {
  return Fmt(vals_[kI11]) + "\t" + Fmt(vals_[kI12]) + "\t" + Fmt(vals_[kI13]) +
         "\n" + Fmt(vals_[kI21]) + "\t" + Fmt(vals_[kI22]) + "\t" +
         Fmt(vals_[kI23]) + "\n" + Fmt(vals_[kI31]) + "\t" + Fmt(vals_[kI32]) +
         "\t" + Fmt(vals_[kI33]);
}

// ---------------------------------------------------------------- Vector
Vector Vector::Cross(const Matrix& by) const {
  float x = vals_[0] * by.Get(Matrix::kI11) + vals_[1] * by.Get(Matrix::kI21) +
            vals_[2] * by.Get(Matrix::kI31);
  float y = vals_[0] * by.Get(Matrix::kI12) + vals_[1] * by.Get(Matrix::kI22) +
            vals_[2] * by.Get(Matrix::kI32);
  float z = vals_[0] * by.Get(Matrix::kI13) + vals_[1] * by.Get(Matrix::kI23) +
            vals_[2] * by.Get(Matrix::kI33);
  return Vector(x, y, z);
}

Vector Vector::Cross(const Vector& with) const {
  float x = vals_[1] * with.vals_[2] - vals_[2] * with.vals_[1];
  float y = vals_[2] * with.vals_[0] - vals_[0] * with.vals_[2];
  float z = vals_[0] * with.vals_[1] - vals_[1] * with.vals_[0];
  return Vector(x, y, z);
}

Vector Vector::Normalize() const {
  float l = Length();
  return Vector(vals_[0] / l, vals_[1] / l, vals_[2] / l);
}

std::string Vector::ToString() const {
  return Fmt(vals_[0]) + "," + Fmt(vals_[1]) + "," + Fmt(vals_[2]);
}

// ---------------------------------------------------------------- LineSegment
Rectangle LineSegment::GetBoundingRectangle() const {
  float x1 = start_point_.Get(0), y1 = start_point_.Get(1),
        x2 = end_point_.Get(0), y2 = end_point_.Get(1);
  return Rectangle(std::min(x1, x2), std::min(y1, y2), std::fabs(x2 - x1),
                   std::fabs(y2 - y1));
}

bool LineSegment::ContainsPoint(const Vector& point) const {
  Vector diff1 = point.Subtract(start_point_);
  if (diff1.Get(0) < 0 || diff1.Get(1) < 0 || diff1.Get(2) < 0) return false;
  Vector diff2 = end_point_.Subtract(point);
  if (diff2.Get(0) < 0 || diff2.Get(1) < 0 || diff2.Get(2) < 0) return false;
  return true;
}

// ----------------------------------------------------------------
// AffineTransform
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

void AffineTransform::GetMatrix(float matrix[6]) const {
  matrix[0] = static_cast<float>(m00_);
  matrix[1] = static_cast<float>(m10_);
  matrix[2] = static_cast<float>(m01_);
  matrix[3] = static_cast<float>(m11_);
  matrix[4] = static_cast<float>(m02_);
  matrix[5] = static_cast<float>(m12_);
}

void AffineTransform::GetMatrix(double matrix[6]) const {
  matrix[0] = m00_;
  matrix[1] = m10_;
  matrix[2] = m01_;
  matrix[3] = m11_;
  matrix[4] = m02_;
  matrix[5] = m12_;
}

void AffineTransform::SetTransform(double m00, double m10, double m01,
                                   double m11, double m02, double m12) {
  m00_ = m00;
  m10_ = m10;
  m01_ = m01;
  m11_ = m11;
  m02_ = m02;
  m12_ = m12;
}

void AffineTransform::SetToRotation(double angle) {
  double sin = std::sin(angle);
  double cos = std::cos(angle);
  if (std::fabs(cos) < 1E-10) {
    cos = 0.0;
    sin = sin > 0.0 ? 1.0 : -1.0;
  } else if (std::fabs(sin) < 1E-10) {
    sin = 0.0;
    cos = cos > 0.0 ? 1.0 : -1.0;
  }
  SetTransform(cos, sin, -sin, cos, 0, 0);
}

void AffineTransform::SetToRotation(double angle, double px, double py) {
  SetToRotation(angle);
  m02_ = px * (1.0 - m00_) + py * m10_;
  m12_ = py * (1.0 - m00_) - px * m10_;
}

AffineTransform AffineTransform::GetTranslateInstance(double mx, double my) {
  AffineTransform t;
  t.SetToTranslation(mx, my);
  return t;
}
AffineTransform AffineTransform::GetScaleInstance(double scx, double scy) {
  AffineTransform t;
  t.SetToScale(scx, scy);
  return t;
}
AffineTransform AffineTransform::GetShearInstance(double shx, double shy) {
  AffineTransform t;
  t.SetToShear(shx, shy);
  return t;
}
AffineTransform AffineTransform::GetRotateInstance(double angle) {
  AffineTransform t;
  t.SetToRotation(angle);
  return t;
}
AffineTransform AffineTransform::GetRotateInstance(double angle, double x,
                                                   double y) {
  AffineTransform t;
  t.SetToRotation(angle, x, y);
  return t;
}

AffineTransform AffineTransform::Multiply(const AffineTransform& t1,
                                          const AffineTransform& t2) {
  return AffineTransform(t1.m00_ * t2.m00_ + t1.m10_ * t2.m01_,
                         t1.m00_ * t2.m10_ + t1.m10_ * t2.m11_,
                         t1.m01_ * t2.m00_ + t1.m11_ * t2.m01_,
                         t1.m01_ * t2.m10_ + t1.m11_ * t2.m11_,
                         t1.m02_ * t2.m00_ + t1.m12_ * t2.m01_ + t2.m02_,
                         t1.m02_ * t2.m10_ + t1.m12_ * t2.m11_ + t2.m12_);
}

void AffineTransform::Concatenate(const AffineTransform& t) {
  *this = Multiply(t, *this);
}
void AffineTransform::PreConcatenate(const AffineTransform& t) {
  *this = Multiply(*this, t);
}

AffineTransform AffineTransform::CreateInverse() const {
  double det = GetDeterminant();
  if (std::fabs(det) < std::numeric_limits<double>::min() * 2)
    throw std::runtime_error(
        "Determinant is zero. Cannot invert transformation.");
  return AffineTransform(m11_ / det, -m10_ / det, -m01_ / det, m00_ / det,
                         (m01_ * m12_ - m11_ * m02_) / det,
                         (m10_ * m02_ - m00_ * m12_) / det);
}

Point AffineTransform::Transform(const Point& src) const {
  return Point(src.x * m00_ + src.y * m01_ + m02_,
               src.x * m10_ + src.y * m11_ + m12_);
}

void AffineTransform::Transform(const float* src, int src_off, float* dst,
                                int dst_off, int num_pts) const {
  std::vector<float> tmp(src + src_off, src + src_off + num_pts * 2);
  for (int i = 0; i < num_pts; ++i) {
    double x = tmp[2 * i], y = tmp[2 * i + 1];
    dst[dst_off + 2 * i] = static_cast<float>(x * m00_ + y * m01_ + m02_);
    dst[dst_off + 2 * i + 1] = static_cast<float>(x * m10_ + y * m11_ + m12_);
  }
}

void AffineTransform::Transform(const double* src, int src_off, double* dst,
                                int dst_off, int num_pts) const {
  std::vector<double> tmp(src + src_off, src + src_off + num_pts * 2);
  for (int i = 0; i < num_pts; ++i) {
    double x = tmp[2 * i], y = tmp[2 * i + 1];
    dst[dst_off + 2 * i] = x * m00_ + y * m01_ + m02_;
    dst[dst_off + 2 * i + 1] = x * m10_ + y * m11_ + m12_;
  }
}

Point AffineTransform::DeltaTransform(const Point& src) const {
  return Point(src.x * m00_ + src.y * m01_, src.x * m10_ + src.y * m11_);
}

Point AffineTransform::InverseTransform(const Point& src) const {
  return CreateInverse().Transform(src);
}

// ---------------------------------------------------------------- Rectangle
Rectangle Rectangle::GetCommonRectangle(
    const std::vector<Rectangle>& rectangles) {
  float ury = -std::numeric_limits<float>::max();
  float llx = std::numeric_limits<float>::max();
  float lly = std::numeric_limits<float>::max();
  float urx = -std::numeric_limits<float>::max();
  for (const auto& rectangle : rectangles) {
    Rectangle rec = rectangle;
    if (rec.GetY() < lly) lly = rec.GetY();
    if (rec.GetX() < llx) llx = rec.GetX();
    if (rec.GetY() + rec.GetHeight() > ury) ury = rec.GetY() + rec.GetHeight();
    if (rec.GetX() + rec.GetWidth() > urx) urx = rec.GetX() + rec.GetWidth();
  }
  return Rectangle(llx, lly, urx - llx, ury - lly);
}

Rectangle Rectangle::CalculateBBox(const std::vector<Point>& points) {
  double min_x = std::numeric_limits<double>::max(), min_y = min_x;
  double max_x = -std::numeric_limits<double>::max(), max_y = max_x;
  for (const auto& p : points) {
    min_x = std::min(min_x, p.x);
    min_y = std::min(min_y, p.y);
    max_x = std::max(max_x, p.x);
    max_y = std::max(max_y, p.y);
  }
  return Rectangle(static_cast<float>(min_x), static_cast<float>(min_y),
                   static_cast<float>(max_x - min_x),
                   static_cast<float>(max_y - min_y));
}

std::vector<Point> Rectangle::ToPointsArray() const {
  return {Point(x_, y_), Point(x_ + width_, y_),
          Point(x_ + width_, y_ + height_), Point(x_, y_ + height_)};
}

bool Rectangle::GetIntersection(const Rectangle& rect, Rectangle& out) const {
  float llx = std::max(x_, rect.x_);
  float lly = std::max(y_, rect.y_);
  float urx = std::min(GetRight(), rect.GetRight());
  float ury = std::min(GetTop(), rect.GetTop());
  float width = urx - llx;
  if (std::fabs(width) < kEps) width = 0;
  float height = ury - lly;
  if (std::fabs(height) < kEps) height = 0;
  if (width >= 0 && height >= 0) {
    out = Rectangle(llx, lly, width, height);
    return true;
  }
  return false;
}

bool Rectangle::Contains(const Rectangle& rect) const {
  float llx = GetX(), lly = GetY(), urx = llx + GetWidth(),
        ury = lly + GetHeight();
  float rllx = rect.GetX(), rlly = rect.GetY(), rurx = rllx + rect.GetWidth(),
        rury = rlly + rect.GetHeight();
  return llx - kEps <= rllx && lly - kEps <= rlly && rurx <= urx + kEps &&
         rury <= ury + kEps;
}

bool Rectangle::Overlaps(const Rectangle& rect, float epsilon) const {
  if ((GetX() + GetWidth()) < (rect.GetX() + epsilon)) return false;
  if ((GetX() + epsilon) > (rect.GetX() + rect.GetWidth())) return false;
  if ((GetY() + GetHeight()) < (rect.GetY() + epsilon)) return false;
  if ((GetY() + epsilon) > (rect.GetY() + rect.GetHeight())) return false;
  return true;
}

Rectangle& Rectangle::SetBbox(float llx, float lly, float urx, float ury) {
  if (llx > urx) std::swap(llx, urx);
  if (lly > ury) std::swap(lly, ury);
  x_ = llx;
  y_ = lly;
  width_ = urx - llx;
  height_ = ury - lly;
  return *this;
}

Rectangle& Rectangle::ApplyMargins(float top_indent, float right_indent,
                                   float bottom_indent, float left_indent,
                                   bool reverse) {
  float s = reverse ? -1.f : 1.f;
  x_ += left_indent * s;
  width_ -= (left_indent + right_indent) * s;
  y_ += bottom_indent * s;
  height_ -= (top_indent + bottom_indent) * s;
  return *this;
}

static int RelativeCcw(double x1, double y1, double x2, double y2, double px,
                       double py) {
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

static bool LinesIntersect(double x1, double y1, double x2, double y2,
                           double x3, double y3, double x4, double y4) {
  return ((RelativeCcw(x1, y1, x2, y2, x3, y3) *
               RelativeCcw(x1, y1, x2, y2, x4, y4) <=
           0) &&
          (RelativeCcw(x3, y3, x4, y4, x1, y1) *
               RelativeCcw(x3, y3, x4, y4, x2, y2) <=
           0));
}

bool Rectangle::IntersectsLine(float x1, float y1, float x2, float y2) const {
  double rx1 = GetX(), ry1 = GetY(), rx2 = rx1 + GetWidth(),
         ry2 = ry1 + GetHeight();
  return (rx1 <= x1 && x1 <= rx2 && ry1 <= y1 && y1 <= ry2) ||
         (rx1 <= x2 && x2 <= rx2 && ry1 <= y2 && y2 <= ry2) ||
         LinesIntersect(rx1, ry1, rx2, ry2, x1, y1, x2, y2) ||
         LinesIntersect(rx2, ry1, rx1, ry2, x1, y1, x2, y2);
}

std::string Rectangle::ToString() const {
  return "Rectangle: " + Fmt(GetWidth()) + 'x' + Fmt(GetHeight());
}

bool Rectangle::EqualsWithEpsilon(const Rectangle& that, float eps) const {
  float dx = std::fabs(x_ - that.x_), dy = std::fabs(y_ - that.y_);
  float dw = std::fabs(width_ - that.width_),
        dh = std::fabs(height_ - that.height_);
  return dx < eps && dy < eps && dw < eps && dh < eps;
}

const PageSize PageSize::kA0(2384, 3370), PageSize::kA1(1684, 2384),
    PageSize::kA2(1190, 1684), PageSize::kA3(842, 1190),
    PageSize::kA4(595, 842), PageSize::kA5(420, 595), PageSize::kA6(298, 420),
    PageSize::kA7(210, 298), PageSize::kA8(148, 210), PageSize::kA9(105, 148),
    PageSize::kA10(74, 105);
const PageSize PageSize::kB0(2834, 4008), PageSize::kB1(2004, 2834),
    PageSize::kB2(1417, 2004), PageSize::kB3(1000, 1417),
    PageSize::kB4(708, 1000), PageSize::kB5(498, 708), PageSize::kB6(354, 498),
    PageSize::kB7(249, 354), PageSize::kB8(175, 249), PageSize::kB9(124, 175),
    PageSize::kB10(88, 124);
const PageSize PageSize::kDefault(595, 842), PageSize::kExecutive(522, 756),
    PageSize::kLedger(1224, 792), PageSize::kLegal(612, 1008),
    PageSize::kLetter(612, 792), PageSize::kTabloid(792, 1224);

// ---------------------------------------------------------------- Path
Point Subpath::GetLastPoint() const {
  Point last_point = start_point_;
  if (!segments_.empty() && !closed_) {
    auto pts = segments_.back()->GetBasePoints();
    last_point = pts.back();
  }
  return last_point;
}

void Subpath::AddSegment(std::shared_ptr<IShape> segment) {
  if (closed_) return;
  if (IsSinglePointOpen()) start_point_ = segment->GetBasePoints()[0];
  segments_.push_back(std::move(segment));
}

bool Subpath::IsDegenerate() const {
  if (!segments_.empty() && closed_) return false;
  for (const auto& segment : segments_) {
    auto pts = segment->GetBasePoints();
    for (const auto& p : pts)
      if (!(p == start_point_)) return false;
  }
  return true;
}

static std::vector<Point> ApproximateBezier(const std::vector<Point>& p) {
  std::vector<Point> out;
  const int steps = 16;
  for (int i = 1; i <= steps; ++i) {
    double t = static_cast<double>(i) / steps;
    double mt = 1 - t;
    double x = mt * mt * mt * p[0].x + 3 * mt * mt * t * p[1].x +
               3 * mt * t * t * p[2].x + t * t * t * p[3].x;
    double y = mt * mt * mt * p[0].y + 3 * mt * mt * t * p[1].y +
               3 * mt * t * t * p[2].y + t * t * t * p[3].y;
    out.emplace_back(x, y);
  }
  return out;
}

std::vector<Point> Subpath::GetPiecewiseLinearApproximation() const {
  std::vector<Point> result;
  if (segments_.empty()) return result;
  result.push_back(start_point_);
  for (const auto& seg : segments_) {
    auto pts = seg->GetBasePoints();
    if (dynamic_cast<BezierCurve*>(seg.get())) {
      auto approx = ApproximateBezier(pts);
      result.insert(result.end(), approx.begin(), approx.end());
    } else {
      result.push_back(pts.back());
    }
  }
  return result;
}

void Path::AddSubpath(const Subpath& subpath) {
  subpaths_.push_back(subpath);
  current_point_ = subpath.GetLastPoint();
}

void Path::AddSubpaths(const std::vector<Subpath>& subpaths) {
  if (subpaths.empty()) return;
  subpaths_.insert(subpaths_.end(), subpaths.begin(), subpaths.end());
  current_point_ = subpaths_.back().GetLastPoint();
}

void Path::MoveTo(float x, float y) {
  current_point_ = Point(x, y);
  Subpath* last = GetLastSubpath();
  if (last && last->IsSinglePointOpen())
    last->SetStartPoint(current_point_);
  else
    subpaths_.emplace_back(current_point_);
}

void Path::LineTo(float x, float y) {
  if (subpaths_.empty())
    throw std::runtime_error("Path shall start with \"re\" or \"m\" operator");
  Point target(x, y);
  GetLastSubpath()->AddSegment(std::make_shared<Line>(current_point_, target));
  current_point_ = target;
}

void Path::CurveTo(float x1, float y1, float x2, float y2, float x3, float y3) {
  if (subpaths_.empty())
    throw std::runtime_error("Path shall start with \"re\" or \"m\" operator");
  Point p2(x1, y1), p3(x2, y2), p4(x3, y3);
  GetLastSubpath()->AddSegment(std::make_shared<BezierCurve>(
      std::vector<Point>{current_point_, p2, p3, p4}));
  current_point_ = p4;
}

void Path::CurveTo(float x2, float y2, float x3, float y3) {
  CurveTo(static_cast<float>(current_point_.x),
          static_cast<float>(current_point_.y), x2, y2, x3, y3);
}

void Path::CurveFromTo(float x1, float y1, float x3, float y3) {
  CurveTo(x1, y1, x3, y3, x3, y3);
}

void Path::Rectangle(const kernel::Rectangle& rect) {
  Rectangle(rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
}

void Path::Rectangle(float x, float y, float w, float h) {
  MoveTo(x, y);
  LineTo(x + w, y);
  LineTo(x + w, y + h);
  LineTo(x, y + h);
  CloseSubpath();
}

void Path::CloseSubpath() {
  Subpath* last = GetLastSubpath();
  if (last && !last->IsClosed()) {
    last->SetClosed(true);
    Point start_point = last->GetStartPoint();
    MoveTo(static_cast<float>(start_point.x),
           static_cast<float>(start_point.y));
  }
}

void Path::CloseAllSubpaths() {
  for (auto& subpath : subpaths_) subpath.SetClosed(true);
}

void Path::ReplaceCloseWithLine() {
  for (auto& subpath : subpaths_) {
    if (subpath.IsClosed() && !subpath.IsEmpty()) {
      subpath.SetClosed(false);
      Point last = subpath.GetLastPoint();
      subpath.AddSegment(std::make_shared<Line>(last, subpath.GetStartPoint()));
    }
  }
}

}  // namespace kernel
}  // namespace pdfcpp
