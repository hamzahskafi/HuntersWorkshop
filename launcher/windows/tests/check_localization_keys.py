#!/usr/bin/env python3
"""The launchers' translation catalogs against their code (0.4.4 beta 28).

Keys are the English texts the code looks up: L.T("..."), L.F($"..."), Localization.T/F and {loc:Tr '...'}
in XAML. zh-CN.json is the reference catalog. Fails (exit 1) when
  - the Windows launcher (and the code it shares with the Avalonia one) looks up a key zh-CN.json does
    not have: it would show in English in every language (the Avalonia launcher is left as it is until
    the Linux work, so its own texts are not checked), or
  - a catalog has a key no code of either launcher uses (a reworded or removed text: dead weight).
A text the code holds in a plain string and looks up later (L.T(variable)) counts as used.
The other catalogs may lack keys zh-CN has (they show English until translated): counted, not failed.

Usage: python3 launcher/windows/tests/check_localization_keys.py
"""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]           # launcher/windows
CATALOGS = ROOT / 'BloodborneLauncher' / 'Localization'
SOURCES = [ROOT / 'BloodborneLauncher', ROOT / 'AvaloniaLauncher', ROOT / 'Shared']

def unescape(body):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            n = body[i + 1]
            m = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'", '\\': '\\', '0': '\0'}
            if n in m:
                out.append(m[n]); i += 2; continue
            if n == 'u':
                out.append(chr(int(body[i + 2:i + 6], 16))); i += 6; continue
        out.append(c); i += 1
    return ''.join(out)

def read_literal(s, i):
    """At s[i] a C# string literal (regular, verbatim, interpolated). Returns (key, end) or None."""
    interp = verbatim = False
    j = i
    while j < len(s) and s[j] in '$@':
        interp |= s[j] == '$'; verbatim |= s[j] == '@'; j += 1
    if j >= len(s) or s[j] != '"':
        return None
    j += 1
    text, args = [], 0
    while j < len(s):
        c = s[j]
        if verbatim and c == '"' and j + 1 < len(s) and s[j + 1] == '"':
            text.append('"'); j += 2; continue
        if not verbatim and c == '\\':
            text.append(s[j:j + 2] if s[j + 1] != 'u' else s[j:j + 6]); j += 2 if s[j + 1] != 'u' else 6; continue
        if c == '"':
            j += 1; break
        if interp and c == '{':
            if s[j + 1] == '{':
                text.append('{{'); j += 2; continue
            # a hole: expression up to a top-level ':' or ',' or the closing '}'
            depth, k, spec = 0, j + 1, ''
            while k < len(s):
                ch = s[k]
                if ch == '"':
                    lit = read_literal(s, k)
                    k = lit[1] if lit else k + 1; continue
                if ch in '([{': depth += 1
                elif ch in ')]': depth -= 1
                elif ch == '}':
                    if depth == 0: break
                    depth -= 1
                elif ch in ':,' and depth == 0 and not (ch == ':' and s[k - 1] == '?'):
                    # ':' of a ternary inside the hole is inside parentheses in this code base
                    end = s.index('}', k)
                    spec = s[k:end]; k = end; break
                k += 1
            text.append('{%d%s}' % (args, spec)); args += 1; j = k + 1; continue
        if interp and c == '}' and j + 1 < len(s) and s[j + 1] == '}':
            text.append('}}'); j += 2; continue
        text.append(c); j += 1
    raw = ''.join(text)
    key = raw if verbatim else unescape(raw)
    if interp:
        # FormattableString.Format keeps {{ }} doubled
        pass
    return key, j

def keys(src):
    found = []
    for m in re.finditer(r'\b(?:L|Localization)\.[TF]\(\s*', src):
        i = m.end()
        parts = []
        while True:
            lit = read_literal(src, i)
            if not lit:
                break
            parts.append(lit[0]); i = lit[1]
            n = re.match(r'\s*\+\s*', src[i:])
            if n and read_literal(src, i + n.end()):
                i += n.end(); continue
            break
        if parts and re.match(r'\s*\)', src[i:]):
            found.append(''.join(parts))
    for m in re.finditer(r"\{loc:Tr (?:&#x27;|')(.*?)(?:&#x27;|')\}", src):
        found.append(m.group(1).replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>').replace('&quot;', '"'))
    return found

def literals(src):
    """Every plain C# string literal's text: what a lookup by variable may be given."""
    out = set()
    for m in re.finditer(r'(?<![$@\w])"((?:[^"\\\n]|\\.)*)"', src):
        out.add(unescape(m.group(1)))
    return out


def main():
    used, plain, windows = set(), set(), set()
    for folder in SOURCES:
        if not folder.is_dir():
            continue
        for path in folder.rglob('*'):
            if path.suffix not in ('.cs', '.xaml', '.axaml') or 'obj' in path.parts or 'bin' in path.parts:
                continue
            text = path.read_text(encoding='utf-8-sig')
            found = keys(text)
            used.update(found)
            if folder.name != 'AvaloniaLauncher':
                windows.update(found)
            plain.update(literals(text))
    reference = json.loads((CATALOGS / 'zh-CN.json').read_text(encoding='utf-8-sig'))
    errors = 0
    missing = sorted(k for k in windows if k not in reference)
    for key in missing:
        print(f'missing from zh-CN.json: {key!r}')
    errors += len(missing)
    for path in sorted(CATALOGS.glob('*.json')):
        catalog = json.loads(path.read_text(encoding='utf-8-sig'))
        unused = sorted(k for k in catalog if k not in used and k not in plain)
        for key in unused:
            print(f'{path.name}: unused key {key!r}')
        errors += len(unused)
        if path.name != 'zh-CN.json':
            extra = sorted(k for k in catalog if k not in reference)
            for key in extra:
                print(f'{path.name}: key not in zh-CN.json {key!r}')
            errors += len(extra)
            gap = sum(1 for k in reference if k not in catalog)
            if gap:
                print(f'{path.name}: {gap} keys not translated yet (shown in English)')
    print(f'{len(used)} keys used, {len(reference)} in zh-CN.json, {errors} problems')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
