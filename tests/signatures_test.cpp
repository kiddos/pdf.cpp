#include <cstdlib>
#include <fstream>

#include "TestHarness.h"
#include "pdfcpp/io/util/StreamUtil.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/layout/Document.h"
#include "pdfcpp/signatures/Signatures.h"

using namespace pdfcpp;
using namespace pdfcpp::kernel;
using namespace pdfcpp::signatures;

static Bytes makePdf(bool fullCompression) {
    io::ByteArrayOutputStream baos;
    {
        PdfWriter writer(baos, WriterProperties().setFullCompressionMode(fullCompression));
        PdfDocument pdf(writer);
        layout::Document doc(pdf);
        doc.add(layout::make<layout::Paragraph>("A document to be signed."));
        doc.close();
    }
    return baos.toByteArray();
}

TEST(signAndVerify) {
    PrivateKey key = PrivateKey::generateRsa(2048);
    Certificate cert = key.createSelfSignedCertificate("pdfcpp test signer");
    for (bool full : {false, true}) {
        Bytes original = makePdf(full);
        io::ByteArrayOutputStream signedOut;
        {
            auto reader = PdfReader::fromBytes(original);
            PdfSigner signer(*reader, signedOut);
            SignerProperties props;
            props.fieldName = "sig1";
            props.reason = "Testing";
            props.location = "Earth";
            props.pageRect = Rectangle(36, 600, 200, 60);
            signer.setSignerProperties(props);
            signer.signDetached(key, {cert});
        }
        Bytes signedPdf = signedOut.toByteArray();
        EXPECT_TRUE(std::equal(original.begin(), original.end(), signedPdf.begin()));  // incremental update
        std::string path = full ? "signed_full.pdf" : "signed.pdf";
        io::StreamUtil::writeFile(path, signedPdf);
        PdfDocument doc(PdfReader::fromBytes(signedPdf));
        SignatureUtil util(doc);
        auto names = util.getSignatureNames();
        EXPECT_EQ(static_cast<int>(names.size()), 1);
        auto result = util.verifySignature("sig1");
        EXPECT_TRUE(result.integrityAndAuthenticity);
        EXPECT_TRUE(result.signatureCoversWholeDocument);
        EXPECT_EQ(result.signerName, std::string("pdfcpp test signer"));
        EXPECT_EQ(result.reason, std::string("Testing"));
        // tamper with the signed bytes: verification must fail
        Bytes tampered = signedPdf;
        size_t pos = std::string(tampered.begin(), tampered.end()).find("signed.");
        if (pos != std::string::npos) tampered[pos] = 'S';
        PdfDocument doc2(PdfReader::fromBytes(tampered));
        SignatureUtil util2(doc2);
        if (pos != std::string::npos) EXPECT_TRUE(!util2.verifySignature("sig1").integrityAndAuthenticity);
    }
    if (std::system("which pdfsig > /dev/null 2>&1") == 0) {
        int rc = std::system("pdfsig signed.pdf > pdfsig.txt 2>&1");
        std::ifstream in("pdfsig.txt");
        std::string out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::cout << out;
        EXPECT_TRUE(rc == 0 || out.find("Signature is Valid") != std::string::npos);
        EXPECT_TRUE(out.find("Signature Validation: Signature is Valid.") != std::string::npos);
    }
}

TEST_MAIN()
