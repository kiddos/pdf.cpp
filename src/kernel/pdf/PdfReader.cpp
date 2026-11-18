#include "pdfcpp/kernel/pdf/PdfReader.h"

#include <cstring>
#include <set>

#include "pdfcpp/io/source/RandomAccessSource.h"
#include "pdfcpp/kernel/crypto/PdfEncryption.h"
#include "pdfcpp/kernel/pdf/PdfDocument.h"
#include "pdfcpp/kernel/pdf/PdfXrefTable.h"
#include "pdfcpp/kernel/pdf/filters/Filters.h"

namespace pdfcpp {
namespace kernel {

namespace {
const Bytes ENDSTREAM = toBytes("endstream");
const Bytes ENDOBJ = toBytes("endobj");

/// A source over a ByteBuffer that can be reused (port of ReusableRandomAccessSource).
class ReusableRandomAccessSource : public io::IRandomAccessSource {
public:
    explicit ReusableRandomAccessSource(io::ByteBuffer* buffer) : buffer_(buffer) {}
    int get(int64_t offset) override {
        if (offset >= buffer_->size()) return -1;
        return buffer_->getInternalBuffer()[offset];
    }
    int get(int64_t offset, uint8_t* bytes, int off, int len) override {
        if (offset >= buffer_->size()) return -1;
        if (offset + len > buffer_->size()) len = static_cast<int>(buffer_->size() - offset);
        std::memcpy(bytes + off, buffer_->getInternalBuffer() + offset, len);
        return len;
    }
    int64_t length() override { return buffer_->size(); }
    void close() override {}

private:
    io::ByteBuffer* buffer_;
};
}  // namespace

PdfReader::PdfReader(const std::string& filename, const ReaderProperties& properties) : properties_(properties) {
    initTokenizer(io::RandomAccessSourceFactory().createBestSource(filename));
}

PdfReader::PdfReader(io::RandomAccessSourcePtr byteSource, const ReaderProperties& properties) : properties_(properties) {
    initTokenizer(std::move(byteSource));
}

std::unique_ptr<PdfReader> PdfReader::fromBytes(const Bytes& bytes, const ReaderProperties& properties) {
    return std::make_unique<PdfReader>(io::RandomAccessSourceFactory().createSource(bytes), properties);
}

PdfReader::~PdfReader() = default;

void PdfReader::initTokenizer(io::RandomAccessSourcePtr byteSource) {
    auto tok = std::make_unique<io::PdfTokenizer>(byteSource);
    int offset = tok->getHeaderOffset();
    if (offset != 0) {
        auto offsetSource = std::make_shared<io::WindowRandomAccessSource>(byteSource, offset);
        tok = std::make_unique<io::PdfTokenizer>(offsetSource);
    }
    tokens_ = std::move(tok);
}

void PdfReader::close() { tokens_->close(); }

bool PdfReader::isOpenedWithFullPermission() const {
    return !encrypted_ || decrypt_->isOpenedWithFullPermission() || unethicalReading_;
}

int PdfReader::getPermissions() const {
    if (encrypted_ && decrypt_->getPermissions()) return static_cast<int>(*decrypt_->getPermissions());
    return 0;
}

int PdfReader::getCryptoMode() const { return decrypt_ ? decrypt_->getCryptoMode() : -1; }

Bytes PdfReader::getOriginalFileId() const {
    auto id = trailer_ ? trailer_->getAsArray(PdfName::ID) : nullptr;
    if (id && id->size() == 2 && id->getAsString(0)) return id->getAsString(0)->getValueBytes();
    return Bytes();
}

Bytes PdfReader::getModifiedFileId() const {
    auto id = trailer_ ? trailer_->getAsArray(PdfName::ID) : nullptr;
    if (id && id->size() == 2 && id->getAsString(1)) return id->getAsString(1)->getValueBytes();
    return Bytes();
}

// ------------------------------------------------------------ streams
Bytes PdfReader::readStreamBytes(PdfStream& stream, bool decode) {
    Bytes b = readStreamBytesRaw(stream);
    if (decode && !b.empty()) return decodeBytes(b, stream);
    return b;
}

Bytes PdfReader::readStreamBytesRaw(PdfStream& stream) {
    auto type = stream.getAsName(PdfName::Type);
    if (!nameEquals(type, PdfName::XRef) && !nameEquals(type, PdfName::ObjStm)) checkPdfStreamLength(stream);
    int64_t offset = stream.getOffset();
    if (offset <= 0) return Bytes();
    int length = stream.getLength();
    if (length <= 0) return Bytes();
    auto file = tokens_->getSafeFile();
    file->seek(offset);
    Bytes bytes(length);
    file->readFully(bytes);
    if (decrypt_ && !decrypt_->isEmbeddedFilesOnly()) {
        PdfObjectPtr filter = stream.get(PdfName::Filter, true);
        bool skip = false;
        if (filter) {
            if (nameEquals(filter, PdfName::Crypt)) {
                skip = true;
            } else if (filter->getType() == PdfObject::ARRAY) {
                auto filters = std::static_pointer_cast<PdfArray>(filter);
                for (int k = 0; k < filters->size(); k++) {
                    if (nameEquals(filters->get(k, true), PdfName::Crypt)) {
                        skip = true;
                        break;
                    }
                }
            }
        }
        if (!skip && !decrypt_->isMetadataEncrypted() && nameEquals(type, PdfName::Metadata)) skip = true;
        if (!skip && stream.getIndirectReference()) {
            decrypt_->setHashKeyForNextObject(stream.getIndirectReference()->getObjNumber(),
                                              stream.getIndirectReference()->getGenNumber());
            bytes = decrypt_->decryptByteArray(bytes);
        }
    }
    return bytes;
}

Bytes PdfReader::decodeBytes(const Bytes& b, const PdfDictionary& streamDictionary) {
    return decodeBytes(b, streamDictionary, FilterHandlers::getDefaultFilterHandlers());
}

Bytes PdfReader::decodeBytes(const Bytes& input, const PdfDictionary& streamDictionary, const FilterHandlerMap& filterHandlers) {
    Bytes b = input;
    PdfObjectPtr filter = streamDictionary.get(PdfName::Filter);
    std::vector<PdfObjectPtr> filters;
    if (filter) {
        if (filter->getType() == PdfObject::NAME) filters.push_back(filter);
        else if (filter->getType() == PdfObject::ARRAY) {
            auto arr = std::static_pointer_cast<PdfArray>(filter);
            for (int i = 0; i < arr->size(); ++i) filters.push_back(arr->get(i));
        }
    }
    std::vector<PdfObjectPtr> dp;
    PdfObjectPtr dpo = streamDictionary.get(PdfName::DecodeParms);
    if (!dpo || (dpo->getType() != PdfObject::DICTIONARY && dpo->getType() != PdfObject::ARRAY))
        dpo = streamDictionary.get(PdfName::DP);
    if (dpo) {
        if (dpo->getType() == PdfObject::DICTIONARY) dp.push_back(dpo);
        else if (dpo->getType() == PdfObject::ARRAY) {
            auto arr = std::static_pointer_cast<PdfArray>(dpo);
            for (int i = 0; i < arr->size(); ++i) dp.push_back(arr->get(i, true));
        }
    }
    for (size_t j = 0; j < filters.size(); ++j) {
        auto filterName = pdf_cast<PdfName>(filters[j]);
        if (!filterName) throw PdfException(format(KernelExceptionMessageConstant::FILTER_IS_NOT_SUPPORTED, "?"));
        auto it = filterHandlers.find(filterName->getValue());
        if (it == filterHandlers.end())
            throw PdfException(format(KernelExceptionMessageConstant::FILTER_IS_NOT_SUPPORTED, filterName->getValue()));
        const PdfDictionary* decodeParams = nullptr;
        if (j < dp.size()) {
            const PdfObjectPtr& entry = dp[j];
            if (!entry || entry->getType() == PdfObject::NULL_) decodeParams = nullptr;
            else if (entry->getType() == PdfObject::DICTIONARY) decodeParams = static_cast<const PdfDictionary*>(entry.get());
            else throw PdfException(format(KernelExceptionMessageConstant::THIS_DECODE_PARAMETER_TYPE_IS_NOT_SUPPORTED, entry->toString()));
        }
        b = it->second->decode(b, *filterName, decodeParams, streamDictionary);
    }
    return b;
}

// ------------------------------------------------------------ reading
void PdfReader::readPdf() {
    std::string version = tokens_->checkPdfHeader();
    try {
        headerPdfVersion_ = PdfVersion::fromString(version);
    } catch (...) {
        throw PdfException("PDF version is not valid: " + version);
    }
    try {
        readXref();
    } catch (const XrefCycledReferencesException&) {
        throw;
    } catch (const MemoryLimitsAwareException&) {
        throw;
    } catch (const std::exception&) {
        if (isStricter(StrictnessLevel::CONSERVATIVE, strictnessLevel_)) rebuildXref();
        else throw;
    }
    pdfDocument_->getXref()->markReadingCompleted();
    readDecryptObj();
}

void PdfReader::readDecryptObj() {
    if (encrypted_) return;
    auto enc = trailer_->getAsDictionary(PdfName::Encrypt);
    if (!enc) return;
    encrypted_ = true;
    auto filter = enc->getAsName(PdfName::Filter);
    if (nameEquals(filter, PdfName::Standard)) {
        decrypt_ = std::make_unique<PdfEncryption>(enc, properties_.password, getOriginalFileId());
    } else {
        throw PdfException("Unsupported security handler: " + (filter ? filter->getValue() : std::string("null")));
    }
}

void PdfReader::readObjectStream(PdfStream& objectStream) {
    int objectStreamNumber = objectStream.getIndirectReference()->getObjNumber();
    auto firstNum = objectStream.getAsNumber(PdfName::First);
    auto nNum = objectStream.getAsNumber(PdfName::N);
    if (!firstNum || !nNum) throw PdfException("Error while reading object stream.");
    int first = firstNum->intValue();
    int n = nNum->intValue();
    Bytes bytes = readStreamBytes(objectStream, true);
    auto saveTokens = std::move(tokens_);
    tokens_ = std::make_unique<io::PdfTokenizer>(io::RandomAccessSourceFactory().createSource(bytes));
    try {
        std::vector<int> address(n), objNumber(n);
        bool ok = true;
        for (int k = 0; k < n; ++k) {
            ok = tokens_->nextToken();
            if (!ok) break;
            if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number) {
                ok = false;
                break;
            }
            objNumber[k] = tokens_->getIntValue();
            ok = tokens_->nextToken();
            if (!ok) break;
            if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number) {
                ok = false;
                break;
            }
            address[k] = tokens_->getIntValue() + first;
        }
        if (!ok) throw PdfException("Error while reading object stream.");
        for (int k = 0; k < n; ++k) {
            tokens_->seek(address[k]);
            tokens_->nextToken();
            PdfIndirectReference* reference = pdfDocument_->getXref()->get(objNumber[k]);
            if (reference == nullptr || reference->refersToRaw() != nullptr ||
                reference->getObjStreamNumber() != objectStreamNumber)
                continue;
            PdfObjectPtr obj;
            if (tokens_->getTokenType() == io::PdfTokenizer::TokenType::Number) {
                obj = std::make_shared<PdfNumber>(tokens_->getByteContent());
            } else {
                tokens_->seek(address[k]);
                obj = readObject(false, true);
            }
            if (!obj) continue;
            reference->setRefersTo(obj);
            obj->setIndirectReference(reference);
        }
        objectStream.getIndirectReference()->setState(PdfObject::ORIGINAL_OBJECT_STREAM);
    } catch (...) {
        tokens_ = std::move(saveTokens);
        throw;
    }
    tokens_ = std::move(saveTokens);
}

