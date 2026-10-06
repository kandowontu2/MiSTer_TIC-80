"""Generate real mouse() calls in every pinned language, twice in each TIC."""
import argparse
import json
from pathlib import Path
from generate_api_bank_cases import LANGUAGES, leb

FIELDS = ('x', 'y', 'left', 'middle', 'right', 'scrollx', 'scrolly')


def source(lang):
    if lang == 'forth':
        return ': TIC\n' + ''.join(' MOUSE\n' + ''.join(f' {base+n} PMEM!\n' for n in reversed(range(7)))
                                  for base in (0, 16)) + ' 7 PMEM 1+ 7 PMEM! ;\n'
    if lang in ('fennel', 'scheme', 'janet'):
        prefix = {'fennel': '', 'scheme': 't80::', 'janet': 'tic80/'}[lang]
        out = {'fennel': '(fn _G.TIC []\n', 'scheme': '(define (TIC)\n',
               'janet': '(import tic80)\n(defn TIC []\n'}[lang]
        for base in (0, 16):
            if lang == 'fennel':
                out += ' (let [m [(mouse)]]\n'
                values = [f'(. m {n+1})' for n in range(7)]
            elif lang == 'scheme':
                out += ' (let ((m (t80::mouse)))\n'
                values = [f'(list-ref m {n})' for n in range(7)]
            else:
                out += ' (let [m (tic80/mouse)]\n'
                values = [f'(get m {n})' for n in range(7)]
            for n, value in enumerate(values):
                if n in (2, 3, 4) and lang != 'scheme': value = f'(if {value} 1 0)'
                out += f' ({prefix}pmem {base+n} {value})\n'
            out += ' )\n'
        return out + f' ({prefix}pmem 7 (+ ({prefix}pmem 7) 1)))\n'
    starts = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n', 'squirrel': 'function TIC(){\n',
              'moon': 'export TIC\nTIC = ->\n', 'yue': 'global TIC = ->\n', 'python': 'def TIC():\n',
              'ruby': 'def TIC\n', 'miniscript': 'TIC=function\n', 'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    prefix = {'wren': 'TIC.', 'miniscript': 'tic80.'}.get(lang, '')
    out = starts[lang]
    for base in (0, 16):
        if lang in ('lua', 'moon', 'yue'):
            out += (' local ' if lang == 'lua' else ' ') + 'x,y,left,middle,right,scrollx,scrolly=mouse()\n'
            values = FIELDS
        else:
            declaration = ('var ' if base == 0 else '') if lang in ('js', 'wren') else ('local ' if base == 0 else '') if lang == 'squirrel' else ''
            out += f' {declaration}m={prefix}mouse()\n'
            values = [f'm[:{field}]' for field in FIELDS] if lang == 'ruby' else [f'm.{field}' for field in FIELDS] if lang == 'miniscript' else [f'm[{n}]' for n in range(7)]
        for n, value in enumerate(values):
            slot = base+n
            if n not in (2, 3, 4):
                out += f' {prefix}pmem({slot},{value})\n'
                continue
            out += f' {prefix}pmem({slot},0)\n'
            if lang in ('js', 'squirrel', 'wren'):
                out += f' if({value}) {{ {prefix}pmem({slot},1) }}\n'
            elif lang in ('lua', 'miniscript'):
                out += f' if {value} then\n  {prefix}pmem({slot},1)\n ' + ('end\n' if lang == 'lua' else 'end if\n')
            else:
                out += f' if {value}' + (':' if lang == 'python' else '') + f'\n  {prefix}pmem({slot},1)\n'
                if lang == 'ruby': out += ' end\n'
    out += f' {prefix}pmem(7,{prefix}pmem(7)+1)\n'
    return out + {'lua': 'end\n', 'ruby': 'end\n', 'miniscript': 'end function\n',
                  'js': '}\n', 'squirrel': '}\n', 'wren': ' }\n}\n'}.get(lang, '')


def wasm():
    funcs = [('mouse', [0x7f], []), ('pmem', [0x7f, 0x7e], [0x7f])]
    vec = lambda xs: leb(len(xs)) + b''.join(xs)
    string = lambda s: leb(len(s)) + s.encode()
    section = lambda tag, data: bytes([tag]) + leb(len(data)) + data
    types = [(p, r) for _, p, r in funcs] + [([], [])]
    out = b'\0asm\x01\0\0\0' + section(1, vec([b'\x60' + leb(len(p)) + bytes(p) + leb(len(r)) + bytes(r) for p, r in types]))
    out += section(2, vec([string('env') + string(name) + b'\0' + leb(n) for n, (name, _, _) in enumerate(funcs)]))
    out += section(3, vec([leb(2)])) + section(5, b'\x01\x00\x04')
    out += section(7, vec([string('TIC') + b'\0\x02']))
    integer = lambda n: b'\x41' + leb(n, True)
    # The public WASM mouse ABI is x/y i16, scrollx/scrolly i8, then three bools.
    fields = [(0, 0x2e, 1), (2, 0x2e, 1), (6, 0x2d, 0), (7, 0x2d, 0),
              (8, 0x2d, 0), (4, 0x2c, 0), (5, 0x2c, 0)]
    body = b'\0'
    for base in (0, 16):
        body += integer(1024) + b'\x10\x00'
        for n, (offset, opcode, alignment) in enumerate(fields):
            body += integer(base+n) + integer(1024) + bytes([opcode, alignment]) + leb(offset)
            # Store the full u32 word; signed i64 -1 means a PMEM read in WASM.
            body += b'\xad\x10\x01\x1a'
    body += integer(7) + integer(7) + b'\x42\x7f\x10\x01' + integer(1) + b'\x6a\xac\x10\x01\x1a\x0b'
    return out + section(10, vec([leb(len(body)) + body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    text = '/* Real mouse API calls; independent expected values in api_mouse_test.c. */\n'
    text += 'static const unsigned char mouse_wasm[] = {' + ','.join(map(str, wasm())) + '};\n'
    text += 'static const struct { const char *language, *source; const unsigned char *binary; unsigned size; } mouse_cases[] = {\n'
    for lang in LANGUAGES:
        binary = 'mouse_wasm,sizeof mouse_wasm' if lang == 'wasm' else 'NULL,0'
        text += '{' + json.dumps(lang) + ',' + json.dumps('// script: wasm\n' if lang == 'wasm' else source(lang)) + ',' + binary + '},\n'
    text += '};\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == '__main__': main()
