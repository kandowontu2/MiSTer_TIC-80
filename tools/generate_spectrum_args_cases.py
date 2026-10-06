"""Exercise spectrum integer coercion and errors in the repaired bindings."""
import argparse
import json
from pathlib import Path
from generate_vqt_cases import APIS as VQT_APIS, source as vqt_source
from generate_fft_cases import APIS as FFT_APIS, source as fft_source

LANGUAGES = 'lua moon yue fennel squirrel wren js'.split()
APIS = (*VQT_APIS, *FFT_APIS)


def token(language, name):
    values = {'integer':'5','fraction':'5.75','negative-fraction':'-5.75',
              'maximum':'2147483647','minimum':'-2147483648',
              'upper-fraction':'2147483647.75','lower-fraction':'-2147483648.75',
              'null':'null','true':'true','numeric-string':'"5.75"','bad-string':'"bad"',
              'nan':'NaN','inf':'Infinity','negative-inf':'-Infinity',
              'wrap-high':'4294967301','wrap-low':'-4294967301',
              'high':'2147483648.0','low':'-2147483649.0','huge':'1e300','negative-huge':'-1e300',
              'list':'[]','throw':'({valueOf:function(){throw new Error("bad spectrum value")}})',
              'symbol':'Symbol("spectrum")'}
    result = values[name]
    if language in ('lua','moon','yue','fennel'):
        result = {'null':'nil','nan':'math.sqrt(-1)','inf':'math.huge','negative-inf':'-math.huge'}.get(name,result)
        if language=='fennel': result={'nan':'(math.sqrt -1)','negative-inf':'(- math.huge)'}.get(name,result)
    elif language=='wren':
        result={'nan':'(0/0)','inf':'(1/0)','negative-inf':'(-1/0)'}.get(name,result)
    elif language=='squirrel':
        result={'nan':'sqrt(-1)','inf':'(1.0/0.0)','negative-inf':'(-1.0/0.0)',
                'upper-fraction':'2147483520.0','lower-fraction':'-2147483520.0',
                'low':'-2147483904.0'}.get(name,result)
    return result


def program(language, api, args, invalid):
    code = fft_source(language,api,args) if api in FFT_APIS else vqt_source(language,api,(' ' if language=='fennel' else ',').join(args))
    if invalid:
        marker='(pmem 63 1) ' if language=='fennel' else 'pmem(63,1)\n '
        needle='(pmem 0 ' if language=='fennel' else 'TIC.pmem(0,' if language=='wren' else 'pmem(0,'
        if language=='wren': marker='TIC.pmem(63,1)\n '
        assert needle in code; code=code.replace(needle,marker+needle,1)
    return code


def valid(language, api):
    names=['integer','fraction','negative-fraction','maximum','minimum','upper-fraction','lower-fraction']
    if language!='wren': names += ['null','true','numeric-string','bad-string']
    if language=='js': names += ['nan','inf','negative-inf']
    if language in ('js','squirrel'): names += ['wrap-high','wrap-low']
    result=[(name,[token(language,name)]) for name in names]
    if language!='wren': result.append(('extra',['5','9','7'] if api in FFT_APIS else ['5','9']))
    if language=='js': result.append(('missing',[]))
    if api in FFT_APIS:
        if language!='wren': result.append(('end-null',['5',token(language,'null')]))
        result.append(('end-fraction',['5','7.75']))
        if language=='js': result.append(('end-undefined',['5','undefined']))
    return result


def invalid(language, api):
    result=[]
    if language!='js': result += [(name,[token(language,name)]) for name in ('nan','inf','negative-inf','high','low','huge','negative-huge')]
    if language=='wren': result += [('wrong-'+name,[token(language,name)]) for name in ('null','true','numeric-string','list')]
    if language!='js': result.append(('missing',[]))
    if language=='js': result += [('throw',[token(language,'throw')]),('symbol',[token(language,'symbol')])]
    if api in FFT_APIS:
        if language!='js': result += [('end-'+name,['5',token(language,name)]) for name in ('nan','high','inf')]
        if language=='js': result += [('end-'+name,['5',token(language,name)]) for name in ('throw','symbol')]
        if language=='wren': result.append(('end-wrong-type',['5','null']))
    if language=='wren': result.append(('extra',['5','9','7'] if api in FFT_APIS else ['5','9']))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    out='/* Generated calls; independent coercion and coverage expectations in C. */\n'
    for label,producer in [('valid',valid),('invalid',invalid)]:
        out+=f'static const struct {{ const char *language,*api,*name,*source; }} spectrum_{label}_cases[]={{\n'
        for language in LANGUAGES:
            for api in APIS:
                for name,values in producer(language,api):
                    out+='{'+','.join(map(json.dumps,(language,api,name,program(language,api,values,label=='invalid'))))+'},\n'
        out+='};\n'
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(out.encode())


if __name__=='__main__': main()
