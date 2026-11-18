#include "pdfcpp/kernel/pdf/PdfOutputStream.h"

#include "pdfcpp/io/source/ByteUtils.h"
#include "pdfcpp/kernel/crypto/PdfEncryption.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfReader.h"
#include "pdfcpp/kernel/pdf/PdfWriter.h"
#include "pdfcpp/kernel/pdf/filters/Filters.h"

namespace pdfcpp {
namespace kernel {

std::shared_ptr<io::OutputStream> FlateCompressionStrategy::createNewOutputStream(std::shared_ptr<io::OutputStream> original,
                                                                                  PdfStream& stream) {
    return std::make_shared<io::DeflaterOutputStream>(std::move(original), stream.getCompressionLevel());
}

PdfOutputStream::PdfOutputStream(std::shared_ptr<io::OutputStream> outputStream)
    : io::HighPrecisionOutputStream(std::move(outputStream)) {}

IStreamCompressionStrategy& PdfOutputStream::getCompressionStrategy() {
    if (!compressionStrategy_) compressionStrategy_ = std::make_shared<FlateCompressionStrategy>();
    return *compressionStrategy_;
}

void PdfOutputStream::writeBigEndian(int64_t bytes, int size) {
    while (--size >= 0) write(static_cast<int>((bytes >> (8 * size)) & 0xff));
}

PdfOutputStream& PdfOutputStream::write(PdfObject& obj) {
    PdfObject* pdfObject = &obj;
    if (pdfObject->checkState(PdfObject::MUST_BE_INDIRECT) && document_ != nullptr) {
        pdfObject->makeIndirect(document_);
        pdfObject = pdfObject->getIndirectReference();
    }
    if (pdfObject->checkState(PdfObject::READ_ONLY))
        throw PdfException(KernelExceptionMessageConstant::CANNOT_WRITE_OBJECT_AFTER_IT_WAS_RELEASED);
    switch (pdfObject->getType()) {
        case PdfObject::ARRAY:
            writeArray(static_cast<PdfArray&>(*pdfObject));
            break;
        case PdfObject::DICTIONARY:
            writeDictionary(static_cast<PdfDictionary&>(*pdfObject));
            break;
        case PdfObject::INDIRECT_REFERENCE:
            writeReference(static_cast<PdfIndirectReference&>(*pdfObject));
            break;
        case PdfObject::NAME:
            writeName(static_cast<PdfName&>(*pdfObject));
            break;
        case PdfObject::NULL_:
        case PdfObject::BOOLEAN:
            writeBytes(static_cast<PdfPrimitiveObject&>(*pdfObject).getInternalContent());
            break;
        case PdfObject::LITERAL: {
            auto& literal = static_cast<PdfLiteral&>(*pdfObject);
            literal.setPosition(getCurrentPos());
            writeBytes(literal.getInternalContent());
            break;
        }
        case PdfObject::STRING:
            writePdfString(static_cast<PdfString&>(*pdfObject));
            break;
        case PdfObject::NUMBER:
            writeNumber(static_cast<PdfNumber&>(*pdfObject));
            break;
        case PdfObject::STREAM:
            writeStream(static_cast<PdfStream&>(*pdfObject));
            break;
    }
    return *this;
}

void PdfOutputStream::writeArray(PdfArray& pdfArray) {
    writeByte('[');
    for (int i = 0; i < pdfArray.size(); i++) {
        PdfObjectPtr value = pdfArray.get(i, false);
        if (PdfIndirectReference* ref = value->getIndirectReference()) writeReference(*ref);
        else write(*value);
        if (i < pdfArray.size() - 1) writeSpace();
    }
    writeByte(']');
}

void PdfOutputStream::writeDictionary(PdfDictionary& pdfDictionary) {
    writeByte('<');
    writeByte('<');
    // copy entries: writing may make nested objects indirect but never changes the key set
    std::vector<std::pair<PdfNamePtr, PdfObjectPtr>> entries(pdfDictionary.entrySet().begin(), pdfDictionary.entrySet().end());
    for (auto& entry : entries) {
        bool spaceWritten = false;
        writeName(*entry.first);
        PdfObjectPtr value = entry.second;
        if (!value) value = PdfNull::PDF_NULL;
        uint8_t t = value->getType();
        if (t == PdfObject::NUMBER || t == PdfObject::LITERAL || t == PdfObject::BOOLEAN || t == PdfObject::NULL_ ||
            t == PdfObject::INDIRECT_REFERENCE || value->checkState(PdfObject::MUST_BE_INDIRECT)) {
            spaceWritten = true;
            writeSpace();
        }
        if (PdfIndirectReference* ref = value->getIndirectReference()) {
            if (!spaceWritten) writeSpace();
            writeReference(*ref);
        } else {
            write(*value);
        }
    }
    writeByte('>');
    writeByte('>');
}

void PdfOutputStream::writeReference(PdfIndirectReference& ref) {
    if (document_ != nullptr && ref.getDocument() != document_)
        throw PdfException(KernelExceptionMessageConstant::PDF_INDIRECT_OBJECT_BELONGS_TO_OTHER_PDF_DOCUMENT);
    if (ref.isFree()) {
        writeBytes(PdfNull::PDF_NULL->getInternalContent());
    } else if (ref.refersToRaw() == nullptr &&
               (ref.checkState(PdfObject::MODIFIED) || ref.getReader() == nullptr ||
                !(ref.getOffset() > 0 || ref.getIndex() >= 0))) {
        writeBytes(PdfNull::PDF_NULL->getInternalContent());
    } else if (ref.getGenNumber() == 0) {
        writeInteger(ref.getObjNumber());
        writeString(" 0 R");
    } else {
        writeInteger(ref.getObjNumber()).writeSpace().writeInteger(ref.getGenNumber());
        writeString(" R");
    }
}

void PdfOutputStream::writePdfString(PdfString& pdfString) {
    pdfString.encrypt(crypto_);
    if (pdfString.isHexWriting()) {
        writeByte('<');
        writeBytes(pdfString.getInternalContent());
        writeByte('>');
    } else {
        writeByte('(');
        writeBytes(pdfString.getInternalContent());
        writeByte(')');
    }
}

void PdfOutputStream::writeName(PdfName& name) {
    writeByte('/');
    writeBytes(name.getInternalContent());
}

void PdfOutputStream::writeNumber(PdfNumber& pdfNumber) {
    if (pdfNumber.hasContent()) writeBytes(pdfNumber.getInternalContent());
    else if (pdfNumber.isDoubleNumber()) writeDouble(pdfNumber.getValue());
    else writeInteger(pdfNumber.intValue());
}

bool PdfOutputStream::checkEncryption(PdfStream& pdfStream) {
    if (crypto_ == nullptr || crypto_->isEmbeddedFilesOnly()) return false;
    if (nameEquals(pdfStream.getAsName(PdfName::Type), PdfName::XRef)) return false;
    PdfObjectPtr filter = pdfStream.get(PdfName::Filter, true);
    if (!filter) return true;
    if (nameEquals(filter, PdfName::Crypt)) return false;
    if (filter->getType() == PdfObject::ARRAY) {
        auto filters = std::static_pointer_cast<PdfArray>(filter);
        if (filters->isEmpty()) return true;
        return !nameEquals(filters->get(0, true), PdfName::Crypt);
    }
    return true;
}

bool PdfOutputStream::containsFlateFilter(PdfStream& pdfStream) {
    PdfNamePtr compressionFilter = getCompressionStrategy().getFilterName();
    PdfObjectPtr filter = pdfStream.get(PdfName::Filter);
    if (!filter) return false;
    if (filter->isFlushed()) return true;
    if (filter->getType() != PdfObject::NAME && filter->getType() != PdfObject::ARRAY)
        throw PdfException(KernelExceptionMessageConstant::FILTER_IS_NOT_A_NAME_OR_ARRAY);
    if (filter->getType() == PdfObject::NAME) return nameEquals(filter, compressionFilter);
    for (const auto& obj : *std::static_pointer_cast<PdfArray>(filter))
        if (obj->isFlushed()) return true;
    return std::static_pointer_cast<PdfArray>(filter)->contains(compressionFilter);
}

void PdfOutputStream::updateCompressionFilter(PdfStream& pdfStream) {
    PdfObjectPtr existingFilter = pdfStream.get(PdfName::Filter);
    PdfNamePtr newFilter = getCompressionStrategy().getFilterName();
    if (!existingFilter) {
        existingFilter = newFilter;
        pdfStream.remove(PdfName::DecodeParms);
    } else if (existingFilter->getType() == PdfObject::ARRAY) {
        std::static_pointer_cast<PdfArray>(existingFilter)->add(0, newFilter);
    } else if (existingFilter->getType() == PdfObject::NAME) {
        auto filterArray = std::make_shared<PdfArray>();
        filterArray->add(newFilter);
        filterArray->add(existingFilter);
        existingFilter = filterArray;
    } else {
        throw PdfException(KernelExceptionMessageConstant::FILTER_IS_NOT_A_NAME_OR_ARRAY);
    }
    pdfStream.put(PdfName::Filter, existingFilter);
    PdfObjectPtr existingDecodeParms = pdfStream.get(PdfName::DecodeParms);
    PdfObjectPtr newDecodeParams = getCompressionStrategy().getDecodeParams();
    if (!newDecodeParams) newDecodeParams = std::make_shared<PdfNull>();
    if (!existingDecodeParms) {
        if (!newDecodeParams->isNull()) pdfStream.put(PdfName::DecodeParms, newDecodeParams);
    } else if (existingDecodeParms->getType() == PdfObject::DICTIONARY) {
        auto array = std::make_shared<PdfArray>();
        array->add(newDecodeParams);
        array->add(existingDecodeParms);
        pdfStream.put(PdfName::DecodeParms, array);
    } else if (existingDecodeParms->getType() == PdfObject::ARRAY) {
        std::static_pointer_cast<PdfArray>(existingDecodeParms)->add(0, newDecodeParams);
    } else {
        throw PdfException(format(KernelExceptionMessageConstant::THIS_DECODE_PARAMETER_TYPE_IS_NOT_SUPPORTED, "?"));
    }
}

Bytes PdfOutputStream::decodeFlateBytes(PdfStream& stream, const Bytes& input) {
    PdfObjectPtr filterObject = stream.get(PdfName::Filter);
    if (!filterObject) return input;
    PdfNamePtr filterName;
    PdfArrayPtr filtersArray;
    if (filterObject->getType() == PdfObject::NAME) {
        filterName = std::static_pointer_cast<PdfName>(filterObject);
    } else if (filterObject->getType() == PdfObject::ARRAY) {
        filtersArray = std::static_pointer_cast<PdfArray>(filterObject);
        filterName = filtersArray->getAsName(0);
    } else {
        throw PdfException(KernelExceptionMessageConstant::FILTER_IS_NOT_A_NAME_OR_ARRAY);
    }
    if (!filterName || !(*filterName == *PdfName::FlateDecode)) return input;
    PdfDictionaryPtr decodeParams;
    PdfArrayPtr decodeParamsArray;
    PdfObjectPtr decodeParamsObject = stream.get(PdfName::DecodeParms);
    if (decodeParamsObject) {
        if (decodeParamsObject->getType() == PdfObject::DICTIONARY) {
            decodeParams = std::static_pointer_cast<PdfDictionary>(decodeParamsObject);
        } else if (decodeParamsObject->getType() == PdfObject::ARRAY) {
            decodeParamsArray = std::static_pointer_cast<PdfArray>(decodeParamsObject);
            decodeParams = decodeParamsArray->getAsDictionary(0);
        }
    }
    Bytes res = FlateDecodeFilter::flateDecode(input, true);
    if (res.empty() && !input.empty()) res = FlateDecodeFilter::flateDecode(input, false);
    Bytes bytes = FlateDecodeFilter::decodePredictor(res, decodeParams.get());
    filterObject = nullptr;
    if (filtersArray) {
        filtersArray->remove(0);
        if (filtersArray->size() == 1) filterObject = filtersArray->get(0);
        else if (!filtersArray->isEmpty()) filterObject = filtersArray;
    }
    decodeParamsObject = nullptr;
    if (decodeParamsArray) {
        decodeParamsArray->remove(0);
        if (decodeParamsArray->size() == 1 && decodeParamsArray->get(0)->getType() != PdfObject::NULL_)
            decodeParamsObject = decodeParamsArray->get(0);
        else if (!decodeParamsArray->isEmpty())
            decodeParamsObject = decodeParamsArray;
    }
    if (!filterObject) stream.remove(PdfName::Filter);
    else stream.put(PdfName::Filter, filterObject);
    if (!decodeParamsObject) stream.remove(PdfName::DecodeParms);
    else stream.put(PdfName::DecodeParms, decodeParamsObject);
    return bytes;
}

void PdfOutputStream::writeStream(PdfStream& pdfStream) {
    bool userDefinedCompression = pdfStream.getCompressionLevel() != CompressionConstants::UNDEFINED_COMPRESSION;
    if (!userDefinedCompression) {
        int defaultLevel = document_ != nullptr && document_->getWriter() != nullptr
                               ? document_->getWriter()->getCompressionLevel()
                               : CompressionConstants::DEFAULT_COMPRESSION;
        pdfStream.setCompressionLevel(defaultLevel);
    }
    bool toCompress = pdfStream.getCompressionLevel() != CompressionConstants::NO_COMPRESSION;
    auto typeName = pdfStream.getAsName(PdfName::Type);
    bool notMetadata = !typeName || !(*typeName == *PdfName::Metadata);
    bool allowCompression = !pdfStream.containsKey(PdfName::Filter) && notMetadata;

    if (pdfStream.getOutputStream() == nullptr && pdfStream.getIndirectReference() != nullptr &&
        pdfStream.getIndirectReference()->getReader() != nullptr) {
        Bytes bytes = pdfStream.getIndirectReference()->getReader()->readStreamBytes(pdfStream, false);
        if (userDefinedCompression) bytes = decodeFlateBytes(pdfStream, bytes);
        pdfStream.initOutputStream();
        pdfStream.getOutputStream()->writeBytes(bytes);
    }
    if (pdfStream.getOutputStream() == nullptr) pdfStream.initOutputStream();

    auto* objectStream = dynamic_cast<PdfObjectStream*>(&pdfStream);
    auto rawBytes = [&]() {
        Bytes all;
        if (objectStream) {
            const Bytes& idx = objectStream->getIndexBytes()->getBuffer();
            all.insert(all.end(), idx.begin(), idx.end());
        }
        const Bytes& data = pdfStream.getByteArrayOutputStream()->getBuffer();
        all.insert(all.end(), data.begin(), data.end());
        return all;
    };

    Bytes result;
    PdfArrayPtr decodeParams = pdfStream.getAsArray(PdfName::DecodeParms);
    bool decodeParamsNotFlushed = !decodeParams || !decodeParams->isFlushed();
    if (toCompress && !containsFlateFilter(pdfStream) && decodeParamsNotFlushed && (allowCompression || userDefinedCompression)) {
        updateCompressionFilter(pdfStream);
        auto baos = std::make_shared<io::ByteArrayOutputStream>();
        auto zip = getCompressionStrategy().createNewOutputStream(baos, pdfStream);
        zip->write(rawBytes());
        if (auto* fin = dynamic_cast<io::IFinishable*>(zip.get())) fin->finish();
        result = std::move(baos->getBuffer());
    } else {
        result = rawBytes();
    }
    if (checkEncryption(pdfStream)) {
        auto encoded = std::make_shared<io::ByteArrayOutputStream>();
        auto ose = crypto_->getEncryptionStream(encoded);
        ose->write(result);
        dynamic_cast<io::IFinishable*>(ose.get())->finish();
        result = std::move(encoded->getBuffer());
    }
    pdfStream.put(PdfName::Length, makeNumber(static_cast<int>(result.size())));
    pdfStream.updateLength(static_cast<int>(result.size()));
    writeDictionary(pdfStream);
    writeString("stream\n");
    writeBytes(result);
    writeString("\nendstream");
}

}  // namespace kernel
}  // namespace pdfcpp
