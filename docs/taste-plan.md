# Style and taste: the plan

Status (2026-10-28): step 1 built (the guides in the engine, the song's style, the guide in the
agent's instructions, Styles in the agent panel and the top bar), reference folders for Berlin,
industrial and hard techno (music discovery, `docs/music-discovery.md`), and the agent's
conversation mode. Next: song maps, then the review. The full order is in "The road to excellent
results" below.

Status (2026-10-30): song maps hear the whole track (analysis 8): its arrangement, its kick, and
now its figures, what the music and the bass play where they're loudest, two bars each on the
16th grid, so the agent writes stabs, chords and basslines in the style's rhythms and intervals
instead of inventing tunes (`song_maps read` gives them as a score and as notes).

Status (2026-10-31): voices (hearing 9) are built, all six steps of "Voices" below: every melodic
voice of a reference through the track as runs of its figures with how they change, how the voices
pair (answering, interlocking, together, their intervals), a library per style from the ticked
maps, writing parts from it (in the song's key, varied, fitted to a partner, styles mixed) and a
check of the song's parts against it (`voices`). Accuracy on rendered songs whose notes are known
(three songs, 145-150 BPM, distorted bass and stabs): basslines 98-100% of notes, stabs, calls and
answers 92-100% in the right voice; held pads come apart into pieces and merge with the stabs,
two parts playing the same note at once can't be told apart, and a filter that closes with pitch
can split a part in two. The grid now starts on the kick's onset, not its peak (it was a 16th late
with long kicks). References heard before need hearing again for their voices.

## The problem

The agent arranges well enough, but the songs sound bad to okay. It picks sounds without knowing the
feeling they carry and overloads the song: loud claps, too much percussion, noisy and bright highs,
too much going on to hold on to. Techno, for example, needs a growling, grooving low end that makes
you move, with subtle highs that give the ears something to hold on to. The user can be in the loop,
but not for everything: ideally the program already knows.

## What doesn't solve it

- More general knowledge (manuals, PDFs). The model already knows the rules ("leave space",
  "sidechain the bass") and breaks them anyway, because it can't hear the result. Commercial books
  also can't be put in the repository.
- The taste model alone. It learns which single sounds the user likes, not how many parts a song
  can carry or how loud each should be.

## The approach: knowledge, ears, process, feedback

### 1. Style guides (the knowledge)

One short, opinionated guide per style: Berlin techno, Detroit techno, hard techno, schranz,
hardstyle, house, hip-hop, trap, juke and footwork, UK garage, jungle, drum and bass. Each has:

- the feeling: what moves the body, what the ears hold on to;
- the parts: what each should sound like, with words to search the library by sound;
- the order to build it in, the mix, the arrangement, and what not to do;
- targets the review can check (a `targets` block): tempo, swing, parts at once, top parts at once
  (hats, percussion, claps, effects), each role's level against the kick, low end against highs,
  hits per beat in the highs, loudness.

Built in (`guides/*.md`, embedded in the engine), editable by the user (their copy in
`<app data>/guides` replaces the built-in one; "reset" brings it back), and they can add their own.

Wiring:
- A song gets a style (`style`: a guide id) set from the top bar or by the agent, so every request
  knows what it aims for.
- The in-app agent gets the song's guide in its instructions, like the notes; Claude Code reads it
  with a `style_guide` tool.
- A guide and a folder of reference tracks with the same style are linked: where references exist,
  their measurements replace the guide's numbers (as the user's references are the real target).
- UI: a Styles section in the agent panel (beside Notes) lists the guides; one opens in a window to
  read and edit.

Berlin techno is drafted from the user's own words. The others need the user's read-through, in
order of how much they care.

### 2. Song maps (how the references are built)

The agent should arrange like someone who knows the style: when each part comes in, what it plays,
how the track changes to keep a loop alive. So every reference track is heard once, in full, into a
map, and the maps are curated and shared instead of heard again.

What a map holds, on the bar grid:

