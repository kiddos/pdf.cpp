"""Generates the PdfName constants from PdfName.java.

Usage: gen_pdfnames.py PdfName.java pdf_name_constants.inc pdf_name_constants.cc

Java constant names are mapped to Google-style constant names: a `k` prefix plus
the Java name with its first letter capitalized (`AcroForm` -> `kAcroForm`,
`_3D` -> `k3D`). The lowercase Java names that would collide with an uppercase
one (`a`/`A`, `r`/`R`) become `kLowerA`/`kLowerR`.
"""
import re
import sys


def cpp_name(java_name, taken):
    base = java_name.lstrip('_')
    name = 'k' + base[0].upper() + base[1:]
    if base[0].islower() and name in taken:
        name = 'kLower' + base[0].upper() + base[1:]
    return name


def main():
    src = open(sys.argv[1], encoding='utf-8').read()
    names = re.findall(
        r'public static final PdfName (\w+)\s*=\s*(?:createDirectName|new PdfName)\("((?:[^"\\]|\\.)*)"\)', src)
    # Uppercase names claim the plain k-name first.
    taken = {cpp_name(j, set()) for j, _ in names if not j.lstrip('_')[0].islower()}
    entries = [(cpp_name(j, taken), j, v) for j, v in names]

    header = '// Generated from PdfName.java by tools/gen_pdfnames.py. Do not edit.'
    decl = [header, '// clang-format off']
    defs = [header, '// clang-format off', '#include "pdfcpp/kernel/pdf/pdf_object.h"', '', 'namespace pdfcpp {',
            'namespace kernel {', '']
    for ident, _, val in entries:
        decl.append('static const PdfNamePtr %s;' % ident)
        defs.append('const PdfNamePtr PdfName::%s = PdfName::CreateDirectName("%s");' % (ident, val))
    defs += ['', 'const std::vector<const PdfNamePtr*>& PdfName::AllStaticNames() {',
             '  static const std::vector<const PdfNamePtr*> kAll = {']
    defs += ['      &PdfName::%s,' % ident for ident, _, _ in entries]
    defs += ['  };', '  return kAll;', '}', '', '}  // namespace kernel', '}  // namespace pdfcpp', '']
    decl.append('// clang-format on')
    open(sys.argv[2], 'w').write('\n'.join(decl) + '\n')
    open(sys.argv[3], 'w').write('\n'.join(defs))
    print(len(entries))

    if len(sys.argv) > 4:  # optional: write the Java -> C++ name map (used for migrating call sites)
        with open(sys.argv[4], 'w') as f:
            for ident, java, _ in entries:
                f.write('%s %s\n' % (java, ident))


if __name__ == '__main__':
    main()
