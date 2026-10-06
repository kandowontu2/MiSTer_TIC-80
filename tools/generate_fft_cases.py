"""Generate FFT dispatch calls in all fourteen runtimes.

Forth uses explicit integer arguments and a scaled integer result. WASM imports
take two i32 arguments and return f64. Expectations remain independent in C.
"""
import argparse
import json
import struct
from pathlib import Path
from generate_api_bank_cases import leb

LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth'.split()
APIS = ('fft', 'ffts', 'fftr', 'fftrs')
SCENARIOS = [('zero', (0,)), ('last', (1023,)), ('range', (3, 19)),
             ('negative', (-7, 23)), ('reversed', (19, 3)),
             ('full', (0, 1023)), ('beyond', (1024, 2048))]


def source(language, api, args):
    if language in ('wren', 'forth') and len(args) == 1:
        args = (*args, -1)
    prefix = {'scheme': 't80::', 'janet': 'tic80/', 'wren': 'TIC.', 'miniscript': 'tic80.'}.get(language, '')
    if language == 'forth':
        return ': TIC ' + ' '.join(map(str, args)) + f' {api.upper()} 0 PMEM! 31 PMEM 1+ 31 PMEM! ;\n'
    if language in ('fennel', 'scheme', 'janet'):
        call = '(' + prefix + api + ' ' + ' '.join(map(str, args)) + ')'
        result = '(* 1024 ' + call + ')'
        if language == 'scheme':
            result = '(inexact->exact ' + result + ')'
        if language == 'fennel':
            return f'(fn _G.TIC [] (pmem 0 {result}) (pmem 31 (+ (pmem 31) 1)))\n'
        if language == 'scheme':
            return f'(define (TIC) (t80::pmem 0 {result}) (t80::pmem 31 (+ (t80::pmem 31) 1)))\n'
        return f'(import tic80)\n(defn TIC [] (tic80/pmem 0 {result}) (tic80/pmem 31 (+ (tic80/pmem 31) 1)))\n'
    call = prefix + api + '(' + ','.join(map(str, args)) + ')'
    result = call + '*1024'
    if language == 'python':
        result = 'int(' + result + ')'
    lines = [f'{prefix}pmem(0,{result})', f'{prefix}pmem(31,{prefix}pmem(31)+1)']
    start = {'lua': 'function TIC()\n', 'js': 'function TIC(){\n',
             'squirrel': 'function TIC(){\n', 'moon': 'export TIC\nTIC = ->\n',
             'yue': 'global TIC = ->\n', 'python': 'def TIC():\n', 'ruby': 'def TIC\n', 'miniscript': 'TIC=function\n',
             'wren': 'class Game is TIC {\n construct new() {}\n TIC() {\n'}[language]
    end = {'lua': 'end\n', 'js': '}\n', 'squirrel': '}\n', 'ruby': 'end\n',
           'wren': ' }\n}\n', 'miniscript': 'end function\n'}.get(language, '')
    return start + ''.join(' ' + line + '\n' for line in lines) + end


def wasm(api, args, result_type=0x7c, arity=2):
    if len(args) == 1:
        args = (*args, -1)
    vec = lambda xs: leb(len(xs)) + b''.join(xs)
    string = lambda s: leb(len(s)) + s.encode()
    section = lambda n, data: bytes([n]) + leb(len(data)) + data
    args = args[:arity]
    types = [([0x7f]*arity, [result_type]), ([0x7f, 0x7e], [0x7f]), ([], [])]
    out = b'\0asm\x01\0\0\0' + section(1, vec([b'\x60' + leb(len(p)) + bytes(p) + leb(len(r)) + bytes(r) for p, r in types]))
    out += section(2, vec([string('env') + string(n) + b'\0' + leb(i) for i, n in enumerate((api, 'pmem'))]))
    out += section(3, b'\x01\x02') + section(5, b'\x01\0\x04') + section(7, vec([string('TIC') + b'\0\x02']))
    integer = lambda n: b'\x41' + leb(n, True)
    body = b'\0' + integer(63) + b'\x42\x01\x10\x01\x1a'
    # Reset the ban marker for valid fixtures; mismatched imports cannot link.
    if result_type == 0x7c and arity == 2:
        body += integer(63) + b'\x42\0\x10\x01\x1a'
    body += integer(0) + b''.join(integer(n) for n in args) + b'\x10\0'
    body += (b'\x44'+struct.pack('<d',1024.0)+b'\xa2\xab' if result_type==0x7c else b'\x43'+struct.pack('<f',1024.0)+b'\x94\xa9') + b'\xad\x10\x01\x1a'
    body += integer(31) + integer(31) + b'\x42\x7f\x10\x01' + integer(1) + b'\x6a\xad\x10\x01\x1a\x0b'
    return out + section(10, vec([leb(len(body)) + body]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = '/* Generated calls; independent argument/result expectations are in C. */\n'
    for api in APIS:
        for scenario, (_, call) in enumerate(SCENARIOS):
            out += f'static const unsigned char fft_wasm_{api}_{scenario}[]={{' + ','.join(map(str, wasm(api, call))) + '};\n'
    out += 'static const struct { const char *language,*api,*name,*source; unsigned scenario; const unsigned char *binary; unsigned size; } fft_cases[]={\n'
    for language in LANGUAGES:
        for api in APIS:
            for scenario, (name, call) in enumerate(SCENARIOS):
                code = '// script: wasm\n' if language == 'wasm' else source(language, api, call)
                binary = f'fft_wasm_{api}_{scenario},sizeof fft_wasm_{api}_{scenario}' if language == 'wasm' else 'NULL,0'
                out += '{' + ','.join([json.dumps(language), json.dumps(api), json.dumps(name),
                                       json.dumps(code), str(scenario), binary]) + '},\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    out += '};\n'
    for api in APIS:
        for name, result_type, arity in [('f32',0x7d,2),('arity',0x7c,1)]:
            out += f'static const unsigned char fft_bad_wasm_{api}_{name}[]={{' + ','.join(map(str,wasm(api,(0,-1),result_type,arity))) + '};\n'
    out += 'static const struct { const char *language,*api,*name,*source; const unsigned char *binary; unsigned size; } fft_invalid_cases[]={\n'
    invalid = [('missing',''),('null','null'),('string','"bad"'),('end-string','0,"bad"'),
               ('start-high','2147483648'),('start-low','-2147483649'),
               ('end-high','0,2147483648'),('end-low','0,-2147483649')]
    for api in APIS:
        for name, call in invalid:
            code = f'TIC=function\n tic80.pmem(63,1)\n tic80.{api}({call})\nend function\n'
            out += '{'+','.join(map(json.dumps,['miniscript',api,name,code]))+',NULL,0},\n'
        for name in ('f32','arity'):
            binary = f'fft_bad_wasm_{api}_{name}'
            out += '{'+','.join(map(json.dumps,['wasm',api,name,'// script: wasm\n']))+f',{binary},sizeof {binary}'+'},\n'
    args.output.write_text(out + '};\n')


if __name__ == '__main__':
    main()
