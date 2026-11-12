# Realistic Radio - text catalogue and built-in translations
#
# Collects every German / English text pair from the source (L10n::Pick / PickA, the settings menu definitions,
# key names, weekdays, ...) and joins them with the built-in translations in tools/l10n/<code>.txt
# (same format as the language files players write, see README). Writes src/L10nTexts.inc.
#
#   python3 tools/l10n/make_l10n.py              build src/L10nTexts.inc, list texts without a translation
#   python3 tools/l10n/make_l10n.py --update     also rewrite tools/l10n/ru.txt in catalogue order (new texts get an empty line)
import os, re, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(ROOT, 'src')
BUILTIN = ['ru']

# texts of the settings file (Config.h defaults) and their English version
CONFIG_DEFAULTS = [
    ('Jetzt läuft:', 'Now playing:'), ('Lautstärke', 'Volume'), ('Radio aus', 'Radio off'), ('Stumm', 'Muted'),
    ('Als Favorit gespeichert', 'Saved as favourite'), ('verbinde…', 'connecting…'), ('Sender nicht erreichbar', 'Station not available'),
    ('Zündung aus', 'Ignition off'), ('Radio', 'Radio'), ('Sender', 'Stations'), ('Titel', 'Track'), ('Pause', 'Pause'),
    ('Kein Medientyp erkannt', 'No media detected'), ('Kunde', 'Customer'), ('Disposition', 'Dispatch'),
    ('Wo bleibt die Lieferung? Wir erwarten {cargo} bald in {city}.', 'Where is the delivery? We are expecting {cargo} in {city} soon.'),
]   # languages compiled into the plugin (column order in L10nTexts.inc after de, en)


# ---------------------------------------------------------------- C++ tokens
def unescape(body):
    out = bytearray(); i = 0
    while i < len(body):
        c = body[i]
        if c != '\\':
            out += c.encode('utf-8'); i += 1; continue
        n = body[i + 1]
        if n == 'u': out += chr(int(body[i + 2:i + 6], 16)).encode('utf-8'); i += 6
        elif n == 'U': out += chr(int(body[i + 2:i + 10], 16)).encode('utf-8'); i += 10
        elif n == 'x':
            m = re.match(r'[0-9a-fA-F]{1,2}', body[i + 2:]); out.append(int(m.group(0), 16)); i += 2 + len(m.group(0))
        else: out += {'n': b'\n', 'r': b'\r', 't': b'\t', '0': b'\0'}.get(n, n.encode('utf-8')); i += 2
    return out.decode('utf-8', 'replace')


def tokens(text):
    """(kind, value, line): kind S = string literal (adjacent ones merged), I = identifier, P = punctuation"""
    toks = []; i = 0; line = 1; n = len(text)
    while i < n:
        c = text[i]
        if c == '\n': line += 1; i += 1; continue
        if c.isspace(): i += 1; continue
        if text.startswith('//', i): j = text.find('\n', i); i = n if j < 0 else j; continue
        if text.startswith('/*', i): j = text.find('*/', i); line += text.count('\n', i, j); i = j + 2; continue
        m = re.match(r'(?:u8|L|u|U)?R"([^(\s]*)\(', text[i:])
        if m:   # raw string: not a text of the menus (JSON templates)
            end = text.index(')' + m.group(1) + '"', i + len(m.group(0)))
            line += text.count('\n', i, end); i = end + len(m.group(1)) + 2; continue
        m = re.match(r'(?:u8|L|u|U)?"', text[i:])
        if m:
            j = i + len(m.group(0)); body = []
            while text[j] != '"':
                if text[j] == '\\': body.append(text[j:j + 2]); j += 2
                else: body.append(text[j]); j += 1
            v = unescape(''.join(body))
            if toks and toks[-1][0] == 'S' and toks[-1][3] == i: toks[-1] = ('S', toks[-1][1] + v, toks[-1][2], None)
            else: toks.append(('S', v, line, None))
            i = j + 1
            # remember where the literal ended (whitespace between adjacent literals is allowed)
            k = i
            while k < n and text[k] in ' \t\r\n': k += 1
            toks[-1] = (toks[-1][0], toks[-1][1], toks[-1][2], k)
            line += text.count('\n', i, k); i = k
            continue
        if c == "'":
            j = i + 1
            while text[j] != "'": j += 2 if text[j] == '\\' else 1
            i = j + 1; continue
        m = re.match(r'[A-Za-z_][A-Za-z0-9_]*', text[i:])
        if m: toks.append(('I', m.group(0), line, None)); i += len(m.group(0)); continue
        toks.append(('P', c, line, None)); i += 1
    return toks


ID = re.compile(r'^[a-z0-9_.\-|:/\\]*$')
def textual(s): return bool(re.search(r'[A-Za-zÄÖÜäöüßА-я]', s)) and not ID.match(s)


