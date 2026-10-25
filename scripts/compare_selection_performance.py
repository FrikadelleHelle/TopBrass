"""Run paired frozen/current engine benchmarks and require identical full audio outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def compare(before,after,bank,output):
    output.mkdir(parents=True,exist_ok=True)
    binaries={'before':before.resolve(),'after':after.resolve()}
    hashes={key:sha(path) for key,path in binaries.items()}
    bank_hash=sha(bank)
    runs=[]
    for pair in range(3):
        results={}
        for label in (('before','after') if pair%2==0 else ('after','before')):
            prefix=output/f'pair{pair}-{label}'
            subprocess.run([str(binaries[label]),str(bank),str(prefix)],check=True)
            results[label]=json.loads(prefix.with_suffix('.json').read_text())
            results[label]['audio_sha256']=sha(prefix.with_suffix('.f32'))
        if results['before']['audio_sha256']!=results['after']['audio_sha256']:
            raise ValueError('Selected recordings or rendered output changed')
        if results['before']['pcm_bytes']!=results['after']['pcm_bytes']:
            raise ValueError('Decoded sample memory changed')
        runs.append(results)
    if len({pair[label]['audio_sha256'] for pair in runs for label in ('before','after')})!=1:
        raise ValueError('Repeated benchmark audio is not deterministic')
    if any(sha(binaries[label])!=digest for label,digest in hashes.items()) or sha(bank)!=bank_hash:
        raise ValueError('Benchmark inputs changed during measurement')
    cases=[]
    for index,before_case in enumerate(runs[0]['before']['note_trigger']):
        name=before_case['case'];row={'case':name}
        for label in ('before','after'):
            times=[]
            for pair in runs:
                case=pair[label]['note_trigger'][index]
                if case['case']!=name:raise ValueError('Mismatched benchmark cases')
                times.extend(case['thread_cpu_us_per_event'])
            row[label+'_median_thread_cpu_us']=statistics.median(times)
        row['speedup']=row['before_median_thread_cpu_us']/row['after_median_thread_cpu_us']
        cases.append(row)
    dense={}
    for metric in ('thread_cpu_mean_us','thread_cpu_p99_us','wall_mean_us','wall_p99_us','wall_max_us'):
        dense[metric]={label:statistics.median(pair[label]['dense_midi'][metric] for pair in runs) for label in ('before','after')}
    reductions=[100*(1-pair['after']['dense_midi']['thread_cpu_mean_us']/pair['before']['dense_midi']['thread_cpu_mean_us']) for pair in runs]
    summary={'description':'Three alternating paired runs of the same benchmark. No-render note-trigger batches isolate selection/routing work. Dense case renders four note-ons and four note-offs per128frames at48kHz, with recorded baritone keys/air. These are single-process engine measurements, not live-host qualification.',
             'bank_sha256':bank_hash,'executable_sha256':hashes,'note_trigger':cases,'dense_midi_median_across_runs':dense,
             'all_audio_bit_identical':True,'audio_sha256':runs[0]['before']['audio_sha256'],
             'dense_midi_paired_cpu_mean_reduction_percent':reductions,
             'dense_midi_median_paired_cpu_mean_reduction_percent':statistics.median(reductions),
             'index_bytes':runs[0]['after']['index_bytes'],'pcm_bytes':runs[0]['after']['pcm_bytes'],'runs':runs}
    (output/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ('note_trigger','dense_midi_median_across_runs','index_bytes','all_audio_bit_identical')},indent=2))
    return summary


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--after',type=Path,default=ROOT/'build/TopBrassSelectionBenchmark')
    parser.add_argument('--bank',type=Path,default=ROOT/'assets/TopBrass.bank')
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/selection-performance')
    args=parser.parse_args()
    compare(args.before,args.after,args.bank.resolve(),args.output.resolve())
