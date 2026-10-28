# Style and taste: the plan

Status (2026-10-28): step 1 built (the guides in the engine, the song's style, the guide in the
agent's instructions, Styles in the agent panel and the top bar), reference folders for Berlin,
industrial and hard techno (music discovery, `docs/music-discovery.md`), and the agent's
conversation mode. Next: the review. The full order is in "The road to excellent results" below.

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

### 2. A review (the ears)

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

### 3. The process

Written into the agent's instructions and the guides:

1. Set the song's style and read its guide.
2. Build the groove first (for techno: kick and rumble) until it meets the low-end targets.
3. Add one part at a time; each must earn its place: if the review says the highs got busier or the
   low end weaker, take it out or turn it down.
4. Few, better sounds: search by sound and fit to the style, prefer the user's trusted folders.
5. Arrange within the parts-at-once budget; review the whole song; master.

### 4. Feedback (the user, in small doses)

- A/B/C: the agent renders three short variations (different kits or densities) and the user picks
  one with a click. The pick trains the taste model on kits, not single sounds.
- Trusted folders: the user marks folders of sounds they like (like the folder that made the first
  good song); searches prefer them.
- Notes: lasting wishes go into the notes, which the agent already follows.

### 5. Testing it

A fixed set of prompts per style ("a 6-minute Berlin techno track at 136 BPM from my library"). For
each version of the guides and the review: build the songs, review them, and the user rates them
blind (good, okay, bad). Change one thing at a time; keep what improves the ratings.

## The road to excellent results

In this order; each step is judged by the test round (6) before the next.

1. **Conversation mode** (done). The agent talks when asked a question or for ideas: it answers,
   explains, offers options with their trade-offs and asks back, and edits only when asked. It looks
   at the song (and renders or compares it with the references) before giving an opinion.
2. **The review** (next; one to two sessions). Measures the song against its guide and the user's
   reference folders and returns ranked, concrete problems ("the clap is 2 dB under the kick; your
   references put it 7 to 16 dB under", "bars 33 to 48 have 9 parts; the budget is 6", "the highs
   are busier than your Berlin references"). The guides' targets come from the references'
   measurements instead of estimates.
3. **The process** (one session). The agent builds the groove first, adds one part at a time,
   reviews after each and fixes the top problem before going on, then masters and compares with
   the references.
4. **Sound choice from the references.** Every search ranks by fit to the song's style folder and
   by the user's taste, so a kick is chosen because it sounds like the references' kicks.
5. **Arrangement from the references.** How the references evolve (parts at once over time,
   breakdowns, intro length) becomes the arrangement target per style.
6. **Testing with the user.** A fixed prompt per style, the songs rated blind (good, okay, bad), one
   change at a time; keep what improves the ratings. The first round comes after step 3.
7. **A/B/C variations and trusted folders.** Three short versions to pick from by ear, which trains
   the taste on whole kits; trusted sample folders searches prefer.
8. **A mastering chain per style**, tuned to the references' loudness and balance.

Music discovery (a Claude Code skill) adds reference folders and sharpens guides for more styles
along the way.

## Open questions for the user

- Which styles matter most after the techno ones, for music discovery and the user's read of their
  guides?
