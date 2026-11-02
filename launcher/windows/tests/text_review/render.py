import json,sys,re,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from proposals import P
D=json.load(open(sys.argv[1]))
SW=D['switches'];EFF=D['effects']
def brief(t,mx=64):
    if not t: return None
    if len(t)<=mx: return t
    for end in ['. ','; ',': ']:
        at=t.find(end)
        if 20<=at<mx: return t[:at]+('.' if end=='. ' else '…')
    cut=t.rfind(' ',0,mx+1)
    return (t[:cut] if cut>20 else t[:mx]).rstrip(',;: ')+'…'
def clean(v):
    if v is None: return None
    v=str(v).strip().strip('"')
    return v
def deflabel(r):
    d=clean(r.get('default'))
    if d is None: return None
    if d.startswith('Updater.CurrentBeta'): return 'On in betas, off in stable releases'
    if r.get('kind') in ('switch',):
        return {'true':'On','1':'On','false':'Off','0':'Off'}.get(d,d)
    for c in r.get('choices') or []:
        if clean(c['value'])==d: return c['label']
    return d or None
PAGES={'Play':'Play','Display':'Display','Graphics':'Graphics','Interface':'Interface','Gameplay':'Gameplay','Controls':'Controls',
       'Engine':'Engine switches','Output':'Log','Settings':'Settings','Online':'Online','Mods':'Mods','Cheats':'Cheats','Trophies':'Trophies'}
DETAIL=['effect_dynamic_shadows','effect_ssao','effect_ssr'];LOOK=['effect_dof','effect_motion_blur','effect_chromatic_aberration']
EXTRAS=['skip_intro','debug_camera','debug_menu']
FIXES=['BB_RESPAWN_DELAY_FIX','BB_SOUND_HACK','BB_OPTISCALER_COMPAT','BB_EFFECT_FIXES','BB_PS4_FPU','BB_RED_ZONE_FUNCTIONS','BB_INTEL_TONEMAP_FIX','BB_SKIP_GAME_CHECK']
EXTRA_NOW={('Graphics','Sharpening (RCAS)'):"Line set by the upscaler: DLSS \"After DLSS (it has none of its own)\", AMD FSR \"FSR's own sharpening\", TAA \"After TAA\", none \"Needs an upscaler or TAA\"",
           ('Graphics','Sharpening strength'):"Line set by the upscaler: AMD FSR \"Above 1 adds a second pass\", otherwise \"Higher is sharper\"",
           ('Graphics','DLSS model'):"Status line with Auto: \"Auto: model K at this quality\" (the model Auto picks)",
           ('Graphics','Anti-aliasing'):"With an upscaler the box shows \"Upscaler (DLSS)\" (or AMD FSR), greyed",
           ('Graphics','Upscaler'):"Status line when an upscaler is unavailable on this PC"}
def expand():
    out=[]
    for r in D['rows']:
        pg=r['page'][:-4]; g=r.get('group') or ''
        g={'L.T(EngineSwitches.Performance)':'Performance','L.T(EngineSwitches.Sound)':'Sound','L.T(group)':'Game fixes'}.get(g,g)
        r=dict(r,page=pg,group=g)
        t=r.get('title') or ''
        if r['kind']=='engine':
            if r.get('env'): out.append(dict(page=pg,group=g,kind='engine',env=r['env']))
            elif g=='Game fixes' and not any(x.get('env')=='BB_RESPAWN_DELAY_FIX' for x in out):
                for e in FIXES: out.append(dict(page=pg,group=g,kind='engine',env=e))
            continue
        if t=='‹effect.Title›':
            keys=DETAIL if g=='Detail' else LOOK if g=='Effects' else EXTRAS
            order=[k for k in EFF if k in keys]
            for k in order: out.append(dict(page=pg,group=g,kind='effect',key=k))
            continue
        if t=='‹title›' and pg=='Interface' and r['kind']=='switch':
            for name,dflt in [('Frame rate','On'),('Frame time','On'),('Upscaler','On'),('CPU usage','Off'),('GPU usage','Off')]:
                out.append(dict(page=pg,group=g,kind='switch',title=name,desc='Shown in the FPS counter',default_label=dflt))
            continue
        if t.startswith('‹') or t.startswith('{'): continue
        if pg=='Cheats' and t=='Amount':
            r['title']='Amount'; r['desc']='How many {title} to keep'
        out.append(r)
    return out