PdfObjectPtr PdfReader::readObject(PdfIndirectReference* reference) { return readObject(reference, true); }

PdfObjectPtr PdfReader::readObject(PdfIndirectReference* reference, bool fixXrefFlag) {
    if (reference == nullptr) return nullptr;
    if (reference->refersToRaw() != nullptr) return reference->refersToRaw();
    currentIndirectReference_ = reference;
    if (reference->getObjStreamNumber() > 0) {
        PdfIndirectReference* osRef = pdfDocument_->getXref()->get(reference->getObjStreamNumber());
        PdfObjectPtr os = osRef ? osRef->getRefersToPtr(false) : nullptr;
        auto objectStream = pdf_cast<PdfStream>(os);
        if (!objectStream)
            throw PdfException(format(KernelExceptionMessageConstant::INVALID_OBJECT_STREAM_NUMBER, reference->getObjNumber(),
                                      reference->getObjStreamNumber(), reference->getIndex()));
        readObjectStream(*objectStream);
        return reference->refersToRaw();
    } else if (reference->getOffset() > 0) {
        PdfObjectPtr object;
        try {
            tokens_->seek(reference->getOffset());
            tokens_->nextValidToken();
            if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Obj ||
                tokens_->getObjNr() != reference->getObjNumber() || tokens_->getGenNr() != reference->getGenNumber())
                tokens_->throwError("Invalid offset for object " + reference->toString() + ".");
            object = readObject(false);
        } catch (const std::exception&) {
            if (fixXrefFlag && reference->getObjStreamNumber() == 0) {
                fixXref();
                return readObject(reference, false);
            }
            throw;
        }
        if (object) object->setIndirectReference(reference);
        return object;
    }
    return nullptr;
}

