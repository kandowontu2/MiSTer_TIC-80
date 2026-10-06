"""Export graphics/text pointer imports with real WASM callback-table entries."""
import argparse
import struct
from pathlib import Path
from generate_api_bank_cases import leb

def module(tick=None):
    funcs=[('trace',[0x7f]*2,[]),('print',[0x7f]*7,[0x7f]),('font',[0x7f]*10,[0x7f]),
           ('spr',[0x7f]*10,[]),('map',[0x7f]*10,[]),('ttri',[0x7d]*12+[0x7f]*3+[0x7d]*3+[0x7f],[])]
    vec=lambda xs:leb(len(xs))+b''.join(xs)
    string=lambda s:leb(len(s))+s.encode()
    section=lambda tag,data:bytes([tag])+leb(len(data))+data
    types=[(p,r) for _,p,r in funcs]+[([],[]),([0x7f]*4,[]),([0x7c]*4,[]),([0x7f]*4,[0x7f])]
    out=b'\0asm\x01\0\0\0'+section(1,vec([b'\x60'+leb(len(p))+bytes(p)+leb(len(r))+bytes(r) for p,r in types]))
    out+=section(2,vec([string('env')+string(name)+b'\0'+leb(n) for n,(name,_,_) in enumerate(funcs)]))
    out+=section(3,vec([leb(6),leb(7),leb(8),leb(9)]))
    out+=section(4,b'\x01\x70\0\x05')+section(5,b'\x01\0\x04')
    out+=section(7,vec([string(name)+b'\0'+leb(n) for n,(name,_,_) in enumerate(funcs)]+[string('TIC')+b'\0\x06']))
    out+=section(9,vec([b'\0\x41\0\x0b'+vec([leb(7),leb(8),leb(6),leb(9)])]))
    # Callback writes the public 12-byte remap result at its possibly unaligned destination.
    callback=b'\0'
    for offset,value,opcode in [(0,9,0x3a),(4,1,0x36),(8,2,0x36)]:
        callback+=b'\x20\x03\x41'+leb(value,True)+bytes([opcode,0])+leb(offset)
    callback+=b'\x0b'
    fault_arguments={
        'trace':[-1,15], 'print':[-1,20,20,15,1,1,0],
        'font':[1024,20,20,-1,1,8,8,1,1,0], 'spr':[0,20,20,-1,1,1,0,0,1,1],
        'map':[0,0,1,1,20,20,0,0,1,-1], 'ttri':[0.]*12+[0,-1,1]+[0.]*3+[0],
        'good':[1024,15]}
    tic=b'\0'
    if tick is not None:
        name='trace' if tick=='good' else tick
        index=next(n for n,f in enumerate(funcs) if f[0]==name)
        for typ,value in zip(funcs[index][1],fault_arguments[tick]):
            tic+=b'\x43'+struct.pack('<f',value) if typ==0x7d else b'\x41'+leb(value,True)
        tic+=b'\x10'+leb(index)
        if funcs[index][2]:tic+=b'\x1a'
    bodies=[tic+b'\x0b',callback,b'\0\x0b',b'\0\x41\0\x0b']
    return out+section(10,vec([leb(len(body))+body for body in bodies]))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    text='/* Actual exported imports, remap callbacks and supervised fault cartridges. */\n'
    for name,tick in [('wasm_pointer_case',None),('wasm_pointer_good','good')]+[('wasm_pointer_'+n,n) for n in ('trace','print','font','spr','map','ttri')]:
        text+='static const unsigned char '+name+'[]={'+','.join(map(str,module(tick)))+'};\n'
    text+='static const struct { const char *name; const unsigned char *bytes; unsigned size; } pointer_fault_cases[]={\n'
    for name in ('trace','print','font','spr','map','ttri'):
        text+='{"'+name+'",wasm_pointer_'+name+',sizeof wasm_pointer_'+name+'},\n'
    args.output.write_text(text+'};\n')
