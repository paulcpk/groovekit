// Mixing and choosing sounds in the song: slots, the mix check, auditions, kick matching, the user's picks.

import { z } from "zod";
import { attempt, defined, longRunning, noteSchema, partEnum, type Extra, type Tools } from "./shared.ts";

/** What the engine says about an offer once the user has answered it (picks.get, or the picks notification). */
interface PickResult {
  choice?: number;
  note?: string;
  more?: boolean;
}

/** How many sounds may be shown for one part without a pick before the agent asks which way to go. */
const SHOWN_CEILING = 50;
const PICK_WAIT_MS = 600_000;
const PICK_POLL_MS = 5_000;

export function registerMixTools({ server, engine, groups, barRange }: Tools) {
  server.registerTool(
    "analyze_mix",
    {
      description: "Renders the mix plus one stem per channel (with its audio clips) and reports: the mix's loudness, true peak, loudness range, spectral balance and stereo image; each source's loudness and share of every frequency band; and masking, i.e. pairs of sources that both carry a big share of the same band (e.g. kick and bass at 60-120 Hz). Slower than render: one render per source.",
      inputSchema: {
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ startBar, endBar }, extra: Extra) =>
      attempt(async () => longRunning(extra, "rendering the stems", async (signal) => engine.call("analyzeMix", await barRange(startBar, endBar), { signal })))
  );

  server.registerTool(
    "mix_slots",
    {
      description: "Frequency slots: each part keeps to its range so sounds that are good alone don't cover each other together. A channel's slot is an EQ first on its own insert (the effect \"slot\", gentle, in addition to any other EQ) shaped for its part; with the song's style heard, bass, synth and vocal slots follow where its references keep those parts. New channels get one automatically from edit_project. list: every channel's part (guessed from its name and sample when not set), its range, its slot and the proposed one. apply: gives channels (default all) their part's proposed slot, making each its own insert when it shares one or plays into master. set: one channel's part or cuts by hand (lowCutHz, highCutHz in Hz, 0 for none; steep doubles the high cut to 48 dB/octave; topShelfDb tames the top above topShelfHz). Bands 2-4 of the slot EQ stay free for notches and peaks.",
      inputSchema: {
        action: z.enum(["list", "apply", "set"]),
        channels: z.array(z.string()).optional().describe("list and apply: these channels (default all)"),
        channel: z.string().optional().describe("set: the channel"),
        part: partEnum.optional().describe("set: what it plays (starts it from that part's slot)"),
        lowCutHz: z.number().min(0).max(20000).optional(),
        highCutHz: z.number().min(0).max(20000).optional(),
        steep: z.boolean().optional(),
        topShelfDb: z.number().min(-24).max(24).optional(),
        topShelfHz: z.number().min(20).max(20000).optional(),
      },
    },
    ({ action, channels, channel, ...cuts }) =>
      attempt(async () => {
        if (action === "list") return engine.call("mix.slots", defined({ channels }));
        if (action === "apply") return engine.call("mix.slots", { ...defined({ channels }), apply: true, group: groups.next() });
        return engine.call("mix.setSlot", { channel, group: groups.next(), ...defined(cuts) });
      })
  );

  server.registerTool(
    "check_mix",
    {
      description: "Hears how the parts combine and lists what goes wrong between them, worst first (severity 0-10), each with what and a fix: channels without a slot, parts outside the low end that carry it, kick and rumble or bass sharing the low end with nothing ducking, two parts covering each other in the same bands, the mix brighter, duller or heavier in a band than the style's references (the top above all: too many highs), highs that never move, loops out of the song's key; and from the arrangement over the whole song, too many parts at once (more than maxParts, default 6: when something comes, something has to go) and parts that come in together (each new part gets its own entrance). Also each channel's part, level and the bands it holds, and the parts per bar. Renders once per channel: give the busiest stretch (a drop, 8-16 bars) rather than the whole song, or listen false for only the arrangement, slots and keys (quick). Fix the worst problem, then check again.",
      inputSchema: {
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
        style: z.string().optional().describe("Compare with this style's references instead of the song's"),
        listen: z.boolean().optional().describe("false: no rendering, only what the song shows (arrangement, slots, keys)"),
        maxParts: z.number().int().min(2).max(16).optional().describe("Parts at once before it's too many (default 6)"),
      },
    },
    ({ startBar, endBar, style, listen, maxParts }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "checking the mix", async (signal) =>
          engine.call("mix.check", { ...(await barRange(startBar, endBar)), ...defined({ style, listen, maxParts }) }, { signal })
        )
      )
  );

  server.registerTool(
    "audition_in_song",
    {
      description: "Tries up to twenty candidate samples in a channel's place inside the song: renders the bars (default the first 8) with each candidate on the channel (loops at their own tempo, through the channel's slot and effects) and as it is now, and says how each sits with the rest: fit 0-100 (100 when the bands it lives in are its own, low when the rest covers it there), covers (the bands where the rest is as loud as it), holds and its level against the mix. Candidates play as loud as the sound they would replace, so the comparison is fair; for bass, synths and vocals, key says whether its notes go with the song's key and the transpose that puts it in. Each has a file to offer_choice as its audition, so the user chooses sounds with the groove around them, not alone. The song isn't changed: put the pick in with edit_project.",
      inputSchema: {
        channel: z.string().describe("A sampler channel already in the song (its first layer is swapped)"),
        paths: z.array(z.string()).min(1).max(20).describe("The candidate samples"),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ channel, paths, startBar, endBar }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "auditioning", async (signal) => engine.call("mix.audition", { channel, paths, ...(await barRange(startBar, endBar)) }, { signal }))
      )
  );

  server.registerTool(
    "match_sound",
    {
      description: "Finds and checks a kick and rumble like a reference's, measured in numbers (the note of the body, how long it rings, attack and click, grit, the rumble between kicks and its grit, width, spectrum, pattern), not just by its fingerprint. The reference is a song map (id, from song_maps list: its kick measured where it plays with least around it) or a library sample (path). find: the library's kicks and drum loops ranked by how close they are (score 0-100: measured 75%, fingerprint 25%), each with what differs most; loops true or false narrows to loops or one-shots. audition: renders samples (paths, up to 20) in a groove at the tempo (default the reference's), one-shots on the reference's pattern and loops stretched, measures each like the reference and says what differs and what to change; each has an audition file for offer_choice. channel: renders one channel of the song alone (start and end in bars, default the first 8) and measures it against the reference: score, differences and changes to make (tune, shorten, punch, EQ, grit, the rumble). A sample doesn't need to be perfect: pick the closest and close the gap with the changes, measuring again after each.",
      inputSchema: {
        action: z.enum(["find", "audition", "channel"]),
        id: z.string().optional().describe("A song map as the reference (style/title)"),
        path: z.string().optional().describe("Or a library sample as the reference"),
        loops: z.boolean().optional().describe("find: true only loops, false only one-shots"),
        limit: z.number().int().min(1).max(100).optional().describe("find: how many (default 20)"),
        paths: z.array(z.string()).optional().describe("audition: the samples"),
        tempo: z.number().optional().describe("audition: BPM (default the reference's)"),
        channel: z.string().optional().describe("channel: the song's channel"),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ action, id, path, loops, limit, paths, tempo, channel, startBar, endBar }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "matching", async (signal) => {
          const reference = id ? { id } : { path };
          if (action === "find") return engine.call("match.kicks", { ...reference, ...defined({ loops, limit }) }, { signal });
          if (action === "audition") return engine.call("match.audition", { ...reference, paths, ...defined({ tempo }) }, { signal });
          return engine.call("match.channel", { ...reference, channel, ...(await barRange(startBar, endBar)) }, { signal });
        })
      )
  );

  /** Sounds shown per part (or offer title) since the user last picked one, across rounds. */
  const shownForPart = new Map<string, number>();

  server.registerTool(
    "offer_choice",
    {
      description: "Puts sounds to the user to pick by ear (A, B, C...): five unless they've set another number or ask for more (two to twenty). A window shows each with a play button, next to the reference when there is one, and this waits (up to ten minutes) for their pick. Give each candidate the path (or an instrument with the notes it plays), the audition file from match_sound or audition_in_song when there is one, and a short label; candidates without one are tried in the song here, in channel's place when you give one, else in a slot of their own over startBar-endBar (default the first 8 bars). Returns the chosen candidate, or what the user answered instead (none of them, another round, or a note in their own words) with what to do next.",
      inputSchema: {
        title: z.string().describe("What's being chosen: \"Which kick for the groove?\""),
        style: z.string().optional().describe("The style the pick is for (the song's)"),
        reference: z.object({ file: z.string(), from: z.number().optional(), title: z.string().optional() }).optional()
          .describe("The reference to compare with: its file and where to play from (seconds), as match_sound gives it"),
        candidates: z.array(z.object({
          path: z.string().optional().describe("A sample"),
          instrument: z.record(z.string(), z.unknown()).optional().describe("Or a whole instrument (a plugin such as Triad with a preset, or a sampler), as in addChannel"),
          notes: z.array(noteSchema).optional().describe("What it plays in its own slot (ticks from the bar, one pattern that repeats): its line, chords or rhythm"),
          part: partEnum.optional(),
          audition: z.string().optional().describe("Its audition file, from match_sound or audition_in_song"),
          label: z.string().optional(),
        })).min(2).max(20),
        again: z.boolean().optional().describe("True when this offer follows a \"none of them\" for the same part"),
        channel: z.string().optional().describe("The song's channel the pick is for: candidates are tried in its place, playing its notes"),
        notes: z.array(noteSchema).optional().describe("Without a channel: the notes every one-shot and instrument candidate plays (else a bar of its part's usual figure)"),
        part: partEnum.optional().describe("What the candidates play, for their slot and figure"),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ title, style, reference, candidates, again, channel, notes, part, startBar, endBar }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "waiting for the user's pick", async (signal) => {
          // The engine says when an offer is answered; listen before offering, so nothing is missed.
          const answers = new Map<number, PickResult>();
          let wake: (() => void) | undefined;
          const stop = engine.on("picks", (p: { id: number } & PickResult) => {
            if (p.choice === undefined) return;
            answers.set(p.id, p);
            wake?.();
          });

          try {
            const { id } = await engine.call<{ id: number }>("picks.offer", {
              title, candidates, ...defined({ style, reference, channel, notes, part }), ...(await barRange(startBar, endBar)),
            }, { signal });

            // How many sounds the user has been shown for this part without picking one.
            const key = part ?? title;
            const shown = (shownForPart.get(key) ?? 0) + candidates.length;
            shownForPart.set(key, shown);

            const deadline = Date.now() + PICK_WAIT_MS;
            while (Date.now() < deadline && !signal.aborted) {
              const answer = answers.get(id) ?? (await engine.call<{ status: string } & PickResult>("picks.get", { id }, { signal }));
              if (answers.has(id) || (answer as { status?: string }).status === "chosen") {
                if (answer.note) return { choice: -1, chosen: "other", note: answer.note, next: "None of these was what the user wants: do what their note says and offer again." };
                const chosen = answer.choice !== undefined && answer.choice >= 0 ? candidates[answer.choice] : undefined;
                if (chosen) {
                  shownForPart.delete(key);
                  return { choice: answer.choice, chosen };
                }
                if (answer.more) {
                  return {
                    choice: -1, chosen: "another round", shown,
                    next: shown >= SHOWN_CEILING
                      ? `None of ${shown} sounds fit this part. Don't offer more of the same: tell the user how many you've been through and ask which way to go instead (other words for the sound, another kind of sound or instrument, another part, or leave it out), then wait for their answer.`
                      : `None of these: offer ${candidates.length} more for the same part, different from every sound offered for it so far (${shown} by now), with again: true.`,
                  };
                }
                // None of them: one more round of different sounds, then on without the part.
                return {
                  choice: -1, chosen: "none of them",
                  next: again
                    ? "None fit a second time: go on without this part (add no sound for it). When the rest is done, tell the user it's left out and that they can ask for it again."
                    : "None of these fit: search once more for the same part with different sounds (other words, folders or fit; none of the ones just offered), then offer_choice again with again: true.",
                };
              }
              // Until the engine says, or a while has passed (in case a notification was missed).
              await new Promise<void>((resolve) => {
                const timer = setTimeout(resolve, PICK_POLL_MS);
                const done = () => {
                  clearTimeout(timer);
                  signal.removeEventListener("abort", done);
                  resolve();
                };
                wake = done;
                signal.addEventListener("abort", done, { once: true });
              });
              wake = undefined;
            }
            if (signal.aborted) throw new Error("The wait for the user's pick was cancelled; the window stays open.");
            return { choice: null, note: "The user hasn't chosen yet; the window stays open. Ask them, or check later with the same offer." };
          } finally {
            stop();
          }
        })
      )
  );

  server.registerTool(
    "compare_to_style",
    {
      description: "Listens to the song against a style: renders the mix and hears it exactly as the style's references were heard (six stretches split into drums, bass and the rest), then reports each part's fit to the references with words for how both sound (mix, drums, kick, hats, bass, music, and single kick, hat and percussion hits), tempo, swing and hits per beat of the hats, loudness, spectral balance and each part's level against the mix, with notes on the biggest differences, most important first. channels says which channels mostly play which part. Takes up to a minute.",
      inputSchema: {
        style: z.string(),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ style, startBar, endBar }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "hearing the song", async (signal) => engine.call("styles.compare", { name: style, ...(await barRange(startBar, endBar)) }, { signal }))
      )
  );
}