PdfObjectPtr PdfReader::readObject(bool readAsDirect, bool objStm) {
    tokens_->nextValidToken();
    using TT = io::PdfTokenizer::TokenType;
    TT type = tokens_->getTokenType();
    switch (type) {
        case TT::StartDic: {
            PdfDictionaryPtr dict = readDictionary(objStm);
            int64_t pos = tokens_->getPosition();
            bool hasNext;
            do {
                hasNext = tokens_->nextToken();
            } while (hasNext && tokens_->getTokenType() == TT::Comment);
            if (hasNext && tokens_->tokenValueEqualsTo(io::PdfTokenizer::Stream)) {
                int ch;
                do {
                    ch = tokens_->read();
                } while (ch == 32 || ch == 9 || ch == 0 || ch == 12);
                if (ch != '\n') ch = tokens_->read();
                if (ch != '\n') tokens_->backOnePosition(ch);
                auto pdfStream = std::make_shared<PdfStream>(tokens_->getPosition(), *dict);
                tokens_->seek(pdfStream->getOffset() + pdfStream->getLength());
                return pdfStream;
            }
            tokens_->seek(pos);
            return dict;
        }
        case TT::StartArray:
            return readArray(objStm);
        case TT::Number:
            return std::make_shared<PdfNumber>(tokens_->getByteContent());
        case TT::String: {
            auto pdfString = std::make_shared<PdfString>(tokens_->getByteContent(), tokens_->isHexString());
            if (encrypted_ && decrypt_ && !decrypt_->isEmbeddedFilesOnly() && !objStm && currentIndirectReference_)
                pdfString->setDecryption(currentIndirectReference_->getObjNumber(),
                                         currentIndirectReference_->getGenNumber(), decrypt_.get());
            return pdfString;
        }
        case TT::Name:
            return readPdfName(readAsDirect);
        case TT::Ref:
            return readReference(readAsDirect);
        case TT::EndOfFile:
            throw PdfException(KernelExceptionMessageConstant::UNEXPECTED_END_OF_FILE);
        default:
            if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::Null)) return createPdfNullInstance(readAsDirect);
            if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::True))
                return readAsDirect ? PdfObjectPtr(PdfBoolean::TRUE_VALUE) : std::make_shared<PdfBoolean>(true);
            if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::False))
                return readAsDirect ? PdfObjectPtr(PdfBoolean::FALSE_VALUE) : std::make_shared<PdfBoolean>(false);
            return nullptr;
    }
}

