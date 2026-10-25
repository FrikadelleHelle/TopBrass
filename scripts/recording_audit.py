"""Audition originals against an installed VST3 or Audio Unit and measure playback transparency."""
import argparse
import datetime
import hashlib
import html
import json
from pathlib import Path
import shutil
import struct
import subprocess
import numpy as np
import soundfile as sf
from scipy import signal

ROOT=Path(__file__).resolve().parents[1]


def capture_output(output):
    if (output/'listening-index.json').exists():
        raise ValueError('The listening bench is reserved. Choose a capture subfolder with --output.')


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def bank_headers(path):
    records=[]
    with path.open('rb') as stream:
        if stream.read(8)!=b'TBBANK04':raise ValueError('Audit currently expects the installed Bank04 corpus')
        count,zones=struct.unpack('<2I',stream.read(8))
        for _ in range(count):
            fields=struct.unpack('<15I',stream.read(60));records.append(fields)
            stream.seek((fields[5]*fields[14]+fields[13])*4,1)
    return records


def recording_envelope(bank_path,index):
    with bank_path.open('rb') as stream:
        stream.seek(16)
        for number in range(index+1):
            h=struct.unpack('<15I',stream.read(60));stream.seek(h[5]*h[14]*4,1)
            values=np.frombuffer(stream.read(h[13]*4),dtype='<f4')
            if number==index:
                positions=np.minimum(np.floor((np.arange(h[5])+h[12])/32+.5).astype(int),len(values)-1)
                return values[positions]


