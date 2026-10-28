# Music discovery

How a style gets its references: the folder of tracks GrooveKit hears as the style (Browser → Style),
and the guide the agent follows (`guides/<id>.md`). It's a Claude Code skill: ask Claude Code for
"music discovery on Juke/Footwork" (or any style) in this repository and it follows
[`.claude/skills/music-discovery/SKILL.md`](../.claude/skills/music-discovery/SKILL.md). The in-app
agent can't do this, as it has no web access.

## The process

1. **Research:** how the style is described (history, strands, eras, sound) and who defines it (clubs, residents, labels, defining mixes), from music press and reference sites.
2. **Guide:** write or sharpen `guides/<id>.md` from the research, separating it from neighbouring styles. Berlin techno (Berghain) and industrial techno (Tresor) are an example of a pair split this way, and hard techno and schranz an example of a pair merged.
3. **Tier one, free:** download podcasts and mixes their publishers offer for download (Resident Advisor's feed, style-specific podcast feeds, official free mixes) into `~/Music/GrooveKit/References/Mixes/<Style>/`.
4. **Tier two, to buy:**
   - Tracklists of the defining mixes give 15 to 25 picks across strands and eras, each with a verified Bandcamp link and its minimum price.
   - Claude adds them to the Bandcamp cart only when asked, at the minimum price, and never checks out.
5. **Measure and sort:**
   - Every track is measured with `groovekit-engine analyze`. Outliers are flagged: intros, bad or quiet masters.
   - Singles go into `~/Music/GrooveKit/References/<Style>/` and albums stay whole in `Albums/`, with only their picks copied into the style folders.
   - The rest goes into `Not used/`.
6. **Slice the mixes:** up to five 90-second slices per mix from its steadiest stretches go into `<Style>/From mixes/`; then the user deletes the full mixes.
7. **Hand over:** add the style folder (not `References` itself) as a style in GrooveKit; its name matches the guide's.

## Scripts

In `.claude/skills/music-discovery/scripts/`:
- `measure_one.sh <file> <out>` measures one file with the engine (built in `build/dev`).
- `measure_table.py <out>` prints loudness, crest and the low end against the highs per file.
- `slice_mix.py <mix> <folder>` cuts the slices.

## Rules

- Download only what's offered as a download: no stream ripping, no getting around blocks.
- Purchases are the user's; Claude may fill the cart when asked, never check out.
- Summarise sources in your own words.
- macOS protects `~/Music/Music` and the Trash from the editor: the user moves folders in and deletes files.
