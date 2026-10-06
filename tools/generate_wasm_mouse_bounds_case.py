"""Generate exported real mouse imports and safe/faulting TIC functions."""
import argparse
from pathlib import Path
from generate_api_bank_cases import leb


def module(pointer=None):
    vec=lambda xs:leb(len(xs))+b''.join(xs)
    string=lambda s:leb(len(s))+s.encode()
    section=lambda tag,data:bytes([tag])+leb(len(data))+data
    data=b'\0asm\x01\0\0\0'
    data+=section(1,vec([b'\x60\x01\x7f\0',b'\x60\x02\x7f\x7e\x01\x7f',b'\x60\0\0']))
    data+=section(2,vec([string('env')+string('mouse')+b'\0\0',string('env')+string('pmem')+b'\0\x01']))
    data+=section(3,vec([b'\x02']))+section(5,b'\x01\0\x04')
    data+=section(7,vec([string('mouse')+b'\0\0',string('TIC')+b'\0\x02']))
    body=b'\0'
    if pointer is not None:body+=b'\x41'+leb(pointer,True)+b'\x10\0'
    body+=b'\x41\0\x42'+leb(123,True)+b'\x10\x01\x1a\x0b'
    return data+section(10,vec([leb(len(body))+body]))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    text='/* Actual linked mouse import; pointer bounds and expected bytes live in C. */\n'
    for name,pointer in [('wasm_mouse_bounds',None),('wasm_mouse_valid',1025),('wasm_mouse_invalid',-1)]:
        text+='static const unsigned char '+name+'[]={'+','.join(map(str,module(pointer)))+'};\n'
    args.output.write_text(text)