1. **Form.** The downbeat, the sections (where things change: almost always on 8, 16 or 32 bars)
   with a role each (intro, groove, build, break, drop, outro), and an energy curve per bar
   (loudness, low end, brightness, density).
2. **Parts.** Which of a fixed list plays in each bar: kick, rumble/sub, bass, clap, snare, closed
   hat, open hat, ride, shaker, percussion, toms, crash, stab/chord, riff/lead, pad/atmosphere,
   vocal, effects. Drum hits are cut from the drum stem, labelled by the sound model and grouped
   (the same sound repeated is one part); rumble is the low tail after each kick, bass the notes in
   the bass stem. Each label has a confidence.
3. **Patterns.** Each part's rhythm per bar on a 16-step grid (32 steps or triplets where the track
   needs them) with accents, the notes of pitched parts, and where each part sits against the grid
   (the groove). Repeats fold together: "pattern A for bars 17-48, a variation every 8th".
4. **Changes.** Filters opening, fills and drop-outs before a phrase, a part out for a bar,
   crashes and risers on section starts, notes changing under a kept rhythm.

The agent reads a map as a score (form, parts and patterns as text), through a tool.

From all maps of a style:

- **Flavours.** The style's tracks grouped by how they unfold (groove-first, tops-first, melodic
  build, hypnotic loop, break and drop, ...): found from the data, named with the user. Each has a
  template with ranges ("the low end comes in at bar 17-33") and its best maps as worked examples.
- **The shared system** under all of them: phrase lengths, how often something changes, what drops
  out before a section, the most parts at once.
- **The rhythm vocabulary**: the kick, hat, clap and bass patterns the style uses and how often, with
  swing and push per part (for drum and bass this is where two-step and shuffle live).

Maps on disk:

- One JSON file per song in `~/Music/GrooveKit/Maps/<style>/<song>.json`, beside
  `References/<style>`, holding everything heard (also the sound fit and levels the hearing kept
  per track), the hearing version and the source (heard here or imported). `index.json` lists them
  with their style, summary and whether each is in use.
- Real titles, on the user's machine only: not in the repository. Sharing is a zip of map files
  between people (export a style or the ticked songs; import by dropping the zip), each zip with
  its own index. Exact notes of pitched parts stay out of an export unless asked for.
- Maps heard by an older hearing are used for what they have; the user hears their own again when
  the hearing improves, and contributors send new zips.

Curating (the References tab in the agent panel):