PdfObjectPtr PdfReader::readReference(bool readAsDirect) {
    int num = tokens_->getObjNr();
    if (num < 0) return createPdfNullInstance(readAsDirect);
    PdfXrefTable* table = pdfDocument_->getXref();
    PdfIndirectReferencePtr reference = table->getPtr(num);
    if (reference) {
        if (reference->isFree()) return createPdfNullInstance(readAsDirect);
        if (reference->getGenNumber() != tokens_->getGenNr()) {
            if (fixedXref_) return createPdfNullInstance(readAsDirect);
            throw PdfException(format(KernelExceptionMessageConstant::INVALID_INDIRECT_REFERENCE,
                                      std::to_string(reference->getObjNumber()) + " " + std::to_string(reference->getGenNumber())));
        }
    } else {
        if (table->isReadingCompleted()) return createPdfNullInstance(readAsDirect);
        if (static_cast<int64_t>(num) > std::max<int64_t>(1 << 20, tokens_->length() * 4)) return createPdfNullInstance(readAsDirect);
        reference = std::make_shared<PdfIndirectReference>(pdfDocument_, num, tokens_->getGenNr(), 0);
        reference->setState(PdfObject::READING);
        table->add(reference);
    }
    return reference;
}

PdfNamePtr PdfReader::readPdfName(bool readAsDirect) {
    if (readAsDirect) {
        std::string raw = tokens_->getStringValue();
        if (raw.find('#') == std::string::npos) return PdfName::of(raw);
    }
    return std::make_shared<PdfName>(tokens_->getByteContent());
}

PdfDictionaryPtr PdfReader::readDictionary(bool objStm) {
    auto dic = std::make_shared<PdfDictionary>();
    using TT = io::PdfTokenizer::TokenType;
    while (true) {
        tokens_->nextValidToken();
        if (tokens_->getTokenType() == TT::EndDic) break;
        if (tokens_->getTokenType() != TT::Name)
            tokens_->throwError(format(KernelExceptionMessageConstant::DICTIONARY_KEY_IS_NOT_A_NAME, tokens_->getStringValue()));
        PdfNamePtr name = readPdfName(true);
        PdfObjectPtr obj = readObject(true, objStm);
        if (!obj) {
            if (tokens_->getTokenType() == TT::EndDic) tokens_->throwError(format(KernelExceptionMessageConstant::UNEXPECTED_TOKEN, ">>"));
            if (tokens_->getTokenType() == TT::EndArray) tokens_->throwError(format(KernelExceptionMessageConstant::UNEXPECTED_TOKEN, "]"));
        }
        dic->put(name, obj ? obj : PdfObjectPtr(PdfNull::PDF_NULL));
    }
    return dic;
}

