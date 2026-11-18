"""Extract Java array literals as integer lists (helper for generating C++ tables)."""
import re, sys

def parse_elem(e):
    e = e.strip()
    if not e:
        return None
    e = re.sub(r'^\((char|byte|int|short)\)\s*', '', e)
    if e.startswith("'"):
        body = e[1:-1]
        if body.startswith('\\u'):
            return int(body[2:], 16)
        if body == '\\0':
            return 0
        if body == "\\'":
            return 39
        if body == '\\\\':
            return 92
        if body.startswith('\\'):
            return {'n': 10, 'r': 13, 't': 9, 'b': 8, 'f': 12}[body[1]]
        return ord(body)
    if e.lower().startswith('0x') or e.lower().startswith('-0x'):
        return int(e, 16)
    return int(e)

def extract(src, name):
    m = re.search(r'\b' + re.escape(name) + r'\s*=\s*(?:new\s+\w+\s*\[\s*\]\s*)?\{', src)
    if not m:
        raise SystemExit('array not found: ' + name)
    i = m.end()
    depth = 1
    j = i
    in_char = False
    while depth:
        c = src[j]
        if c == "'" and src[j-1] != '\\':
            in_char = not in_char
        elif not in_char:
            if c == '{': depth += 1
            elif c == '}': depth -= 1
        j += 1
    body = src[i:j-1]
    body = re.sub(r'//[^\n]*', '', body)
    # split on commas not inside quotes
    elems, cur, q = [], '', False
    for k, c in enumerate(body):
        if c == "'" and (k == 0 or body[k-1] != '\\' or body[k-2:k] == '\\\\'):
            q = not q
        if c == ',' and not q:
            elems.append(cur); cur = ''
        else:
            cur += c
    elems.append(cur)
    vals = [parse_elem(x) for x in elems]
    return [v for v in vals if v is not None]

def cpp(name, vals, ctype='uint16_t', per_line=16):
    lines = []
    for k in range(0, len(vals), per_line):
        lines.append('    ' + ', '.join(str(v) for v in vals[k:k+per_line]) + ',')
    return 'static const %s %s[%d] = {\n%s\n};\n' % (ctype, name, len(vals), '\n'.join(lines))

if __name__ == '__main__':
    src = open(sys.argv[1], encoding='utf-8').read()
    for spec in sys.argv[2:]:
        name, _, cname = spec.partition(':')
        ctype = 'uint16_t'
        if '@' in cname:
            cname, ctype = cname.split('@')
        print(cpp(cname or name, extract(src, name), ctype))
