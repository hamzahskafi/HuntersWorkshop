# Extracts every launcher row's texts (title, description, tooltip, choices, default) to JSON.
# Usage: python3 extract.py now.json, then python3 render.py now.json rows.md; the review is head.md + rows.md + tail.md.
import re,json,os,sys
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','BloodborneLauncher')
def strip_comments(t):
    out=[];i=0;n=len(t)
    while i<n:
        c=t[i]
        if c=='"' or (c in '@$' and i+1<n and t[i+1]=='"') or (c=='$' and t[i+1:i+3]=='@"'):
            j=i
            verbatim = c=='@' or t[i:i+3]=='$@"'
            while t[j]!='"': j+=1
            j+=1
            while j<n:
                if not verbatim and t[j]=='\\': j+=2; continue
                if t[j]=='"':
                    if verbatim and t[j+1:j+2]=='"': j+=2; continue
                    break
                j+=1
            out.append(t[i:j+1]); i=j+1; continue
        if t.startswith('//',i):
            k=t.find('\n',i); i=n if k<0 else k; continue
        if t.startswith('/*',i):
            k=t.find('*/',i); i=n if k<0 else k+2; continue
        out.append(c); i+=1
    return ''.join(out)
def args_of(t,i):
    depth=0;j=i;args=[];cur=''
    while j<len(t):
        c=t[j]
        if c=='"' or (c in '@$' and t[j+1]=='"'):
            k=j+1 if c=='"' else j+2
            verbatim=(c=='@')
            while k<len(t):
                if t[k]=='\\' and not verbatim: k+=2; continue
                if t[k]=='"':
                    if verbatim and t[k+1:k+2]=='"': k+=2; continue
                    break
                k+=1
            cur+=t[j:k+1]; j=k+1; continue
        if c in '([{':
            depth+=1
            if depth==1 and c=='(': j+=1; continue
        elif c in ')]}':
            depth-=1
            if depth==0: args.append(cur.strip()); return args,j
        elif c==',' and depth==1:
            args.append(cur.strip()); cur=''; j+=1; continue
        cur+=c; j+=1
    return args,j
def text(a):
    if a is None: return None
    parts=re.findall(r'"((?:[^"\\]|\\.)*)"',a)
    if not parts:
        return None
    s=''.join(parts).replace('\\"','"').replace('\\\\','\\').replace('\\n','\n')
    return s
def calls(t,name):
    for m in re.finditer(r'\bnew(?:\s+Choice)?\s*\(',t):
        yield m
def parse_choices(body):
    out=[]
    for m in re.finditer(r'\bnew(?:\s+Choice)?\s*\(',body):
        a,_=args_of(body,m.end()-1)
        if len(a)<2: continue
        label=text(a[0]) if '"' in a[0] else a[0]
        value=text(a[1]) if '"' in a[1] else a[1]
        note=text(a[2]) if len(a)>2 and '"' in a[2] else None
        out.append({'label':label,'value':value,'note':note})
    return out
# Choices.cs arrays
ch=strip_comments(open(f'{ROOT}/Core/Choices.cs',encoding='utf-8-sig').read())
CHOICES={}
for m in re.finditer(r'public static readonly Choice\[\] (\w+)\s*=\s*\{',ch):
    depth=1;j=m.end()
    while depth: 
        if ch[j]=='{': depth+=1
        elif ch[j]=='}': depth-=1
        j+=1
    CHOICES[m.group(1)]=parse_choices(ch[m.end():j-1])
# Effects
EFFECTS={}
m=re.search(r'Effect\[\] Effects\s*=\s*\{(.*?)\n\s*\};',ch,re.S)
for e in re.finditer(r'new\(',m.group(1)):
    a,_=args_of(m.group(1),e.end()-1)
    EFFECTS[text(a[0])]={'title':text(a[1]),'default':a[2],'hint':text(a[3]) if len(a)>3 else None}
# Engine switches
es=strip_comments(open(f'{ROOT}/Core/EngineSwitches.cs',encoding='utf-8-sig').read())
SW={}
body=es[es.index('All = new[]'):]
for m in re.finditer(r'\b(Pick|Toggle|Flag|OnUnless0|Text)\(\s*"(BB_\w+)"',body):
    a,_=args_of(body,m.end()-len(m.group(2))-3+len(m.group(1))+0) if False else args_of(body,body.index('(',m.start()))
    kind=m.group(1); env=text(a[0])
    d={'kind':kind,'env':env,'group':a[1],'title':text(a[2]),'hint':text(a[3])}
    if kind=='Pick':
        d['default']=text(a[4]) if '"' in a[4] else a[4]
        opts=','.join(a[5:])
        if opts.strip().startswith('Auto('):
            aa,_=args_of(opts,opts.index('('))
            d['options']=[{'label':'Auto','value':'','note':text(aa[0])}]+[{'label':text(x),'value':text(x),'note':None} for x in aa[1:]]
        elif opts.strip()=='AutoOnOff':
            d['options']=[{'label':'Auto','value':'','note':None},{'label':'On','value':'1','note':None},{'label':'Off','value':'0','note':None}]
        else:
            d['options']=parse_choices(opts)
    elif kind=='Flag': d['default']='off'
    elif kind=='OnUnless0': d['default']='on'
    elif kind=='Text': d['default']=''; d['placeholder']=text(a[4])
    SW[env]=d