PdfArrayPtr PdfReader::readArray(bool objStm) {
    auto array = std::make_shared<PdfArray>();
    while (true) {
        PdfObjectPtr obj = readObject(true, objStm);
        if (!obj) {
            if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::EndArray) {
                if (!isStricter(StrictnessLevel::CONSERVATIVE, strictnessLevel_))
                    tokens_->throwError(format(KernelExceptionMessageConstant::UNEXPECTED_TOKEN, tokens_->getStringValue()));
            }
            break;
        }
        array->add(obj);
    }
    return array;
}

void PdfReader::readXref() {
    tokens_->seek(tokens_->getStartxref());
    tokens_->nextToken();
    if (!tokens_->tokenValueEqualsTo(io::PdfTokenizer::Startxref)) throw PdfException("PDF startxref not found.");
    tokens_->nextToken();
    if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number)
        throw PdfException("PDF startxref is not followed by a number.");
    int64_t startxref = tokens_->getLongValue();
    lastXref_ = startxref;
    eofPos_ = tokens_->getPosition();
    try {
        if (readXrefStream(startxref)) {
            xrefStm_ = true;
            return;
        }
    } catch (const XrefCycledReferencesException&) {
        throw;
    } catch (const MemoryLimitsAwareException&) {
        throw;
    } catch (const std::exception&) {
    }
    pdfDocument_->getXref()->clear();
    tokens_->seek(startxref);
    trailer_ = readXrefSection();
    PdfDictionaryPtr trailer2 = trailer_;
    std::set<int64_t> visited;
    while (true) {
        visited.insert(startxref);
        PdfNumberPtr prev = getXrefPrev(trailer2->get(PdfName::Prev, false));
        if (!prev) break;
        int64_t prevOffset = prev->longValue();
        if (visited.count(prevOffset)) throw XrefCycledReferencesException("Xref table has cycled references.");
        startxref = prevOffset;
        tokens_->seek(startxref);
        trailer2 = readXrefSection();
    }
    if (!trailer_->getAsInt(PdfName::Size)) throw PdfException("Invalid xref table.");
}

PdfDictionaryPtr PdfReader::readXrefSection() {
    tokens_->nextValidToken();
    if (!tokens_->tokenValueEqualsTo(io::PdfTokenizer::Xref)) tokens_->throwError("xref subsection not found.");
    PdfXrefTable* xref = pdfDocument_->getXref();
    while (true) {
        tokens_->nextValidToken();
        if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::Trailer)) break;
        if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number)
            tokens_->throwError("Object number of the first object in this xref subsection not found.");
        int start = tokens_->getIntValue();
        tokens_->nextValidToken();
        if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number)
            tokens_->throwError("Number of entries in this xref subsection not found.");
        int end = tokens_->getIntValue() + start;
        if (start < 0 || end < start || static_cast<int64_t>(end) > std::max<int64_t>(1 << 20, tokens_->length() * 4))
            throw MemoryLimitsAwareException("Xref structure size exceeds the limit.");
        for (int num = start; num < end; num++) {
            tokens_->nextValidToken();
            int64_t pos = tokens_->getLongValue();
            tokens_->nextValidToken();
            int gen = tokens_->getIntValue();
            tokens_->nextValidToken();
            if (pos == 0 && gen == 65535 && num == 1 && start != 0) {
                num = 0;
                end--;
                continue;
            }
            PdfIndirectReference* reference = xref->get(num);
            bool refReadingState = reference != nullptr && reference->checkState(PdfObject::READING) && reference->getGenNumber() == gen;
            bool refFirstEncountered = reference == nullptr || (!refReadingState && reference->getDocument() == nullptr);
            PdfIndirectReferencePtr newRef;
            if (refFirstEncountered) {
                newRef = std::make_shared<PdfIndirectReference>(pdfDocument_, num, gen, pos);
                reference = newRef.get();
            } else if (refReadingState) {
                reference->setOffset(pos);
                reference->clearState(PdfObject::READING);
            } else {
                continue;
            }
            if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::N)) {
                if (pos == 0) tokens_->throwError("File position 0 cross-reference entry in this xref subsection.");
            } else if (tokens_->tokenValueEqualsTo(io::PdfTokenizer::F)) {
                if (refFirstEncountered) reference->setState(PdfObject::FREE);
            } else {
                tokens_->throwError("Invalid cross reference entry in this xref subsection.");
            }
            if (refFirstEncountered) xref->add(newRef);
        }
    }
    auto trailer = pdf_cast<PdfDictionary>(readObject(false));
    if (!trailer) throw PdfException(KernelExceptionMessageConstant::TRAILER_NOT_FOUND);
    PdfObjectPtr xrs = trailer->get(PdfName::XRefStm);
    if (xrs && xrs->getType() == PdfObject::NUMBER) {
        int loc = std::static_pointer_cast<PdfNumber>(xrs)->intValue();
        try {
            readXrefStream(loc);
            xrefStm_ = true;
            hybridXref_ = true;
        } catch (...) {
            xref->clear();
            throw;
        }
    }
    return trailer;
}

