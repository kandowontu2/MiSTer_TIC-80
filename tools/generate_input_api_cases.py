"""Generate all-runtime input calls; expected events live separately in the C test."""
import argparse
import json
from pathlib import Path
from generate_api_bank_cases import LANGUAGES, expr, call, leb

# These IDs are checked against the pinned enum with C static assertions.
KEYS = (1, 23, 17, 5, 50, 66, 63, 64, 78, 94)


def queries():
    for index in range(32):
        yield index, call('btn', index)
        yield 32 + index, call('btnp', index)
        yield 64 + index, call('btnp', index, 3, 2)
    for index, key in enumerate(KEYS):
        yield 96 + index, call('key', key)
        yield 106 + index, call('keyp', key)
        yield 116 + index, call('keyp', key, 3, 2)
    yield 126, call('key')
    yield 128, call('keyp')


def source(lang):
    if lang == 'forth':
        def typed(query):
            name, args = query
            if name in ('key', 'keyp') and not args: args = (0,)
            if name == 'key': name = 'keypressed'
            return call(name, *args, -1, -1) if name in ('btnp', 'keyp') and len(args) == 1 else call(name, *args)
        return ': TIC\n' + ''.join(
            f' {expr(typed(query), lang)} IF 1 ELSE 0 THEN {slot} PMEM!\n'
            for slot, query in queries()) + ' 127 PMEM 1+ 127 PMEM! ;\n'
    if lang in ('fennel', 'scheme', 'janet'):
        prefix = {'fennel': '', 'scheme': 't80::', 'janet': 'tic80/'}[lang]
        start = {'fennel': '(fn _G.TIC []\n', 'scheme': '(define (TIC)\n',
                 'janet': '(import tic80)\n(defn TIC []\n'}[lang]
        statements = ''.join(f' ({prefix}pmem {slot} (if {expr(query, lang)} 1 0))\n'
                             for slot, query in queries())
        return start + statements + f' ({prefix}pmem 127 (+ ({prefix}pmem 127) 1)))\n'
    starts = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n',
              'squirrel': 'function TIC(){\n', 'moon': 'export TIC\nTIC = ->\n',
              'yue': 'global TIC = ->\n', 'python': 'def TIC():\n', 'ruby': 'def TIC\n',
              'miniscript': 'TIC=function\n',
              'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    out = starts[lang]
    for slot, query in queries():
        zero = expr(call('pmem', slot, 0), lang)
        one = expr(call('pmem', slot, 1), lang)
        if lang == 'wren' and query[0] in ('key', 'keyp') and not query[1]:
            query = call(query[0], 0)
        condition = expr(query, lang)
        if lang in ('js', 'squirrel', 'wren'):
            out += f' {zero}\n if({condition}) {{ {one} }}\n'
        elif lang in ('lua', 'miniscript'):
            end = 'end' if lang == 'lua' else 'end if'
            out += f' {zero}\n if {condition} then\n  {one}\n {end}\n'
        else:
            out += f' {zero}\n if {condition}' + (':' if lang == 'python' else '') + f'\n  {one}\n'
            if lang == 'ruby': out += ' end\n'
    prefix = {'wren': 'TIC.', 'miniscript': 'tic80.'}.get(lang, '')
    out += f' {prefix}pmem(127,{prefix}pmem(127)+1)\n'
    return out + {'lua': 'end\n', 'ruby': 'end\n', 'miniscript': 'end function\n',
                  'js': '}\n', 'squirrel': '}\n', 'wren': ' }\n}\n'}.get(lang, '')


def wasm():
    funcs = [('btn', [0x7f], [0x7f]), ('btnp', [0x7f]*3, [0x7f]),
             ('key', [0x7f], [0x7f]), ('keyp', [0x7f]*3, [0x7f]),
             ('pmem', [0x7f, 0x7e], [0x7f])]
    vec = lambda values: leb(len(values)) + b''.join(values)
    string = lambda value: leb(len(value)) + value.encode()
    section = lambda tag, payload: bytes([tag]) + leb(len(payload)) + payload
    types = [(params, results) for _, params, results in funcs] + [([], [])]
    out = b'\0asm\x01\0\0\0' + section(1, vec([
        b'\x60' + leb(len(p)) + bytes(p) + leb(len(r)) + bytes(r) for p, r in types]))
    out += section(2, vec([string('env') + string(name) + b'\0' + leb(n)
                          for n, (name, _, _) in enumerate(funcs)]))
    out += section(3, vec([leb(len(funcs))])) + section(5, b'\x01\x00\x04')
    out += section(7, vec([string('TIC') + b'\0' + leb(len(funcs))]))
    ids = {name: n for n, (name, _, _) in enumerate(funcs)}
    integer = lambda value: b'\x41' + leb(value, True)
    body = b'\0'
    for slot, (name, args) in queries():
        if name in ('key', 'keyp') and not args: args = (0,)
        if name in ('btnp', 'keyp') and len(args) == 1: args = (*args, -1, -1)
        # Convert every ABI's nonzero button result to the same stored boolean.
        body += integer(slot) + b''.join(integer(n) for n in args) + b'\x10' + leb(ids[name])
        body += b'\x45\x45\xad\x10\x04\x1a'
    body += integer(127) + integer(127) + b'\x42\x7f\x10\x04' + integer(1) + b'\x6a\xad\x10\x04\x1a\x0b'
    return out + section(10, vec([leb(len(body)) + body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = '/* Generated input calls, independent expectations in api_input_test.c. */\n'
    output += 'static const unsigned char input_wasm[] = {' + ','.join(map(str, wasm())) + '};\n'
    output += 'static const struct { const char *language, *source; const unsigned char *binary; unsigned size; } input_cases[] = {\n'
    for lang in LANGUAGES:
        code = '// script: wasm\n' if lang == 'wasm' else source(lang)
        binary = 'input_wasm,sizeof input_wasm' if lang == 'wasm' else 'NULL,0'
        output += '{' + json.dumps(lang) + ',' + json.dumps(code) + ',' + binary + '},\n'
    output += '};\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__': main()
