import json,re,csv,statistics as st
from pathlib import Path
p=Path(__file__).parent
a=json.loads((p/'early-source.json').read_text());host=[]
for l in (p/'early-windows.jsonl').read_text().splitlines():
 r=json.loads(l);m=re.search(r'FRAME wire=(\d+) windows=(\d+) dirty=(\d+) move=(\d+) bytes=(\d+)',r['message'])
 if m and a['start']-2<=r['time']<=a['end']+2:host.append((r['time'],*map(int,m.groups())))
board=(p/'early-board.log').read_text().split('EARLY_RUN_BEGIN')[-1].split('EARLY_RUN_END')[0]
ev=[(int(i),int(s),int(t)+int(u)/1e6) for i,s,t,u in re.findall(r'PRESENT (\d+) (\d+) (\d+) (\d+)',board)]
# Anchor in the first second; the 10-bit id takes >=17 seconds to wrap.
j=0; output=[]
for wire,seq,t in ev:
 k=next((k for k in range(j,min(j+35,len(host))) if host[k][1]==wire),None)
 if k is None:continue
 h=host[k];j=k+1
 output.append((*h,seq,t))
assert output and len(output)>0.97*len(ev),(len(output),len(ev))
with (p/'early-matched.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['host_epoch','wire','windows','dirty','move','bytes','vblank','kms_seconds']);w.writerows(output)
first=next(r[-1] for r in output if r[0]>=a['start']+5)
assert output[-1][-1]>=first+60
selected=[r for r in output if first<=r[-1]<first+60]
seen=set(); unique=[]
for r in selected:
 if r[2] not in seen and (r[3] or r[4]):unique.append(r);seen.add(r[2])
bins=[sum(first+i<=r[-1]<first+i+1 for r in unique) for i in range(60)]
dt=[1000*(r[-1]-l[-1]) for l,r in zip(unique,unique[1:])]
hfirst=selected[0][0];hlast=selected[-1][0];hw=[r for r in host if hfirst<=r[0]<=hlast]
print('pairs',len(output),'events',len(ev),'window',len(selected),'unique',len(unique),'host',len(hw),'mean',len(unique)/60,'min/max',min(bins),max(bins),'bins>=55',sum(i>=55 for i in bins),'P95',st.quantiles(dt,n=100)[94],'bins',bins)
