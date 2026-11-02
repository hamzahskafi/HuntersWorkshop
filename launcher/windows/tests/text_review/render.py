"""The review's tables: every row today (from extract.py's JSON) beside its proposal (proposals.py).

python3 render.py now.json rows.md [pages_dir]
rows.md gets one table per page; with pages_dir, each page's table is also written alone (Play.md, ...)."""
import json, sys, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from proposals import P, KEEP

D = json.load(open(sys.argv[1]))
SW = D['switches']; EFF = D['effects']


def brief(t, mx=64):
    """Classic's line under the title: the first sentence up to 64 characters, else cut with an ellipsis."""
    if not t: return None
    if len(t) <= mx: return t
    for end in ['. ', '; ', ': ']:
        at = t.find(end)
        if 20 <= at < mx: return t[:at] + ('.' if end == '. ' else '…')
    cut = t.rfind(' ', 0, mx + 1)
    return (t[:cut] if cut > 20 else t[:mx]).rstrip(',;: ') + '…'


def clean(v):
    if v is None: return None
    return str(v).strip().strip('"')


def deflabel(r):
    d = clean(r.get('default'))
    if d is None: return None
    if d.startswith('Updater.CurrentBeta'): return 'On in betas, off in stable releases'
    if r.get('kind') in ('switch',):
        return {'true': 'On', '1': 'On', 'false': 'Off', '0': 'Off'}.get(d, d)
    for c in r.get('choices') or []:
        if clean(c['value']) == d: return c['label']
    return d or None


PAGES = {'Play': 'Play', 'Display': 'Display', 'Graphics': 'Graphics', 'Interface': 'Interface', 'Gameplay': 'Gameplay',
         'Controls': 'Controls', 'Engine': 'Engine switches', 'Output': 'Log', 'Settings': 'Settings', 'Online': 'Online',
         'Mods': 'Mods & patches', 'Cheats': 'Cheats', 'Trophies': 'Trophies'}
DETAIL = ['effect_dynamic_shadows', 'effect_ssao', 'effect_ssr']; LOOK = ['effect_dof', 'effect_motion_blur', 'effect_chromatic_aberration']
EXTRAS = ['skip_intro', 'debug_camera', 'debug_menu']
FIXES = ['BB_RESPAWN_DELAY_FIX', 'BB_SOUND_HACK', 'BB_OPTISCALER_COMPAT', 'BB_EFFECT_FIXES', 'BB_PS4_FPU', 'BB_RED_ZONE_FUNCTIONS',
         'BB_INTEL_TONEMAP_FIX', 'BB_SKIP_GAME_CHECK']
EXTRA_NOW = {('Graphics', 'Sharpening (RCAS)'): "Line set by the upscaler: DLSS \"After DLSS (it has none of its own)\", AMD FSR \"FSR's own sharpening\", TAA \"After TAA\", none \"Needs an upscaler or TAA\"",
             ('Graphics', 'Sharpening strength'): "Line set by the upscaler: AMD FSR \"Above 1 adds a second pass\", otherwise \"Higher is sharper\"",
             ('Graphics', 'DLSS model'): "Status line with Auto: \"Auto: model K at this quality\" (the model Auto picks)",
             ('Graphics', 'Anti-aliasing'): "With an upscaler the box shows \"Upscaler (DLSS)\" (or AMD FSR), greyed",
             ('Graphics', 'Upscaler'): "Status line when an upscaler is unavailable on this PC"}
# Defaults the extraction reads as code, not as what the row shows.
RAW_DEFAULTS = {'en', 'default', 'auto'}


def expand():
    out = []
    for r in D['rows']:
        pg = r['page'][:-4]; g = r.get('group') or ''
        g = {'L.T(EngineSwitches.Performance)': 'Performance', 'L.T(EngineSwitches.Sound)': 'Sound', 'L.T(group)': 'Game fixes'}.get(g, g)
        r = dict(r, page=pg, group=g)
        t = r.get('title') or ''
        if r['kind'] == 'engine':
            if r.get('env'): out.append(dict(page=pg, group=g, kind='engine', env=r['env']))
            elif g == 'Game fixes' and not any(x.get('env') == 'BB_RESPAWN_DELAY_FIX' for x in out):
                for e in FIXES: out.append(dict(page=pg, group=g, kind='engine', env=e))
            continue
        if t == '‹effect.Title›':
            keys = DETAIL if g == 'Detail' else LOOK if g == 'Effects' else EXTRAS
            for k in [k for k in EFF if k in keys]: out.append(dict(page=pg, group=g, kind='effect', key=k))
            continue
        if t == '‹title›' and pg == 'Interface' and r['kind'] == 'switch':
            for name, dflt in [('Frame rate', 'On'), ('Frame time', 'On'), ('Upscaler', 'On'), ('CPU usage', 'Off'), ('GPU usage', 'Off')]:
                out.append(dict(page=pg, group=g, kind='switch', title=name, desc='Shown in the FPS counter', default_label=dflt))
            continue
        if t.startswith('‹') or t.startswith('{'): continue
        # The amount row sits under each item and shows for Custom amount (CheatsPage.ItemRow): one row each.
        if pg == 'Cheats' and t == 'Amount': continue
        out.append(r)
        if pg == 'Cheats' and r['kind'] == 'cheat':
            out.append(dict(page=pg, group=g, kind='slider', title='Amount', desc=f"How many {t} to keep"))
    return out


