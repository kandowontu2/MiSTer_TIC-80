"""SFX argument boundaries for Janet and the WASM import ABI."""
import argparse,json
from pathlib import Path
from generate_api_bank_cases import leb

CASES=[('id64',64,3,1),('idmax',2147483647,3,1),
       ('channel4',0,4,2),('channelneg1',0,-1,2),('channelmax',0,2147483647,2),
       ('last',63,3,0),('first',0,0,0),('stop',-1,3,0),
       ('stopneg2',-2,3,0),('stopmin',-2147483648,3,0)]

def wasm(index,channel):
    vec=lambda xs:leb(len(xs))+b''.join(xs)
    string=lambda s:leb(len(s))+s.encode()
    section=lambda n,data:bytes([n])+leb(len(data))+data
    types=[([0x7f]*8,[]),([0x7f,0x7e],[0x7f]),([],[])]
    out=b'\0asm\x01\0\0\0'+section(1,vec([b'\x60'+leb(len(p))+bytes(p)+leb(len(r))+bytes(r) for p,r in types]))
    out+=section(2,vec([string('env')+string(name)+b'\0'+leb(i) for i,name in enumerate(('sfx','pmem'))]))
    out+=section(3,b'\x01\x02')+section(5,b'\x01\0\x04')+section(7,vec([string('TIC')+b'\0\x02']))
    integer=lambda n:b'\x41'+leb(n,True)
    call=lambda i,args:b''.join(integer(a) for a in args)+b'\x10'+leb(i)
    body=b'\x01\x01\x7f'+integer(31)+b'\x42\x7f\x10\x01\x21\0'
    body+=integer(31)+b'\x20\0'+integer(1)+b'\x6a\xad\x10\x01\x1a'
    body+=b'\x20\0\x45\x04\x40'+call(0,[0,0,5,-1,3,15,15,0])
    body+=b'\x05'+call(0,[index,0,5,-1,channel,15,15,0])+b'\x0b\x0b'
    return out+section(10,vec([leb(len(body))+body]))

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); out='/* Generated SFX bounds corpus. */\n'
    for n,(name,index,channel,error) in enumerate(CASES):
        out+='static const unsigned char bounds_wasm_'+str(n)+'[]={'+','.join(map(str,wasm(index,channel)))+'};\n'
    out+='typedef struct { const char *name,*language,*source; int index,channel,error; const unsigned char *binary; unsigned size; } tm_sfx_bounds_case;\nstatic const tm_sfx_bounds_case bounds_cases[]={\n'
    for n,(name,index,channel,error) in enumerate(CASES):
        code='(import tic80)\n(defn TIC [] (def n (tic80/pmem 31)) (tic80/pmem 31 (+ n 1))\n (if (= n 0) (tic80/sfx 0 60 -1 3 15 0) (tic80/sfx '+str(index)+' 60 -1 '+str(channel)+' 15 0)))\n'
        out+='{"janet-'+name+'","janet",'+json.dumps(code)+f',{index},{channel},{error},NULL,0'+'},\n'
        # The existing WASM channel check silently ignores invalid channels.
        # Keep that behavior; an invalid nonnegative effect ID must trap.
        out+='{"wasm-'+name+'","wasm","// script: wasm\\n",'+f'{index},{channel},{1 if error==1 else 0},bounds_wasm_{n},sizeof bounds_wasm_{n}'+'},\n'
    out+='};\n'; args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(out)

if __name__=='__main__': main()
