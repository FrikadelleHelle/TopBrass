"""Measure recording attack growth separately from digital silence and MIDI scheduling."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[1]
NAMES=['Original tenor','Trumpet','Trombone','Tuba','Baritone','Alternate tenor']


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def growth(audio,rate):
    x=np.asarray(audio,dtype=np.float64)
    if x.ndim==1:x=x[:,None]
    if x.ndim!=2 or not len(x) or not np.isfinite(x).all():raise ValueError('Expected finite nonempty audio')
    peak=np.max(np.abs(x),axis=1)
    result={}
    for label,threshold in [('nonzero',0),('minus100db',1e-5),('minus80db',1e-4),('minus60db',1e-3)]:
        crossing=np.flatnonzero(peak>threshold)
        result[label+'_sample']=int(crossing[0]) if len(crossing) else None
    energy=np.mean(x*x,axis=1);cumulative=np.r_[0,np.cumsum(energy)]
    for window_ms in [1,5,10]:
        window=max(1,round(rate*window_ms/1000))
        if window>len(energy):continue
        envelope=np.sqrt(np.maximum(0,(cumulative[window:]-cumulative[:-window])/window))
        reference=float(np.max(envelope));metrics={'window_samples':window,'reference_rms':reference}
        for fraction in [.1,.25,.5]:
            crossing=np.flatnonzero(envelope>reference*fraction) if reference else []
            metrics[str(fraction)+'_window_end_sample']=int(crossing[0]+window) if len(crossing) else None
        result[str(window_ms)+'ms_rms']=metrics
    return result


def self_check():
    rate=48000;prefix=7680;audio=np.r_[np.zeros(prefix),np.ones(4320)]
    assert growth(audio,rate)['nonzero_sample']==prefix
    assert growth(np.column_stack([audio,np.r_[np.zeros(100),np.ones(11900)]]),rate)['nonzero_sample']==100
    assert growth(np.zeros(12000),rate)['nonzero_sample'] is None
    quiet=audio*.001;assert growth(quiet,rate)['nonzero_sample']==prefix
    for window in ['1ms_rms','5ms_rms','10ms_rms']:
        for fraction in ['0.1','0.25','0.5']:
            key=fraction+'_window_end_sample'
            assert growth(quiet,rate)[window][key]==growth(audio,rate)[window][key]


def native(bank):
    rows=[]
    with bank.open('rb') as stream:
        assert stream.read(8)==b'TBBANK04';count,zones=struct.unpack('<2I',stream.read(8))
        for index in range(count):
            header=struct.unpack('<15I',stream.read(60));instrument,articulation,key,layer,rate,frames=header[:6]
            start=stream.tell();length=min(frames,round(rate*.25));audio=[]
            for channel in range(header[14]):
                stream.seek(start+channel*frames*4);audio.append(np.frombuffer(stream.read(length*4),dtype='<f4'))
            pcm=np.column_stack(audio)
            metrics=growth(pcm,rate);gain_metrics=metrics
            if header[13]:
                stream.seek(start+frames*header[14]*4)
                envelope=np.frombuffer(stream.read(header[13]*4),dtype='<f4')
                positions=np.minimum(np.floor((np.arange(length)+header[12])/32+.5).astype(int),len(envelope)-1)
                gain_metrics=growth(pcm*envelope[positions,None],rate)
            rows.append(dict(index=index,instrument=instrument,articulation=articulation,key=key,layer=layer,rate=rate,
                             measured_frames=length,metrics=metrics,gain_table_entries=header[13],gain_applied_metrics=gain_metrics))
            stream.seek(start+(frames*header[14]+header[13])*4)
    return rows


def prepare(output):
    cases=[]
    for alternate in [False,True]:
        for articulation in [0,1]:
            label=('alternate' if alternate else 'original')+('-sustain' if articulation==0 else '-short')
            for isolated in [None,0,1,2,3,4]:
                identifier=label+('-section' if isolated is None else '-leakage' if isolated==4 else f'-player{isolated}')
                folder=output/identifier;folder.mkdir(parents=True,exist_ok=True)
                case=dict(id=identifier,directory=str(folder.resolve()),ensemble=True,part=0,instrument=0,
                          articulation=articulation,alternate_tenor=alternate,horn_feel=0,
                          rate=48000,root=60,velocity=112,frames=96000,post_release_frames=96000)
                if isolated is not None:case['isolate_player']=isolated
                cases.append(case)
    config=output/'config.json';config.write_text(json.dumps(dict(cases=cases),indent=2)+'\n')
    return config


def analyze(output,bank,plugin,host):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    rows=native(bank);(output/'native-recordings.json').write_text(json.dumps(rows,indent=2)+'\n')
    assert all(row['metrics']['nonzero_sample'] is not None and row['gain_applied_metrics']['nonzero_sample'] is not None for row in rows), 'A recording is silent across the measured prefix; the maximum cannot describe the complete bank'
    largest=max(row['metrics']['nonzero_sample']/row['rate']*1000 for row in rows)
    gain_largest=max(row['gain_applied_metrics']['nonzero_sample']/row['rate']*1000 for row in rows if row['gain_applied_metrics']['nonzero_sample'] is not None)
    stats=[]
    for instrument in range(6):
        for art in range(6):
            group=[row for row in rows if row['instrument']==instrument and row['articulation']==art]
            if not group:continue
            values=[row['metrics']['5ms_rms']['0.5_window_end_sample']/row['rate']*1000 for row in group]
            stats.append(dict(instrument=instrument,articulation=art,recordings=len(group),
                              rms5ms_half_peak_window_end_ms=dict(zip(['min','median','p90','max'],map(float,np.percentile(values,[0,50,90,100]))))))
    captures=[];panels=[]
    for alternate in [False,True]:
        for articulation in [0,1]:
            label=('alternate' if alternate else 'original')+('-sustain' if articulation==0 else '-short')
            leakage,rate=sf.read(output/(label+'-leakage')/'neutral-plugin-raw.wav',always_2d=True)
            leakage_peak=float(np.max(np.abs(leakage[:,0])))
            fig,axes=plt.subplots(4,1,figsize=(11,9),layout='constrained',sharex=True)
            caption=('Alternate tenor' if alternate else 'Original tenor')+' section, '+('Sustain' if articulation==0 else 'Short')
            fig.suptitle(caption+': actual VST, all four players enabled, Feel at zero')
            for part,axis in enumerate(axes):
                audio,rate=sf.read(output/(label+f'-player{part}')/'neutral-plugin-raw.wav',always_2d=True)
                signal=audio[:12000,0];peak=float(np.max(np.abs(signal)))
                assert peak>1e-7 and leakage_peak<peak*1e-5,(label,part,peak,leakage_peak)
                metrics=growth(signal,rate)
                captures.append(dict(case=label,part=part,metrics=metrics,peak=peak,leakage_peak=leakage_peak,
                                     leakage_relative_db=float(20*np.log10(max(leakage_peak,1e-30)/peak))))
                signal=signal[:7200];time=np.arange(len(signal))/rate*1000
                axis.plot(time,signal/peak,linewidth=.5,color='#9d692d')
                for fraction,color in [('0.1','#287d8e'),('0.5','#af4861')]:
                    sample=metrics['5ms_rms'][fraction+'_window_end_sample']
                    axis.axvline(sample/rate*1000,color=color,linestyle='--',label=f'{float(fraction)*100:g}% of first 250 ms peak RMS, 5 ms window end')
                axis.axvline(0,color='black',label='First MIDI hit')
                axis.set(title=['Saxophone','Trumpet','Trombone','Tuba'][part],ylabel='Peak-normalized')
            axes[-1].set(xlabel='Time after first MIDI hit (ms)',xlim=(0,150));axes[0].legend(fontsize=8)
            figure=label+'-attacks.png';fig.savefig(output/figure,dpi=120);plt.close(fig)
            original,rate=sf.read(output/(label+'-section')/'neutral-plugin-raw.wav',always_2d=True)
            listening=original*10**(18/20);assert np.max(np.abs(listening))<1
            sf.write(output/(label+'-listen.wav'),listening,rate,subtype='FLOAT')
            panels.append(dict(label=caption,figure=figure,audio=label+'-listen.wav'))
    reference=np.zeros(192000);rate=48000
    for step in [0,2,3,6,8,10,11,14,15]:
        n=np.arange(240);reference[step*6000:step*6000+240]=.1*np.sin(2*np.pi*2000*n/rate)*(1-n/240)
    sf.write(output/'scheduled-hits-reference.wav',reference,rate,subtype='FLOAT')
    report=dict(plugin=str(plugin),plugin_executable_sha256=sha(plugin/'Contents/MacOS/TopBrass'),
                bank_sha256=sha(bank),host_executable_sha256=sha(host),recordings=len(rows),
                longest_measured_exact_zero_prefix_ms=largest,longest_gain_applied_zero_prefix_ms=gain_largest,authored_gain_tables=sum(row["gain_table_entries"]>0 for row in rows),native_summary=stats,actual_vst_first_hit_growth=captures,
                limits=['Thresholds describe signal growth, not perceptual onsets or player intention.',
                        'Native corpus summary is not weighted by actual playable mappings; pitch and region gain change playback.',
                        'First250ms reference and1/5/10ms windows are explicit analysis choices; slower low-note cycles affect short windows.',
                        'Stems keep all four players and scheduler active. Desired player is hard-left, all others hard-right; stereo sax analysis uses its left channel.',
                        'No trimming, timing compensation, lookahead, source replacement or installed DSP change was made.'])
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>TopBrass attack timing</title><style>body{font:18px/1.6 system-ui;background:#151719;color:#eee;max-width:1000px;margin:40px auto;padding:20px}audio,img{width:100%}a{color:#dfbb75}section{border-top:1px solid #555;padding-top:16px;margin-top:28px}</style><h1>Shared rhythm, different recorded attacks</h1>'''
    page+=f'<p>Actual VST 0.16.0. All four players share the same rhythm, with Feel at zero and neutral Tone/Accent. Compare Short and Sustain below.</p><p>The longest exact-zero prefix across {len(rows):,} recordings is {largest:.3f} ms, including the authored recording gain tables. The waveforms show different energy growth rather than long digital silence. These measurements are not perceptual onset estimates.</p>'
    page+='<p>One fixed +18 dB listening gain. No trimming or time shifts. Alternate tenor Short gates its sustain recording.</p><p>Separate clicks at the scheduled hit times</p><audio controls preload="metadata" src="scheduled-hits-reference.wav"></audio>'
    for panel in panels:
        page+=f'<section><h2>{html.escape(panel["label"])}</h2><audio controls preload="metadata" src="{panel["audio"]}"></audio></section>'
    page+='<section><h2>First 150 ms of each player</h2><p>Dashed lines mark 10% and 50% of the first 250 ms peak RMS, using a 5 ms window. They describe energy growth, not perceived onset times.</p></section>'
    for panel in panels:
        page+=f'<section><h2>{html.escape(panel["label"])}</h2><img src="{panel["figure"]}" alt="First 150 ms waveforms for each section player, with energy growth markers"></section>'
    page+="""<p>The waveform panels isolate each player's left output while keeping all four players enabled. The measured leakage floor is in the report. A slow energy rise can be wanted tonguing or breath. These measurements do not justify trimming it away. <a href="results.json">Measurements and limits</a>. <a href="../">Listening bench</a>.</p><script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')for(const a of document.querySelectorAll('audio'))if(a!==e.target)a.pause()},true)</script></html>"""
    (output/'index.html').write_text(page)
    print(f'PASS:{len(rows)} native prefixes;16 actual VST first-hit envelopes; longest digital prefix{largest:.3f}ms, gain-applied{gain_largest:.3f}ms')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--plugin',type=Path,default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3')
    parser.add_argument('--host',type=Path,default=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit')
    args=parser.parse_args();self_check();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    if (output/'results.json').exists():raise ValueError('Choose a new output folder to retain previous results')
    config=prepare(output);subprocess.run([str(args.host.resolve()),str(args.plugin.resolve()),str(config)],check=True)
    analyze(output,args.plugin/'Contents/Resources/TopBrass.bank',args.plugin.resolve(),args.host.resolve())


if __name__=='__main__':main()
