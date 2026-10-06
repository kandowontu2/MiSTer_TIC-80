"""Independently review completed raw hour-soak evidence; never access hardware."""
import json
from pathlib import Path
import struct
import zlib

from analyze_audio_queue import analyze as analyze_queue
from analyze_video_clock import analyze as analyze_alignment
from analyze_hdmi_clock import analyze as analyze_hdmi


def read_json(path):
    return json.loads(Path(path).read_text())


def rows(path):
    return [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]


def probe(directory, label, recorded):
    raw=rows(Path(directory)/(label+'-audio.jsonl'))
    assert len(raw)==recorded['samples'] and raw[0]==recorded['first'] and raw[-1]==recorded['last']
    assert len(raw)>30 and len({s['session'] for s in raw})==1
    assert all(s['underruns']==0 for s in raw) and recorded['underruns']==0
    assert ((raw[-1]['played']-raw[0]['played'])&0xffffffff)>90000
    assert ((raw[-1]['heartbeat']-raw[0]['heartbeat'])&0xffffffff)>100
    return raw[0]['session']


def hdmi(directory, label, recorded):
    lines=(Path(directory)/(label+'-hdmi.log')).read_text().splitlines()
    assert len(lines)==52
    actual=analyze_hdmi([dict(registers=dict(line.split() for line in lines[n:n+13]))
                        for n in range(0,len(lines),13)])
    assert actual==recorded and actual['maximum_error_ppm']<=1000
    return actual


