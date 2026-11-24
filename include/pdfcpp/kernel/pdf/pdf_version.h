#ifndef PDFCPP_KERNEL_PDF_PDF_VERSION_H_
#define PDFCPP_KERNEL_PDF_PDF_VERSION_H_

#include <string>

#include "pdfcpp/kernel/pdf/pdf_object.h"

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.pdf.PdfVersion (value type).
class PdfVersion {
 public:
  static const PdfVersion kPdf10, kPdf11, kPdf12, kPdf13, kPdf14, kPdf15,
      kPdf16, kPdf17, kPdf20;

  constexpr PdfVersion(int major, int minor) : major_(major), minor_(minor) {}
  /// Parses "PDF-1.7".
  static PdfVersion FromString(const std::string& value);
  /// Parses a /Version name like /1.7.
  static PdfVersion FromPdfName(const PdfName& name);
  std::string ToString() const {
    return "PDF-" + std::to_string(major_) + "." + std::to_string(minor_);
  }
  PdfNamePtr ToPdfName() const {
    return std::make_shared<PdfName>(std::to_string(major_) + "." +
                                     std::to_string(minor_));
  }
  int GetMajor() const { return major_; }
  int GetMinor() const { return minor_; }
  int CompareTo(const PdfVersion& o) const {
    int c = major_ - o.major_;
    return c != 0 ? c : minor_ - o.minor_;
  }
  bool operator==(const PdfVersion& o) const {
    return major_ == o.major_ && minor_ == o.minor_;
  }
  bool operator!=(const PdfVersion& o) const { return !(*this == o); }
  bool operator<(const PdfVersion& o) const { return CompareTo(o) < 0; }
  bool operator>=(const PdfVersion& o) const { return CompareTo(o) >= 0; }

 private:
  int major_;
  int minor_;
};

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_PDF_PDF_VERSION_H_