bool PdfReader::readXrefStream(int64_t ptr) {
    std::set<int64_t> visited;
    while (ptr != -1) {
        tokens_->seek(ptr);
        if (!tokens_->nextToken()) return false;
        if (tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number) return false;
        if (!tokens_->nextToken() || tokens_->getTokenType() != io::PdfTokenizer::TokenType::Number) return false;
        if (!tokens_->nextToken() || !tokens_->tokenValueEqualsTo(io::PdfTokenizer::Obj)) return false;
        visited.insert(ptr);
        PdfXrefTable* xref = pdfDocument_->getXref();
        PdfObjectPtr object = readObject(false);
        auto xrefStream = pdf_cast<PdfStream>(object);
        if (!xrefStream || !nameEquals(xrefStream->get(PdfName::Type), PdfName::XRef)) return false;
        if (!trailer_) {
            trailer_ = std::make_shared<PdfDictionary>();
            trailer_->putAll(*xrefStream);
            trailer_->remove(PdfName::DecodeParms);
            trailer_->remove(PdfName::Filter);
            trailer_->remove(PdfName::Prev);
            trailer_->remove(PdfName::Length);
        }
        auto sizeNum = pdf_cast<PdfNumber>(xrefStream->get(PdfName::Size));
        if (!sizeNum) throw PdfException(KernelExceptionMessageConstant::INVALID_XREF_STREAM);
        int size = sizeNum->intValue();
        PdfArrayPtr index;
        PdfObjectPtr obj = xrefStream->get(PdfName::Index);
        if (!obj) {
            index = std::make_shared<PdfArray>();
            index->add(makeNumber(0));
            index->add(makeNumber(size));
        } else {
            index = pdf_cast<PdfArray>(obj);
        }
        PdfArrayPtr w = xrefStream->getAsArray(PdfName::W);
        if (!w || !index) throw PdfException(KernelExceptionMessageConstant::INVALID_XREF_STREAM);
        int64_t prev = -1;
        auto prevNum = getXrefPrev(xrefStream->get(PdfName::Prev, false));
        if (prevNum) prev = prevNum->longValue();
        // Guard against absurd /Size values (port of MemoryLimitsAwareHandler checks).
        const int64_t maxElements = std::max<int64_t>(1 << 20, tokens_->length() * 4);
        if (size < 0 || size > maxElements) throw MemoryLimitsAwareException("Xref structure size exceeds the limit.");
        xref->setCapacity(size);
        // the xref stream object must be readable before references are resolved: read directly
        xrefStream->updateLength(xrefStream->getAsNumber(PdfName::Length) ? xrefStream->getAsNumber(PdfName::Length)->intValue() : 0);
        Bytes b;
        {
            auto file = tokens_->getSafeFile();
            file->seek(xrefStream->getOffset());
            b.resize(std::max(0, xrefStream->getLength()));
            file->readFully(b);
            b = decodeBytes(b, *xrefStream);
        }
        size_t bptr = 0;
        int wc[3];
        for (int k = 0; k < 3; ++k) wc[k] = w->getAsNumber(k) ? w->getAsNumber(k)->intValue() : 0;
        for (int idx = 0; idx + 1 < index->size(); idx += 2) {
            int start = index->getAsNumber(idx)->intValue();
            int length = index->getAsNumber(idx + 1)->intValue();
            if (start < 0 || length < 0 || static_cast<int64_t>(start) + length > maxElements)
                throw MemoryLimitsAwareException("Xref structure size exceeds the limit.");
            xref->setCapacity(start + length);
            while (length-- > 0) {
                if (bptr + wc[0] + wc[1] + wc[2] > b.size()) break;
                int type = 1;
                if (wc[0] > 0) {
                    type = 0;
                    for (int k = 0; k < wc[0]; ++k) type = (type << 8) + b[bptr++];
                }
                int64_t field2 = 0;
                for (int k = 0; k < wc[1]; ++k) field2 = (field2 << 8) + b[bptr++];
                int field3 = 0;
                for (int k = 0; k < wc[2]; ++k) field3 = (field3 << 8) + b[bptr++];
                int base = start;
                PdfIndirectReferencePtr newReference;
                switch (type) {
                    case 0:
                        newReference = std::make_shared<PdfIndirectReference>(pdfDocument_, base, field3, field2);
                        newReference->setState(PdfObject::FREE);
                        break;
                    case 1:
                        newReference = std::make_shared<PdfIndirectReference>(pdfDocument_, base, field3, field2);
                        break;
                    case 2:
                        newReference = std::make_shared<PdfIndirectReference>(pdfDocument_, base, 0, field3);
                        newReference->setObjStreamNumber(static_cast<int>(field2));
                        break;
                    default:
                        throw PdfException(KernelExceptionMessageConstant::INVALID_XREF_STREAM);
                }
                PdfIndirectReference* reference = xref->get(base);
                bool refReadingState = reference != nullptr && reference->checkState(PdfObject::READING) &&
                                       reference->getGenNumber() == newReference->getGenNumber();
                bool refFirstEncountered = reference == nullptr || (!refReadingState && reference->getDocument() == nullptr);
                if (refFirstEncountered) {
                    xref->add(newReference);
                } else if (refReadingState) {
                    if (newReference->getObjStreamNumber() != 0) {
                        reference->setIndex(newReference->getIndex());
                        reference->setObjStreamNumber(newReference->getObjStreamNumber());
                    } else {
                        reference->setOffset(newReference->getOffset());
                    }
                    reference->clearState(PdfObject::READING);
                }
                ++start;
            }
        }
        ptr = prev;
        if (visited.count(ptr)) throw XrefCycledReferencesException("Xref stream has cycled references.");
    }
    return true;
}

