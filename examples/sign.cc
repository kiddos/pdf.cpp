// Signs a PDF with a key/certificate (PEM files or PKCS#12), like iText's
// PdfSigner.signDetached.
#include <iostream>

#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/signatures/signatures.h"

using namespace pdfcpp;
using namespace pdfcpp::signatures;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: sign <in.pdf> <out.pdf> [key.pem cert.pem | file.p12 "
                 "password]\n"
                 "       without credentials a self-signed test certificate is "
                 "generated\n";
    return 1;
  }
  PrivateKey key;
  std::vector<Certificate> chain;
  if (argc >= 5 && std::string(argv[3]).find(".p12") != std::string::npos) {
    Pkcs12 p12 = Pkcs12::Load(io::StreamUtil::ReadFile(argv[3]), argv[4]);
    key = p12.key;
    chain = p12.chain;
  } else if (argc >= 5) {
    key = PrivateKey::FromPem(io::StreamUtil::ReadFile(argv[3]));
    chain = Certificate::FromPem(io::StreamUtil::ReadFile(argv[4]));
  } else {
    key = PrivateKey::GenerateRsa(2048);
    chain = {key.CreateSelfSignedCertificate("pdfcpp self-signed")};
  }
  kernel::PdfReader reader(argv[1]);
  io::FileOutputStream out(argv[2]);
  PdfSigner signer(reader, out);
  SignerProperties props;
  props.reason = "Approved";
  props.location = "pdfcpp";
  props.page_rect = kernel::Rectangle(36, 36, 200, 50);
  signer.SetSignerProperties(props);
  signer.SignDetached(key, chain, "SHA256");
  std::cout << "Signed " << argv[1] << " -> " << argv[2] << "\n";
  return 0;
}
