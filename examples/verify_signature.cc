// Inspects and verifies all digital signatures in a PDF using SignatureUtil.
#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/signatures/signatures.h"

using namespace pdfcpp::kernel;
using namespace pdfcpp::signatures;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: verify_signature <file.pdf>\n";
    return 1;
  }
  PdfDocument pdf(std::make_unique<PdfReader>(argv[1]));
  SignatureUtil util(pdf);
  auto names = util.GetSignatureNames();
  if (names.empty()) {
    std::cout << "No signatures found in " << argv[1] << "\n";
    return 0;
  }

  bool all_valid = true;
  std::cout << "Found " << names.size() << " signature(s) across "
            << util.GetTotalRevisions() << " revision(s):\n";
  for (const auto& name : names) {
    SignatureVerificationResult res = util.VerifySignature(name);
    std::cout << "  Field: " << name << "\n"
              << "    Integrity & authenticity: "
              << (res.integrity_and_authenticity ? "VALID" : "INVALID") << "\n"
              << "    Covers whole document:    "
              << (res.signature_covers_whole_document ? "yes" : "no") << "\n";
    if (!res.signer_name.empty())
      std::cout << "    Signer:   " << res.signer_name << "\n";
    if (!res.sub_filter.empty())
      std::cout << "    SubFilter:" << " " << res.sub_filter << "\n";
    if (!res.sign_date.empty())
      std::cout << "    Date:     " << res.sign_date << "\n";
    if (!res.reason.empty())
      std::cout << "    Reason:   " << res.reason << "\n";
    if (!res.location.empty())
      std::cout << "    Location: " << res.location << "\n";
    if (!res.error.empty()) std::cout << "    Error:    " << res.error << "\n";
    if (!res.integrity_and_authenticity) all_valid = false;
  }
  return all_valid ? 0 : 1;
}
