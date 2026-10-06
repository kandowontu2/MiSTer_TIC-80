"""Generate eight VQT calls in all fourteen runtimes; C defines expectations.

WASM uses one i32/f64; Forth uses one cell and the spectrum's scaled result.
Fractional inputs are included only for wrappers whose conversion accepts them.
"""
import argparse
import json
import struct
from pathlib import Path
from generate_api_bank_cases import leb
from generate_fft_cases import LANGUAGES

APIS = ('vqt', 'vqts', 'vqtr', 'vqtrs', 'vqtw', 'vqtsw', 'vqtrw', 'vqtrsw')
SCENARIOS = [('zero', 0), ('last', 119), ('middle', 57), ('negative', -1),
             ('beyond', 120), ('int-min', -2147483648), ('int-max', 2147483647)]
FRACTIONAL = 'lua js moon yue fennel squirrel wren ruby miniscript'.split()


def source(language, api, bin):
    prefix = {'scheme': 't80::', 'janet': 'tic80/', 'wren': 'TIC.', 'miniscript': 'tic80.'}.get(language, '')
    if language == 'forth':
        return f': TIC {bin} {api.upper()} 0 PMEM! 31 PMEM 1+ 31 PMEM! ;\n'
    if language in ('fennel', 'scheme', 'janet'):
        value = f'(* 1024 ({prefix}{api} {bin}))'
        if language == 'scheme': value = f'(inexact->exact {value})'
        if language == 'fennel': return f'(fn _G.TIC [] (pmem 0 {value}) (pmem 31 (+ (pmem 31) 1)))\n'
        if language == 'scheme': return f'(define (TIC) (t80::pmem 0 {value}) (t80::pmem 31 (+ (t80::pmem 31) 1)))\n'
        return f'(import tic80)\n(defn TIC [] (tic80/pmem 0 {value}) (tic80/pmem 31 (+ (tic80/pmem 31) 1)))\n'
    value = f'{prefix}{api}({bin})*1024'
    if language == 'python': value = f'int({value})'
    starts = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n', 'squirrel': 'function TIC(){\n',
              'moon': 'export TIC\nTIC = ->\n', 'yue': 'global TIC = ->\n', 'python': 'def TIC():\n',
              'ruby': 'def TIC\n', 'miniscript': 'TIC=function\n',
              'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    ends = {'lua': 'end\n', 'js': '}\n', 'squirrel': '}\n', 'ruby': 'end\n',
            'wren': ' }\n}\n', 'miniscript': 'end function\n'}
    return starts[language] + f' {prefix}pmem(0,{value})\n {prefix}pmem(31,{prefix}pmem(31)+1)\n' + ends.get(language, '')


def wasm(api, bin, result_type=0x7c, arity=1, argument_type=0x7f):
    vec = lambda xs: leb(len(xs)) + b''.join(xs)
    string = lambda s: leb(len(s)) + s.encode()
    section = lambda n, data: bytes([n]) + leb(len(data)) + data
    types = [([argument_type]*arity, [result_type]), ([0x7f, 0x7e], [0x7f]), ([], [])]
    out = b'\0asm\x01\0\0\0' + section(1, vec([b'\x60'+leb(len(p))+bytes(p)+leb(len(r))+bytes(r) for p, r in types]))
    out += section(2, vec([string('env')+string(n)+b'\0'+leb(i) for i, n in enumerate((api, 'pmem'))]))
    out += section(3, b'\x01\x02') + section(5, b'\x01\0\x04') + section(7, vec([string('TIC')+b'\0\x02']))
    integer = lambda n: b'\x41'+leb(n, True)
    body = b'\0'+integer(63)+b'\x42\x01\x10\x01\x1a'
    if result_type == 0x7c and arity == 1 and argument_type == 0x7f:
        body += integer(63)+b'\x42\0\x10\x01\x1a'
    body += integer(0)
    for _ in range(arity):
        body += integer(bin) if argument_type == 0x7f else b'\x44'+struct.pack('<d', bin)
    body += b'\x10\0'
    body += (b'\x44'+struct.pack('<d', 1024)+b'\xa2\xab' if result_type == 0x7c else b'\x43'+struct.pack('<f', 1024)+b'\x94\xa9')+b'\xad\x10\x01\x1a'
    body += integer(31)+integer(31)+b'\x42\x7f\x10\x01'+integer(1)+b'\x6a\xad\x10\x01\x1a\x0b'
    return out + section(10, vec([leb(len(body))+body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = '/* Generated calls; independent coverage/arguments/results live in C. */\n'
    for api in APIS:
        for scenario, (_, bin) in enumerate(SCENARIOS):
            out += f'static const unsigned char vqt_wasm_{api}_{scenario}[]={{'+','.join(map(str, wasm(api, bin)))+'};\n'
    out += 'static const struct { const char *language,*api,*name,*source; unsigned scenario; const unsigned char *binary; unsigned size; } vqt_cases[]={\n'
    for language in LANGUAGES:
        scenarios = SCENARIOS + ([('fraction', 5.75)] if language in FRACTIONAL else [])
        for api in APIS:
            for scenario, (name, bin) in enumerate(scenarios):
                code = '// script: wasm\n' if language == 'wasm' else source(language, api, bin)
                binary = f'vqt_wasm_{api}_{scenario},sizeof vqt_wasm_{api}_{scenario}' if language == 'wasm' else 'NULL,0'
                out += '{'+','.join([json.dumps(language), json.dumps(api), json.dumps(name), json.dumps(code), str(scenario), binary])+'},\n'
    out += '};\n'
    bad = [('f32', 0x7d, 1, 0x7f), ('arity', 0x7c, 0, 0x7f), ('argument', 0x7c, 1, 0x7c)]
    for api in APIS:
        for name, result, arity, argument in bad:
            out += f'static const unsigned char vqt_bad_wasm_{api}_{name}[]={{'+','.join(map(str, wasm(api, 0, result, arity, argument)))+'};\n'
    out += 'static const struct { const char *language,*api,*name,*source; const unsigned char *binary; unsigned size; } vqt_invalid_cases[]={\n'
    for api in APIS:
        for name, call in [('missing',''), ('null','null'), ('string','"bad"'), ('high','2147483648'), ('low','-2147483649')]:
            code = f'TIC=function\n tic80.pmem(63,1)\n tic80.{api}({call})\nend function\n'
            out += '{'+','.join(map(json.dumps, ['miniscript',api,name,code]))+',NULL,0},\n'
        for name, *_ in bad:
            binary = f'vqt_bad_wasm_{api}_{name}'
            out += '{'+','.join(map(json.dumps,['wasm',api,name,'// script: wasm\n']))+f',{binary},sizeof {binary}'+'},\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(out+'};\n')


if __name__ == '__main__': main()