def phase(directory, result, mode):
    directory=Path(directory)
    assert mode in ('studio','player')
    p=result['sustained_frontends'][mode]
    assert p['passed']
    jobs=[j for j in result['jobs'] if j['mode'] in (mode,mode+'-memory')]
    assert len(jobs)==2 and {j['mode'] for j in jobs}=={mode,mode+'-memory'}
    for job in jobs:
        assert job['dispatched'] and job['started_once'] and job['collected'] and job['status']=='0'
        assert (directory/(job['mode']+'-job.status')).read_text().strip()=='0'
    samples=rows(directory/(mode+'-samples.jsonl'))
    assert samples==rows(directory/(mode+'-job.log'))
    assert len(samples)==p['samples'] and len(samples)>=14400
    assert len({s['session'] for s in samples})==1 and samples[0]['session']>0
    assert all(s['underruns']==0 for s in samples)
    assert all(abs(((s['slots']-s['underruns'])&0xffffffff)-s['played'])<=2 for s in samples)
    first,last=samples[0],samples[-1]
    seconds=(last['elapsed_ns']-first['elapsed_ns'])/1e9
    assert seconds>=3599 and seconds==p['seconds']
    gaps=[(y['elapsed_ns']-x['elapsed_ns'])/1e9 for x,y in zip(samples,samples[1:])]
    assert all(gap>0 for gap in gaps)
    audio=((last['slots']-first['slots'])&0xffffffff)/seconds
    game=(((last['presented']>>2)-(first['presented']>>2))&0x3fffffff)/seconds
    assert audio==p['audio_clock_hz'] and game==p['game_clock_hz']
    assert abs(audio-48000)<24 and abs(game-60)<.1
    assert p['underruns']==0
    queue=analyze_queue((directory/(mode+'-samples.jsonl')).read_bytes())
    assert queue==p['queue'] and abs(queue['fitted_queue_change_ms'])<=2
    alignment=analyze_alignment(samples)
    assert alignment==p['alignment']
    raw_memory=rows(directory/(mode+'-memory-job.log'))
    memory=[dict(point,seconds=point['elapsed_ns']/1e9) for point in raw_memory]
    assert memory==p['memory']==read_json(directory/(mode+'-memory.json'))
    assert len(memory)>=120 and memory[0]['seconds']<5 and memory[-1]['seconds']>=3570
    elapsed=[y['seconds']-x['seconds'] for x,y in zip(memory,memory[1:])]
    captures=[(y['captured_monotonic_ns']-x['captured_monotonic_ns'])/1e9 for x,y in zip(memory,memory[1:])]
    assert all(0<gap<=40 for gap in elapsed+captures)
    coverage=p['memory_checkpoint_coverage']
    assert coverage['passed'] and coverage['count']==len(memory)
    assert coverage['sampling_on_remote_host'] and coverage['clock']=='MiSTer CLOCK_MONOTONIC'
    assert abs(coverage['maximum_monitor_gap_seconds']-max(elapsed))<1e-10
    assert coverage['maximum_capture_gap_seconds']==max(captures)
    expected_cpu='1' if mode=='studio' else '0'
    rss={}
    for role,limit in (('parent',256),('worker',2048)):
        assert len({m[role] for m in memory})==len({m[role+'_starttime'] for m in memory})==1
        assert all(m[role+'_cpu']==expected_cpu and m[role+'_scheduler']==0 for m in memory)
        nice=-20 if role=='parent' and mode=='studio' else -10
        assert all(m[role+'_nice']==nice for m in memory)
        assert all(m[role+'_fds']<=memory[0][role+'_fds']+1 for m in memory)
        assert all(m[role+'_fds']==min(m[role+'_fd_samples']) and
                   m[role+'_fd_peak']==max(m[role+'_fd_samples']) for m in memory)
        values=[m[role+'_rss_kib'] for m in memory if m['seconds']>=60]
        assert values
        rss[role]=max(values)-min(values);assert rss[role]<=limit
    assert memory[0]['parent']==int(p['parent'])
    assert all(m['capture_duration_ns']>0 and m['save_CRC_valid'] and m['save_boots']==1 for m in memory)
    assert all(y['save_ticks']>=x['save_ticks'] and y['save_elapsed_ms']>=x['save_elapsed_ms']
               for x,y in zip(memory,memory[1:]))
    saved=[dict(ticks=m['save_ticks'],elapsed_ms=m['save_elapsed_ms'],boots=m['save_boots'],
                largest_gap_ms=m['save_largest_gap_ms']) for m in memory]
    assert saved==read_json(directory/(mode+'-saves.json'))
    payload=(directory/(mode+'-final.pmem')).read_bytes()
    assert len(payload)==1036 and payload[:4]==b'TMPM' and struct.unpack_from('<I',payload,4)[0]==1
    assert struct.unpack_from('<I',payload,8)[0]==zlib.crc32(payload[12:])
    final=dict(zip(('ticks','elapsed_ms','boots','largest_gap_ms'),struct.unpack_from('<4I',payload,12)))
    assert final==p['final_save'] and final['boots']==1 and final['largest_gap_ms']<250
    assert final['ticks']>=saved[-1]['ticks'] and final['elapsed_ms']>=saved[-1]['elapsed_ms']
    assert p['saves']==saved+[final]
    observer=read_json(directory/(mode+'-observer-saves.json'))
    assert observer and all(s['boots']==1 for s in observer)
    assert all(y['ticks']>=x['ticks'] and y['elapsed_ms']>=x['elapsed_ms'] for x,y in zip(observer,observer[1:]))
    first_save=observer[0]
    rate=(final['ticks']-first_save['ticks'])/((final['elapsed_ms']-first_save['elapsed_ms'])/1000)
    assert rate==p['TIC_completion_rate_hz'] and abs(rate-60)<.1
    for label in ('before','after'):
        hdmi(directory,mode+'-'+label,p['hdmi_'+label])
        assert probe(directory,mode+'-'+label,p['audio_'+label])==first['session']
    disconnect=p['observer_disconnect_test']
    assert disconnect['dispatched'] and disconnect['seconds_closed']>=35
    assert disconnect['transport_only'] and not disconnect['board_network_outage_claimed'] and not disconnect['jobs_restarted']
    log=(directory/(mode+'-final.log')).read_text()
    if mode=='studio':
        assert 'recoveries=0' in log and 'departed=1 error=0' in log and 'Studio RUN: waiting_frames=0' in log
    else:
        import re
        assert 'Core switched; runtime stopped' in log and re.search(r'TIC-80 service stopped: ticks=\d+ error=0',log)
    assert 'Studio log: dropped=' not in log
    return dict(mode=mode,seconds=seconds,audio_samples=len(samples),audio_underruns=0,
                audio_clock_hz=audio,game_clock_hz=game,queue_change_ms=queue['fitted_queue_change_ms'],
                alignment=alignment,maximum_audio_sample_gap_seconds=max(gaps),
                memory_checkpoints=len(memory),maximum_memory_capture_gap_seconds=max(captures),
                sampled_steady_RSS_ranges_kib=rss,final_save=final,save_rate_hz=rate,
                original_job_statuses=[j['status'] for j in jobs],passed=True)


def complete(directory, result):
    assert result['passed'] and result['restored'] and not result['temporary_activation']
    assert result['sustained_playback_passed'] and 'error' not in result and 'rollback_error' not in result
    assert result['soak_duration_seconds']==3600 and result['soak_modes']==['studio','player']
    assert len(result['jobs'])==4 and {j['mode'] for j in result['jobs']}=={'studio','studio-memory','player','player-memory'}
    assert result['restored_audio']['underruns']==0 and result['restored_hdmi']['maximum_error_ppm']<=1000
    probe(directory,'restored',result['restored_audio'])
    hdmi(directory,'restored',result['restored_hdmi'])
    assert all(not event['activation_repeated'] and not event['monitor_restarted']
               for event in result.get('observer_reconnections',[]))
    return {mode:phase(directory,result,mode) for mode in ('studio','player')}
