# Launcher text review

Every row of the Windows launcher (208 rows on 13 pages), its text today beside the proposed text, as of
Hunter's Workshop 0.4.4 beta 28 (2026-11-02). Fifth draft: the fourth draft, corrected after an outside review
and with the tester's decisions on it. Nothing here has been applied yet: beta 28's text step applies it,
after it is made again from the launcher as it is then (`launcher/windows/tests/text_review`).

## How to review

- Each page has one table. **Today** is the launcher word for word ("Line" is what the Classic layout shows
  under a title, cut at 64 characters today; "Tooltip" is the full text on hover and behind Modern's (i)).
- The proposed columns show the full text. *(unchanged)* marks today's text, kept with its translations. In
  the tooltip column, *Default: …* is the line the launcher adds from the setting's own default.
- Edit a proposed cell or comment on it. Rows left alone are taken as approved.
- Decisions already taken are listed below with their reasons; changes to how settings behave (not only what
  they say) are in "Changes outside the texts", after the pages.

## How it is written

Hunter's Workshop is a port, and its texts read like the port's own notes: plain, short and specific. This
draft keeps that voice and fixes what is wrong, cut or out of date, rather than restyling everything.

- **Today's texts stay** wherever they are right, with their translations (110 descriptions are translated
  in all 13 languages). Tooltips change in 36 rows: 16 corrected (wrong, overpromising or out of date), 20
  added where today says nothing useful, mostly rows with no tooltip at all, such as the picture sliders.
- **The line under the title** is new in 122 rows, where Classic cuts today's text mid-sentence or it says
  too little (a variable's name alone, "Relaxed is the default"). It is a complete phrase saying what the
  setting is or does ("Frees VRAM held by textures the GPU drew"), with no instruction ("Choose…") and no
  full stop. 60 characters is the English target; a translated line wraps instead of being cut. Folders,
  files and the FPS counter's items keep no line.
- **Specifics stay:** VRAM, FSR and DLSS names, `BB_` names on Engine switches and Log, costs ("about 10%
  FPS", measured), advice ("Recommended: on", "turn it off if…"), and "Applied when the game starts" where
  it is said today.
- **No promise past what the code does.** Each claim was checked against the code (the outside review
  found five that went too far; they are corrected).
- **Defaults:** the launcher adds "Default: …" from the setting itself, except where the tooltip already
  says it ("On by default").
- **Lists:** Off or None first where Off or None is a choice; Automatic and numeric lists keep their order.
  Only the order and labels change, never the value a choice stores.
- **Words:** "the in-game overlay menu (Insert or L3 + R3)" for Hunter's Workshop's own menu, not "the
  port's settings menu", "the Insert menu" or "the game menu"; "the game's menus" for Bloodborne's own.

## Decisions taken

| Decision | Why | Where |
| --- | --- | --- |
| Off or None first in every list where it is a choice | The usual order; the tester's call | V-Sync, Area prefetch, Texture filtering, Model detail, Background, Background blur, In-game menu shortcut |
| Anti-aliasing: Off, The game's own, TAA; "The game's own" keeps its name | The tester's call | Graphics (applied) |
| XeSS is not offered | Not implemented | Graphics (applied) |
| One Log options fold with the log level first | The tester's call | Log (applied) |
| The port's own voice: today's texts stay where right | The tester: the third draft read like a production game's | Every page |
| Engine update available says why it shows | It shows when the launcher updated itself and the engine package is older | Play |
| Frame rate's line names the PS4's 30 and the engine's corrections | The tester's call | Display |
| Renames: Upscaling quality (was Quality), Allow other game versions (was Skip the game files check), Suggested settings for this PC (was Best settings for this PC), Reduce shader stalls (was No shader freezes) | Each old name misleads: overall quality; a check you skip; a measured best; a promise of no freezes | Graphics, Engine switches; Quality and Sharpness also in the in-game overlay menu |
| Graphics' Detail and Effects become Game effects and Enhancements | Five switches leave out what the game has; reflections, texture filtering and model detail go past it | Graphics and the in-game overlay menu |
| Unified frame time joins Frame rate corrections | It works only with the corrections on, and every log since 0.3.0 ran with both | Engine switches |
| Fixed-refresh frame lock greys while Precise frame pacing is off | It needs it; its own value is kept | Engine switches |
| Share your settings shares everything except what belongs to this PC, to the player or to one session | The tester's call | Settings |
| Suggested settings read the game's monitor; Changed switches lists what Extra variables override | Two bugs the outside review found | Graphics, Engine switches |
