"""Generate WASM page-limit, growth and remap-growth modules with real instructions."""
import argparse
from pathlib import Path
from generate_api_bank_cases import leb

def module(minimum=4,maximum=None,imported=False):
    vec=lambda xs:leb(len(xs))+b''.join(xs)
    string=lambda s:leb(len(s))+s.encode()
    section=lambda tag,data:bytes([tag])+leb(len(data))+data
    limits=(b'\0'+leb(minimum)) if maximum is None else (b'\x01'+leb(minimum)+leb(maximum))
    binary=b'\0asm\x01\0\0\0'+section(1,vec([
        b'\x60\x0a'+b'\x7f'*10+b'\0',b'\x60\0\0',b'\x60\x01\x7f\x01\x7f',
        b'\x60\x04'+b'\x7f'*4+b'\0']))
    imports=[string('env')+string('map')+b'\0\0']
    if imported:imports.append(string('env')+string('memory')+b'\x02'+limits)
    binary+=section(2,vec(imports))+section(3,vec([b'\x01',b'\x02',b'\x03']))
    binary+=section(4,b'\x01\x70\0\x01')
    if not imported:binary+=section(5,b'\x01'+limits)
    binary+=section(7,vec([string('TIC')+b'\0\x01',string('grow')+b'\0\x02',string('map')+b'\0\0']))
    binary+=section(9,vec([b'\0\x41\0\x0b'+vec([b'\x03'])]))
    callback=b'\0\x41'+leb(1024,True)+b'\x20\0\x40\0\x36\x02\0'
    for offset,value,opcode in [(0,9,0x3a),(4,1,0x36),(8,2,0x36)]:
        callback+=b'\x20\x03\x41'+leb(value,True)+bytes([opcode,0])+leb(offset)
    bodies=[b'\0\x0b',b'\0\x20\0\x40\0\x0b',callback+b'\x0b']
    return binary+section(10,vec([leb(len(body))+body for body in bodies]))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    cases=[('defined2',2,None,False,True),('defined3',3,None,False,True),('defined4',4,None,False,True),
        ('limited2',2,2,False,True),('limited3',2,3,False,True),('limited4',2,4,False,True),
        ('wide_max',4,5,False,True),('too_small0',0,None,False,False),('too_small1',1,None,False,False),
        ('too_big5',5,None,False,False),('import0',0,None,True,True),('import2',2,None,True,True),
        ('import4',4,4,True,True),('import_wide',4,5,True,True),
        ('import_too_big',5,None,True,False),('import_bounded_three',2,3,True,True),
        ('import_fixed_two',2,2,True,True),('import_fixed_three',3,3,True,True),
        ('import_zero_fixed_two',0,2,True,True),('import_maximum_below_RAM',0,1,True,False)]
    text='/* Generated page declarations and real memory.grow remap callbacks. */\n'
    for name,minimum,maximum,imported,valid in cases:
        binary=module(minimum,maximum,imported)
        text+='static const unsigned char memory_'+name+'[]={'+','.join(map(str,binary))+'};\n'
    text+='static const struct { const char *name; const unsigned char *bytes; unsigned size,pages,maximum; int valid; } memory_cases[]={\n'
    for name,minimum,maximum,imported,valid in cases:
        text+='{"'+name+'",memory_'+name+',sizeof memory_'+name+','+str(min(4,maximum or 4) if imported else minimum)+','+str(min(4,maximum or 4))+','+str(int(valid))+'},\n'
    args.output.write_text(text+'};\n')
