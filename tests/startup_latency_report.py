"""Summarize raw benchmark JSONL without conflating clock domains or visibility."""
import csv,json,math,statistics
from pathlib import Path
from collections import defaultdict
BASE=Path(__file__).resolve().parents[1]/'latency-results/2026-10-22'
def stats(values):
    a=sorted(v for v in values if v is not None)
    if not a:return dict(n=0,min=None,p50=None,p95=None,max=None)
    return dict(n=len(a),min=min(a),p50=statistics.median(a),p95=a[math.ceil(.95*len(a))-1],max=max(a))
def flatten(row):
    d=row.get('diagnostics',{});s=d.get('startup',{});m=s.get('milestones',{})
    r={k:v for k,v in row.items() if k!='diagnostics'}
    r.setdefault('resident',False)
    if row['scenario']=='J1' and not row.get('settleSeconds'):r['scenario']='J1-early'
    r.update(kind=s.get('kind'),environmentMode=d.get('environmentStartup',{}).get('mode'),controllerPrewarmed=d.get('controllerPrewarmed'),version=d.get('version'))
    # A3 reuses the old creation timeline. A4 creation reveal includes the deliberate hidden wait.
    if row['scenario']!='A3':
        r.update(m)
        for label,start,end in [('environmentWaitMs','environmentWaitStartMs','environmentReadyMs'),('controllerMs','controllerStartMs','controllerReadyMs'),('controllerToNavigationMs','controllerReadyMs','navigationMs'),('navigationToLoadMs','navigationMs','loadCompleteMs'),('snapshotWaitMs','frameRequestMs','frameReadyMs'),('parametersToPresentMs','parametersReadyMs','presentMs'),('presentToRevealMs','presentMs','revealCompleteMs'),('connectToParametersMs','connectMs','parametersReadyMs')]:
            if start in m and end in m and m[end]>=m[start]:r[label]=m[end]-m[start]
    if row['scenario'] not in ['A3','A4'] and 'environmentWaitStartMs' in m:r['beforeEnvironmentMs']=m['environmentWaitStartMs']
    # A cached reopen retains the first document's page logs; never report those as a new interaction.
    if s.get('kind')!='cachedReopen' and row['scenario']!='A3':
        for log in d.get('recentLogs',[]):
            if log.get('message','').startswith('LATENCY_PAGE '):
                text=log['message'].split(' ',1)[1]
                try:page=json.loads(text)
                except json.JSONDecodeError:
                    # ReaWeb_Log's preview is bounded. Recover only a complete,
                    # independently parseable prefix; never infer missing values.
                    r['pageTraceTruncated']=True
                    try:page=json.loads(text.split(',"navigation":',1)[0]+'}')
                    except json.JSONDecodeError:continue
                for k in ['dom','load','sdk','appReady','raf1','raf2','syntheticClick']:r['js_'+k+'Ms']=page.get(k)
                r['syntheticAck']=page.get('syntheticAck')
                r['viewport']=json.dumps(page.get('viewport'))
                nav=page.get('navigation') or {}
                if 'domContentLoadedEventEnd' in nav:r['js_domEventDurationMs']=nav['domContentLoadedEventEnd']-nav['domContentLoadedEventStart']
                if 'canvasCalls' in page:
                    calls=page['canvasCalls']
                    r['js_canvasTotalMs']=sum(x['duration'] for x in calls)
                    r['js_canvasMaxMs']=max((x['duration'] for x in calls),default=0)
                if 'backgroundCalls' in page:
                    for name in ['getComputedStyle','clearRect','fillRect','getImageData']:
                        calls=[x['duration'] for x in page['backgroundCalls'] if x['name']==name]
                        r['js_'+name+'TotalMs']=sum(calls)
                        r['js_'+name+'MaxMs']=max(calls,default=0)
    r['nativeLastError']=d.get('lastError')
    r['pageErrors']=json.dumps([l['message'] for l in d.get('recentLogs',[]) if l.get('level')=='error'],ensure_ascii=False)
    return r
