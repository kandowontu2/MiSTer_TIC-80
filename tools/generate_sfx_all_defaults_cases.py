"""Stored pitch, omitted speed and explicit overrides in fourteen runtimes."""
import argparse,json
from pathlib import Path
from generate_api_bank_cases import LANGUAGES,leb

CALLS=[('zero',(0,)),('one',(1,)),('last',(63,)),
       ('zero-duration',(0,None,6)),('last-duration',(63,None,6)),
       ('zero-note',(0,48)),('zero-speed',(0,None,-1,2,15,0)),
       ('zero-explicit',(0,60,8,1,5,3)),('zero-stereo',(0,None,7,3,(0,15))),
       ('stop',(-1,)),('zero-sentinel',(0,-1)),('last-sentinel',(63,-1)),
       ('zero-negative-speed',(0,-1,8,1,15,-1)),('one-negative-speed',(1,-1,8,1,15,-1)),
       ('one-explicit',(1,61,7,2,3,-4)),('zero-low-note',(0,0,7,2,10)),
       ('zero-preset-speed',(0,-1,8,1,15,8))]
SCALAR_VOLUME={'python','janet','forth'}

def typed(args):
    a=list(args)+[None]*(6-len(args)); index,note,duration,channel,volume,speed=a
    pitch=(-1,-1) if note is None or note==-1 else (note%12,note//12)
    left,right=volume if isinstance(volume,tuple) else (15 if volume is None else volume,)*2
    return [index,*pitch,-1 if duration is None else duration,0 if channel is None else channel,left,right,0 if speed is None else speed]

def arguments(language,args):
    if language in SCALAR_VOLUME and len(args)>4 and isinstance(args[4],tuple): args=(*args[:4],5,*args[5:])
    def value(x):
        if x is None: return {'python':'None','ruby':'nil','lua':'nil','moon':'nil','yue':'nil','fennel':'nil','scheme':'#f','janet':'nil'}.get(language,'null')
        if isinstance(x,tuple):
            return '{0,15}' if language in ('lua','moon','yue') else '[0 15]' if language=='fennel' else '(list 0 15)' if language=='scheme' else '[0,15]'
        return str(x)
    if language=='forth':
        a=typed(args); return ' '.join(map(str,[*a[:6],a[7]]))+' SFX'
    prefix={'scheme':'t80::','janet':'tic80/','wren':'TIC.','miniscript':'tic80.'}.get(language,'')
    return '('+prefix+'sfx '+' '.join(map(value,args))+')' if language in ('fennel','scheme','janet') else prefix+'sfx('+','.join(map(value,args))+')'

def source(language,name,args):
    calls={0:[arguments(language,(0,) if name=='stop' else args)],12:[arguments(language,(-1,0,0,c,0,0)) for c in range(4)]}
    if name=='stop': calls[2]=[arguments(language,args)]
    prefix={'scheme':'t80::','janet':'tic80/','wren':'TIC.','miniscript':'tic80.'}.get(language,'')
    if language=='forth':
        out=': TIC 31 PMEM\n'
        for frame,ops in sorted(calls.items()): out+=f' DUP {frame} = IF '+' '.join(ops)+' THEN\n'
        return out+' 1+ 31 PMEM! ;\n'
    if language in ('fennel','scheme','janet'):
        start={'fennel':'(fn _G.TIC [] (let [n (pmem 31)]\n','scheme':'(define (TIC) (let ((n (t80::pmem 31)))\n','janet':'(import tic80)\n(defn TIC [] (def n (tic80/pmem 31))\n'}[language]
        return start+''.join(f' (when (= n {n}) '+' '.join(ops)+')\n' for n,ops in sorted(calls.items()))+f' ({prefix}pmem 31 (+ n 1))'+(')\n' if language=='janet' else '))\n')
    starts={'lua':'function TIC()\n','js':'function TIC(){\n','squirrel':'function TIC(){\n','moon':'export TIC\nTIC = ->\n','yue':'global TIC = ->\n','python':'def TIC():\n','ruby':'def TIC\n','miniscript':'TIC=function\n','wren':'class Game is TIC {\n construct new() {}\n TIC() {\n'}
    declaration='var ' if language in ('js','wren') else 'local ' if language in ('lua','squirrel') else ''
    out=starts[language]+f' {declaration}n={prefix}pmem(31)\n'
    for frame,ops in sorted(calls.items()):
        if language in ('js','squirrel','wren'): out+=f' if(n=={frame}) {{\n'+'\n'.join('  '+op for op in ops)+'\n }\n'
        elif language in ('lua','miniscript'): out+=f' if n=={frame} then\n'+'\n'.join('  '+op for op in ops)+'\n '+('end' if language=='lua' else 'end if')+'\n'
        else: out+=f' if n=={frame}'+(':' if language=='python' else '')+'\n'+'\n'.join('  '+op for op in ops)+'\n'+(' end\n' if language=='ruby' else '')
    out+=f' {prefix}pmem(31,n+1)\n'
    return out+{'lua':'end\n','ruby':'end\n','miniscript':'end function\n','js':'}\n','squirrel':'}\n','wren':' }\n}\n'}.get(language,'')

def wasm(name,args):
    vec=lambda xs:leb(len(xs))+b''.join(xs)
    string=lambda s:leb(len(s))+s.encode()
    section=lambda n,data:bytes([n])+leb(len(data))+data
    types=[([0x7f]*8,[]),([0x7f,0x7e],[0x7f]),([],[])]
    out=b'\0asm\x01\0\0\0'+section(1,vec([b'\x60'+leb(len(p))+bytes(p)+leb(len(r))+bytes(r) for p,r in types]))
    out+=section(2,vec([string('env')+string(n)+b'\0'+leb(i) for i,n in enumerate(('sfx','pmem'))]))
    out+=section(3,b'\x01\x02')+section(5,b'\x01\0\x04')+section(7,vec([string('TIC')+b'\0\x02']))
    integer=lambda n:b'\x41'+leb(n,True)
    phases={0:[typed((0,) if name=='stop' else args)],12:[typed((-1,0,0,c,0,0)) for c in range(4)]}
    if name=='stop': phases[2]=[typed(args)]
    body=b'\x01\x01\x7f'+integer(31)+b'\x42\x7f\x10\x01\x21\0'
    for frame,ops in sorted(phases.items()):
        body+=b'\x20\0'+integer(frame)+b'\x46\x04\x40'
        body+=b''.join(b''.join(integer(a) for a in op)+b'\x10\0' for op in ops)+b'\x0b'
    body+=integer(31)+b'\x20\0'+integer(1)+b'\x6a\xad\x10\x01\x1a\x0b'
    return out+section(10,vec([leb(len(body))+body]))

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    out='/* Generated default/override calls; expectations are independent in C. */\n'
    for n,(name,call) in enumerate(CALLS): out+=f'static const unsigned char all_defaults_wasm_{n}[]={{'+','.join(map(str,wasm(name,call)))+'};\n'
    out+='static const struct { const char *language,*name,*source; unsigned scenario; const unsigned char *binary; unsigned size; } all_defaults_cases[]={\n'
    for language in LANGUAGES:
        for scenario,(name,call) in enumerate(CALLS):
            test_name='zero-volume' if name=='zero-stereo' and language in SCALAR_VOLUME else name
            code='// script: wasm\n' if language=='wasm' else source(language,name,call)
            binary=f'all_defaults_wasm_{scenario},sizeof all_defaults_wasm_{scenario}' if language=='wasm' else 'NULL,0'
            out+='{'+','.join([json.dumps(language),json.dumps(test_name),json.dumps(code),str(scenario),binary])+'},\n'
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(out+'};\n')
if __name__=='__main__': main()
