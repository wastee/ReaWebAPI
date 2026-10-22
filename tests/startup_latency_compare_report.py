"""Recompute the .33-only comparison without merging experimental arms."""
from pathlib import Path
import collections,csv,json,math,statistics

BASE=Path(__file__).resolve().parents[1]/'latency-results/2026-10-22-optimization'

def distribution(values):
    values=sorted(values)
    return dict(n=len(values),min=min(values),p50=statistics.median(values),p95=values[math.ceil(len(values)*.95)-1],max=max(values)) if values else {}

def read_rows(file):
    # REAPER's device identifiers may contain ACP bytes inside otherwise UTF-8
    # Lua output. Preserve original files and represent only those bytes as hex.
    for line in file.read_bytes().decode('utf-8','surrogateescape').splitlines():
        row=json.loads(line)
        for name in ['audioBefore','audioAfter']:
            for key in ['IDENT_IN','IDENT_OUT']:
                state=row.get(name,{})
                if isinstance(state.get(key),str):
                    state[key+'_bytesHex']=state.pop(key).encode('utf-8','surrogateescape').hex()
        yield row

def main():
    samples=[];groups=collections.defaultdict(list)
    # trace used the old bundled SDK for diagnosis only; never include it in
    # the requested latest-version comparison or its sample count.
    for file in sorted(BASE.glob('*.jsonl')):
        if not file.name.startswith(('baseline33','optimized','current33')):continue
        for row in read_rows(file):
            d=row.get('diagnostics',{})
            r={k:v for k,v in row.items() if k!='diagnostics'}
            marks=d.get('startup',{}).get('milestones',{})
            r.update(marks)
            before,after=r.get('audioBefore'),r.get('audioAfter')
            r['audioStableOpen']=bool(before and before.get('MODE') and before==after)
            for name,start,end in [('controllerMs','controllerStartMs','controllerReadyMs'),
                    ('connectToParametersMs','connectMs','parametersReadyMs'),
                    ('nativeCapabilitiesMs','connectMs','capabilitiesReadyMs'),
                    ('nativeParametersMs','parametersStartMs','parametersReadyMs'),
                    ('nativeEditorSizeMs','editorSizeStartMs','editorSizeReadyMs')]:
                if end in marks and start in marks and marks[end]>=marks[start]:r[name]=marks[end]-marks[start]
            r['prewarmed']=d.get('controllerPrewarmed')
            r['pageDetailTruncated']=False
            if d.get('startup',{}).get('kind')!='cachedReopen':
                for log in d.get('recentLogs',[]):
                    text=log['message']
                    if text.startswith('LATENCY_DETAIL '):
                        try:detail=json.loads(text[len('LATENCY_DETAIL '):])
                        except json.JSONDecodeError:r['pageDetailTruncated']=True;continue
                        for name,start,duration in detail['measures']:r[name+'Ms']=duration
                        calls=[(n,t,v) for n,t,v in detail['measures'] if n.startswith('bench.on.')]
                        if calls:r['subscriptionSpanMs']=max(t+v for n,t,v in calls)-min(t for n,t,v in calls)
                    elif text.startswith('LATENCY_STATE '):
                        state=json.loads(text[len('LATENCY_STATE '):]);p=state['snapshot']['parameters']
                        r['initialValues']=json.dumps([(x['index'],x['value']) for x in p if x['index']!=6])
                        r['initialCount']=state['snapshot']['count'];r['editorSize']=json.dumps(state['size'],sort_keys=True)
                    elif text.startswith('LATENCY_PAGE '):
                        try:page=json.loads(text[len('LATENCY_PAGE '):])
                        except json.JSONDecodeError:continue
                        r['themeAck']=page.get('syntheticAck');r['viewport']=json.dumps(page.get('viewport'),sort_keys=True)
                        r['jsAppReadyMs']=page.get('appReady')
            samples.append(r)
            if not r['warmup']:groups[tuple(r.get(k) for k in ['run','scenario','page','owner','mode','instances'])].append(r)
    fields=list(dict.fromkeys(k for r in samples for k in r))
    with (BASE/'comparison-samples.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
        writer.writerows({k:json.dumps(v,ensure_ascii=False) if isinstance(v,(dict,list)) else v for k,v in r.items()} for r in samples)
    summary=[]
    for key,rows in groups.items():
        metrics={k:distribution([r[k] for r in rows if r.get('ok') and isinstance(r.get(k),(int,float))]) for k in fields if k.endswith('Ms')}
        metrics={k:v for k,v in metrics.items() if v}
        summary.append(dict(zip(['run','scenario','page','owner','mode','instances'],key),total=len(rows),success=sum(r['ok'] for r in rows),metrics=metrics))
    (BASE/'comparison-statistics.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    by_trace={r['traceId']:r for r in samples};resource=[]
    for file in BASE.glob('*-resources.json'):
        if not file.name.startswith(('baseline33','optimized','current33')):continue
        by_sample=collections.defaultdict(list)
        for point in json.loads(file.read_text()):
            trace=point['status'].split(' ',1)[0]
            if trace not in by_trace:continue
            procs=point['processes']
            by_sample[trace].append(dict(privateMiB=sum(p['private'] for p in procs)/2**20,
                cpuPercentMachine=sum(p['cpuPercentOneCore'] or 0 for p in procs)/16,processes=len(procs)))
        for trace,points in by_sample.items():
            row=by_trace[trace]
            resource.append({**{k:row.get(k) for k in ['traceId','run','scenario','page','warmup','ok']},
                **{k:max(p[k] for p in points) for k in points[0]}})
    (BASE/'comparison-resources.json').write_text(json.dumps(resource,indent=2),encoding='utf-8')
    with (BASE/'comparison-statistics.csv').open('w',encoding='utf-8-sig',newline='') as f:
        keys=['run','scenario','page','owner','mode','instances','total','success']
        writer=csv.DictWriter(f,fieldnames=keys+['metric','n','min','p50','p95','max']);writer.writeheader()
        for s in summary:
            for name,value in s['metrics'].items():writer.writerow({**{k:s[k] for k in keys},'metric':name,**value})
    for s in summary:
        print(s['run'],s['scenario'],s['page'],s['success'],{k:round(v['p50'],2) for k,v in s['metrics'].items() if k in ['revealCompleteMs','revealObservedUpperMs','subscriptionSpanMs','nativeCapabilitiesMs','nativeParametersMs','nativeEditorSizeMs','reaweb.background.clearMs']})
    return samples,summary

if __name__=='__main__':main()