void PdfReader::fixXref() {
    fixedXref_ = true;
    PdfXrefTable* xref = pdfDocument_->getXref();
    tokens_->seek(0);
    io::ByteBuffer buffer(24);
    io::PdfTokenizer lineTokenizer(std::make_shared<ReusableRandomAccessSource>(&buffer));
    for (;;) {
        int64_t pos = tokens_->getPosition();
        buffer.reset();
        if (!tokens_->readLineSegment(buffer, true)) break;
        if (buffer.get(0) >= '0' && buffer.get(0) <= '9') {
            auto obj = io::PdfTokenizer::checkObjectStart(lineTokenizer);
            if (!obj) continue;
            PdfIndirectReference* reference = xref->get((*obj)[0]);
            if (reference != nullptr && reference->getGenNumber() == (*obj)[1]) reference->fixOffset(pos);
        }
    }
}

bool PdfReader::isCurrentObjectATrailer() {
    try {
        auto dic = pdf_cast<PdfDictionary>(readObject(false));
        return dic && dic->get(PdfName::Root, false) != nullptr;
    } catch (...) {
        return false;
    }
}

void PdfReader::rebuildXref() {
    xrefStm_ = false;
    hybridXref_ = false;
    rebuiltXref_ = true;
    PdfXrefTable* xref = pdfDocument_->getXref();
    xref->clear();
    tokens_->seek(0);
    trailer_ = nullptr;
    io::ByteBuffer buffer(24);
    io::PdfTokenizer lineTokenizer(std::make_shared<ReusableRandomAccessSource>(&buffer));
    std::optional<int64_t> trailerIndex;
    for (;;) {
        int64_t pos = tokens_->getPosition();
        buffer.reset();
        if (!tokens_->readLineSegment(buffer, true)) break;
        if (buffer.get(0) == 't') {
            if (!io::PdfTokenizer::checkTrailer(buffer)) continue;
            tokens_->seek(pos);
            tokens_->nextToken();
            pos = tokens_->getPosition();
            if (isCurrentObjectATrailer()) trailerIndex = pos;
            else tokens_->seek(pos);
        } else if (buffer.get(0) >= '0' && buffer.get(0) <= '9') {
            auto obj = io::PdfTokenizer::checkObjectStart(lineTokenizer);
            if (!obj) continue;
            int num = (*obj)[0], gen = (*obj)[1];
            if (xref->get(num) == nullptr || xref->get(num)->getGenNumber() <= gen)
                xref->add(std::make_shared<PdfIndirectReference>(pdfDocument_, num, gen, pos));
        }
    }
    if (!trailerIndex) {
        // Try to find a cross-reference stream acting as trailer
        for (int i = 1; i < xref->size(); ++i) {
            PdfIndirectReference* ref = xref->get(i);
            if (!ref) continue;
            try {
                auto s = pdf_cast<PdfStream>(ref->getRefersToPtr(false));
                if (s && nameEquals(s->getAsName(PdfName::Type), PdfName::XRef) && s->get(PdfName::Root, false)) {
                    trailer_ = std::make_shared<PdfDictionary>();
                    trailer_->putAll(*s);
                    trailer_->remove(PdfName::DecodeParms);
                    trailer_->remove(PdfName::Filter);
                    trailer_->remove(PdfName::Prev);
                    trailer_->remove(PdfName::Length);
                }
            } catch (...) {
            }
        }
        if (!trailer_) throw PdfException(KernelExceptionMessageConstant::TRAILER_NOT_FOUND);
        return;
    }
    tokens_->seek(*trailerIndex);
    auto dic = pdf_cast<PdfDictionary>(readObject(false));
    if (dic && dic->get(PdfName::Root, false)) trailer_ = dic;
    if (!trailer_) throw PdfException(KernelExceptionMessageConstant::TRAILER_NOT_FOUND);
}

