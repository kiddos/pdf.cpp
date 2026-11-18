import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
names = re.findall(r'public static final PdfName (\w+)\s*=\s*(?:createDirectName|new PdfName)\("((?:[^"\\]|\\.)*)"\)', src)
decl = ['// Generated from PdfName.java by tools/gen_pdfnames.py. Do not edit.']
defs = ['// Generated from PdfName.java by tools/gen_pdfnames.py. Do not edit.',
        '#include "pdfcpp/kernel/pdf/PdfObject.h"', '', 'namespace pdfcpp {', 'namespace kernel {', '']
for ident, val in names:
    decl.append('static const PdfNamePtr %s;' % ident)
    defs.append('const PdfNamePtr PdfName::%s = PdfName::createDirectName("%s");' % (ident, val))
defs += ['', 'const std::vector<const PdfNamePtr*>& PdfName::allStaticNames() {', '    static const std::vector<const PdfNamePtr*> all = {'] + ['        &PdfName::%s,' % i for i, _ in names] + ['    };', '    return all;', '}']
defs += ['', '}  // namespace kernel', '}  // namespace pdfcpp', '']
open(sys.argv[2], 'w').write('\n'.join(decl) + '\n')
open(sys.argv[3], 'w').write('\n'.join(defs))
print(len(names))