# ---------------------------------------------------------------- catalogue
def catalogue():
    pairs = []          # (de, en, where)
    seen = {}
    def add(de, en, where):
        if not de.strip() or not textual(de) and not textual(en) or de in ('Deutsch', 'English', 'Русский'):
            if de not in ('\u201e', '\u201c'): return   # quotation marks are kept: other languages use other ones
        k = de.strip()
        if k in seen: seen[k][2].add(where); return
        e = [de, en, {where}]; seen[k] = e; pairs.append(e)

    first = ['SettingsMenu.cpp', 'RadioManager.cpp', 'Phone.cpp', 'OverlayImage.cpp', 'MediaLibrary.cpp']   # menu first in the lists
    files = sorted(glob.glob(os.path.join(SRC, '*.cpp')), key=lambda p: (first.index(os.path.basename(p)) if os.path.basename(p) in first else 99, p))
    for path in files:
        found = []
        def fadd(de, en, where): found.append((int(re.search(r':(\d+)$', where).group(1)) if re.search(r':(\d+)$', where) else 10**6, de, en, where))
        name = os.path.basename(path)
        if name.startswith('L10n'): continue
        text = open(path, encoding='utf-8').read()
        t = tokens(text)
        for i, tok in enumerate(t):
            # L10n::Pick(L"de", L"en", ...) / PickA("de", "en", ...)
            # ... and the menu helpers T("de", "en") / SetStatus("de", "en")
            if tok[0] == 'I' and (tok[1] in ('Pick', 'PickA') and i >= 2 and t[i - 1][1] == ':' or tok[1] in ('T', 'SetStatus')) and i + 4 < len(t) \
                    and t[i + 1][1] == '(' and t[i + 2][0] == 'S' and t[i + 3][1] == ',' and t[i + 4][0] == 'S':
                fadd(t[i + 2][1], t[i + 4][1], f'{name}:{tok[2]}')
        # groups of literals separated only by commas: menu definitions {"de", "en", "hint de", "hint en"}, label pairs, tables
        if name in ('SettingsMenu.cpp', 'MediaLibrary.cpp', 'RadioManager.cpp', 'Phone.cpp', 'OverlayImage.cpp'):
            i = 0
            while i < len(t):
                if t[i][0] != 'S': i += 1; continue
                g = [t[i]]; j = i + 1
                while j + 1 < len(t) and t[j][1] == ',' and t[j + 1][0] == 'S': g.append(t[j + 1]); j += 2
                before = t[i - 1][1] if i else ''; before2 = t[i - 2][1] if i > 1 else ''
                vals = [x[1] for x in g]
                ctor = before == '(' and before2 in ('Slider', 'Toggle', 'Choice', 'KeyRow', 'Button', 'Heading', 'TextRow')
                # {"de", "en"} label pairs, {"Page", "Page", "Title", "Title", {...}} pages, {"id", L"de", L"en"} tables - not {"id", "id", ...} lists
                nxt = t[j][1] if j < len(t) else ''
                brace = before == '{' and (len(g) == 2 and nxt == '}' or len(g) == 4 and nxt == ',' or len(g) == 3 and nxt == '}')
                if name == 'SettingsMenu.cpp' and (ctor or brace):
                    if len(vals) == 3 and ID.match(vals[0]) and vals[0]: vals = vals[1:]
                    if len(vals) % 2 == 0 and not all(ID.match(v) for v in vals):
                        for k in range(0, len(vals), 2): fadd(vals[k], vals[k + 1], f'{name}:{g[k][2]}')
                elif name == 'MediaLibrary.cpp' and brace and len(vals) == 3 and ID.match(vals[0]):
                    fadd(vals[1], vals[2], f'{name}:{g[0][2]}')
                elif name == 'RadioManager.cpp' and brace and len(vals) == 2 and t[j][1] == '}' and textual(vals[0]) and vals[0] != vals[1] \
                        and any(c.isupper() for c in vals[0][:1]):
                    fadd(vals[0], vals[1], f'{name}:{g[0][2]}')
                i = j
        # x.de = "..."; x.en = "..." (menu rows built in code)
        for m in re.finditer(r'\.(h?|btn)[dD]e = "((?:[^"\\]|\\.)*)";\s*\w+\.\1[eE]n = "((?:[^"\\]|\\.)*)"', text):
            fadd(unescape(m.group(2)), unescape(m.group(3)), f'{name}:{text.count(chr(10), 0, m.start()) + 1}')
        # parallel arrays: de[] / en[] (weekdays), lde[] / len[] (phone apps)
        arrays = {}
        for m in re.finditer(r'\*?\s*(\w+)\[\]\s*=\s*\{([^{}]*)\}', text):
            vals = [unescape(x) for x in re.findall(r'L?"((?:[^"\\]|\\.)*)"', m.group(2))]
            if vals: arrays.setdefault(m.group(1), []).append((vals, text.count('\n', 0, m.start()) + 1))
        for a, b in (('de', 'en'), ('lde', 'len')):
            for (dv, ln), (ev, _) in zip(arrays.get(a, []), arrays.get(b, [])):
                if len(dv) == len(ev):
                    for d, e in zip(dv, ev): fadd(d, e, f'{name}:{ln}')
        # key names: LabelDe / LabelEn switch
        if name == 'SettingsMenu.cpp':
            def labels(fn):
                body = text[text.index(f'const char* {fn}(Action a)'):]
                body = body[:body.index('default:')]
                return {m.group(1): unescape(m.group(2)) for m in re.finditer(r'case Action::(\w+): return "((?:[^"\\]|\\.)*)"', body)}
            de, en = labels('LabelDe'), labels('LabelEn')
            for k in de: fadd(de[k], en.get(k, ''), f'{name}:LabelDe')
        for _, de, en, where in sorted(found, key=lambda x: x[0]): add(de, en, where)
    # German defaults of the texts in the settings file (Config.h), shown through L10n::Ov while they are unchanged
    for de, en in CONFIG_DEFAULTS: add(de, en, 'Config.h')
    return pairs


