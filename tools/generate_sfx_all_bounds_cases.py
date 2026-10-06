"""Effect/channel boundaries and stop calls in all fourteen runtimes."""
import argparse
import json
from pathlib import Path
from generate_sfx_bounds_cases import wasm
from generate_sfx_notes_cases import source as note_source

LANGUAGES = 'lua js moon yue fennel scheme squirrel python wren janet wasm ruby miniscript forth'.split()
CASES = [('id64',64,3),('idmax',2147483647,3),
         ('channel4',0,4),('channelneg1',0,-1),('channelmax',0,2147483647),
         ('last',63,3),('first',0,0),('stop',-1,3),
         ('stopneg2',-2,3),('stopmin',-2147483648,3),
         ('channelmin',0,-2147483648),('channel1',0,1),('channel2',0,2),
         ('bothinvalid',64,4),('stopchannel4',-1,4),('stopchannelneg1',-1,-1)]

def source(language,index,channel):
    if language == 'forth':
        return (': TIC 31 PMEM DUP 1+ 31 PMEM!\n'
                ' 0= IF 0 0 5 -1 3 15 0 SFX\n'
                f' ELSE {index} 0 5 -1 {channel} 15 0 SFX THEN ;\n')
    code = note_source(language,'unused')
    marker = 'sfx 0 "unused" -1 3 15 0' if language in ('fennel','scheme','janet') else 'sfx(0,"unused",-1,3,15,0)'
    call = f'sfx {index} 60 -1 {channel} 15 0' if language in ('fennel','scheme','janet') else f'sfx({index},60,-1,{channel},15,0)'
    assert code.count(marker) == 1
    return code.replace(marker,call)

def expected_error(language,index,channel):
    # Existing WASM imports deliberately ignore invalid channels; retain that
    # behavior, including negative-ID stop calls. Python checks channel first.
    bad_channel = channel < 0 or channel >= 4
    if language == 'python' and bad_channel: return 2
    if index >= 64: return 1
    return 2 if bad_channel and language != 'wasm' else 0

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out='/* Generated SFX bounds carts; the C audio reference is independent. */\n'
    for n,(_,index,channel) in enumerate(CASES):
        out+=f'static const unsigned char all_bounds_wasm_{n}[]={{'+','.join(map(str,wasm(index,channel)))+'};\n'
    out+='static const struct { const char *name,*language,*source; int index,channel,error; const unsigned char *binary; unsigned size; } all_bounds_cases[]={\n'
    for language in LANGUAGES:
        for n,(name,index,channel) in enumerate(CASES):
            code='// script: wasm\n' if language=='wasm' else source(language,index,channel)
            binary=f'all_bounds_wasm_{n},sizeof all_bounds_wasm_{n}' if language=='wasm' else 'NULL,0'
            out+='{'+','.join([json.dumps(language+'-'+name),json.dumps(language),json.dumps(code),str(index),str(channel),str(expected_error(language,index,channel)),binary])+'},\n'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(out+'};\n')

if __name__=='__main__': main()
