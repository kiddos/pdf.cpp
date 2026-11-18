#pragma once

#include <string>

#include "pdfcpp/kernel/pdf/PdfObject.h"

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.pdf.PdfVersion (value type).
class PdfVersion {
public:
    static const PdfVersion PDF_1_0, PDF_1_1, PDF_1_2, PDF_1_3, PDF_1_4, PDF_1_5, PDF_1_6, PDF_1_7, PDF_2_0;

    constexpr PdfVersion(int major, int minor) : major_(major), minor_(minor) {}
    /// Parses "PDF-1.7".
    static PdfVersion fromString(const std::string& value);
    /// Parses a /Version name like /1.7.
    static PdfVersion fromPdfName(const PdfName& name);
    std::string toString() const { return "PDF-" + std::to_string(major_) + "." + std::to_string(minor_); }
    PdfNamePtr toPdfName() const { return std::make_shared<PdfName>(std::to_string(major_) + "." + std::to_string(minor_)); }
    int getMajor() const { return major_; }
    int getMinor() const { return minor_; }
    int compareTo(const PdfVersion& o) const {
        int c = major_ - o.major_;
        return c != 0 ? c : minor_ - o.minor_;
    }
    bool operator==(const PdfVersion& o) const { return major_ == o.major_ && minor_ == o.minor_; }
    bool operator!=(const PdfVersion& o) const { return !(*this == o); }
    bool operator<(const PdfVersion& o) const { return compareTo(o) < 0; }
    bool operator>=(const PdfVersion& o) const { return compareTo(o) >= 0; }

private:
    int major_;
    int minor_;
};

}  // namespace kernel
}  // namespace pdfcpp