def now_fields(r):
    if r['kind']=='engine':
        s=SW[r['env']]
        dv={'on':'on','off':'off','':'not set'}.get(s.get('default'),None)
        if dv is None:
            dv=next((o['label'] for o in s.get('options',[]) if clean(o['value'])==clean(s.get('default'))),s.get('default'))
        tip=f"{s['hint']}\n{r['env']}, default: {dv}. Applied when the game starts"
        ch=s.get('options')
        return s['title'],brief(s['hint']),tip,ch,dv
    if r['kind']=='effect':
        e=EFF[r['key']]
        return e['title'],brief(e['hint']),e['hint'],None,('On' if e['default']=='true' else 'Off')
    desc=r.get('desc')
    tips=[x for x in (r.get('tip') or []) if x]
    tip=desc if not tips else ((desc+'\n\n') if desc else '')+'\n'.join(tips)
    return r['title'],brief(desc),tip,r.get('choices'),r.get('default_label') or deflabel(r)
def pkey(r):
    if r['kind']=='engine': return (r['env'],)
    if r['kind']=='effect': return (r['key'],)
    return (r['page'],r['title'])
def fmt_choices(ch):
    if not ch: return '—'
    return '<br>'.join(re.sub(r'\{(\d+)\}',r'\1',f"{c['label']}" + (f" · {c['note']}" if c.get('note') else '')) for c in ch)
def esc(s):
    s=re.sub(r'\{(\d+)\}',r'\1',s or '—')
    return s.replace('|','\\|').replace('\n','<br>')
lines=[];missing=[]
rows=expand()
cur_page=None;cur_group=None;count=0
for r in rows:
    title,line_now,tip_now,ch_now,def_now=now_fields(r)
    k=pkey(r); p=P.get(k)
    if p is None: missing.append(k); p={}
    if r['page']!=cur_page:
        cur_page=r['page'];cur_group=None
        lines.append(f"\n## {PAGES[cur_page]}\n")
    if r['group']!=cur_group:
        cur_group=r['group']
        if cur_group:
            name=cur_group.replace(' (fold)','')
            name=(name.strip() or 'Advanced')+(' (folded)' if cur_group.endswith('(fold)') else '')
            lines.append(f"\n### {name}\n")
    count+=1
    newtitle=p.get('rename') or title
    pl=p.get('line')
    if pl=='keep': pl=line_now
    ptip=p.get('tip')
    dl=def_now
    auto=None
    if dl and dl not in('not set',):
        rec=p.get('recommended') or any(c.split(' · ')[0].replace(' (recommended)','')==dl and '(recommended)' in c for c in p.get('choices') or [])
        auto=f"Default: {dl}" + (" (recommended)" if rec else "")
    ptip_full=(ptip+'<br>' if ptip else '')+(f"*[{auto}]*" if auto else '')
    lines.append(f"#### {newtitle}" + (f" (now: {title})" if p.get('rename') else ''))
    lines.append("| | Now | Proposed |\n|---|---|---|")
    lines.append(f"| Title | {esc(title)} | {esc(newtitle) if p.get('rename') else 'Same'} |")
    lines.append(f"| Line under the title (Classic) | {esc(line_now)} | {esc(pl)} |")
    lines.append(f"| Tooltip (hover; Modern's (i)) | {esc(tip_now)} | {ptip_full or '—'} |")
    extra=EXTRA_NOW.get((r['page'],title))
    if extra: lines.append(f"| Status line | {esc(extra)} | {'Shown only when it explains why the setting is unavailable' if 'Sharpening' in title else 'Same'} |")
    if ch_now or p.get('choices'):
        lines.append(f"| Choices | {fmt_choices(ch_now) if ch_now else esc(r.get('choices_expr'))} | {'<br>'.join(p['choices']) if p.get('choices') else 'Same'} |")
    if def_now: lines.append(f"| Default | {esc(def_now)} | {'Same' } |")
    if p.get('ingame'): lines.append(f"| In-game menu | {esc(p['ingame'])} | Marked with the in-game icon |")
    if p.get('impact'): lines.append(f"| Performance | — | {esc(p['impact'])} |")
    if p.get('note'): lines.append(f"| Note | | {esc(p['note'])} |")
    lines.append('')
open(sys.argv[2],'w',encoding='utf-8').write('\n'.join(lines))
print(count,'rows rendered; missing proposals:',missing)