- **Songs**: every reference track by style, like the folders, with its status (heard, hearing,
  queued, failed, heard by an older version, imported without audio, skipped). A tick means in use:
  the style's flavours and templates come from the ticked maps, and the ticks also pick songs for
  the actions: hear (again), forget (delete the map and don't hear it again), remove from the
  references (the audio goes to "Not used"), export.
- **Processing**: the hearing queue, every song with its steps and progress, the time left, pause
  and resume; it carries on after a restart.
- A map opens in a window: its measurements now, the form, parts and patterns as they come, with
  playback from any bar and corrections to labels.
- **A song's references**: a project can name its own references ("make it like these two"), so its
  review keeps comparing with those, whatever is ticked.

Built in this order:

1. Map files, the index, the queue, the References tab and export/import, with what the hearing
   already measures (tempo, swing, density, loudness, balance, levels and sounds per part). Built.
2. The full-track hearing: form, parts per bar, energy; the map window shows them. The user checks
   the first maps against tracks they know by heart. Built (hearing 6: the whole track is split,
   about three minutes for a seven-minute track; the agent reads maps with song_maps).
3. Drum-hit labels, patterns per part, notes, the timing feel; the rhythm vocabulary.
4. Flavours and templates per style, into the guides and the agent's tools; the agent plans a song
   as a map first, and a song can name its references.
5. The review (below) compares the song's own map with the template.

### Sound matching (a sound like the reference's)

A perfect arrangement of the wrong kick and rumble still sounds wrong. The agent has to find, or
make, the sound a reference uses for a part, not just the right kind of sound. The fingerprints
(CLAP) hear "dark techno kick", not "a short click, a body at 48 Hz, a distorted rumble tail of
180 ms", and they compare a stretch of a mixed track with a clean file. So samples are measured
in the same terms as the references' parts:

1. **Clean reference parts.** The map knows where a part plays alone or nearly ("bars 1-16: kick
   and rumble only"); a part is measured there, not under the whole mix. The measurements are
   kept in the map, so they work for imported maps too.
2. **Sound maps of samples.** One-shots and loops get the same measurements: for kicks the note
   of the body, the click and attack, how long the body rings, the rumble tail (length, level,
   grit) and width; for loops also their rhythm on the 16-step grid and their timing. Drum loops
   are drums already, so they're split by band, not by the separator. Run in the background like
   the sound index.
3. **"Sounds like this".** A search by a reference part (a map, a part, its clean bars): samples
   ranked by the measurements first and the fingerprint second, each with what's close and what
   differs ("1 semitone higher, a 60 ms shorter tail").
4. **Checked by ear, in numbers.** The agent renders its best candidates at the song's tempo, alone,
   measures them like the reference part and keeps the closest. The best three go to the user as
   A/B/C (a window with play and pick); the pick and the ones passed over teach the taste model.
5. **Closing the gap.** What the closest still differs by, as changes: tune to the reference's
   note, EQ towards its spectrum, attack, saturation; measured again after each.
6. **Built from one-shots.** A kick chosen by its measurements, played in the reference's rhythm,
   with the rumble made the classic way (kick, reverb, distortion, low-pass, ducked by the kick),
   compared with the reference part like any candidate.

It won't find the record's exact sound; it finds the same family and brings it closer, and the
numbers say how close. The user's ear stays the judge.

Built for the kick and rumble (hearing 6 measures each reference's on its clean stretch; the
library's kicks and drum loops are measured in the background into `kick-sounds.json`): the agent's
match_sound (find, audition, channel) and offer_choice tools, the A/B/C window, and "Find kicks
like it" in a map's window. Hats, percussion and bass come next, the same way.

### Mix slots (each part its own room)

Sounds chosen one at a time can each be good and still clash: every sample fills more of the
spectrum than its part should, and nothing carved out the room between them. A clean, crisp mix
gives each part its own band, which is what producers do first:

- **Parts and slots.** Every channel has a part (kick, rumble, drums, bass, percussion, hats, synth,
  vocal, fx; `part` on the channel, guessed from its name and sample) and a slot: an EQ first on
  its own insert (the effect `slot`) that keeps it to its range. Kick and rumble are low-passed
  steeply (48 dB/octave), synths band-passed to the mids, percussion and hats high-passed with
  their top tamed. When the song has a style whose references were heard with their stems'
  spectra, the bass, synth and vocal slots follow where those references keep the parts.
- **Automatic.** A channel the agent adds gets its slot in the same undo step; `mix_slots` lists,
  applies and sets them. The user's own edits are left alone.
- **Choosing in the song.** `audition_in_song` puts up to four candidates in a channel's place and
  renders the groove with each, through the slot, saying how much of each is covered by the rest;
  the files go to `offer_choice`, so the user picks a sound with the groove around it.
- **The combination check.** `check_mix` renders each channel and lists what goes wrong between
  the parts, worst first, each with a fix: no slot, low end outside the low parts, kick and rumble
  or bass sharing the low end with nothing ducking, parts covering each other, the balance against
  the references (too many highs above all), highs that never move, loops out of key.

The agent's process: one part at a time; choose it in the song; check the mix and fix the worst
problem before the next part.

### Voices (how the melodic parts talk to each other)

The figures are a start, but a melody alone doesn't make a track. What separates a good producer
from a mid-tier one is how several melodic parts work together: a bassline, a simple stab on top of
it, a second synth that answers the stab (call and response, shout and echo), a pad or atmosphere
underneath, sometimes a kick pitched up and down. Push and pull between parts that seem to talk to
each other. Until the agent hears and writes that, its songs stay mid-tier, however good the sounds
and the mix are.

**Goal.** From the ticked references of a style (about 40 tracks), a library of how the style's
melodic parts play: which voices exist, what each plays (notes and rhythm), how they evolve through
the track, and how they fit together. The agent writes new parts from it: never the same notes,
but in the song's key, with related rhythms, the right number of voices, and parts that answer each
other. Styles can be crossed ("hard techno with a house melodic groove": the rhythms and voices of
one, the sounds and energy of the other).

**1. Hearing every voice in a track.** The separation gives drums, bass and the rest (all synths,
pads and chords together), so the voices have to be pulled apart again from the notes:

- Transcribe the whole bass and music stems, not two bars of each: the transcriber takes about a
  tenth of the audio's length, and the stems already exist from the whole-track hearing.
- Split the music stem's notes into voices by what they have in common: register (low, mid,
  high), length (stabs, plucks, held notes, pads), onsets together (a chord is one voice), timbre
  at each note's start (the sound embedding of a short slice, so two synths in the same register
  still come apart), and repetition (a voice repeats its figure every bar, two or four).
- Name each voice's role: bass, sub, stab, lead, arp, chords, pad, atmosphere, pitched kick (the
  kick's pitch from the drums' low end where it moves), with how sure the split is.

