"""Equivalent string-note and error-preservation carts in twelve runtimes."""
import argparse
import json
from pathlib import Path

LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet ruby miniscript'.split()
CASES = [(name.replace('#', 'sharp').replace('-', 'natural'), name+'4', 0)
         for name in ('C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-')]
CASES += [('octave0', 'C#0', 0), ('octave1', 'C-1', 0), ('octave8', 'B-8', 0),
          ('unknown', 'Z-4', 1), ('unsupported-sharp', 'E#4', 1), ('flat', 'Cb4', 1),
          ('octave9', 'C-9', 1), ('octave-letter', 'C-x', 1), ('lowercase', 'c-4', 1),
          ('empty', '', 1), ('short', 'C-', 1), ('long', 'C-10', 1)]

def source(language, text):
    prefix = {'scheme':'t80::', 'janet':'tic80/', 'wren':'TIC.', 'miniscript':'tic80.'}.get(language, '')
    quoted = json.dumps(text)
    if language in ('fennel', 'scheme', 'janet'):
        start = {'fennel':'(fn _G.TIC [] (let [n (pmem 31)]\n',
                 'scheme':'(define (TIC) (let ((n (t80::pmem 31)))\n',
                 'janet':'(import tic80)\n(defn TIC [] (def n (tic80/pmem 31))\n'}[language]
        return start+f' ({prefix}pmem 31 (+ n 1))\n (if (= n 0) ({prefix}sfx 0 60 -1 3 15 0) ({prefix}sfx 0 {quoted} -1 3 15 0))\n'+(')\n' if language=='janet' else '))\n')
    starts={'lua':'function TIC()\n','js':'function TIC(){\n','squirrel':'function TIC(){\n',
            'moon':'export TIC\nTIC = ->\n','yue':'global TIC = ->\n','python':'def TIC():\n',
            'ruby':'def TIC\n','miniscript':'TIC=function\n','wren':'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    declaration='var ' if language in ('js','wren') else 'local ' if language in ('lua','squirrel') else ''
    out=starts[language]+f' {declaration}n={prefix}pmem(31)\n {prefix}pmem(31,n+1)\n'
    warm=f'{prefix}sfx(0,60,-1,3,15,0)'; call=f'{prefix}sfx(0,{quoted},-1,3,15,0)'
    if language in ('js','squirrel','wren'): out+=f' if(n==0) {{ {warm} }} else {{ {call} }}\n'
    elif language in ('lua','miniscript'): out+=f' if n==0 then\n  {warm}\n else\n  {call}\n '+('end if' if language=='miniscript' else 'end')+'\n'
    else: out+=f' if n==0'+(':' if language=='python' else '')+f'\n  {warm}\n else'+(':' if language=='python' else '')+f'\n  {call}\n'+(' end\n' if language=='ruby' else '')
    return out+{'lua':'end\n','ruby':'end\n','miniscript':'end function\n','js':'}\n','squirrel':'}\n','wren':' }\n}\n'}.get(language,'')

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); out='/* Generated note carts; engine expectations are independent in the C fixture. */\n'
    out+='static const struct { const char *name,*language,*source; unsigned scenario,error; } note_cases[]={\n'
    for language in LANGUAGES:
        for scenario,(name,text,error) in enumerate(CASES):
            out+='{'+','.join([json.dumps(language+'-'+name),json.dumps(language),json.dumps(source(language,text)),str(scenario),str(error)])+'},\n'
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(out+'};\n')

if __name__=='__main__': main()
