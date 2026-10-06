"""Generate supported memory types and malformed/incompatible declaration cases."""
import argparse
from pathlib import Path
from generate_wasm_memory_case import module

def cases():
    bounded=module(2,4,False)
    anchor=b'\x05\x04\x01\x01\x02\x04'
    assert bounded.count(anchor)==1
    replace=lambda body:bounded.replace(anchor,b'\x05'+bytes([len(body)])+body)
    return [
        ('import_without_max',module(0,None,True),True,4),
        ('import_fixed_four',module(4,4,True),True,4),
        ('import_fixed_two',module(2,2,True),True,2),
        ('import_fixed_three',module(3,3,True),True,3),
        ('import_bounded_three',module(2,3,True),True,3),
        ('import_zero_fixed_two',module(0,2,True),True,2),
        ('defined_fixed_two',module(2,2,False),True,2),
        ('defined_wasm32_max',module(2,65536,False),True,2),
        ('import_zero_max',module(0,0,True),False,0),
        ('import_maximum_below_RAM',module(0,1,True),False,0),
        ('minimum_exceeds_zero_max',module(2,0,False),False,0),
        ('minimum_exceeds_nonzero_max',module(2,1,False),False,0),
        ('maximum_exceeds_wasm32',module(2,65537,False),False,0),
        ('import_maximum_exceeds_wasm32',module(0,65537,True),False,0),
        ('truncated_maximum',replace(b'\x01\x01\x02'),False,0),
        ('truncated_minimum',replace(b'\x01\x00\x80'),False,0),
        ('truncated_flag',replace(b'\x01'),False,0),
        ('unsupported_shared_without_max',replace(b'\x01\x02\x02\x04'),False,0),
        ('unsupported_shared_with_max',replace(b'\x01\x03\x02\x04'),False,0),
        ('unsupported_memory64',replace(b'\x01\x04\x02\x04'),False,0),
        ('illegal_limit_flag',replace(b'\x01\x7f\x02\x04'),False,0),
    ]

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    text='/* Generated supported and rejected WASM memory types. */\n'
    entries=cases()
    for name,blob,valid,pages in entries:
        text+='static const unsigned char declaration_'+name+'[]={'+','.join(map(str,blob))+'};\n'
    text+='static const struct {const char *name; const unsigned char *bytes; unsigned size,pages; int valid;} declaration_cases[]={\n'
    for name,blob,valid,pages in entries:
        text+='{"'+name+'",declaration_'+name+',sizeof declaration_'+name+','+str(pages)+','+str(int(valid))+'},\n'
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text+'};\n')