**2. Each voice through the track.** On the arrangement's bar grid: where the voice plays, its
figure (rhythm on 16ths with accents, and notes as scale degrees as well as absolute keys), and
how it changes: notes changing under a kept rhythm, a figure growing or thinning, a filter opening,
a transposition, a drop-out before a section. Repeats fold together ("figure A bars 17-48, the last
note up a fifth every 8th bar").

**3. How the voices fit together.** Measured per section, per pair of voices:

- Interlock: on which 16ths both play, one plays, or neither (complementary rhythms leave each
  other's gaps; doubled ones play together).
- Call and response: one voice's phrase followed by another's in its gap, with the delay in 16ths.
- Against the kick: on the beat, off it, between.
- Register and intervals: how far apart they sit, the intervals between their notes when they
  meet (a stab on the bass's root, a fifth over it, a third), who holds the root.
- Density: notes per bar of each, and of all together; the number of voices at once.

**4. The style's library.** From all ticked maps of a style: the voices it uses and how often
(how many at once, per section role), a vocabulary of figures per role (rhythms clustered into
families with their variants, interval shapes as degrees), the usual pairings and how they
interlock, and how voices evolve over a track. Stored with the maps (`Maps/<style>/voices.json`),
built again when ticks change, shareable like the maps; exact notes stay out of an export unless
asked for.

**5. Writing with it.** A tool that turns the library into notes for the song: a voice for a role
in the song's key and scale, from a figure family, varied (transposed, displaced, thinned or filled,
notes swapped within the scale, the rhythm by analogy rather than copied), and a partner voice
for one already there, built to interlock with it or answer it. A style mix takes the rhythms
and pairings of one style and the voices and register of another. The agent picks; the user
hears it in the song (as with `audition_in_song`) and chooses.

**6. Checking it.** The review compares the song's voices with the style's library like
`check_mix` compares the mix: too many voices, two voices doubling where the style interlocks, a
stab with no answer, nothing changing for 64 bars, a voice outside the key.

**Accuracy first.** Transcribing dense, distorted techno is hard, and everything above depends on
it. Before building the library: render songs whose notes are known (GrooveKit songs with several
synths, at the styles' tempos and with their distortion), hear them like references, and measure
how many notes and voices come back right. Then listen with the user to a few maps of reference
tracks they know. Curated references matter more here than anywhere: experimental tracks in a
style's folder teach the wrong voices, so the user's ticks decide what goes into the library.

### 3. A review (the ears)

A `review` tool (engine `song.review`) that checks the song against its guide and returns a ranked
list of problems, each with a concrete fix:

- Arrangement, from the song itself: how many channels sound in each bar, how many of them are top
  parts; bars over the budget. ("Bars 33-48 have 9 parts, the budget is 6: drop the second
  percussion loop and the ride.")
- Mix, from a render of each channel alone (the mix analysis already does this): each element's
  level while it sounds against the kick's, by role, against the targets. ("The clap is 2 dB under
  the kick; the guide wants 7 to 16 dB under: turn it down about 8 dB.")
- Balance: energy below 120 Hz against above 4 kHz. (The user's techno references measure about
  15 dB.)
- Busy highs: hits per beat above 5 kHz, and which channels make them.
- Noise: a constant high-frequency floor (hiss, noise beds), and which channels carry it.
- Loudness, tempo and swing.

Roles come from the samples' categories (the library knows them), the channel names, or the
agent's own word (`roles`).

The agent must run the review after every part it adds and fix the top problem before going on. It
reports the result to the user in a line ("within the guide except a busy top end at bar 49").

### 4. The process

Written into the agent's instructions and the guides:

1. Set the song's style and read its guide.
2. Build the groove first (for techno: kick and rumble) until it meets the low-end targets.
3. Add one part at a time; each must earn its place: if the review says the highs got busier or the
   low end weaker, take it out or turn it down.
4. Few, better sounds: search by sound and fit to the style, prefer the user's trusted folders.
5. Arrange within the parts-at-once budget; review the whole song; master.

### 5. Feedback (the user, in small doses)

- A/B/C: the agent renders three short variations (different kits or densities) and the user picks
  one with a click. The pick trains the taste model on kits, not single sounds.
- Trusted folders: the user marks folders of sounds they like (like the folder that made the first
  good song); searches prefer them.
- Notes: lasting wishes go into the notes, which the agent already follows.

### 6. Testing it

A fixed set of prompts per style ("a 6-minute Berlin techno track at 136 BPM from my library"). For
each version of the guides and the review: build the songs, review them, and the user rates them
blind (good, okay, bad). Change one thing at a time; keep what improves the ratings.

## The road to excellent results

In this order; each step is judged by the test round (6) before the next.

1. **Conversation mode** (done). The agent talks when asked a question or for ideas: it answers,
   explains, offers options with their trade-offs and asks back, and edits only when asked. It looks
   at the song (and renders or compares it with the references) before giving an opinion.
2. **Song maps** (in progress; see "Song maps" above: step 1, the files, the Maps tab and the queue,
   is built). The references heard in full into maps the user curates and shares; flavours,
   templates and the rhythm vocabulary per style come from them. This takes in "Arrangement from
   the references" (the old step 5).
3. **Sound matching** (built for the kick and rumble; see "Sound matching" above). This takes in
   "Sound choice from the references".
3b. **Voices** (built; see "Voices" above). Every melodic voice of a reference heard through the
   track, how they fit together, a library per style, writing new voices and partners from it,
   and the check. Next: pads and held chords heard from what rings through each bar rather than
   from note starts, and a test round of written parts with the user.
4. **The review** (the mix half is built: mix slots and `check_mix`, see "Mix slots" above; the
   arrangement half and the guide's targets are next). Measures the song against its guide and the user's
   reference folders and returns ranked, concrete problems ("the clap is 2 dB under the kick; your
   references put it 7 to 16 dB under", "bars 33 to 48 have 9 parts; the budget is 6", "the highs
   are busier than your Berlin references"). The guides' targets come from the references'
   measurements instead of estimates.
5. **The process** (one session). The agent builds the groove first, adds one part at a time,
   reviews after each and fixes the top problem before going on, then masters and compares with
   the references.
6. **Testing with the user.** A fixed prompt per style, the songs rated blind (good, okay, bad), one
   change at a time; keep what improves the ratings. The first round comes after step 5.
7. **A/B/C variations and trusted folders.** Three short versions to pick from by ear, which trains
   the taste on whole kits; trusted sample folders searches prefer.
8. **A mastering chain per style**, tuned to the references' loudness and balance.

Music discovery (a Claude Code skill) adds reference folders and sharpens guides for more styles
along the way.

## Open questions for the user

- Which styles matter most after the techno ones, for music discovery and the user's read of their
  guides?