# ---------------------------------------------------------------- language files
def read_lang(path):
    """entries {de.strip(): translation} and header {CODE, NAME, ...} of a language file"""
    head, tr, cur = {}, {}, None   # entries start at the first [ ] line
    if not os.path.exists(path): return head, tr
    for raw in open(path, encoding='utf-8-sig').read().replace('\r', '').split('\n'):
        if raw.startswith('#'): continue
        m = re.match(r'^(CODE|NAME|VOICE|DECIMAL|THOUSANDS):\s?(.*)$', raw)
        if m and cur is None: head[m.group(1)] = m.group(2).strip(); continue
        if raw.startswith('['): cur = {}; continue
        if cur is None: continue
        m = re.match(r'^([A-Z]{2,3}|TEXT)(?: \(gleiche Zeile / same line\))?:\s?(.*)$', raw)
        if not m: continue
        k, v = m.group(1), m.group(2)
        if k in ('DE', 'EN'):
            if k in cur: continue
            cur[k] = v.replace('\\r', '\r').replace('\\n', '\n')
        elif 'DE' in cur and v.strip():
            tr.setdefault(cur['DE'].strip(), v.strip().replace('\\r', '\r').replace('\\n', '\n'))
    return head, tr


def esc_line(s): return s.replace('\r', '\\r').replace('\n', '\\n')


def wlit(s):
    out = ''
    for ch in s:
        o = ord(ch)
        if ch == '\\': out += '\\\\'
        elif ch == '"': out += '\\"'
        elif ch == '\n': out += '\\n'
        elif ch == '\r': out += '\\r'
        elif o < 128: out += ch
        elif o > 0xFFFF: out += '\\U%08x' % o
        else: out += '\\u%04x' % o
    return 'L"' + out + '"'


def main():
    cat = catalogue()
    langs = {c: read_lang(os.path.join(HERE, c + '.txt')) for c in BUILTIN}
    rows, missing = [], {c: [] for c in BUILTIN}
    for de, en, where in cat:
        cols = []
        for c in BUILTIN:
            v = langs[c][1].get(de.strip(), '')
            if not v: missing[c].append((de, en, where))
            cols.append(v)
        rows.append('    {%s, %s, %s},' % (wlit(de), wlit(en), ', '.join(wlit(v) for v in cols)))
    open(os.path.join(SRC, 'L10nTexts.inc'), 'w', encoding='ascii', newline='\n').write(
        '// generated by tools/l10n/make_l10n.py - do not edit (columns: de, en, ' + ', '.join(BUILTIN) + ')\n' + '\n'.join(rows) + '\n')
    print(len(rows), 'texts')
    for c in BUILTIN:
        print(f'{c}: {len(missing[c])} without translation')
        for de, en, where in missing[c]: print(f'   [{", ".join(sorted(where))}] {de!r} / {en!r}')
    if '--update' in sys.argv:
        for c in BUILTIN:
            head, tr = langs[c]
            out = [f'# Realistic Radio - built-in translation ({c}). Format: see the README, section "Add a language".',
                   f'CODE: {head.get("CODE", c)}', f'NAME: {head.get("NAME", c)}']
            for k in ('VOICE', 'DECIMAL', 'THOUSANDS'):
                if k in head: out.append(f'{k}: {head[k]}')
            out.append('')
            for de, en, where in cat:
                out += [f'[{", ".join(sorted(where))}]', 'DE: ' + esc_line(de), 'EN: ' + esc_line(en),
                        f'{c.upper()}: ' + esc_line(tr.get(de.strip(), '')), '']
            open(os.path.join(HERE, c + '.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))


if __name__ == '__main__':
    main()
