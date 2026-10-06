"""Native-word arities from the documented TIC Forth stack conventions."""
import argparse,json
from pathlib import Path
WORDS=[
 ('CLS',1,0),('PRINT',8,1),('PIX',2,1),('PIX!',3,0),('LINE',5,0),
 ('RECT',5,0),('RECTB',5,0),('SPR',9,0),('BTN',1,1),('BTNP',3,1),
 ('SFX',7,0),('MAP',8,0),('MGET',2,1),('MSET',3,0),('PEEK',2,1),
 ('POKE',3,0),('PEEK1',1,1),('POKE1',2,0),('PEEK2',1,1),('POKE2',2,0),
 ('PEEK4',1,1),('POKE4',2,0),('MEMCPY',3,0),('MEMSET',3,0),('TRACE',3,0),
 ('PMEM',1,1),('PMEM!',2,0),('TIME',0,1),('TSTAMP',0,1),('EXITGAME',0,0),
 ('FONT',10,1),('MOUSE',0,7),('CIRC',4,0),('CIRCB',4,0),('ELLI',5,0),
 ('ELLIB',5,0),('PAINT',4,0),('TRI',7,0),('TRIB',7,0),('TTRI',18,0),
 ('CLIP',4,0),('CLIP0',0,0),('MUSIC',7,0),('SYNC',3,0),('VBANK',1,1),
 ('RESET',0,0),('KEYPRESSED',1,1),('KEYP',3,1),('FGET',2,1),('FSET',3,0),
 ('FFT',2,1),('FFTS',2,1),('FFTR',2,1),('FFTRS',2,1),
 ('VQT',1,1),('VQTS',1,1),('VQTR',1,1),('VQTRS',1,1),
 ('VQTW',1,1),('VQTSW',1,1),('VQTRW',1,1),('VQTRSW',1,1)]
def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--output',type=Path,required=True); args=p.parse_args()
    out='/* Generated programs; independent native depth/output oracle in C. */\nstatic const struct { const char* name; unsigned word,kind,provided; const char* source; } forth_stack_cases[]={\n'
    counts=[0,0,0,0]
    for index,(word,required,outputs) in enumerate(WORDS):
        for kind in range(4):
            for provided in ([required] if kind<2 else range(required)):
                prefix='91 92 ' if kind==1 else ''
                invoke='0 '*provided+word
                if kind==3:
                    code=f': BAD {invoke} ;\n: TIC {index} 249 PMEM! [\'] BAD CATCH 248 PMEM! 7 244 PMEM! ;\n'
                else:
                    tail='DROP '*outputs
                    if kind==1: tail+='246 PMEM! 245 PMEM! '
                    code=f': TIC {index} 249 PMEM! {prefix}{invoke} {tail}7 244 PMEM! ;\n'
                name=f'{word}-'+['exact','surplus','unhandled','caught'][kind]+f'-{provided}'
                out+='{'+json.dumps(name)+f',{index},{kind},{provided},'+json.dumps(code)+'},\n'; counts[kind]+=1
    out+='};\n'; args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(out.encode()); print('Forth stack corpus counts '+str(counts))
if __name__=='__main__': main()
