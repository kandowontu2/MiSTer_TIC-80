"""Generate the same asset-bank API contract in all 14 pinned languages.

Expected values are specified separately in api_bank_test.c. WASM is encoded
directly so neither a WASI SDK nor an extra compiler is needed for this fixture.
"""
import argparse
import json
from pathlib import Path

LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth'.split()
FLAGS = 0x14404


def call(name, *args):
    return name, args


def phases(bank):
    other = 7 if bank != 7 else 0
    poke = lambda addr, value: call('poke', addr, value, 8)
    return [
        [call('sync', 103, bank, False)],
        [poke(0x4000, 129 + bank), poke(0x6000, 145 + bank), call('mset', 0, 0, 161 + bank),
         poke(FLAGS, 177 + bank), call('sync', 103, bank, True)],
        [poke(0x4000, 0), poke(0x6000, 0), call('mset', 0, 0, 0), poke(FLAGS, 0),
         call('sync', 103, bank, False)],
        [call('sync', 103, other, False)],
    ]


def tail():
    return [call('pmem', 0, call('peek', 0x4000, 8)),
            call('pmem', 1, call('peek', 0x6000, 8)),
            call('pmem', 2, call('mget', 0, 0)),
            call('pmem', 3, call('peek', FLAGS, 8)),
            call('pmem', 4, call('peek', 0x3FC3, 8)),
            call('vbank', 1), call('pmem', 5, call('peek', 0x3FC6, 8)),
            call('cls', 0), call('pix', 10, 10, 2), call('vbank', 0),
            call('cls', 0), call('pix', 0, 0, 1),
            call('pmem', 15, call('add', 'n', 1))]


def expr(node, lang):
    if isinstance(node, bool):
        return ('True' if node else 'False') if lang == 'python' else (
            '#t' if node else '#f') if lang == 'scheme' else (
            '1' if node else '0') if lang in ('miniscript', 'forth') else ('true' if node else 'false')
    if not isinstance(node, tuple):
        return str(node)
    name, args = node
    if lang == 'forth':
        if name == 'pmem' and len(args) == 2:
            return f'{expr(args[1], lang)} {expr(args[0], lang)} PMEM!'
        if name == 'add':
            return f'{expr(args[0], lang)} {expr(args[1], lang)} +'
        fn = 'PIX!' if name == 'pix' else name.upper()
        return ' '.join([*(expr(a, lang) for a in args), fn])
    prefix = {'scheme': 't80::', 'janet': 'tic80/', 'wren': 'TIC.', 'miniscript': 'tic80.'}.get(lang, '')
    if name == 'add':
        return f'(+ {expr(args[0], lang)} {expr(args[1], lang)})' if lang in ('fennel', 'scheme', 'janet') else 'n+1'
    fn = prefix + name
    if lang in ('fennel', 'scheme', 'janet'):
        return '(' + ' '.join([fn, *(expr(a, lang) for a in args)]) + ')'
    return fn + '(' + ','.join(expr(a, lang) for a in args) + ')'


