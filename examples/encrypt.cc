// Encrypts an existing PDF using AES-256 with user and owner passwords.
#include <iostream>

#include "pdfcpp/kernel/pdf/pdf_document.h"
#include "pdfcpp/kernel/pdf/pdf_reader.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: encrypt <in.pdf> <out.pdf> <user_password> "
                 "[owner_password]\n";
    return 1;
  }
  std::string user_pw = argv[3];
  std::string owner_pw = argc > 4 ? argv[4] : user_pw + "_owner";

  WriterProperties writer_props;
  writer_props.SetStandardEncryption(
      ToBytes(user_pw), ToBytes(owner_pw),
      encryption_constants::kAllowPrinting | encryption_constants::kAllowCopy,
      encryption_constants::kEncryptionAes256);

  PdfDocument src(std::make_unique<PdfReader>(argv[1]));
  PdfDocument dest(std::make_unique<PdfWriter>(argv[2], writer_props));
  int pages = src.GetNumberOfPages();
  src.CopyPagesTo(1, pages, dest);
  dest.Close();
  src.Close();
  std::cout << "Encrypted " << pages << " page(s) into " << argv[2]
            << " (AES-256)\n";
  return 0;
}