def now_fields(r):
    """Title, Classic's line, the tooltip, the list and the default, as the launcher shows them today."""
    if r['kind'] == 'engine':
        s = SW[r['env']]
        dv = {'on': 'on', 'off': 'off', '': 'not set'}.get(s.get('default'), None)
        if dv is None:
            dv = next((o['label'] for o in s.get('options', []) if clean(o['value']) == clean(s.get('default'))), s.get('default'))
        tip = f"{s['hint']}\n{r['env']}, default: {dv}. Applied when the game starts"
        return s['title'], brief(s['hint']), tip, s.get('options'), dv
    if r['kind'] == 'effect':
        e = EFF[r['key']]
        return e['title'], brief(e['hint']), e['hint'], None, ('On' if e['default'] == 'true' else 'Off')
    desc = r.get('desc')
    tips = [x for x in (r.get('tip') or []) if x]
    tip = desc if not tips else ((desc + '\n\n') if desc else '') + '\n'.join(tips)
    title, line, ch, dv = r['title'], brief(desc), r.get('choices'), r.get('default_label') or deflabel(r)
    if title == 'HDR output':  # its text is set in code apart from the row
        tip = "The game's picture on an HDR screen, with brighter highlights. Applied when the game starts"; line = brief(tip)
    if title == 'V-Sync': dv = 'On (latest frame)'
    if title == 'Upscaler' and dv == 'off': dv = 'None'  # config.ini's value for the list's None
    return title, line, tip, ch, dv


def pkey(r):
    if r['kind'] == 'engine': return (r['env'],)
    if r['kind'] == 'effect': return (r['key'],)
    return (r['page'], r['title'])


# Lists the extraction sees only by name.
LISTS = {'Modes': "Off; Infinite (Kept full at the normal carry limit, even if you have none. Items put into storage while it "
                  "is on come back); Custom amount (Refilled to the amount below whenever it drops)"}
# Values the code puts into its texts, as the launcher shows them.
VALUES = {'{SaveBackups.InGameEvery.TotalMinutes:0}': '10', '{SaveBackups.Keep}': '10', '{SaveBackups.InGameKeep}': '5',
          '{Updater.Repository}': 'hamzahskafi/HuntersWorkshop', '{title}': 'Blood Vials (or Quicksilver Bullets)',
          'l.Name': 'one per launcher language', 'LauncherTheme.Themes': 'the themes', 'AppIcon.Choices': 'the icons',
          '‹Paths.ConfigFile›': "the file's path", '‹Paths.IniPath›': "the file's path"}


def c(s):
    s = re.sub(r'\{(\d+)\}', r'\1', s or '')
    for k, v in VALUES.items(): s = s.replace(k, v)
    return s.replace('|', '/').replace('\n', ' ').strip()


