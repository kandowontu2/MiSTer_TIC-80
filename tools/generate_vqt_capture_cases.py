"""Generate real-spectrum queries for all bins and eight APIs in fourteen VMs."""
import argparse
import json
import struct
from pathlib import Path
from generate_api_bank_cases import leb
from generate_fft_cases import LANGUAGES
from generate_vqt_cases import APIS, FRACTIONAL


def source(language, api):
    prefix = {'scheme': 't80::', 'janet': 'tic80/', 'wren': 'TIC.', 'miniscript': 'tic80.'}.get(language, '')
    queries = list(enumerate(range(120))) + list(zip(range(120, 124), (-1, 120, -2147483648, 2147483647)))
    if language in FRACTIONAL:
        queries.append((124, 5.75))
    if language == 'forth':
        return ': TIC\n' + ''.join(f' {arg} {api.upper()} {slot} PMEM!\n' for slot, arg in queries) + ' 250 PMEM 1+ 250 PMEM! ;\n'
    statements = []
    for slot, arg in queries:
        if language in ('fennel', 'scheme', 'janet'):
            value = f'(* 1024 ({prefix}{api} {arg}))'
            value = f'(math.floor {value})' if language == 'fennel' else f'(inexact->exact (floor {value}))' if language == 'scheme' else f'(math/floor {value})'
            statements.append(f' ({prefix}pmem {slot} {value})\n')
        else:
            value = f'{prefix}{api}({arg})*1024'
            if language in ('lua', 'moon', 'yue'): value = f'math.floor({value})'
            elif language == 'js': value = f'Math.floor({value})'
            elif language == 'python': value = f'int({value})'
            elif language in ('ruby', 'wren'): value = f'({value}).floor'
            else: value = f'floor({value})'
            statements.append(f' {prefix}pmem({slot},{value})\n')
    if language in ('fennel', 'scheme', 'janet'):
        statements.append(f' ({prefix}pmem 250 (+ ({prefix}pmem 250) 1))\n')
        start = {'fennel': '(fn _G.TIC []\n', 'scheme': '(define (TIC)\n', 'janet': '(import tic80)\n(defn TIC []\n'}[language]
        return start + ''.join(statements) + ')\n'
    statements.append(f' {prefix}pmem(250,{prefix}pmem(250)+1)\n')
    starts = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n', 'squirrel': 'function TIC(){\n',
              'moon': 'export TIC\nTIC = ->\n', 'yue': 'global TIC = ->\n', 'python': 'def TIC():\n',
              'ruby': 'def TIC\n', 'miniscript': 'TIC=function\n',
              'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    ends = {'lua': 'end\n', 'js': '}\n', 'squirrel': '}\n', 'ruby': 'end\n',
            'wren': ' }\n}\n', 'miniscript': 'end function\n'}
    return starts[language] + ''.join(statements) + ends.get(language, '')


def wasm(api):
    vec = lambda xs: leb(len(xs)) + b''.join(xs)
    string = lambda s: leb(len(s)) + s.encode()
    section = lambda n, data: bytes([n]) + leb(len(data)) + data
    types = [([0x7f], [0x7c]), ([0x7f, 0x7e], [0x7f]), ([], [])]
    out = b'\0asm\x01\0\0\0' + section(1, vec([b'\x60'+leb(len(p))+bytes(p)+leb(len(r))+bytes(r) for p, r in types]))
    out += section(2, vec([string('env')+string(n)+b'\0'+leb(i) for i, n in enumerate((api, 'pmem'))]))
    out += section(3, b'\x01\x02') + section(5, b'\x01\0\x04') + section(7, vec([string('TIC')+b'\0\x02']))
    integer = lambda n: b'\x41'+leb(n, True)
    body = b'\0'
    queries = list(enumerate(range(120))) + list(zip(range(120, 124), (-1, 120, -2147483648, 2147483647)))
    for slot, arg in queries:
        body += integer(slot)+integer(arg)+b'\x10\0\x44'+struct.pack('<d',1024)+b'\xa2\xab\xad\x10\x01\x1a'
    body += integer(250)+integer(250)+b'\x42\x7f\x10\x01'+integer(1)+b'\x6a\xad\x10\x01\x1a\x0b'
    return out + section(10, vec([leb(len(body))+body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = '/* Generated real capture calls; expectations and coverage live in C. */\n'
    for api in APIS:
        out += f'static const unsigned char vqt_capture_wasm_{api}[]={{'+','.join(map(str,wasm(api)))+'};\n'
    out += 'static const struct { const char *language,*api,*source; const unsigned char *binary; unsigned size; } vqt_capture_cases[]={\n'
    for language in LANGUAGES:
        for api in APIS:
            code = '// script: wasm\n' if language == 'wasm' else source(language,api)
            binary = f'vqt_capture_wasm_{api},sizeof vqt_capture_wasm_{api}' if language == 'wasm' else 'NULL,0'
            out += '{'+','.join(map(json.dumps,(language,api,code)))+','+binary+'},\n'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((out+'};\n').encode())


if __name__ == '__main__':
    main()
