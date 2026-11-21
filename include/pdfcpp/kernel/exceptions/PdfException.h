#pragma once

#include <string>

#include "pdfcpp/io/exceptions/IoException.h"

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
    static constexpr const char* PdfReaderNotOpenedWithOwnerPassword =
        "PdfReader is not opened with owner password";
    explicit BadPasswordException(const std::string& message) : PdfException(message) {}
};

/// Port of XrefCycledReferencesException.
class XrefCycledReferencesException : public PdfException {
public:
    explicit XrefCycledReferencesException(const std::string& message) : PdfException(message) {}
};

/// Port of MemoryLimitsAwareException.
class MemoryLimitsAwareException : public PdfException {
public:
    explicit MemoryLimitsAwareException(const std::string& message) : PdfException(message) {}
};

/// Port of KernelExceptionMessageConstant (subset used by the port).
namespace KernelExceptionMessageConstant {
inline constexpr const char* CANNOT_FLUSH_OBJECT = "Cannot flush object.";
inline constexpr const char* CANNOT_COPY_FLUSHED_OBJECT = "Cannot copy flushed object.";
inline constexpr const char* CANNOT_COPY_INDIRECT_OBJECT_FROM_THE_DOCUMENT_THAT_IS_BEING_WRITTEN =
    "Cannot copy indirect object from the document that is being written.";
inline constexpr const char* CANNOT_COPY_TO_DOCUMENT_OPENED_IN_READING_MODE =
    "Cannot copy to document opened in reading mode.";
inline constexpr const char* DOCUMENT_FOR_COPY_TO_CANNOT_BE_NULL = "Document for copyTo cannot be null.";
inline constexpr const char* THERE_IS_NO_ASSOCIATE_PDF_WRITER_FOR_MAKING_INDIRECTS =
    "There is no associate PdfWriter for making indirects.";
inline constexpr const char* XREF_STREAM_HAS_SELF_REFERENCED_OBJECT = "Xref stream has self-referenced object {0}.";
inline constexpr const char* CANNOT_OPERATE_WITH_FLUSHED_PDF_STREAM = "Cannot operate with the flushed PdfStream.";
inline constexpr const char* CANNOT_GET_PDF_STREAM_BYTES = "Cannot get PdfStream bytes.";
inline constexpr const char* CANNOT_READ_A_STREAM_IN_ORDER_TO_APPEND_NEW_BYTES =
    "Cannot read a stream in order to append new bytes.";
inline constexpr const char* CANNOT_WRITE_OBJECT_AFTER_IT_WAS_RELEASED =
    "Cannot write object after it was released. In normal situation the object must be read once again before "
    "being written.";
inline constexpr const char* PDF_INDIRECT_OBJECT_BELONGS_TO_OTHER_PDF_DOCUMENT =
    "Pdf indirect object belongs to other PDF document. Copy object to current pdf document.";
inline constexpr const char* FILTER_IS_NOT_A_NAME_OR_ARRAY = "filter is not a name or array.";
inline constexpr const char* THIS_DECODE_PARAMETER_TYPE_IS_NOT_SUPPORTED = "Decode parameter type {0} is not supported.";
inline constexpr const char* FILTER_IS_NOT_SUPPORTED = "Filter {0} is not supported.";
inline constexpr const char* DOCUMENT_HAS_NO_PAGES = "Document has no pages.";
inline constexpr const char* DOCUMENT_CLOSED_IT_IS_IMPOSSIBLE_TO_EXECUTE_ACTION =
    "Document was closed. It is impossible to execute action.";
inline constexpr const char* INVALID_PAGE_STRUCTURE = "Invalid page structure {0}.";
inline constexpr const char* REQUESTED_PAGE_NUMBER_IS_OUT_OF_BOUNDS = "Requested page number {0} is out of bounds.";
inline constexpr const char* TRAILER_NOT_FOUND = "Trailer not found.";
inline constexpr const char* INVALID_XREF_STREAM = "Invalid xref stream.";
inline constexpr const char* INVALID_XREF_SECTION = "Invalid xref section.";
inline constexpr const char* INVALID_OBJECT_STREAM_NUMBER =
    "Unable to read object {0} with object stream number {1} and index {2} from object stream.";
inline constexpr const char* INVALID_INDIRECT_REFERENCE = "Invalid indirect reference {0}.";
inline constexpr const char* DICTIONARY_KEY_IS_NOT_A_NAME = "Dictionary key {0} is not a name.";
inline constexpr const char* UNEXPECTED_TOKEN = "unexpected {0} was encountered.";
inline constexpr const char* UNEXPECTED_GT_GT = "unexpected >> was encountered.";
inline constexpr const char* UNEXPECTED_CLOSE_BRACKET = "unexpected close bracket.";
inline constexpr const char* UNEXPECTED_END_OF_FILE = "Unexpected end of file.";
inline constexpr const char* CANNOT_READ_PDF_OBJECT = "Cannot read PdfObject.";
inline constexpr const char* PDF_DOCUMENT_MUST_BE_CREATED_WITH_READER_OR_WRITER = "PdfDocument must be created with reader or writer";
inline constexpr const char* CANNOT_CLOSE_DOCUMENT = "Cannot close document.";
inline constexpr const char* CANNOT_ADD_KIDS_TO_FLUSHED_PAGE_TREE_NODE = "Cannot add kids to flushed page tree node.";
inline constexpr const char* FLUSHED_PAGE_CANNOT_BE_ADDED_OR_INSERTED = "Flushed page cannot be added or inserted.";
inline constexpr const char* PAGE_ALREADY_FLUSHED = "The page has been already flushed.";
inline constexpr const char* INVALID_RANGE_ARRAY = "Invalid range array.";
inline constexpr const char* UNKNOWN_ENCRYPTION_TYPE_R = "Unknown encryption type R == {0}.";
inline constexpr const char* UNKNOWN_ENCRYPTION_TYPE_V = "Unknown encryption type V == {0}.";
inline constexpr const char* BAD_USER_PASSWORD = "Bad user password. Password is not provided or wrong password provided.";
inline constexpr const char* BAD_CERTIFICATE_AND_KEY = "Bad public key certificate and/or private key.";
inline constexpr const char* UNSUPPORTED_FONT = "Unsupported font.";
inline constexpr const char* CANNOT_BE_EMBEDDED_DUE_TO_LICENSING_RESTRICTIONS =
    "{0} cannot be embedded due to licensing restrictions.";
inline constexpr const char* CANNOT_CREATE_TYPE_0_FONT_WITH_TRUE_TYPE_FONT_PROGRAM_WITHOUT_EMBEDDING_IT =
    "Cannot create Type0 font with true type font program without embedding it.";
inline constexpr const char* FONT_PROVIDER_NOT_SET_FONT_FAMILY_NOT_RESOLVED =
    "FontProvider and FontSet are empty. Cannot resolve font family name (see ElementPropertyContainer#setFontFamily) "
    "without initialized FontProvider.";
inline constexpr const char* COLOR_SPACE_NOT_FOUND = "ColorSpace not found.";
inline constexpr const char* UNBALANCED_SAVE_RESTORE_STATE_OPERATORS = "Unbalanced save restore state operators.";
inline constexpr const char* UNBALANCED_BEGIN_END_MARKED_CONTENT_OPERATORS =
    "Unbalanced begin/end marked content operators.";
inline constexpr const char* UNBALANCED_LAYER_OPERATORS = "Unbalanced layer operators.";
inline constexpr const char* INCORRECT_PAGE_ROTATION = "Incorrect page rotation.";
inline constexpr const char* CANNOT_DRAW_ELEMENTS_ON_ALREADY_FLUSHED_PAGES = "Cannot draw elements on already flushed pages.";
}  // namespace KernelExceptionMessageConstant

}  // namespace kernel
}  // namespace pdfcpp
