#pragma once

#include "pdfcpp/io/source/ByteBuffer.h"

namespace pdfcpp {
namespace io {

/// Port of com.itextpdf.io.source.ByteUtils: compact ISO-8859-1 number formatting for PDF output.
class ByteUtils {
public:
    static bool HighPrecision;

    static Bytes getIsoBytes(const std::string& text);
    static Bytes getIsoBytes(uint8_t pre, const std::string& text, uint8_t post = 0);
    static Bytes getIsoBytes(int n);
    static Bytes getIsoBytes(long long n);
    static Bytes getIsoBytes(double d);
    static Bytes getIsoBytes(double d, bool highPrecision);

    /// Formats a number into its compact PDF textual representation.
    static std::string formatDouble(double d) { return toLatin1String(getIsoBytes(d)); }
    static std::string formatDouble(double d, bool highPrecision) { return toLatin1String(getIsoBytes(d, highPrecision)); }
};

}  // namespace io
}  // namespace pdfcpp
