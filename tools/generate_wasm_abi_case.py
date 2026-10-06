"""Export the actual byte-result imports for a reused-slot WASM ABI contract."""
import argparse
from pathlib import Path
from generate_api_bank_cases import leb

def module():
    funcs=[('peek',[0x7f,0x7f]),('peek4',[0x7f]),('peek2',[0x7f]),
           ('peek1',[0x7f]),('pix',[0x7f]*3),('vbank',[0x7f]),('pmem',[0x7f,0x7e])]
    vector=lambda xs: leb(len(xs))+b''.join(xs)
    string=lambda s: leb(len(s))+s.encode()
    section=lambda n,data: bytes([n])+leb(len(data))+data
    data=b'\0asm\x01\0\0\0'
    data+=section(1,vector([b'\x60'+leb(len(args))+bytes(args)+b'\x01\x7f' for _,args in funcs]+[b'\x60\0\0']))
    data+=section(2,vector([string('env')+string(name)+b'\0'+leb(i) for i,(name,_) in enumerate(funcs)]))
    probes=[('peek',[0x4000,8]),('peek',[0x4000*2+1,4]),('peek4',[0x4000*2+1]),
            ('peek2',[0x4000*4+3]),('peek1',[0x4000*8+7]),('pix',[3,4,-1]),
            ('pix',[3,4,6]),('vbank',[-1]),('vbank',[1]),('vbank',[-1])]
    data+=section(3,vector([leb(len(funcs))]+[b'\x01']*len(probes)))
    data+=section(5,b'\x01\0\x04')
    data+=section(7,vector([string(name)+b'\0'+leb(i) for i,(name,_) in enumerate(funcs)]+[string('TIC')+b'\0'+leb(len(funcs))]))
    ids={name:i for i,(name,_) in enumerate(funcs)}
    const=lambda n: b'\x41'+leb(n,True)
    call=lambda name,args: b''.join(const(a) for a in args)+b'\x10'+leb(ids[name])
    tick=b'\0'+call('vbank',[0])+b'\x1a'+call('pix',[3,4,13])+b'\x1a'
    bodies=[]
    for i,(name,args) in enumerate(probes):
        # At the same Wasm call depth, a full-width pmem result is followed by
        # the byte-result import. Each probe returns that import's i32 unchanged.
        body=b'\0'+const(255)+b'\x20\0\xad\x10'+leb(ids['pmem'])+b'\x1a'
        body+=const(255)+b'\x42'+leb(-1,True)+b'\x10'+leb(ids['pmem'])+b'\x1a'
        body+=call(name,args)+b'\x0b'
        bodies.append(body)
        tick+=const(i)+const(0x7ead0080+i*256)+b'\x10'+leb(len(funcs)+1+i)+b'\xad\x10'+leb(ids['pmem'])+b'\x1a'
    tick+=b'\x0b'
    data+=section(10,vector([leb(len(body))+body for body in [tick,*bodies]]))
    return data

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('/* Generated WASM fixture: imported functions exported unchanged. */\n'
        'static const unsigned char wasm_abi_case[]={'+','.join(map(str,module()))+'};\n')