PdfNumberPtr PdfReader::getXrefPrev(const PdfObjectPtr& prevObjectToCheck) {
    if (!prevObjectToCheck) return nullptr;
    if (prevObjectToCheck->getType() == PdfObject::NUMBER) return std::static_pointer_cast<PdfNumber>(prevObjectToCheck);
    if (prevObjectToCheck->getType() == PdfObject::INDIRECT_REFERENCE &&
        isStricter(StrictnessLevel::CONSERVATIVE, strictnessLevel_)) {
        auto value = static_cast<PdfIndirectReference*>(prevObjectToCheck.get())->getRefersToPtr(true);
        if (value && value->getType() == PdfObject::NUMBER) return std::static_pointer_cast<PdfNumber>(value);
    }
    throw PdfException("Prev pointer in xref structure shall be direct number object.");
}

void PdfReader::checkPdfStreamLength(PdfStream& pdfStream) {
    int64_t fileLength = tokens_->length();
    int64_t start = pdfStream.getOffset();
    bool calc = false;
    int streamLength = 0;
    PdfNumberPtr pdfNumber = pdfStream.getAsNumber(PdfName::Length);
    if (pdfNumber) {
        streamLength = pdfNumber->intValue();
        if (streamLength + start > fileLength - 20) {
            calc = true;
        } else {
            tokens_->seek(start + streamLength);
            std::string line = tokens_->readString(20);
            if (line.rfind("\nendstream", 0) != 0 && line.rfind("\r\nendstream", 0) != 0 &&
                line.rfind("\rendstream", 0) != 0 && line.rfind("endstream", 0) != 0)
                calc = true;
        }
    } else {
        pdfNumber = makeNumber(0);
        pdfStream.put(PdfName::Length, pdfNumber);
        calc = true;
    }
    if (calc) {
        io::ByteBuffer line(16);
        tokens_->seek(start);
        int64_t pos;
        while (true) {
            pos = tokens_->getPosition();
            line.reset();
            if (!tokens_->readLineSegment(line, false)) {
                if (!isStricter(StrictnessLevel::CONSERVATIVE, strictnessLevel_))
                    throw PdfException("Stream shall end with endstream keyword.");
                break;
            }
            if (line.startsWith(ENDSTREAM)) break;
            if (line.startsWith(ENDOBJ)) {
                tokens_->seek(pos - 16);
                std::string s = tokens_->readString(16);
                auto index = s.find("endstream");
                if (index != std::string::npos) pos = pos - 16 + static_cast<int64_t>(index);
                break;
            }
        }
        streamLength = static_cast<int>(pos - start);
        tokens_->seek(pos - 2);
        if (tokens_->read() == 13) streamLength--;
        tokens_->seek(pos - 1);
        if (tokens_->read() == 10) streamLength--;
        if (streamLength < 0) streamLength = 0;
        pdfNumber->setValue(streamLength);
        pdfStream.updateLength(streamLength);
    }
}

PdfObjectPtr PdfReader::createPdfNullInstance(bool readAsDirect) {
    if (readAsDirect) return PdfNull::PDF_NULL;
    return std::make_shared<PdfNull>();
}

}  // namespace kernel
}  // namespace pdfcpp
