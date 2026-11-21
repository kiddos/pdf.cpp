// Encrypts an existing PDF using AES-256 with user and owner passwords.
#include <iostream>

#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: encrypt <in.pdf> <out.pdf> <user_password> [owner_password]\n";
        return 1;
    }
    std::string userPw = argv[3];
    std::string ownerPw = argc > 4 ? argv[4] : userPw + "_owner";

    WriterProperties writerProps;
    writerProps.setStandardEncryption(
        toBytes(userPw), toBytes(ownerPw),
        EncryptionConstants::ALLOW_PRINTING | EncryptionConstants::ALLOW_COPY,
        EncryptionConstants::ENCRYPTION_AES_256);

    PdfDocument src(std::make_unique<PdfReader>(argv[1]));
    PdfDocument dest(std::make_unique<PdfWriter>(argv[2], writerProps));
    int pages = src.getNumberOfPages();
    src.copyPagesTo(1, pages, dest);
    dest.close();
    src.close();
    std::cout << "Encrypted " << pages << " page(s) into " << argv[2] << " (AES-256)\n";
    return 0;
}