# defaults
ls=open(f'{ROOT}/Core/LauncherSettings.cs',encoding='utf-8-sig').read()
LDEF={k:v.strip() for k,v in re.findall(r'\["([\w:]+)"\]\s*=\s*([^,\n]+),',ls)}
ini=open(f'{ROOT}/Core/IniFile.cs',encoding='utf-8-sig').read()
IDEF=dict(re.findall(r'\("(\w+)", "([^"]*)"\)',ini))
HELP={ # name: (kind, title idx, hint idx, choices idx, key idx or 'get' arg idx)
 'Card':('card',1,2,None,None),'ComboRow':('list',1,2,3,('get',4)),'SettingCombo':('list',1,2,3,4),'IniCombo':('list',1,2,3,4),
 'TrackedCombo':('list',1,2,3,4),'ToggleRow':('switch',1,2,None,('get',3)),'SettingToggle':('switch',1,2,None,3),'IniToggle':('switch',1,2,None,3),
 'Tracked':('switch',1,2,None,3),'Feature':('switch',1,2,None,3),'SliderRow':('slider',1,2,None,('get',6)),'TuningSlider':('slider',1,None,None,4),
 'CameraSlider':('slider',1,None,None,4),'IniSlider':('slider',1,None,None,4),'IniText':('text',1,2,None,4),'TextRow':('text',1,2,None,('get',4)),
 'FoldToggle':('switch',1,2,None,('get',3)),'Cheat':('cheat',1,None,None,2),'SwitchRow':('engine',0,None,None,None),'Switch':('engine',0,None,None,None),
 'Group':('group',0,None,None,None),'Advanced':('advanced',0,None,None,None),'SetTip':('tip',0,None,None,None),'SetInfo':('info',0,None,None,None)}
pat=re.compile(r'(?<![\w.])(?:LastRow\.|_\w+\.)?\b('+'|'.join(HELP)+r')\s*\(')
def keyof(a,spec):
    if spec is None: return None,None
    if isinstance(spec,tuple):
        idx=spec[1]
        s=a[idx] if len(a)>idx else ''
        m=re.search(r'(?:Ini\.Get|Ini\.Flag|S\.Str|S\.Bool|S\.Int)\("(\w+)"(?:,\s*"([^"]*)")?',s)
        return (m.group(1),m.group(2)) if m else (None,None)
    s=a[spec] if len(a)>spec else ''
    return (text(s),None)
def default(key,fb):
    if fb is not None: return fb
    if key in IDEF: return IDEF[key]
    if key in LDEF: return LDEF[key]
    return None
rows=[]
pages=['PlayPage','DisplayPage','GraphicsPage','InterfacePage','GameplayPage','ControlsPage','EnginePage','OutputPage','SettingsPage','OnlinePage','ModsPage','CheatsPage','TrophiesPage']
for pg in pages:
    t=strip_comments(open(f'{ROOT}/Views/{pg}.cs',encoding='utf-8-sig').read())
    group=None
    for m in pat.finditer(t):
        name=m.group(1)
        pre=t[max(0,m.start()-40):m.start()]
        if re.search(r'(void|SettingRow|FrameworkElement|ToggleSwitch|Slider|Panel|static|Action|TextBox|bool)\s+$',pre): continue
        a,_=args_of(t,t.index('(',m.start()+len(name)-1))
        kind,ti,hi,ci,ki=HELP[name]
        if kind in('group','advanced'):
            group=(text(a[0]) if a and '"' in a[0] else (a[0] if a else 'Advanced'))
            if kind=='advanced': group=f'{group} (fold)'
            continue
        if kind in('tip','info'):
            if rows and rows[-1]['page']==pg:
                rows[-1].setdefault(kind,[]).append(text(a[0]) if a and '"' in a[0] else (a[0] if a else None))
            continue
        if kind=='engine':
            env=re.search(r'"(BB_\w+)"',a[0]).group(1) if '"' in a[0] else None
            rows.append({'page':pg,'group':group,'kind':'engine','env':env}); continue
        title=text(a[ti]) if len(a)>ti and '"' in a[ti] else ('‹'+(a[ti] if len(a)>ti else '?')+'›')
        hint=text(a[hi]) if hi is not None and len(a)>hi and '"' in a[hi] else (None if hi is None or len(a)<=hi or a[hi] in('null','') else '‹'+a[hi]+'›')
        r={'page':pg,'group':group,'kind':kind,'title':title,'desc':hint}
        if ci is not None and len(a)>ci:
            c=a[ci]
            mm=re.search(r'Choices\.(\w+)',c)
            if mm and mm.group(1) in CHOICES: r['choices']=CHOICES[mm.group(1)]; r['choices_name']=mm.group(1)
            elif 'new' in c: r['choices']=parse_choices(c)
            else: r['choices_expr']=c
        k,fb=keyof(a,ki)
        if k: r['key']=k; r['default']=default(k,fb)
        if kind=='cheat': r['choices']=None; r['choices_expr']='Modes'
        rows.append(r)
json.dump({'rows':rows,'switches':SW,'effects':EFFECTS,'choices':CHOICES},open(sys.argv[1],'w'),ensure_ascii=False,indent=1)
print(len(rows),'rows;',len(SW),'switches;',len(EFFECTS),'effects;',len(CHOICES),'choice lists')