def prepare(output,plugin,hold_seconds=None):
    capture_output(output)
    if hold_seconds is not None and (not np.isfinite(hold_seconds) or not .1<=hold_seconds<=30):
        raise ValueError('Hold duration must be between0.1 and30seconds')
    output.mkdir(parents=True,exist_ok=True)
    plugin_format={'.vst3':'VST3','.component':'AudioUnit'}.get(plugin.suffix.lower())
    if plugin_format is None:raise ValueError('Expected an installed VST3 or Audio Unit bundle')
    headers=bank_headers(plugin/'Contents/Resources/TopBrass.bank')
    lock=json.loads((ROOT/'assets/samples.lock.json').read_text())
    retained=[e for e in lock if e['instrument'] in (0,3,4)]
    catalog=json.loads((ROOT/'assets/cache/bell-and-bone/playback-catalog.json').read_text())
    cases=[]
    for instrument,label,target in [(0,'Tenor sax',60),(1,'Trumpet',60),(2,'Trombone',48),(3,'Tuba',36),(4,'Baritone sax',48)]:
        for art,art_name in [(1,'short'),(0,'sustain'),(5,'double-tongue')]:
            if art==5 and not any(h[0]==instrument and h[1]==5 for h in headers):continue
            velocity=127;zone=None
            if instrument in (0,3,4):
                entries=[(i,e) for i,e in enumerate(retained) if e['instrument']==instrument and e['articulation']==art]
                index,entry=min(entries,key=lambda row:(abs(row[1]['root']-target),-row[1]['layer'],row[1]['path']))
                source=ROOT/'assets/cache'/entry['repo'].split('/')[-1]/entry['path'];raw=source.read_bytes()
                if hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest()!=entry['blob']:raise ValueError('Pinned source changed')
                provenance=entry
            else:
                name='trumpet' if instrument==1 else 'trombone';kind='sustain' if art==0 else 'double-tongue' if art==5 else 'staccatissimo'
                zs=[z for z in catalog['zones'] if z['instrument']==name and z['articulation']==kind and z['round_robin']==0
                    and z['low_key']<=z['root']<=z['high_key'] and z['pitch_cents']==0
                    and (z['dynamic_layer']==3 if art==0 else z['low_velocity']<=127<=z['high_velocity'])]
                zone=min(zs,key=lambda z:(abs(z['root']-target),-z['high_velocity']))
                record=catalog['recordings'][zone['recording']];source=ROOT/record['file'];audio,_=sf.read(source,dtype='float32')
                if hashlib.sha256(audio.astype('<f4').tobytes()).hexdigest()!=record['audio_sha256']:raise ValueError('Extracted source PCM changed')
                index=len(retained)+zone['recording'];velocity=zone['high_velocity'] if art==0 else 127
                provenance={'recording':record,'zone':zone,'source_kind':'Original extracted supplier note, before player gain/tuning/envelopes'}
            h=headers[index];info=sf.info(source)
            if (h[0],h[1],h[4],h[5])!=(instrument,art,info.samplerate,info.frames):raise ValueError('Bank/source identity differs')
            end=min(h[5]-3,h[7]-h[8]-3 if h[7] else h[5]-3,h[9]-3 if h[9] else h[5]-3)
            hold_frames=info.frames if hold_seconds is None else int(info.samplerate*hold_seconds)
            end=min(end,hold_frames-3)
            if end<info.samplerate*.08:raise ValueError('Native comparison window too short')
            identifier=label.lower().replace(' ','-')+'-'+art_name;directory=output/identifier;directory.mkdir(exist_ok=True)
            shutil.copy2(source,directory/'original-native.wav')
            cases.append(dict(id=identifier,label=label,articulation_name=art_name,instrument=instrument,part=0 if instrument==4 else instrument,
                              articulation=art,double_tongue=art==5,root=h[2],velocity=velocity,expression=127,rate=info.samplerate,frames=hold_frames,
                              post_release_frames=max(int(info.samplerate*.25),h[5]-h[9]+int(info.samplerate*.02)) if h[9] else int(info.samplerate*.25),
                              directory=str(directory.resolve()),source_path=str(source),source_sha256=digest(source),provenance=provenance,
                              analysis_start=0,analysis_end=end,loop_start=h[6],loop_end=h[7],loop_crossfade=h[8],
                              authored_gain_table=bool(h[13]),native_pitch=True,bank_recording_index=index))
    manifest=dict(plugin_path=str(plugin),plugin_format=plugin_format,plugin_executable_sha256=digest(plugin/'Contents/MacOS/TopBrass'),bank_sha256=digest(plugin/'Contents/Resources/TopBrass.bank'),
                  description=f'Actual installed {plugin_format}; one solo note, native source rate/pitch, first take; CC11=127 isolates the forte sustain layer. Tone/Accent/keys disabled for neutral.',cases=cases)
    (output/'config.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest


def stereo(x):
    return np.repeat(x,2,axis=1) if x.shape[1]==1 else x


def spectrum(x,rate):
    n=min(8192,len(x));f,p=signal.welch(x,rate,window='hann',nperseg=n,noverlap=n//2,axis=0,detrend=False)
    return f,np.mean(p,axis=1)


def compare_arrays(original,rendered,rate):
    x=stereo(np.asarray(original,dtype=float));y=stereo(np.asarray(rendered,dtype=float))
    if x.shape!=y.shape:raise ValueError('Comparison windows must have identical channels and frame count')
    ex=float(np.mean(x*x));ey=float(np.mean(y*y))
    if min(ex,ey)<1e-20:raise ValueError('Original or rendered comparison window is silent')
    gain=np.sqrt(ex/ey);y=y*gain
    f,px=spectrum(x,rate);_,py=spectrum(y,rate)
    bands=[]
    for center in 40*2**(np.arange(28)/3):
        mask=(f>=center/2**(1/6))&(f<center*2**(1/6))
        if not np.any(mask) or center>min(18000,rate*.45):continue
        a=float(np.sum(px[mask]));b=float(np.sum(py[mask]));bands.append([float(center),a,b])
    peak=max(row[1] for row in bands);usable=[r for r in bands if r[1]>peak*1e-6]
    errors=[10*np.log10(max(r[2],1e-30)/r[1]) for r in usable]
    residual=y-x
    channel=int(np.argmax(np.mean(x*x,axis=0)));ref=x[:,channel];test=y[:,channel];max_lag=min(int(rate*.02),len(ref)//4)
    correlation=signal.correlate(test,ref,mode='full',method='fft');middle=len(ref)-1
    lag=int(np.argmax(correlation[middle-max_lag:middle+max_lag+1])-max_lag)
    return dict(gain_correction=float(gain),raw_gain_db=float(-20*np.log10(gain)),
                residual_relative_db=float(10*np.log10(max(float(np.mean(residual*residual))/ex,1e-30))),
                max_waveform_error=float(np.max(np.abs(residual))),lag_samples=lag,
                max_abs_band_error_db=float(max(abs(v) for v in errors)),
                bands=[dict(center_hz=r[0],original_power=r[1],rendered_power=r[2],difference_db=float(10*np.log10(max(r[2],1e-30)/r[1]))) for r in usable],
                original_channel_correlation=float(np.corrcoef(x.T)[0,1]) if np.all(np.std(x,axis=0)>1e-15) else None,rendered_channel_correlation=float(np.corrcoef(y.T)[0,1]) if np.all(np.std(y,axis=0)>1e-15) else None,
                transparency_pass=bool(max(abs(v) for v in errors)<.25 and 10*np.log10(max(float(np.mean(residual*residual))/ex,1e-30)) < -60 and lag==0))


def plot_pair(x,y,rate,metrics,start,end,path,title,loop=None):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(3,2,figsize=(13,10),layout='constrained');fig.suptitle(title)
    def stft(data):
        n=min(2048,len(data));transform=signal.ShortTimeFFT.from_window('hann',rate,n,n-n//8,scale_to='magnitude')
        s=transform.stft(data,axis=0);power=np.mean(np.abs(s)**2,axis=1)
        return transform.f,transform.t(len(data)),power
    fx,tx,p=stft(x);_,ty,q=stft(y);reference=max(float(p.max()),1e-30)
    for ax,t,power,label in [(axes[0,0],tx,p,'Original'),(axes[0,1],ty,q,'Plugin, constant level matched')]:
        ax.pcolormesh(t,fx,10*np.log10(np.maximum(power/reference,1e-12)),vmin=-80,vmax=0,cmap='magma',shading='auto',rasterized=True)
        ax.set(yscale='log',ylim=(40,min(18000,rate*.49)),xlim=(0,len(x)/rate),title=label,xlabel='Time (s)',ylabel='Frequency (Hz)')
        ax.axvspan(start/rate,end/rate,color='cyan',alpha=.08)
        if loop:ax.axvline(loop/rate,color='cyan',linewidth=.8,linestyle='--')
    f,px=spectrum(x[start:end],rate);_,py=spectrum(y[start:end],rate)
    axes[1,0].semilogx(f,10*np.log10(np.maximum(px,1e-25)),label='Original');axes[1,0].semilogx(f,10*np.log10(np.maximum(py,1e-25)),label='Plugin',alpha=.8)
    axes[1,0].set(xlim=(40,min(18000,rate*.49)),title='Spectrum in the comparison window',xlabel='Frequency (Hz)',ylabel='PSD (dB/Hz)');axes[1,0].legend()
    bands=metrics['bands'];axes[1,1].semilogx([b['center_hz'] for b in bands],[b['difference_db'] for b in bands],marker='.')
    axes[1,1].axhline(0,color='gray');axes[1,1].axhspan(-.25,.25,color='green',alpha=.12);axes[1,1].set(title='Plugin − original, level removed',xlabel='Frequency (Hz)',ylabel='1/3-octave power difference (dB)')
    hop=max(1,int(rate*.005));n=len(x)//hop
    envx=np.sqrt(np.mean(x[:n*hop].reshape(n,hop,-1)**2,axis=(1,2)));envy=np.sqrt(np.mean(y[:n*hop].reshape(n,hop,-1)**2,axis=(1,2)))
    axes[2,0].plot(np.arange(n)*hop/rate,envx,label='Original');axes[2,0].plot(np.arange(n)*hop/rate,envy,label='Plugin',alpha=.8);axes[2,0].legend();axes[2,0].set(title='5 ms RMS envelope',xlabel='Time (s)',ylabel='Amplitude')
    residual=y-x;_,tr,r=stft(residual);axes[2,1].pcolormesh(tr,fx,10*np.log10(np.maximum(r/reference,1e-12)),vmin=-100,vmax=0,cmap='magma',shading='auto',rasterized=True)
    axes[2,1].set(yscale='log',ylim=(40,min(18000,rate*.49)),xlim=(0,len(x)/rate),title='Residual, same spectral scale (−100 to 0 dB)',xlabel='Time (s)',ylabel='Frequency (Hz)')
    fig.savefig(path,dpi=120);plt.close(fig)


def analyze(output):
    capture_output(output)
    config=json.loads((output/'config.json').read_text());results=[]
    for item in config['cases']:
        directory=Path(item['directory']);raw,rate=sf.read(directory/'original-native.wav',always_2d=True);x=stereo(raw)
        for variant in config.get('variants',['neutral','current-tone']):
            y,sr=sf.read(directory/(variant+'-plugin-raw.wav'),always_2d=True)
            if sr!=rate:raise ValueError('Source/plugin sample rates differ')
            end=min(item['analysis_end'],len(y));start=item['analysis_start'];metrics=compare_arrays(x[start:end],y[start:end],rate)
            matched=y*metrics['gain_correction'];length=max(len(x),len(matched));xp=np.pad(x,((0,length-len(x)),(0,0)));yp=np.pad(matched,((0,length-len(matched)),(0,0)))
            listen_gain=min(.12/np.sqrt(np.mean(x[start:end]**2)),.9/max(np.max(abs(xp)),np.max(abs(yp)),1e-15))
            sf.write(directory/(variant+'-original-listen.wav'),xp*listen_gain,rate,subtype='PCM_24');sf.write(directory/(variant+'-plugin-listen.wav'),yp*listen_gain,rate,subtype='PCM_24')
            plot_pair(xp,yp,rate,metrics,start,end,directory/(variant+'-analysis.png'),item['label']+' / '+item['articulation_name']+' / '+variant,item['loop_end']-item['loop_crossfade'] if item['loop_end'] else None)
            row=dict(case=item,variant=variant,metrics=metrics,listen_gain=float(listen_gain),note='Only one constant gain is removed; no EQ/time alignment/pitch correction. Full-note changes after looping/release remain visible.')
            if item['authored_gain_table']:
                envelope=recording_envelope(Path(config['plugin_path'])/'Contents/Resources/TopBrass.bank',item['bank_recording_index'])
                expected=x*envelope[:,None]
                row['authored_envelope_comparison']=compare_arrays(expected[start:end],y[start:end],rate)
            (directory/(variant+'-metrics.json')).write_text(json.dumps(row,indent=2)+'\n');results.append(row)
    (output/'results.json').write_text(json.dumps(results,indent=2)+'\n');write_html(output,config,results)
    return results


def write_html(output,config,results):
    capture_output(output)
    options=''.join(f'<option value="{html.escape(c["id"])}">{html.escape(c["label"]+" · "+c["articulation_name"]+" · MIDI "+str(c["root"]))}</option>' for c in config['cases'])
    doc='''<!doctype html><html><meta charset="utf-8"><title>TopBrass recording audit</title><style>
body{font:16px system-ui;background:#f5f3ec;color:#202b2b;margin:32px auto;max-width:1120px;padding:0 22px}h1{font-size:32px}select,button{font:inherit;padding:10px;margin:4px 12px 4px 0}audio{width:100%}.players{display:grid;grid-template-columns:1fr 1fr;gap:24px}img{width:100%;background:white;margin-top:20px}pre{white-space:pre-wrap;font:14px system-ui;background:white;padding:16px;border-radius:8px}.muted{color:#52615f}a{color:#235f65}h2{font-size:20px}button{cursor:pointer}#decision{background:#fff5d5;padding:14px} @media(max-width:700px){.players{grid-template-columns:1fr}}
</style><h1>Original recording → installed VST3</h1><p>Listen first. One solo instrument, the same source pitch and sample rate, first recorded take. Neutral disables Tone, Accent and key noise. The second view changes only Tone to −1.</p><p class="muted">Playback files use a documented constant level match; no EQ, pitch correction or time alignment is applied. Full original and raw plugin WAVs remain available. Analysis uses the original segment before the first loop seam. Later loop/release differences are expected and visible.</p><select id="case">OPTIONS</select><select id="variant"><option value="neutral">Neutral plugin</option><option value="current-tone">Warm Tone −1 only</option></select><div class="players"><section><h2>1. Original recording</h2><audio controls id="original"></audio><p><a id="rawOriginal">Original native WAV</a></p></section><section><h2>2. Same note through VST3</h2><audio controls id="plugin"></audio><p><a id="rawPlugin">Raw plugin output</a> · <a id="metrics">Measurements</a></p></section></div><pre id="stats"></pre><div id="decision">Your listening judgment decides whether the source is good enough. A transparency pass means the tested playback preserves the source; it does not mean the horn sounds convincing.</div><img id="plot" alt="Source/plugin spectrograms, spectral profiles, band differences and residual"><p class="muted">Spectrograms average channel power rather than summing stereo channels. Cyan shading marks the measured pre-loop window; a dashed line marks the first loop seam. Bands more than 60 dB below source band peak are excluded from the summary.</p><p><a href="config.json">Sources, plugin identity and exact MIDI setup</a> · <a href="results.json">All measurements</a></p><script>
const rows=RESULTS;const $=id=>document.getElementById(id);function update(){for(const a of document.querySelectorAll('audio'))a.pause();const c=$('case').value,v=$('variant').value,r=rows.find(x=>x.case.id===c&&x.variant===v),m=r.metrics;const base=c+'/'+v;$('original').src=base+'-original-listen.wav';$('plugin').src=base+'-plugin-listen.wav';$('rawOriginal').href=c+'/original-native.wav';$('rawPlugin').href=base+'-plugin-raw.wav';$('metrics').href=base+'-metrics.json';$('plot').src=base+'-analysis.png';$('stats').textContent=`${m.transparency_pass?'Transparency window passes':'Measured change in playback'}\nLargest usable-band difference: ${m.max_abs_band_error_db.toFixed(4)} dB\nWaveform residual relative to source: ${m.residual_relative_db.toFixed(1)} dB\nEstimated lag: ${m.lag_samples} samples\nConstant plugin gain removed: ${m.raw_gain_db.toFixed(2)} dB\n${r.case.generic_pair?'Supplied WAV pair: MIDI and pitch equivalence are not verified.':`Note velocity: ${r.case.velocity}; expression CC11: 127; native pitch, ${r.case.rate} Hz`}\nWindow: 0–${(r.case.analysis_end/r.case.rate).toFixed(3)} s. Supplier gain envelope: ${r.case.generic_pair?'unknown':r.case.authored_gain_table?'present':'none'}${r.authored_envelope_comparison?'\\nAfter accounting for the authored envelope: residual '+r.authored_envelope_comparison.residual_relative_db.toFixed(1)+' dB':''}`;} $('case').onchange=update;$('variant').onchange=update;update();
</script></html>'''
    if config.get('generic_pair'):
        doc=doc.replace('<option value="current-tone">Warm Tone −1 only</option>','')
        doc=doc.replace('2. Same note through VST3','2. Supplied render')
        doc=doc.replace('Original recording → installed VST3','Original recording → supplied render').replace('Listen first. One solo instrument, the same source pitch and sample rate, first recorded take. Neutral disables Tone, Accent and key noise. The second view changes only Tone to −1.','These two WAVs were supplied directly. The comparison measures their differences; verify that they contain the same performance and pitch before interpreting a spectral ratio as a response change.')
    if not config.get('generic_pair'):doc=doc.replace('VST3',html.escape(config.get('plugin_format','VST3')))
    (output/'index.html').write_text(doc.replace('OPTIONS',options).replace('RESULTS',json.dumps(results).replace('</','<\\/')))


def prepare_pair(reference,rendered,output):
    capture_output(output)
    output.mkdir(parents=True,exist_ok=True);directory=output/'pair';directory.mkdir(exist_ok=True)
    x,sr=sf.read(reference,always_2d=True);y,other=sf.read(rendered,always_2d=True)
    if sr!=other or x.shape[1] not in (1,2) or y.shape[1] not in (1,2):raise ValueError('Supply mono/stereo WAVs at the same sample rate')
    shutil.copy2(reference,directory/'original-native.wav');shutil.copy2(rendered,directory/'neutral-plugin-raw.wav')
    item=dict(id='pair',label=reference.stem,articulation_name='supplied audio pair',root='unspecified',velocity='unspecified',rate=sr,frames=len(x),directory=str(directory.resolve()),analysis_start=0,analysis_end=min(len(x),len(y)),loop_start=0,loop_end=0,loop_crossfade=0,authored_gain_table=False,generic_pair=True)
    (output/'config.json').write_text(json.dumps(dict(variants=['neutral'],generic_pair=True,cases=[item]),indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'artifacts/recording-audit/audits'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));parser.add_argument('--plugin',type=Path,default=Path.home()/'Library/Audio/Plug-Ins/VST3/TopBrass.vst3');parser.add_argument('--reference',type=Path);parser.add_argument('--rendered',type=Path);parser.add_argument('--stage',choices=['prepare','analyze','all'],default='all');parser.add_argument('--hold-seconds',type=float);args=parser.parse_args();output=args.output.resolve()
    if args.reference or args.rendered:
        if not (args.reference and args.rendered):parser.error('Supply both --reference and --rendered')
        prepare_pair(args.reference,args.rendered,output);analyze(output);return
    if args.stage in ('prepare','all'):config=prepare(output,args.plugin,args.hold_seconds)
    if args.stage=='all':
        host=ROOT/'build/TopBrassRecordingAudit_artefacts/Release/TopBrass Recording Audit.app/Contents/MacOS/TopBrass Recording Audit'
        subprocess.run([str(host),str(args.plugin),str(output/'config.json')],check=True)
        if digest(args.plugin/'Contents/MacOS/TopBrass')!=config['plugin_executable_sha256']:raise ValueError('Installed plugin changed during audit')
    if args.stage in ('analyze','all'):
        rows=analyze(output)
        for row in rows:print(row['case']['id'],row['variant'],row['metrics']['max_abs_band_error_db'],row['metrics']['residual_relative_db'])


if __name__=='__main__':main()
