// Inspects and verifies all digital signatures in a PDF using SignatureUtil.
#include <iostream>

#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/signatures/Signatures.h"

using namespace pdfcpp::kernel;
using namespace pdfcpp::signatures;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: verify_signature <file.pdf>\n";
        return 1;
    }
    PdfDocument pdf(std::make_unique<PdfReader>(argv[1]));
    SignatureUtil util(pdf);
    auto names = util.getSignatureNames();
    if (names.empty()) {
        std::cout << "No signatures found in " << argv[1] << "\n";
        return 0;
    }

    bool allValid = true;
    std::cout << "Found " << names.size() << " signature(s) across " << util.getTotalRevisions() << " revision(s):\n";
    for (const auto& name : names) {
        SignatureVerificationResult res = util.verifySignature(name);
        std::cout << "  Field: " << name << "\n"
                  << "    Integrity & authenticity: " << (res.integrityAndAuthenticity ? "VALID" : "INVALID") << "\n"
                  << "    Covers whole document:    " << (res.signatureCoversWholeDocument ? "yes" : "no") << "\n";
        if (!res.signerName.empty()) std::cout << "    Signer:   " << res.signerName << "\n";
        if (!res.subFilter.empty()) std::cout << "    SubFilter:" << " " << res.subFilter << "\n";
        if (!res.signDate.empty()) std::cout << "    Date:     " << res.signDate << "\n";
        if (!res.reason.empty()) std::cout << "    Reason:   " << res.reason << "\n";
        if (!res.location.empty()) std::cout << "    Location: " << res.location << "\n";
        if (!res.error.empty()) std::cout << "    Error:    " << res.error << "\n";
        if (!res.integrityAndAuthenticity) allValid = false;
    }
    return allValid ? 0 : 1;
}