def main():
    rows=[]
    for f in sorted(BASE.glob('*.jsonl')):
        if f.name.startswith('smoke'):continue
        for line in f.read_text(encoding='utf-8').splitlines():
            if line.strip():rows.append(flatten(json.loads(line)))
    fields=list(dict.fromkeys(k for r in rows for k in r))
    with (BASE/'samples.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    groups=defaultdict(list)
    for r in rows:
        if r['warmup']:continue
        key=tuple(r[k] for k in ['scenario','page','owner','mode','instances','idleTarget','resident'])
        groups[key].append(r)
    summaries=[]
    for key,rs in groups.items():
        metrics={k:stats([r.get(k) for r in rs if r.get('ok')]) for k in fields if k.endswith('Ms')}
        summaries.append(dict(zip(['scenario','page','owner','mode','instances','idleTarget','resident'],key),total=len(rs),success=sum(r['ok'] for r in rs),metrics=metrics,
          kinds=dict((kind,sum(r.get('kind')==kind for r in rs)) for kind in {r.get('kind') for r in rs}),
          prewarmed=sum(r.get('controllerPrewarmed')==True for r in rs)))
    (BASE/'statistics.json').write_text(json.dumps(summaries,indent=2,ensure_ascii=False),encoding='utf-8')
    with (BASE/'statistics.csv').open('w',newline='',encoding='utf-8-sig') as f:
        fields=['scenario','page','owner','mode','instances','idleTarget','resident','total','success','metric','n','min','p50','p95','max']
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
        for s in summaries:
            for metric,values in s['metrics'].items():
                if values['n']:w.writerow({**{k:s[k] for k in fields[:9]},'metric':metric,**values})
    for s in summaries:
        v=s['metrics']['revealObservedUpperMs'];n=s['metrics'].get('revealCompleteMs',{})
        print(s['scenario'],s['page'],s['owner'],s['mode'],s['instances'],s['idleTarget'],'n',s['success'],'upper',round(v['p50'] or 0,1),'native',round(n.get('p50') or 0,1),'prewarm',s['prewarmed'])
    resource=[]
    by_trace={r['traceId']:r for r in rows};resource_samples=[]
    for f in BASE.glob('*-resources.json'):
        if f.name.startswith('smoke'):continue
        rr=json.loads(f.read_text());points=[];seen_browser=set();by_sample=defaultdict(list)
        for p in rr:
            ps=p['processes'];parent=[v for v in ps if v['role']=='reaper'];child=[v for v in ps if v['role']!='reaper']
            browsers={v['pid'] for v in child if v['role']=='browser'}
            point=dict(elapsed=p['elapsed'],status=p['status'],processCount=len(ps),browserCount=len(browsers),newBrowserPids=len(browsers-seen_browser),
               privateMiB=sum(v['private'] for v in ps)/2**20,reaperPrivateMiB=sum(v['private'] for v in parent)/2**20,
               webviewPrivateMiB=sum(v['private'] for v in child)/2**20,
               reaperWorkingSetMiB=sum(v['rss'] for v in parent)/2**20,
               maxSingleWebviewWorkingSetMiB=max([v['rss']/2**20 for v in child],default=0),
               cpuPercentMachine=sum(v['cpuPercentOneCore'] or 0 for v in ps)/16)
            seen_browser|=browsers;points.append(point)
            trace=p['status'].split(' ',1)[0]
            if trace in by_trace:by_sample[trace].append(point)
        resource.append(dict(run=f.name.removesuffix('-resources.json'),uniqueBrowserPids=len(seen_browser),metrics={k:stats([x[k] for x in points]) for k in points[0] if k not in ['status','elapsed']} if points else {}))
        for trace,values in by_sample.items():
            row=by_trace[trace]
            resource_samples.append({**{k:row[k] for k in ['traceId','scenario','page','owner','mode','instances','idleTarget','resident','warmup','ok']},
                **{k:max(v[k] for v in values) for k in values[0] if k not in ['status','elapsed','newBrowserPids']},
                'newBrowserPids':sum(v['newBrowserPids'] for v in values),'sampleCount':len(values)})
    (BASE/'resource-statistics.json').write_text(json.dumps(resource,indent=2),encoding='utf-8')
    if resource_samples:
        with (BASE/'resource-samples.csv').open('w',newline='',encoding='utf-8-sig') as f:
            w=csv.DictWriter(f,fieldnames=list(resource_samples[0]));w.writeheader();w.writerows(resource_samples)
if __name__=='__main__':main()
