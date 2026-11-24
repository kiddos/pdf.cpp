#include "pdfcpp/signatures/signatures.h"

#include <cstdlib>
#include <fstream>

#include "pdfcpp/io/util/stream_util.h"
#include "pdfcpp/kernel/pdf/pdf_writer.h"
#include "pdfcpp/layout/document.h"
#include "test_harness.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::signatures;

static Bytes MakePdf(bool full_compression) {
  io::ByteArrayOutputStream baos;
  {
    PdfWriter writer(
        baos, WriterProperties().SetFullCompressionMode(full_compression));
    PdfDocument pdf(writer);
    layout::Document doc(pdf);
    doc.Add(layout::Make<layout::Paragraph>("A document to be signed."));
    doc.Close();
  }
  return baos.ToByteArray();
}

TEST(SignAndVerify) {
  PrivateKey key = PrivateKey::GenerateRsa(2048);
  Certificate cert = key.CreateSelfSignedCertificate("pdfcpp test signer");
  for (bool full : {false, true}) {
    Bytes original = MakePdf(full);
    io::ByteArrayOutputStream signed_out;
    {
      auto reader = PdfReader::FromBytes(original);
      PdfSigner signer(*reader, signed_out);
      SignerProperties props;
      props.field_name = "sig1";
      props.reason = "Testing";
      props.location = "Earth";
      props.page_rect = Rectangle(36, 600, 200, 60);
      signer.SetSignerProperties(props);
      signer.SignDetached(key, {cert});
    }
    Bytes signed_pdf = signed_out.ToByteArray();
    EXPECT_TRUE(std::equal(original.begin(), original.end(),
                           signed_pdf.begin()));  // incremental update
    std::string path = full ? "signed_full.pdf" : "signed.pdf";
    io::StreamUtil::WriteFile(path, signed_pdf);
    PdfDocument doc(PdfReader::FromBytes(signed_pdf));
    SignatureUtil util(doc);
    auto names = util.GetSignatureNames();
    EXPECT_EQ(static_cast<int>(names.size()), 1);
    auto result = util.VerifySignature("sig1");
    EXPECT_TRUE(result.integrity_and_authenticity);
    EXPECT_TRUE(result.signature_covers_whole_document);
    EXPECT_EQ(result.signer_name, std::string("pdfcpp test signer"));
    EXPECT_EQ(result.reason, std::string("Testing"));
    // tamper with the signed bytes: verification must fail
    Bytes tampered = signed_pdf;
    size_t pos = std::string(tampered.begin(), tampered.end()).find("signed.");
    if (pos != std::string::npos) tampered[pos] = 'S';
    PdfDocument doc2(PdfReader::FromBytes(tampered));
    SignatureUtil util2(doc2);
    if (pos != std::string::npos)
      EXPECT_TRUE(!util2.VerifySignature("sig1").integrity_and_authenticity);
  }
  if (std::system("which pdfsig > /dev/null 2>&1") == 0) {
    int rc = std::system("pdfsig signed.pdf > pdfsig.txt 2>&1");
    std::ifstream in("pdfsig.txt");
    std::string out((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    std::cout << out;
    EXPECT_TRUE(rc == 0 || out.find("Signature is Valid") != std::string::npos);
    EXPECT_TRUE(out.find("Signature Validation: Signature is Valid.") !=
                std::string::npos);
  }
}

static Bytes SignedPdf() {
  PrivateKey key = PrivateKey::GenerateRsa(2048);
  Certificate cert = key.CreateSelfSignedCertificate("pdfcpp test signer");
  io::ByteArrayOutputStream signed_out;
  auto reader = PdfReader::FromBytes(MakePdf(false));
  PdfSigner signer(*reader, signed_out);
  SignerProperties props;
  props.field_name = "sig1";
  props.page_rect = Rectangle(36, 600, 200, 60);
  signer.SetSignerProperties(props);
  signer.SignDetached(key, {cert});
  return signed_out.ToByteArray();
}

static PdfArrayPtr ByteRange(std::initializer_list<int64_t> values) {
  auto array = std::make_shared<PdfArray>();
  for (int64_t v : values) array->Add(std::make_shared<PdfNumber>(double(v)));
  return array;
}

// BUGS.md #28: forged /ByteRange values are reported as an error instead of
// allocating or reading outside the file.
TEST(VerifyRejectsForgedByteRange) {
  Bytes signed_pdf = SignedPdf();
  const int64_t length = static_cast<int64_t>(signed_pdf.size());
  for (auto range : {ByteRange({0, -5, 10, 10}), ByteRange({-1, 10, 10, 10}),
                     ByteRange({0, 4000000000LL, 10, 10}),
                     ByteRange({0, 10, length - 5, 10})}) {
    PdfDocument doc(PdfReader::FromBytes(signed_pdf));
    SignatureUtil util(doc);
    auto dict = util.GetSignatureDictionary("sig1");
    EXPECT_TRUE(dict != nullptr);
    if (!dict) continue;
    dict->Put(PdfName::kByteRange, range);
    SignatureVerificationResult result;
    bool threw = false;
    try {
      result = util.VerifySignature("sig1");
    } catch (...) {
      threw = true;
    }
    EXPECT_TRUE(!threw);
    EXPECT_TRUE(!result.integrity_and_authenticity);
    EXPECT_EQ(result.error, std::string("Invalid /ByteRange"));
  }
}

// BUGS.md #28: a document that was not read from a file has no signed bytes
// to verify; that is an error, not a null dereference.
TEST(VerifyWithoutReader) {
  io::ByteArrayOutputStream baos;
  PdfWriter writer(baos);
  PdfDocument doc(writer);
  doc.AddNewPage();
  auto value = std::make_shared<PdfDictionary>();
  value->Put(PdfName::kType, PdfName::kSig);
  value->Put(PdfName::kContents, std::make_shared<PdfString>("0"));
  value->Put(PdfName::kByteRange, ByteRange({0, 10, 20, 10}));
  auto field = std::make_shared<PdfDictionary>();
  field->Put(PdfName::kFT, PdfName::kSig);
  field->Put(PdfName::kT, std::make_shared<PdfString>("sig1"));
  field->Put(PdfName::kV, value);
  auto fields = std::make_shared<PdfArray>();
  fields->Add(field);
  auto acro_form = std::make_shared<PdfDictionary>();
  acro_form->Put(PdfName::kFields, fields);
  doc.GetCatalog().GetPdfObject()->Put(PdfName::kAcroForm, acro_form);
  SignatureUtil util(doc);
  auto result = util.VerifySignature("sig1");
  EXPECT_TRUE(!result.integrity_and_authenticity);
  EXPECT_EQ(result.error, std::string("Document has no reader"));
  doc.Close();
}

TEST_MAIN()
