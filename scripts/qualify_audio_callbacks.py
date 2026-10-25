"""Capture or recheck silent native AU callback qualification without changing device settings."""
import argparse
import copy
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inspect(report, rows):
    count = report['callbacks']
    if count < 2 or len(rows) != count or [int(row['callback']) for row in rows] != list(range(count)):
        raise ValueError('Missing or duplicated callback rows')
    wall = []; cpu = []; nonzero = valid_host = valid_sample = discontinuities = errors = steady_misses = startup_misses = 0
    energy = 0
    for index, row in enumerate(rows):
        frames = int(row['frames']); flags = int(row['timestamp_flags'])
        values = [float(row[key]) for key in ['callback_wall_us','callback_cpu_us','au_render_wall_us','au_render_cpu_us','energy']]
        if frames <= 0 or not all(math.isfinite(value) and value >= 0 for value in values):
            raise ValueError('Invalid frame count, timing, or rendered energy')
        if int(row['exit_host_time']) < int(row['entry_host_time']):
            raise ValueError('Host clock moved backward')
        if flags & 1:
            valid_sample += 1
        if flags & 2:
            valid_host += 1
        if index and flags & 1 and int(rows[index-1]['timestamp_flags']) & 1:
            previous = rows[index-1]
            if abs(float(row['sample_time'])-float(previous['sample_time'])-int(previous['frames'])) > .5:
                discontinuities += 1
        if int(row['render_status']):
            errors += 1
        if values[4] > 0:
            nonzero += 1
        energy += values[4]
        if values[0] > frames/report['sample_rate']*1e6:
            if index:
                steady_misses += 1
            else:
                startup_misses += 1
        if index:
            wall.append(values[0]); cpu.append(values[1])
    for key, observed in [('valid_host_timestamp_callbacks',valid_host), ('valid_sample_timestamp_callbacks',valid_sample),
                          ('sample_time_discontinuities',discontinuities), ('render_errors',errors),
                          ('nonzero_render_callbacks',nonzero), ('steady_service_budget_overruns',steady_misses),
                          ('startup_service_budget_overruns',startup_misses)]:
        if report[key] != observed:
            raise ValueError('Summary disagrees with callback trace: '+key)
    if not math.isclose(energy, report['pre_silencing_energy'], rel_tol=1e-9, abs_tol=1e-10):
        raise ValueError('Rendered energy summary disagrees with trace')
    if errors or not report['finite_energy'] or not report['output_silent'] or report['microphone_input']:
        raise ValueError('Render or silent-output validation failed')
    if report['mode']=='instrument' and nonzero < count-1:
        raise ValueError('Instrument workload did not produce sustained audio')
    if report['mode']=='control' and nonzero:
        raise ValueError('Empty control unexpectedly rendered audio')
    for key, values in [('callback_wall',wall), ('callback_cpu',cpu)]:
        ordered = sorted(values)
        if not math.isclose(report[key+'_p99_us'], ordered[len(ordered)*99//100], abs_tol=.005):
            raise ValueError('Percentile disagrees with callback trace')
        if not math.isclose(report[key+'_max_us'], ordered[-1], abs_tol=.005):
            raise ValueError('Maximum disagrees with callback trace')
    if report['device_settings_changed'] or valid_host != count or valid_sample != count or discontinuities or report['device_overload_notifications'] or not report['device_overload_notifications_supported']:
        return 'inconclusive_device_observation'
    if steady_misses or startup_misses:
        return 'service_budget_exceeded'
    return 'clean_observed_run'


def check_counterexamples(report, rows):
    checks = []
    changed = copy.deepcopy(report); changed_rows = copy.deepcopy(rows)
    for row in changed_rows:
        row['energy'] = '0'
    changed['nonzero_render_callbacks'] = 0; changed['pre_silencing_energy'] = 0
    try:
        inspect(changed, changed_rows)
    except ValueError as error:
        assert 'sustained audio' in str(error)
        checks.append('silent instrument rejected despite consistent zero-energy metadata')
    else:
        raise AssertionError('Silent instrument incorrectly qualified')
    changed = copy.deepcopy(report); changed_rows = copy.deepcopy(rows)
    for row in changed_rows:
        row['timestamp_flags'] = '0'
    changed['valid_host_timestamp_callbacks'] = changed['valid_sample_timestamp_callbacks'] = 0
    changed['sample_time_discontinuities'] = 0
    assert inspect(changed, changed_rows) == 'inconclusive_device_observation'
    checks.append('unavailable timestamps do not qualify as continuous playback')
    changed = copy.deepcopy(report); changed['device_overload_notifications'] = 1
    assert inspect(changed, rows) == 'inconclusive_device_observation'
    checks.append('device overload prevents clean qualification without blaming the plugin')
    changed = copy.deepcopy(report); changed['device_overload_notifications_supported'] = False
    assert inspect(changed, rows) == 'inconclusive_device_observation'
    checks.append('unsupported overload notifications are unavailable evidence rather than zero overloads')
    changed = copy.deepcopy(report); changed['device_settings_changed'] = True
    assert inspect(changed, rows) == 'inconclusive_device_observation'
    checks.append('changed device configuration prevents clean qualification')
    changed = copy.deepcopy(report); changed_rows = copy.deepcopy(rows)
    changed_rows[1]['callback_wall_us'] = str(int(changed_rows[1]['frames'])/changed['sample_rate']*1e6*2)
    timings = sorted(float(row['callback_wall_us']) for row in changed_rows[1:])
    changed['callback_wall_p99_us'] = timings[len(timings)*99//100]
    changed['callback_wall_max_us'] = timings[-1]
    changed['steady_service_budget_overruns'] = sum(float(row['callback_wall_us']) > int(row['frames'])/changed['sample_rate']*1e6 for row in changed_rows[1:])
    assert inspect(changed, changed_rows) == 'service_budget_exceeded'
    checks.append('excess wall service time fails even when recorded CPU stays below budget')
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=ROOT/'build/TopBrassNativeAudioAudit')
    parser.add_argument('--baseline', type=Path, default=ROOT/'artifacts/validation-summary.json')
    parser.add_argument('--callbacks', type=int, default=2000)
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--analyze', action='store_true')
    parser.add_argument('--verify', action='store_true', help='Recheck an existing report and its raw traces without replacing it')
    args = parser.parse_args(); output = args.output.resolve()
    if args.analyze and args.verify:
        raise ValueError('Choose analyze or verify')
    previous = json.loads((output/'native-results.json').read_text()) if args.verify else None
    if (output/'native-results.json').exists() and not args.verify:
        raise ValueError('Existing qualification report retained; choose another output folder')
    if not args.analyze and not args.verify:
        output.mkdir(parents=True, exist_ok=True)
        if not 100 <= args.callbacks <= 100000 or not 1 <= args.repeats <= 10:
            raise ValueError('Invalid callback or repeat count')
        cases = [('native-control-long','control')]+[(f'native-instrument-long-{index+1}','instrument') for index in range(args.repeats)]
        for name, mode in cases:
            with (output/(name+'.log')).open('w') as log:
                result = subprocess.run([str(args.tool.resolve()),str(output/name),str(args.callbacks),mode],stdout=log,stderr=subprocess.STDOUT)
            if result.returncode not in (0,2,3):
                raise RuntimeError('Native audit failed: '+name)
    baseline = json.loads(args.baseline.read_text())
    expected = next(row for row in baseline['bundles'] if row['format']=='AU')
    versions = [int(value) for value in baseline['version'].split('.')]
    expected_version = versions[0]*65536+versions[1]*256+versions[2]
    results = []; counterexamples = []
    for path in sorted(output.glob('native-*-long*.json')):
        report = json.loads(path.read_text())
        with path.with_suffix('.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        status = inspect(report, rows)
        if report['component_version'] != expected_version or sha(Path(report['component_image'])) != expected['executable_sha256']:
            raise ValueError('Audited component does not match the selected release')
        if report['mode']=='instrument' and not counterexamples:
            counterexamples = check_counterexamples(report, rows)
        results.append(dict(case=path.stem, status=status, report=report, json_sha256=sha(path), csv_sha256=sha(path.with_suffix('.csv'))))
    instruments = [row for row in results if row['report']['mode']=='instrument']
    controls = [row for row in results if row['report']['mode']=='control']
    if not instruments or not controls:
        raise ValueError('Both instrument and empty-output control captures are required')
    configurations = {(row['report']['device_id'], row['report']['sample_rate'], row['report']['device_buffer_frames']) for row in results}
    if len(configurations) != 1:
        raise ValueError('Current-device qualification requires matching device configurations')
    summary = dict(version=baseline['version'], installed_au_executable_sha256=expected['executable_sha256'],
                   tool_path=str(args.tool.resolve()), tool_sha256=sha(args.tool.resolve()),
                   cases=results, qualification_counterexamples=counterexamples,
                   all_observed_runs_clean=all(row['status']=='clean_observed_run' for row in results),
                   instrument_callbacks=sum(row['report']['callbacks'] for row in instruments),
                   limits=['Current device and observed buffer size only; no128-frame hardware or Logic/MainStage qualification.',
                           '128 MIDI note-ons and eight-channel breath triggers are the input load; active playhead count is not observed through this AU interface.',
                           'Service time excludes final telemetry publication. Device overload notifications cannot identify the responsible client.',
                           'Finite nonzero rendering and timing do not establish musical realism or the cause of intermittent CLI stalls.'])
    if previous is not None:
        if previous['installed_au_executable_sha256'] != summary['installed_au_executable_sha256'] or previous['tool_sha256'] != summary['tool_sha256']:
            raise ValueError('Qualification input identity changed')
        if previous['cases'] != summary['cases'] or previous['all_observed_runs_clean'] != summary['all_observed_runs_clean']:
            raise ValueError('Qualification report disagrees with current trace verification')
    else:
        (output/'native-results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('Verified',summary['instrument_callbacks'],'actual AU callbacks; all observed runs clean:',summary['all_observed_runs_clean'])
    if not summary['all_observed_runs_clean']:
        raise SystemExit(2)


if __name__=='__main__':
    main()
