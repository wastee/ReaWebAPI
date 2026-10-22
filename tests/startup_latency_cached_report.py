"""Summarize cached reopening without adding nested observer durations."""
import csv
import json
import math
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'latency-results/2026-10-22-cached'
RUNS = {'before': ['guard34-forward', 'guard34-final'],
        'after': ['candidate34-forward', 'candidate34-reverse']}

def distribution(values):
    values = sorted(values)
    return dict(n=len(values), mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[math.ceil(len(values)*.95)-1], min=values[0], max=values[-1])

def read(name):
    return [json.loads(line) for line in (BASE/(name+'.jsonl')).read_text(encoding='utf-8').splitlines()]

def flatten(row):
    result = {k: row[k] for k in ['run','sample','ok','warmup','apiReturnMs','revealObservedUpperMs','apiDiagnosticsMs','pollCostMs']}
    result.update(row['diagnostics']['startup']['milestones'])
    result.update({'apiObserver.'+k:v for k,v in row['apiObserver'].items()})
    result['afterApiToDetectionMs'] = row['revealObservedUpperMs']-row['apiReturnMs']
    # Hook processing is excluded while notification inspection is active.
    # Nested inspect/prepare/hide phases are never added to these outer scopes.
    result['otherSynchronousMs'] = row['apiReturnMs']-row['apiObserver']['hookMs']-row['apiObserver']['notificationMs']
    for name,start,end in [
        ('binding', 'fxDiscoveredMs','bindingReadyMs'),
        ('association','bindingReadyMs','associationReadyMs'),
        ('hostPrepare','associationReadyMs','hostPreparedMs'),
        ('sizeRestore','hostPreparedMs','sizeRestoredMs'),
        ('cacheMatch','sizeRestoredMs','cacheMatchedMs'),
        ('reparent','cacheMatchedMs','reparentReadyMs'),
        ('hostAttach','reparentReadyMs','hostAttachedMs'),
        ('stateNotify','hostAttachedMs','stateNotifiedMs'),
        ('stateToPresent','stateNotifiedMs','presentMs'),
        ('presentToReveal','presentMs','revealCompleteMs')]:
        result['nativePhase.'+name+'Ms'] = result[end]-result[start]
    return result

def main():
    summary = {}
    flat = []
    rows_by_arm = {}
    for arm,names in RUNS.items():
        records = [row for name in names for row in read(name)]
        assert all(r['ok'] and r['audioBefore']==r['audioAfter'] and r['audioBefore']['SRATE']=='44100' for r in records)
        assert all(r['idleTarget']==0 and r['diagnostics']['version']=='0.3.8.34' for r in records)
        records = [r for r in records if not r['warmup']]
        assert len(records)==60
        assert all(r['diagnostics']['startup']['kind']=='cachedReopen' for r in records)
        assert all(r['diagnostics']['startup']['documentGeneration']==1 for r in records)
        values = [dict(arm=arm,**flatten(r)) for r in records]
        rows_by_arm[arm] = values
        flat += values
        numeric = [k for k,v in values[0].items() if isinstance(v,(int,float)) and not isinstance(v,bool)]
        summary[arm] = {k:distribution([r[k] for r in values]) for k in numeric}
        summary[arm]['totalSharesPercent'] = {k:100*summary[arm][k]['mean']/summary[arm]['revealObservedUpperMs']['mean']
            for k in ['apiReturnMs','apiObserver.notificationMs','apiObserver.hookMs','apiObserver.hideControlsMs','otherSynchronousMs','afterApiToDetectionMs']}
    (BASE/'cached-statistics.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    with (BASE/'cached-samples.csv').open('w',newline='',encoding='utf-8-sig') as file:
        writer=csv.DictWriter(file,fieldnames=list(flat[0]))
        writer.writeheader();writer.writerows(flat)
    with (BASE/'cached-statistics.csv').open('w',newline='',encoding='utf-8-sig') as file:
        writer=csv.writer(file);writer.writerow(['arm','metric','n','mean','p50','p95','min','max'])
        for arm,metrics in summary.items():
            for key,stats in metrics.items():
                if key!='totalSharesPercent': writer.writerow([arm,key]+[stats[k] for k in ['n','mean','p50','p95','min','max']])
    print(json.dumps({arm:{k:values[k] for k in ['apiReturnMs','revealObservedUpperMs','apiObserver.hideControlsMs','totalSharesPercent']}
                      for arm,values in summary.items()},ensure_ascii=False,indent=2))
    return summary

if __name__=='__main__': main()