def source(lang, bank):
    bodies = phases(bank)
    if lang == 'forth':
        # A single N stays below each balanced API operation on the stack.
        out = ': TIC 15 PMEM\n'
        for index, body in enumerate(bodies):
            out += f' DUP {index} = IF\n'
            for c in body:
                out += '  ' + expr(c, lang) + '\n'
            out += ' THEN\n'
        for c in tail()[:-1]:
            out += ' ' + expr(c, lang) + (' DROP' if c[0] == 'vbank' else '') + '\n'
        return out + ' 1+ 15 PMEM! ;\n'
    if lang in ('fennel', 'scheme', 'janet'):
        get = expr(call('pmem', 15), lang)
        start = {'fennel': f'(fn _G.TIC [] (let [n {get}]\n',
                 'scheme': f'(define (TIC) (let ((n {get}))\n',
                 'janet': f'(import tic80)\n(defn TIC [] (def n {get})\n'}[lang]
        for index, body in enumerate(bodies):
            start += f' (when (= n {index})\n' + ''.join('  ' + expr(c, lang) + '\n' for c in body) + ' )\n'
        start += ''.join(' ' + expr(c, lang) + '\n' for c in tail())
        return start + (')\n' if lang == 'janet' else '))\n')
    starts = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n', 'squirrel': 'function TIC(){\n',
              'moon': 'export TIC\nTIC = ->\n', 'yue': 'global TIC = ->\n', 'python': 'def TIC():\n',
              'ruby': 'def TIC\n', 'miniscript': 'TIC=function\n',
              'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    out = starts[lang]
    declaration = 'var ' if lang in ('js', 'wren') else 'local ' if lang in ('lua', 'squirrel') else ''
    semi = ';' if lang in ('js', 'squirrel') else ''
    out += ' ' + declaration + 'n=' + expr(call('pmem', 15), lang) + semi + '\n'
    for index, body in enumerate(bodies):
        if lang in ('js', 'squirrel', 'wren'):
            out += f' if(n=={index}) {{\n'
        elif lang in ('lua', 'miniscript'):
            out += f' if n=={index} then\n'
        else:
            out += f' if n=={index}' + (':' if lang == 'python' else '') + '\n'
        out += ''.join('  ' + expr(c, lang) + semi + '\n' for c in body)
        if lang in ('js', 'squirrel', 'wren'):
            out += ' }\n'
        elif lang == 'miniscript':
            out += ' end if\n'
        elif lang in ('lua', 'ruby'):
            out += ' end\n'
    out += ''.join(' ' + expr(c, lang) + semi + '\n' for c in tail())
    return out + {'lua': 'end\n', 'ruby': 'end\n', 'miniscript': 'end function\n',
                  'js': '}\n', 'squirrel': '}\n', 'wren': ' }\n}\n'}.get(lang, '')


def leb(value, signed=False):
    out = bytearray()
    while True:
        low = value & 127
        value >>= 7
        done = (value == 0 and (not signed or not low & 64)) or (signed and value == -1 and low & 64)
        out.append(low if done else low | 128)
        if done:
            return bytes(out)


def wasm(bank):
    # name, parameter types, result types; pmem uses a signed i64 sentinel -1.
    funcs = [('sync', [0x7F]*3, []), ('peek', [0x7F]*2, [0x7F]), ('poke', [0x7F]*3, []),
             ('mget', [0x7F]*2, [0x7F]), ('mset', [0x7F]*3, []), ('vbank', [0x7F], [0x7F]),
             ('cls', [0x7F], []), ('pix', [0x7F]*3, [0x7F]), ('pmem', [0x7F,0x7E], [0x7F])]
    types = [(params, returns) for _, params, returns in funcs] + [([], [])]
    vector = lambda xs: leb(len(xs)) + b''.join(xs)
    string = lambda s: leb(len(s)) + s.encode()
    section = lambda n, b: bytes([n]) + leb(len(b)) + b
    module = b'\0asm\x01\0\0\0'
    module += section(1, vector([b'\x60' + bytes([len(p)]) + bytes(p) + bytes([len(r)]) + bytes(r) for p, r in types]))
    module += section(2, vector([string('env') + string(n) + b'\0' + leb(i) for i,(n,_,_) in enumerate(funcs)]))
    module += section(3, vector([leb(len(funcs))]))
    module += section(5, b'\x01\x00\x04')
    module += section(7, vector([string('TIC') + b'\x00' + leb(len(funcs))]))
    ids = {n:i for i,(n,_,_) in enumerate(funcs)}
    def emit(node, i64=False):
        if node == 'n':
            out = b'\x20\0'
        elif not isinstance(node, tuple):
            return bytes([0x42 if i64 else 0x41]) + leb(int(node), True)
        else:
            name, args = node
            if name == 'add':
                out = emit(args[0]) + emit(args[1]) + b'\x6a'
            else:
                if name == 'pmem' and len(args) == 1:
                    args = (*args, -1)
                out = b''.join(emit(a, t == 0x7E) for a,t in zip(args, funcs[ids[name]][1])) + b'\x10' + leb(ids[name])
        return out + b'\xad' if i64 else out  # unsigned i32 -> i64 for stored byte values
    def stmt(node):
        out = emit(node)
        return out + b'\x1a' if funcs[ids[node[0]]][2] else out
    body = b'\x01\x01\x7f' + emit(call('pmem', 15)) + b'\x21\0'
    for index, operations in enumerate(phases(bank)):
        body += b'\x20\0' + emit(index) + b'\x46\x04\x40' + b''.join(stmt(n) for n in operations) + b'\x0b'
    body += b''.join(stmt(n) for n in tail()) + b'\x0b'
    return module + section(10, vector([leb(len(body)) + body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = '/* Generated by tools/generate_api_bank_cases.py. */\n'
    for bank in (0, 1, 7):
        data = wasm(bank)
        out += f'static const unsigned char api_wasm_{bank}[] = {{' + ','.join(map(str, data)) + '};\n'
    out += 'static const struct { const char *language; unsigned bank; const char *source; const unsigned char *binary; unsigned binary_size; } api_bank_cases[] = {\n'
    for lang in LANGUAGES:
        for bank in (0, 1, 7):
            if lang == 'wasm':
                out += f'{{"wasm",{bank},"// script: wasm\\n",api_wasm_{bank},sizeof api_wasm_{bank}}},\n'
            else:
                out += '{' + f'"{lang}",{bank},' + json.dumps(source(lang, bank)) + ',NULL,0},\n'
    out += '};\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(out)


if __name__ == '__main__':
    main()
