---
name: music-discovery
description: Researches a music style (e.g. "Juke/Footwork", "Berlin techno") and builds the user's reference folder for it in GrooveKit, then writes or sharpens its style guide. Use when the user asks for "music discovery" on a style, or wants reference tracks, mixes or a guide for a style. It finds how the style is described and who defines it, downloads free official podcasts and mixes, finds the best tracks with verified Bandcamp links, measures everything, sorts it into ~/Music/GrooveKit/References/<Style>, and cuts reference slices from the mixes.
---

# Music discovery

Builds a style's references from what defines it: the clubs, residents, labels and mixes. The result
is a folder of 15 to 30 reference tracks plus slices from mixes, which GrooveKit hears as the style, and
a guide in `guides/` that says how to make it. Work through the steps in order; report briefly after
each, and ask before anything that costs money.

## 1. Research the style

Search the web (several searches in one turn) for:
- **How it's described:** its history, origins and strands (classic and current, revivals), tempo, the kick, the bass, the highs, the mood, how it's arranged. Prefer RA, Mixmag, Crack, DJ Mag, Electronic Beats, Groove, The Quietus, Bandcamp Daily; Wikipedia or Last.fm for history.
- **Who defines it:** the clubs and their residents, the labels, the DJs and producers of each era, the defining mixes (official mix series, RA podcasts, Boiler Room, festival sets).

Read a few articles. Summarise in your own words: one quote at most, and short. Be clear about what
the sources say and what is guesswork; sources often disagree on dates and tempos.

Report: how the style is described, its strands and eras, who to listen to, with sources as links.

## 2. Write or sharpen the guide

Guides are `guides/<id>.md` (see an existing one, e.g. `guides/berlin-techno.md`): the name (with
another name in brackets if it has one, e.g. "Hard techno (Schranz)", which also finds it), the
feeling, where it comes from and its strands, the parts with words to search the library by, the
order to build in, mix, arrangement, what not to do, who to listen to, and a ```targets block. Use
the research; keep the user's own words where they gave them. If the style overlaps another guide,
separate them (say what makes each different) or merge them. Build the engine and run the guides test
(`gk_tests "Style guides*"`; it counts the guides), then commit.

## 3. Tier one: free, official downloads

Download only what the publisher offers as a download: podcast RSS feeds and official free mixes.
Never rip streams or get around blocks.

- **Resident Advisor:** `https://ra.co/xml/podcast.xml` (the last 500 episodes; MP3 enclosures). Match titles against the artists found in step 1. The conversation series: `https://ra.co/xml/podcast-exchange.xml`.
- **Style-specific podcasts:** find their RSS feed through podnews.net or podstatus.com (look for a `feed`, `rss` or `podcast_feed` URL), then read the items. Examples: Schranzcast (hard techno), label and club series.
- **SoundCloud:** only podcast feeds the account publishes (`feeds.soundcloud.com/users/soundcloud:users:<id>/sounds.rss`), or mixes with the account's own download button, which the user clicks.
- **Mixcloud:** stream only; use it for tracklists, not downloads.

List the episodes with size and length, sort them by style, and download into
`~/Music/GrooveKit/References/Mixes/<Style>/` (check free disk space first). Writing under
`~/Downloads` or `~/Music` needs the sandbox off, which asks the user. Leave out episodes that turn out
to be another style, or put them in `Mixes/Other styles`.

## 4. Tier two: the best tracks, to buy

1. Get tracklists of the defining mixes: RA podcast pages, Discogs (`https://api.discogs.com/releases/<id>`, since discogs.com blocks fetching), official mix pages.
2. Pick 15 to 25 tracks across the strands and eras: tracks in several mixes and well-known staples first.
3. Find each one's Bandcamp page, open it (WebFetch) and confirm the artist, title and that it's for sale as a digital track (or note "album only" with the price). Never guess a URL; write "not found" instead. Juno and Beatport block fetching: name those tracks without links.
4. Present a table per strand: track, year, label, why it's a good reference, link, minimum price. Call out compilations that cover many picks at once, albums that are the only way to get one track, and off-style tracks (e.g. electro among straight-kick techno).

Only when the user asks, add them to their Bandcamp cart in Chrome (the claude-in-chrome tools):
- Open the track page, read `script[data-tralbum]` (`current.minimum_price`), click "Buy Digital Track" (`button.buy-link`), put the minimum into `#userPrice` (with input, keyup and change events) and click "Add to cart".
- Never "Check out with PayPal", and never check out at all.
- Each `<label>.bandcamp.com` needs the extension's permission. Navigate to a new subdomain on its own (outside a batch), wait a second, then run the script.
- Skip anything already in the cart.
- Finish with the cart's contents and totals per currency.

The user buys and downloads, and tells you where the files are. `~/Music/Music` (the Apple Music
library) is protected by macOS: ask them to move the folder into `~/Music/GrooveKit/References/`.

## 5. Measure and sort

1. Measure every track: `scripts/measure_one.sh <file> <out-folder>` (in parallel: `find … -print0 | xargs -0 -P 6 -n 1 -J % scripts/measure_one.sh % <out>`), then `python3 -I scripts/measure_table.py <out>` for a table of loudness, crest and low end against highs. The engine must be built (`build/dev`). Measure the style's existing folder too, for comparison.
2. Flag outliers rather than sorting them blindly:
   - Intros and interludes: short, with little low end.
   - Badly mastered or off-sound tracks.
   - Old, quiet masters (about 4 dB or more under the rest). Keep classics, but say they pull the loudness targets down; use only a few from quiet albums.
3. Sort:
   - Singles move to `~/Music/GrooveKit/References/<Style>/`.
   - Albums stay whole in `~/Music/GrooveKit/References/Albums/`; only their reference tracks are copied into the style folders.
   - Duplicates (`cmp` to confirm) and non-references go to `~/Music/GrooveKit/References/Not used/`. The editor can't use the Trash, and nothing is deleted for good.
4. The folder name is the style's name, matching the guide ("Berlin Techno" finds "Berlin techno (Berghain)").

## 6. Slice the mixes

`python3 -I scripts/slice_mix.py <mix> "~/Music/GrooveKit/References/<Style>/From mixes"`:
- It cuts up to five 90-second slices per mix, about one per 10 minutes, from the steadiest stretches (one track on its own, not a transition).
- It skips quiet breakdowns and the first and last three minutes.
- About 18 s per hour-long mix.

Mention that slices near a mix's end can be off-style if the DJ changes direction. Then ask the user
to delete the full mixes (`References/Mixes`) themselves; you can't use the Trash.

## 7. Hand it to GrooveKit

The user adds the style folder in the browser's Style menu (or `styles.add` with its path), and
GrooveKit hears it. Add each style folder on its own, never `References` itself, which would also
hear the albums, mixes and leftovers. GrooveKit remembers the folder's path, so a moved folder must
be added again. Adding a folder again updates it, hearing only what changed. Report:
- the counts per folder;
- what was left out and why;
- the next step: references whose measurements should replace the guide's estimated targets.

## Limits

- **Money:** ask before anything that costs money. The user approves purchases and checks out.
- **Quotes:** one per answer at most, short; summarise the rest.
- **Folder size:** aim for 10 to 15 strong tracks per style plus slices. More adds variety, not accuracy.