pages = {}; problems = []; seen = set()
count = dict(rows=0, lines=0, tips=0, renames=0, lists=0)
for r in expand():
    title, line_now, tip_now, ch_now, def_now = now_fields(r)
    k = pkey(r); seen.add(k)
    if k not in P: problems.append(('no proposal', k))
    p = P.get(k, {})
    count['rows'] += 1
    parts = []
    if line_now: parts.append(f"Line: {c(line_now)}")
    if tip_now and tip_now != line_now: parts.append(f"Tooltip: {c(tip_now)}")
    elif tip_now: parts.append("Tooltip: same text")
    if ch_now: parts.append("Choices: " + '; '.join(c(x['label']) + (f" ({c(x['note'])})" if x.get('note') else '') for x in ch_now))
    elif r.get('choices_expr'): parts.append(f"Choices: {c(LISTS.get(r['choices_expr'], r['choices_expr']))}")
    if def_now and def_now not in RAW_DEFAULTS: parts.append(f"Default: {c(def_now)}")
    extra = EXTRA_NOW.get((r['page'], title))
    if extra: parts.append(f"Status: {c(extra)}")
    today = ' · '.join(parts) or '—'
    name = p.get('rename')
    if name: count['renames'] += 1
    setting = f"**{c(name)}** (was {c(title)})" if name else f"**{c(title)}**"
    if r.get('group'):
        setting += f" · {c(r['group']).replace('(fold)', '').strip() or 'Advanced'}"
    pl = p.get('line', '?')
    if p.get('removed'):
        pages.setdefault(r['page'], []).append(f"| {setting} | {today} | Row removed: {c(p['removed'])} | — | — | {c(p.get('note'))} |")
        count['removed'] = count.get('removed', 0) + 1
        continue
    if pl == KEEP: pl_txt = 'Same' if line_now else ('Same (set by the upscaler)' if extra else '?')
    elif pl is None: pl_txt = '—'
    else: pl_txt = c(pl); count['lines'] += 1
    if pl is None and line_now and 'FPS counter' not in (line_now or '') and k[0] != 'Interface': problems.append(('line dropped', k, line_now))
    if pl == KEEP and line_now and line_now.endswith('…'): problems.append(('kept a cut line', k, line_now))
    if isinstance(pl, str) and pl != KEEP and len(pl) > 60: problems.append(('line over 60', k, len(pl), pl))
    tp = p.get('tip')
    if tp is None: tp_txt = 'Same' if tip_now else '—'
    elif tp == '': tp_txt = 'None: the line says it all' if pl else 'None'
    else: tp_txt = c(tp); count['tips'] += 1
    # The default line the row code adds, unless the text or the list already says it (engine switches'
    # tooltips end with "BB_X, default: …" today).
    said = ' '.join([tp if tp else '', tip_now or '' if tp is None else '', ' '.join(x['label'] for x in ch_now or [])]).lower()
    dp = (def_now or '').replace(' (default)', '').replace(' (Default)', '').replace(' (recommended)', '')
    if dp and dp != 'not set' and dp not in RAW_DEFAULTS and r['kind'] != 'engine' and 'default' not in said:
        tp_txt = (f"*Default: {dp}*" if tp_txt in ('—', 'None') else tp_txt + f" + *Default: {dp}*")
    pch = c(p['choices']) if p.get('choices') else ('Same' if ch_now or r.get('choices_expr') else '—')
    if p.get('choices'): count['lists'] += 1
    notes = []
    if p.get('rewrite'): notes.append('Tooltip corrected.')
    if p.get('ingame'): notes.append(f"In-game menu: {c(p['ingame'])}.")
    if p.get('note'): notes.append(c(p['note']))
    pages.setdefault(r['page'], []).append(f"| {setting} | {today} | {pl_txt} | {tp_txt} | {pch} | {' '.join(notes)} |")
for k in P:
    if k not in seen: problems.append(('proposal for no row', k))

# A proposal for a page as a whole, shown above its table.
INTROS = {
    'Graphics': "Decided: the Detail and Effects groups become two groups by where each setting comes from, in the launcher "
                "and the in-game overlay menu. **Game effects**: what the game has and a switch leaves out (depth of field, "
                "motion blur with its upscaler fix under it, chromatic aberration, ambient occlusion, shadows from dynamic "
                "lights); caption \"The game's own effects. Off leaves one out; applied when the game starts\". "
                "**Enhancements**: what goes past the game, or below it for weaker GPUs (texture filtering, model detail, "
                "screen-space reflections); caption \"Model detail and texture filtering above or below the game's, and "
                "reflections it doesn't have. Applied when the game starts\". Today both groups say \"Patches to the game, "
                "applied when it starts\". Two effects are on in the original game and off here by default (chromatic "
                "aberration, motion blur); their tooltips now say so.",
    'Output': "One Log options fold holds the log level and every diagnostic again (the tester's call, already applied). "
              "The BB_ names stay first in each tooltip, as today; these texts are translated in zh-CN only.",
    'Engine': "Rows with a BB_ name already end their tooltip with \"BB_X, default: …. Applied when the game starts\"; that "
              "stays. Decided here: Unified frame time joins Frame rate corrections (it only works with them on), Fixed-refresh "
              "frame lock greys while Precise frame pacing is off, No shader freezes becomes Reduce shader stalls, and Changed "
              "switches also lists what Extra variables override (Changes outside the texts).",
}

HEAD = "| Setting | Today | Proposed line | Proposed tooltip | Proposed choices | Notes |\n| --- | --- | --- | --- | --- | --- |\n"
out = []
for pg, rows in pages.items():
    table = HEAD + '\n'.join(rows) + '\n'
    out.append(f"\n## {PAGES[pg]}\n\n" + (INTROS[pg] + "\n\n" if pg in INTROS else '') + table)
    if len(sys.argv) > 3:
        os.makedirs(sys.argv[3], exist_ok=True)
        open(os.path.join(sys.argv[3], f"{pg}.md"), 'w', encoding='utf-8').write(table)
open(sys.argv[2], 'w', encoding='utf-8').write(''.join(out))
print(count)
for x in problems: print(*x)
