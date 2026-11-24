#ifndef PDFCPP_KERNEL_EXCEPTIONS_PDF_EXCEPTION_H_
#define PDFCPP_KERNEL_EXCEPTIONS_PDF_EXCEPTION_H_

#include <string>

#include "pdfcpp/io/exceptions/io_exception.h"

namespace pdfcpp {
namespace kernel {

/// Port of com.itextpdf.kernel.exceptions.PdfException.
class PdfException : public ITextException {
 public:
  explicit PdfException(const std::string& message) : ITextException(message) {}
};

/// Port of BadPasswordException.
class BadPasswordException : public PdfException {
 public:
  static constexpr const char* kPdfReaderNotOpenedWithOwnerPassword =
      "PdfReader is not opened with owner password";
  explicit BadPasswordException(const std::string& message)
      : PdfException(message) {}
};

/// Port of XrefCycledReferencesException.
class XrefCycledReferencesException : public PdfException {
 public:
  explicit XrefCycledReferencesException(const std::string& message)
      : PdfException(message) {}
};

/// Port of MemoryLimitsAwareException.
class MemoryLimitsAwareException : public PdfException {
 public:
  explicit MemoryLimitsAwareException(const std::string& message)
      : PdfException(message) {}
};

/// Port of KernelExceptionMessageConstant (subset used by the port).
namespace kernel_exception_message_constant {
inline constexpr const char* kCannotFlushObject = "Cannot flush object.";
inline constexpr const char* kCannotCopyFlushedObject =
    "Cannot copy flushed object.";
inline constexpr const char*
    kCannotCopyIndirectObjectFromTheDocumentThatIsBeingWritten =
        "Cannot copy indirect object from the document that is being written.";
inline constexpr const char* kCannotCopyToDocumentOpenedInReadingMode =
    "Cannot copy to document opened in reading mode.";
inline constexpr const char* kDocumentForCopyToCannotBeNull =
    "Document for CopyTo cannot be null.";
inline constexpr const char* kThereIsNoAssociatePdfWriterForMakingIndirects =
    "There is no associate PdfWriter for making indirects.";
inline constexpr const char* kXrefStreamHasSelfReferencedObject =
    "Xref stream has self-referenced object {0}.";
inline constexpr const char* kCannotOperateWithFlushedPdfStream =
    "Cannot operate with the flushed PdfStream.";
inline constexpr const char* kCannotGetPdfStreamBytes =
    "Cannot get PdfStream bytes.";
inline constexpr const char* kCannotReadAStreamInOrderToAppendNewBytes =
    "Cannot read a stream in order to append new bytes.";
inline constexpr const char* kCannotWriteObjectAfterItWasReleased =
    "Cannot write object after it was released. In normal situation the object "
    "must be read once again before "
    "being written.";
inline constexpr const char* kPdfIndirectObjectBelongsToOtherPdfDocument =
    "Pdf indirect object belongs to other PDF document. Copy object to current "
    "pdf document.";
inline constexpr const char* kFilterIsNotANameOrArray =
    "filter is not a name or array.";
inline constexpr const char* kThisDecodeParameterTypeIsNotSupported =
    "Decode parameter type {0} is not supported.";
inline constexpr const char* kFilterIsNotSupported =
    "Filter {0} is not supported.";
inline constexpr const char* kDocumentHasNoPages = "Document has no pages.";
inline constexpr const char* kDocumentClosedItIsImpossibleToExecuteAction =
    "Document was closed. It is impossible to execute action.";
inline constexpr const char* kInvalidPageStructure =
    "Invalid page structure {0}.";
inline constexpr const char* kRequestedPageNumberIsOutOfBounds =
    "Requested page number {0} is out of bounds.";
inline constexpr const char* kTrailerNotFound = "Trailer not found.";
inline constexpr const char* kInvalidXrefStream = "Invalid xref stream.";
inline constexpr const char* kInvalidXrefSection = "Invalid xref section.";
inline constexpr const char* kInvalidObjectStreamNumber =
    "Unable to read object {0} with object stream number {1} and index {2} "
    "from object stream.";
inline constexpr const char* kInvalidIndirectReference =
    "Invalid indirect reference {0}.";
inline constexpr const char* kDictionaryKeyIsNotAName =
    "Dictionary key {0} is not a name.";
inline constexpr const char* kUnexpectedToken =
    "unexpected {0} was encountered.";
inline constexpr const char* kUnexpectedGtGt = "unexpected >> was encountered.";
inline constexpr const char* kUnexpectedCloseBracket =
    "unexpected close bracket.";
inline constexpr const char* kUnexpectedEndOfFile = "Unexpected end of file.";
inline constexpr const char* kCannotReadPdfObject = "Cannot read PdfObject.";
inline constexpr const char* kPdfDocumentMustBeCreatedWithReaderOrWriter =
    "PdfDocument must be created with reader or writer";
inline constexpr const char* kCannotCloseDocument = "Cannot close document.";
inline constexpr const char* kCannotAddKidsToFlushedPageTreeNode =
    "Cannot add kids to flushed page tree node.";
inline constexpr const char* kFlushedPageCannotBeAddedOrInserted =
    "Flushed page cannot be added or inserted.";
inline constexpr const char* kPageAlreadyFlushed =
    "The page has been already flushed.";
inline constexpr const char* kInvalidRangeArray = "Invalid range array.";
inline constexpr const char* kUnknownEncryptionTypeR =
    "Unknown encryption type R == {0}.";
inline constexpr const char* kUnknownEncryptionTypeV =
    "Unknown encryption type V == {0}.";
inline constexpr const char* kBadUserPassword =
    "Bad user password. Password is not provided or wrong password provided.";
inline constexpr const char* kBadCertificateAndKey =
    "Bad public key certificate and/or private key.";
inline constexpr const char* kUnsupportedFont = "Unsupported font.";
inline constexpr const char* kCannotBeEmbeddedDueToLicensingRestrictions =
    "{0} cannot be embedded due to licensing restrictions.";
inline constexpr const char*
    kCannotCreateType0FontWithTrueTypeFontProgramWithoutEmbeddingIt =
        "Cannot create Type0 font with true type font program without "
        "embedding it.";
inline constexpr const char* kFontProviderNotSetFontFamilyNotResolved =
    "FontProvider and FontSet are empty. Cannot resolve font family name (see "
    "ElementPropertyContainer#setFontFamily) "
    "without initialized FontProvider.";
inline constexpr const char* kColorSpaceNotFound = "ColorSpace not found.";
inline constexpr const char* kUnbalancedSaveRestoreStateOperators =
    "Unbalanced save restore state operators.";
inline constexpr const char* kUnbalancedBeginEndMarkedContentOperators =
    "Unbalanced begin/end marked content operators.";
inline constexpr const char* kUnbalancedLayerOperators =
    "Unbalanced layer operators.";
inline constexpr const char* kIncorrectPageRotation =
    "Incorrect page rotation.";
inline constexpr const char* kCannotDrawElementsOnAlreadyFlushedPages =
    "Cannot draw elements on already flushed pages.";
}  // namespace kernel_exception_message_constant

}  // namespace kernel
}  // namespace pdfcpp

#endif  // PDFCPP_KERNEL_EXCEPTIONS_PDF_EXCEPTION_H_
