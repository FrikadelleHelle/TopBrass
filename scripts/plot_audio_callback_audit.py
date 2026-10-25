"""Plot paired CPU and wall times without treating CLI and device periods as equivalent."""
import argparse
import csv
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    args = parser.parse_args(); folder = args.input
    with (folder/'baseline-0.csv').open() as stream:
        cli = list(csv.DictReader(stream))
    cli_report = json.loads((folder/'baseline-0.json').read_text())
    qualified = json.loads((folder/'native-results.json').read_text())
    native = []; startup = []; rates = set()
    for case in qualified['cases']:
        if case['report']['mode'] != 'instrument':
            continue
        with (folder/(case['case']+'.csv')).open() as stream:
            rows = list(csv.DictReader(stream))
        startup.append(rows[0]); native.extend(rows[1:])
        rates.add(case['report']['sample_rate'])
    frames = {int(row['frames']) for row in native+startup}
    if len(frames)!=1 or len(rates)!=1:
        raise ValueError('A single-period plot requires matching native callback frame counts and rates')
    native_frames=frames.pop(); native_rate=rates.pop()
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.6), layout='constrained')
    for axis, rows, keys, budget, title in [
        (axes[0],cli,('thread_cpu_us','wall_us'),cli_report['block_size']/cli_report['sample_rate'],f"Unpaced engine CLI · {cli_report['block_size']} frames"),
        (axes[1],native,('callback_cpu_us','callback_wall_us'),native_frames/native_rate,f'Installed AU on Core Audio · {native_frames} frames'),
    ]:
        cpu = np.array([float(row[keys[0]])/1000 for row in rows])
        wall = np.array([float(row[keys[1]])/1000 for row in rows])
        limit = budget*1000; late = wall>limit
        axis.scatter(cpu[~late],wall[~late],s=5,alpha=.25,color='#197c9f',rasterized=True)
        if np.any(late):
            axis.scatter(cpu[late],wall[late],s=24,color='#c73535',label='Wall time over frame period')
            axis.legend(loc='lower right',fontsize=8)
        axis.axhline(limit,color='#b86a10',ls='--',lw=1)
        axis.axvline(limit,color='#b86a10',ls='--',lw=1)
        extra_cpu = [float(row['callback_cpu_us'])/1000 for row in startup] if axis is axes[1] else []
        extra_wall = [float(row['callback_wall_us'])/1000 for row in startup] if axis is axes[1] else []
        axis.set(xlabel='Thread CPU time (ms)',ylabel='Wall service window (ms)',title=title,
                 xlim=(0,max([limit,float(np.max(cpu))]+extra_cpu)*1.1),ylim=(0,max([limit,float(np.max(wall))]+extra_wall)*1.1))
        axis.grid(alpha=.15)
        axis.text(.97,.97,f'{len(rows):,} windows\n{np.sum(late)} wall overruns\n{np.sum(cpu>limit)} CPU overruns\nPeriod {limit:.3f} ms',
                  transform=axis.transAxes,ha='right',va='top',fontsize=9,bbox=dict(facecolor='white',alpha=.9,edgecolor='#ddd'))
    axes[1].scatter([float(row['callback_cpu_us'])/1000 for row in startup],
                    [float(row['callback_wall_us'])/1000 for row in startup],marker='*',s=90,color='#665099',label='Startup MIDI burst')
    axes[1].legend(loc='lower right',fontsize=8)
    fig.suptitle('Different test conditions and frame periods; this is not a speed comparison',fontsize=12)
    fig.supxlabel('Late CLI windows do not establish their cause. Clean device runs do not qualify other buffers, hosts, or workloads.',fontsize=9)
    for extension in ('png','svg'):
        fig.savefig(folder/('callback-timing.'+extension),dpi=160)
    plt.close(fig)
    print('Saved paired timing plots for',len(cli),'CLI windows and',len(native)+len(startup),'actual AU callbacks')


if __name__=='__main__':
    main()
