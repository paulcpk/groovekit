// The GrooveKit MCP tools: a few coarse tools over the engine API.

import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";
import { EngineClient, ticksPerBar } from "./engine.ts";
import type { Reference } from "./reference.ts";

export const instructions = `GrooveKit is a music production app (a DAW) that you operate through these tools. The human directs; you arrange, mix and master.

Talking and doing: when the user asks a question, wants your opinion or ideas, or thinks out loud ("what would make the drop hit harder?", "is the bass too busy?", "where could this go next?"), talk: answer, explain why, offer two or three concrete options with what each would do to the sound, and ask back where their taste decides. Don't change the song until they ask you to or agree to an option. Look before you give an opinion: read the song (get_summary), and render it or compare it with the style's references when the question is about how it sounds, so what you say is about this song, not techno in general. Argue from the song's style guide and the user's notes where they apply, and say plainly when something works against them. When the user asks for a change, make it.

Model: channels (instruments: the built-in sampler with one or more sample layers, or a plugin) feed mixer inserts; patterns hold notes for any channels; the playlist (the arrangement) holds pattern, audio and automation clips in numbered rows (a clip's row: 0, 1, 2, ...; any clip in any row, no setup needed; rows only arrange, with a name, mute and solo, and moveRows reorders them with their clips). How a clip sounds and where it plays is set by its channel and the mixer, never by its row. Inserts chain effects, sends and a fader, and feed other inserts or master.

Units: time is integer ticks at 960 per quarter note (in 4/4 a 16th is 240, a beat 960, a bar 3840). Volumes in dB, pan -1..1, velocity 1-127, keys are MIDI numbers (60 = C4). Sample paths are absolute or relative to the project folder.

Sounds: search_samples finds samples in the user's library by words, category, loop, tempo and key, with a description of each sound. Use one-shots as sampler layers; set rootKey from the result's rootKey so pitched samples play in tune. Drums keep the default rootKey 60 and their notes go on C4 (key 60), where they play at their own pitch; leave pitchTracking on (the default) so the user can play any sample from the keyboard. Use loops as audio clips: an audio clip plays the first layer of a sampler channel, so add a channel ({"group":"arrangement","instrument":{"type":"sampler","layers":[{"id":"sample","sample":path,"sourceTempo":sourceTempo}]}}, with the result's sourceTempo, its tempo made exact by its length so it doesn't drift; sourceTempo fits it to the song: it plays as many bars at any song tempo, time-stretched with its pitch kept, or resampled with "resample":true so the pitch follows the speed like a record; to fill a chosen number of bars, set sourceTempo to bars × beats per bar × 60 / the part's seconds; at most 8 times faster or slower), then clips naming it. The layer's start/end, reverse, transpose and fine tune (pitch-shifted, length kept), gain, pan and envelope, and the channel's filter, volume, pan and insert apply to all its clips; a clip itself only has its position, offset and fades. group "arrangement" lists the channel apart from the pattern channels in the rack. Beyond that, for notes: a layer can loop (forward or pingPong) while its note is held, fade in and out, normalize, invert, swap sides and shape its transients (punch, tail), be shortened with length (0.25 makes a 0.8 s hit a 0.2 s one, cut off there; give it a fadeOut so the new end doesn't click or sound chopped), crossfade where a forward loop comes round, have effects worked into its sample (process: trimDb, removeDc, stretchSemitones and stretchLength for pitch and length apart at full quality, pitchBend, ringMix and ringHz, eq, cutoff and resonance, boostDb with clip, reverb with reverbSize, stereoDelayMs, fadeStereo), and play as grains (grains: {on: true, size, density, speed, spray, detune, width, shape}): keys then change the pitch but not the timing, and speed the timing but not the pitch (0.5 twice as long, 0 holds a texture while the note is held); few short grains stutter, many scattered, detuned, wide ones make clouds and pads from any sample; envelopes have delay, hold and curves; the channel's modulation moves volume and cutoff with LFOs, and resonance, pan and pitch with envelopes and LFOs (a pitch envelope of amount 0.5, decaying fast, is a kick's drop); the filter is lowpass, lowpass24, highpass, highpass24, bandpass or notch. describe_sample gives full measurements (spectrum bands, attack, pitch, loudness). Paths in results are absolute.

Plugins: the user's VST3/AU plugins are listed by plugins (scan first if the list is empty). GrooveKit ships its own (manufacturer GrooveKit, always listed), each with factory presets you choose by name with "preset" in the pluginRef (describe_plugin lists them): Triad, a three-oscillator synth (sine, triangle, square, saw, soft saw, pulse, noise; FM and ring between oscillators, unison, a filter with its envelope, an LFO, glide) for basses, stabs, leads and pads; Quartet, four-operator FM with eight algorithms, a chord memory (minor 7, minor 9, house voicing, fourths...) and an analogue layer, the synth for house chords, techno stabs, keys and FM basses ("Stabs: Hard Techno Stab", "Chords: Deep House Chord", "Bass: Lately Bass"); Sextet, six morphing oscillators with FM, ring and sync between them, two filters, three envelopes, three LFOs and a ten-slot modulation matrix with drive, chorus, delay and reverb, the synth for pads, long chords, trance sounds and raw basslines ("Pads: Warm Trance Pad", "Bass: Reese", "Leads: Supersaw Lead"); Chamber, an algorithmic reverb from rooms to endless washes with ducking and freeze ("Techno: Dub Tail", "Halls: Cathedral"); and Echo, a stereo delay synced to the tempo with ping-pong, dual times, tape saturation and wow, dub feedback and ducking ("Ping-pong: Dotted Ping-pong", "Tape: Space Echo"). Start a synth part from the preset nearest what's wanted and set parameters by name from there ("Osc 1 shape", "Cutoff"). describe_plugin gives a pluginRef, the presets and parameter names; use it in a channel instrument {"type":"plugin","plugin":pluginRef} or an insert effect {"type":"plugin","id":...,"plugin":pluginRef}, add "preset" by name, set "params" by name (normalized 0-1), and automate them with automation clips (an instrument's: scope channel with param its parameter id or name; an effect's: scope effect). A sampler channel's filterCutoff, filterResonance and filterEnvelopeAmount are automatable too, like any channel's volume and pan. Every insert effect, built-in or plugin, has a mix from 0 (the dry signal) to 1 (only the effect): mix reverbs and delays in rather than adding sends for everything; it is automatable as the effect parameter "mix".

Listening: GrooveKit can hear its sample library and the user's reference tracks (once its listening models are downloaded, a 400 MB download the user starts in the Browser). Choose sounds by how they sound, never by their names: two files called "stab" can have nothing in common. A style is a folder of reference tracks the user loves (styles); styles describe gives its tempo range, swing, loudness, the levels of drums, bass and music against the mix, and words for what each part sounds like. search_samples with style ranks the library by fit to that style for each sample's part (category), 0 being an average sample of the user's library and 100 as close as the references are to each other: build from the top (fit 50 and above), use sound (words like "dusty warm chord stab") to steer within the style, and like (a path) to find sounds that belong with one you chose, so the kit hangs together. Results say in words what each sample sounds like. Sounds like the references': the kick and rumble carry techno, so choose them by matching, not by name or a fingerprint alone. Take a reference (a song map the user names, or a ticked one of the song's style), match_sound find for the closest kicks (one-shots and loops), audition the best ones in a groove, offer_choice the best five (or as many as the user asks for) to the user with their audition files and the reference (its file and from), then build the kick with the pick and match_sound channel it against the reference: apply the changes it gives (tune, shorten, punch, EQ, grit) one at a time and measure again until it's close. A one-shot has no rumble: make it from the kick (a send to an insert with reverb fully wet, distortion, a lowpass around 100-150 Hz and duck keyed by the kick), then measure again. Before arranging in a style, read two or three of its references' maps (song_maps list, then read): their scores show how the user's favourite tracks are built (how long the intro runs, when the rumble and the hats come in, where the breaks fall and what's left in them, how many parts play at once, what changes every 8 or 16 bars); build the song's form from them rather than from habit, and when the user names tracks to sound like, use theirs. The maps' figures show what the style's stabs, chords and basslines actually play: write notes in their rhythm and intervals, moved into the song's key, not tunes of your own; when the user gives you notes, place exactly those. After arranging, compare_to_style renders the song and reports how far each part is from the references and what differs most (tempo, swing, busy or sparse drums, balance, levels); fix the biggest differences and compare again. When the user says a sound fits or doesn't, record it with rate_sample: their verdicts steer later searches, and from two samples that fit GrooveKit learns what they have in common (results then give taste, 0-100: how likely the user is to say a sample fits, trusted more the more they've rated), so ask for verdicts on the sounds you pick.


Keys and melodies: give the song a key with the setKey command ("A minor"); get_summary shows it. Samples have keys too: search_samples results give key (from the file name, else heard; keySource says which, heardKey when the sound disagrees with the name) and rootKey (a one-shot's pitch, drums included: a kick's is its tail's). fitsKey ("song" or a key) keeps samples that go with that key (the same notes, a fifth away, or a root in its scale) and gives each keyFit.transpose: the semitones (layer transpose) that put it exactly in key, tonic for one-shots, so loops and kicks can be tuned to the song. The melody tool turns melodies into notes on the song's grid, in its key and a register: from the user's voice (they hum or sing on "da" in a voice message, often after saying which channel it's for: the message then mentions the hummed melody, which it does only when they say there's one: "this is the melody", "it goes like") or from a reference track (a part of a style's tracks: its lead line, chords or bassline). Place the result with edit_project (addPattern if needed, then addNotes with the notes, channel set).
Styles: a song can have a style (the setStyle command with a guide's id or name; get_summary shows it), and each style has a guide (the style_guide tool). Before you start on a song, set its style when the user names or implies one, then read its guide and follow it: it says what the style should feel like, which parts to use and how each should sound, the order to build in, how loud each part sits against the kick, how many parts play at once, and what to avoid. Fewer, better parts beat many. When a request clearly belongs to another style than the song's, ask before changing it. The user's notes win where they disagree with a guide.

Notes: the user keeps notes for you (the notes tool), standing wishes like "prefer loops over one-shots" or what a style should sound like. Read them (notes list) before you start on a song and follow the enabled ones that apply here (here: true), unless the user asks otherwise in the conversation. When the user asks you to remember something for later, add a note (scope song when it's about this song only); change or delete notes only when the user asks.

Mixing: sounds that are good alone can be bad together, because each fills more of the spectrum than its part should; a clean, crisp mix gives every part its own room. Every channel has a part (kick, rumble, drums, bass, percussion, hats, synth, vocal, fx) and a slot: an EQ first on its own insert that keeps it to its range (kick and rumble low-passed steeply, synths band-passed to the mids, percussion and hats high-passed with the top tamed). New channels get theirs from edit_project; check the part it guessed and fix it with mix_slots set. Build part by part and, after each new sound: choose it in the song, not alone (audition_in_song the best candidates in the channel's place over the groove, offer_choice them with those files; for a part played by notes, offer one-shots or instruments such as Triad presets with the line they'd play, so the user picks those by ear in the song too), then check_mix the busiest bars and fix the worst problem before adding the next part (raise a low cut, notch where two parts cover each other, duck the rumble or bass under the kick, tame the highs). Keep the low end for the kick and one low part ducked under it, the top sparing and moving (filter or level automation over 8-16 bars), and fewer parts that each own a band over many that fight.

Arranging: a part earns its place by being heard. At most five or six parts play at once (a quiet bed of texture under the whole track doesn't count); bring new parts in one at a time, a section apart (kick and percussion, then hats, then the sub, then the next), never two or three new ones on the same bar; and once the mix is full, when something comes in, something else goes out (the stab comes, the hat loop rests; the clap comes back, the metal goes). check_mix with listen false checks this in a moment.

How the user likes it made: use loops and samples from their library rather than programming parts or building synth sounds yourself; program only when they ask or nothing in the library fits. Percussion and effect loops should be unpitched (no melody in them), so they don't clash with the music. Keep hats short, crisp and in the background, a shuffle under the groove, and claps subtle. Move filters organically: automation on curves over 4 to 16 bars that opens and closes like a hand on a knob, rarely the same twice, rather than a strict LFO. Give stabs a little reverb and delay (around 20% wet), enough for air, never a wash.

Sidechains and pumping: any built-in effect can be keyed by another channel or insert (sidechain: its id; a channel that plays audio clips keys from its insert): a compressor on the bass or pads keyed by the kick channel (thresholdDb -30, ratio 10, attackMs 0.5, releaseMs 150) makes them duck under every kick, the backbone of house and techno grooves. The duck effect pumps on a tempo grid without a key (sync, depth, release, curve). Keep it subtle unless the style wants the pump.

Recording: the record tool captures hardware like the user's Digitakt II per track into clips, with GrooveKit as MIDI clock master. After a recording, each take is an audio clip with its own sampler channel; cut (updateClip offset/length), copy and arrange them like any other.

Workflow: get_summary to see the song, get_schema once for exact shapes, edit_project to change it (commands apply atomically; errors name the offending field), render to hear it as numbers (LUFS, peak, onsets), then adjust and re-render. Every edit is undoable; edits made close together form one group that undo reverts in one step.`;

const GROUP_IDLE_MS = 120_000;

function text(value: unknown): CallToolResult {
  return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 1) }] };
}

function failure(error: unknown): CallToolResult {
  return { content: [{ type: "text", text: `Error: ${error instanceof Error ? error.message : String(error)}` }], isError: true };
}

async function attempt(fn: () => Promise<unknown>): Promise<CallToolResult> {
  try {
    return text(await fn());
  } catch (e) {
    return failure(e);
  }
}

/** Groups edits that happen close together, so one agent request undoes in one step. */
export class GroupTracker {
  private current = "";
  private last = 0;
  private count = 0;
  private readonly now: () => number;

  constructor(now: () => number = Date.now) {
    this.now = now;
  }

  next(): string {
    const t = this.now();
    if (!this.current || t - this.last > GROUP_IDLE_MS) this.current = `agent-${t}-${++this.count}`;
    this.last = t;
    return this.current;
  }

  /** Makes the next edit start a new group. */
  reset() {
    this.current = "";
  }
}

/** The agent's tools, on an MCP server (or anything with the same registerTool, like the app's own
    agent). reference describes the data model and commands (reference-files.ts in Node). */
export function registerTools(server: McpServer, engine: EngineClient, reference: Reference, groups = new GroupTracker()) {
  const { commandReference, typeDefinitions } = reference;
  const barToTicks = (bar: number) => Math.round((bar - 1) * ticksPerBar(engine.document));

  server.registerTool(
    "get_summary",
    {
      description: "Compact overview of the song: tempo, channels and their samples, patterns with note counts, the playlist with clip positions in bars (1-based), and the mixer.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("getSummary"))
  );

  server.registerTool(
    "get_project",
    {
      description: "The exact song.json, or the part at a JSON Pointer (e.g. /patterns/0/notes, /mixer/inserts/2). Use it to see notes and full settings before editing.",
      inputSchema: { path: z.string().optional().describe("JSON Pointer; omit for the whole document") },
      annotations: { readOnlyHint: true },
    },
    ({ path }) => attempt(() => engine.call("get", { path: path ?? "" }))
  );

  server.registerTool(
    "get_schema",
    {
      description: "TypeScript definitions of the project model (Channel, Pattern, Note, Clip, Insert, Effect...) and of the edit commands, with field docs and defaults. Read once before building commands.",
      inputSchema: { which: z.enum(["project", "commands"]).optional().describe("Default: project") },
      annotations: { readOnlyHint: true },
    },
    async ({ which }) => text(typeDefinitions(which === "commands" ? "protocol" : "project"))
  );

  server.registerTool(
    "edit_project",
    {
      description: `Applies commands to the song as one atomic, undoable change. If any command fails or the result is invalid, nothing changes and the error names the field. Each command is an object with "type" plus the fields below (shapes in get_schema):\n${commandReference()}\n\nExample: [{"type":"updateChannel","channel":"kick","changes":{"volumeDb":-3}},{"type":"addNotes","pattern":"beat","notes":[{"channel":"hat","start":480,"length":120}]}]`,
      inputSchema: {
        commands: z.array(z.object({ type: z.string() }).passthrough()).min(1).describe("Commands, applied in order"),
        label: z.string().optional().describe("Short description for the history, e.g. \"Add hi-hat groove\""),
      },
    },
    ({ commands, label }) =>
      attempt(async () => {
        const group = groups.next();
        const result = (await engine.call("execute", { commands, group, ...(label ? { label } : {}) })) as Record<string, unknown>;

        // New channels get their part's frequency slot straight away, in the same undo step.
        const added = commands.flatMap((c) => {
          const channel = (c as { channel?: { id?: unknown } }).channel;
          return c.type === "addChannel" && typeof channel?.id === "string" ? [channel.id] : [];
        });
        if (added.length === 0) return result;

        try {
          const slots = (await engine.call("mix.slots", { channels: added, apply: true, onlyNew: true, group })) as { channels: unknown[] };
          return { ...result, slots: slots.channels, slotsNote: "Each new channel got its own insert with a slot EQ for its part, first on the insert. If a part is wrong, mix_slots set it." };
        } catch {
          return result;
        }
      })
  );

  server.registerTool(
    "undo",
    {
      description: "Undoes the most recent group of changes (all edits made close together count as one group). Repeat to go further back.",
      inputSchema: { steps: z.number().int().min(1).max(50).optional().describe("Groups to undo; default 1") },
    },
    ({ steps }) =>
      attempt(async () => {
        let result: unknown;
        for (let i = 0; i < (steps ?? 1); i++) result = await engine.call("undo");
        groups.reset();
        return result;
      })
  );

  server.registerTool(
    "redo",
    {
      description: "Redoes the most recently undone group of changes.",
      inputSchema: { steps: z.number().int().min(1).max(50).optional() },
    },
    ({ steps }) =>
      attempt(async () => {
        let result: unknown;
        for (let i = 0; i < (steps ?? 1); i++) result = await engine.call("redo");
        groups.reset();
        return result;
      })
  );

  server.registerTool(
    "get_history",
    {
      description: "The change history: who changed what and when, and the current position.",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("getHistory"))
  );

  server.registerTool(
    "render",
    {
      description: "Renders the song (or bars of it) offline to a file (WAV 48 kHz 24-bit unless chosen; FLAC or MP3 too) and returns its path with peak, true peak, RMS, integrated/short-term/momentary LUFS, loudness range, spectral balance (10 bands, dB share), stereo correlation and onset times. Use this to check levels, tone and timing after changes. With stems, each mixer insert is also rendered on its own (soloed) next to the mix, for the user to take elsewhere. silenceBefore and silenceAfter add silence (seconds); normalize brings the peak to a level (dB), the stems by the same gain.",
      inputSchema: {
        startBar: z.number().min(1).optional().describe("First bar, 1-based; default the start"),
        endBar: z.number().min(1).optional().describe("Bar to stop before; default the end of the last clip"),
        tailSeconds: z.number().min(0).max(30).optional().describe("Extra time for reverb and release tails"),
        path: z.string().optional().describe("Where to write it; default <project>/renders/"),
        format: z.enum(["wav", "flac", "mp3"]).optional(),
        sampleRate: z.number().min(8000).max(192000).optional().describe("MP3: 44100 or 48000"),
        bitDepth: z.union([z.literal(16), z.literal(24), z.literal(32)]).optional().describe("WAV 16, 24 or 32 (float); FLAC 16 or 24"),
        bitrate: z.number().int().min(96).max(320).optional().describe("MP3 kbit/s, default 320"),
        silenceBefore: z.number().min(0).max(60).optional(),
        silenceAfter: z.number().min(0).max(60).optional(),
        normalize: z.number().min(-24).max(0).optional().describe("Peak level in dB"),
        stems: z.boolean().optional(),
      },
    },
    ({ startBar, endBar, ...rest }) =>
      attempt(async () => {
        await engine.connect();
        const options = Object.fromEntries(Object.entries(rest).filter(([, v]) => v !== undefined));
        return engine.call("render", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          ...options,
        });
      })
  );

  server.registerTool(
    "master",
    {
      description: "Brings the song to a target loudness with the master limiter (added last on master as \"master-limiter\" if missing): renders, measures integrated LUFS and adjusts the limiter's input gain until within 0.2 LU. Shape the tone first with your own chain on master, e.g. eq (gentle shelves), compressor (ratio 2, thresholdDb -18, attackMs 30, releaseMs 100) for glue, multiband if the bands need taming (bandsMode 1 uses only its master section), then a clipper (a few dB of driveDb, ceilingDb -1) to take the sharpest peaks off before the limiter. Typical targets: -14 LUFS streaming, -9 to -7 club. Undoable like any edit.",
      inputSchema: {
        targetLufs: z.number().min(-40).max(-3).optional().describe("Default -14"),
        ceilingDb: z.number().min(-12).max(0).optional().describe("Limiter ceiling; default -1"),
        releaseMs: z.number().min(1).max(1000).optional(),
        startBar: z.number().min(1).optional().describe("Measure only from here; default the whole song"),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ targetLufs, ceilingDb, releaseMs, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("master", {
          ...(targetLufs !== undefined ? { targetLufs } : {}),
          ...(ceilingDb !== undefined ? { ceilingDb } : {}),
          ...(releaseMs !== undefined ? { releaseMs } : {}),
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          group: groups.next(),
        });
      })
  );

  server.registerTool(
    "transport",
    {
      description: "Plays the song (or one pattern on a loop) through the user's speakers, stops it, or moves the playhead, so the user can listen. loop repeats bars until stopped. Stop keeps the position. Rendering doesn't need this.",
      inputSchema: {
        action: z.enum(["play", "stop", "state"]),
        fromBar: z.number().min(1).optional().describe("For play: start here (1-based)"),
        loopBars: z.tuple([z.number().min(1), z.number().min(1)]).optional().describe("For play: [first bar, bar to stop before] to loop"),
        pattern: z.string().optional().describe("For play: loop this pattern instead of the song (pattern mode, as in the user's PAT switch)"),
      },
    },
    ({ action, fromBar, loopBars, pattern }) =>
      attempt(async () => {
        await engine.connect();
        if (action === "play" && pattern) {
          await engine.call("transport.setMode", { mode: "pattern", pattern });
          return engine.call("transport.play");
        }
        if (action === "play" && (fromBar !== undefined || loopBars)) await engine.call("transport.setMode", { mode: "song" });
        if (action === "play") {
          await engine.call("transport.setLoop", loopBars ? { start: barToTicks(loopBars[0]), end: barToTicks(loopBars[1]) } : {});
          if (fromBar !== undefined || loopBars) await engine.call("transport.setPosition", { position: barToTicks(fromBar ?? loopBars![0]) });
        }
        return engine.call(`transport.${action}`);
      })
  );

  server.registerTool(
    "project",
    {
      description: `The project (a folder holding song.json, its history and recordings) that the engine has open, shared with the user's GrooveKit window.
- current: its folder and the recently opened ones.
- open: opens another project folder.
- new: creates a project in a new, empty folder and opens it.
- saveAs: copies this project to a new, empty folder and opens the copy. (Every change is saved as it's made; there's no separate save.)
Opening, creating or saving as switches the user's window too, so only do it when the user asks.`,
      inputSchema: {
        action: z.enum(["current", "open", "new", "saveAs"]),
        path: z.string().optional().describe("For open, new and saveAs: the project folder (absolute)"),
      },
    },
    ({ action, path }) =>
      attempt(async () => {
        await engine.connect();
        if (action === "current") return engine.call("project.recent");
        if (!path) throw new Error(`${action} needs a path`);
        return engine.call(`project.${action}`, { path });
      })
  );

  server.registerTool(
    "record",
    {
      description: `Records audio inputs into clips, e.g. the user's Digitakt II over USB (16 tracks; each can be an audio or a MIDI track, so some outputs are silent).
- devices: lists audio input devices, inputs and MIDI outputs.
- setup: selects the input device (e.g. "Digitakt II") and turns on MIDI clock to a MIDI output (e.g. "Elektron Digitakt II"), so the hardware starts and stops with GrooveKit and follows its tempo.
- start: records the given inputs (or "all": Main, Track 1-16 and the Delay, Reverb and Chorus returns; per-track outputs are dry) from fromBar for bars bars, each into its own insert and arrangement row (named after the input). Silent takes are skipped, and each take's channel gain is set so it plays at about -20 LUFS (hardware outputs arrive quiet). Returns at once; the takes become clips when it stops.
- stop: stops early.
- calibrate: once per setup, with the hardware playing clean hits on the given input (a kick track), measures the round trip so takes land on the grid.`,
      inputSchema: {
        action: z.enum(["devices", "setup", "start", "stop", "calibrate"]),
        inputDevice: z.string().optional().describe("For setup"),
        clockOutput: z.string().optional().describe("For setup: MIDI output to send clock to"),
        inputs: z.union([z.literal("all"), z.array(z.string())]).optional().describe("For start; default all"),
        fromBar: z.number().min(1).optional().describe("For start; default 1"),
        bars: z.number().min(1).optional().describe("For start (default 8) and calibrate (default 2)"),
        input: z.string().optional().describe("For calibrate; default Track 1"),
      },
    },
    (args) =>
      attempt(async () => {
        await engine.connect();
        switch (args.action) {
          case "devices": {
            const audio = await engine.call<any>("audio.devices");
            return { audio, inputs: (await engine.call<any>("record.inputs")).inputs, midi: await engine.call("midi.devices") };
          }
          case "setup": {
            if (args.inputDevice) await engine.call("audio.setDevice", { input: args.inputDevice });
            if (args.clockOutput) await engine.call("midi.setClock", { output: args.clockOutput, enabled: true });
            return { inputs: (await engine.call<any>("record.inputs")).inputs, alignment: await engine.call("record.alignment") };
          }
          case "start": {
            const names: string[] = !args.inputs || args.inputs === "all"
              ? ["Main", ...Array.from({ length: 16 }, (_, i) => `Track ${i + 1}`), "Delay", "Reverb", "Chorus"]
              : args.inputs;
            const slug = (n: string) => "rec-" + n.toLowerCase().replace(/[^a-z0-9]+/g, "");
            const doc = engine.document!;
            const commands: unknown[] = [];
            // Each input records into the arrangement row of its name, or the next free one below the song.
            const used = new Set<number>([...doc.playlist.clips.map((c) => c.row), ...doc.playlist.rows.flatMap((r, i) => (r.name ? [i] : []))]);
            let next = Math.max(-1, ...used) + 1;
            const rows = new Map<string, number>();
            for (const name of names) {
              const id = slug(name);
              if (!doc.mixer.inserts.some((i) => i.id === id)) commands.push({ type: "addInsert", insert: { id, name } });
              const existing = doc.playlist.rows.findIndex((r) => r.name === name);
              const row = existing >= 0 ? existing : next++;
              if (existing < 0) commands.push({ type: "updateRow", row, changes: { name } });
              rows.set(name, row);
            }
            if (commands.length) await engine.call("execute", { commands, group: groups.next(), label: "Add recording rows" });
            return engine.call("record.start", {
              targets: names.map((name) => ({ input: name, insert: slug(name), row: rows.get(name) })),
              fromBar: args.fromBar ?? 1,
              bars: args.bars ?? 8,
              dropSilent: true,
              normalizeLufs: -20,
            });
          }
          case "stop":
            return engine.call("record.stop", { group: groups.next() });
          case "calibrate":
            return engine.call("record.calibrate", { input: args.input ?? "Track 1", bars: args.bars ?? 2 });
        }
      })
  );

  server.registerTool(
    "analyze_mix",
    {
      description: "Renders the mix plus one stem per channel (with its audio clips) and reports: the mix's loudness, true peak, loudness range, spectral balance and stereo image; each source's loudness and share of every frequency band; and masking, i.e. pairs of sources that both carry a big share of the same band (e.g. kick and bass at 60-120 Hz). Slower than render: one render per source.",
      inputSchema: {
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("analyzeMix", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  server.registerTool(
    "mix_slots",
    {
      description: "Frequency slots: each part keeps to its range so sounds that are good alone don't cover each other together. Think of every channel in four ranges, lows, low mids, upper mids and highs, and shape it toward its own: a channel's slot is an EQ first on its own insert (the effect \"slot\", gentle, in addition to any other EQ) that does that. Kick: lows, 32 Hz to 4 kHz with the top eased off 4 dB above 2.5 kHz, so it's round, not clicky. Rumble: lows, a steep low-pass at 250 Hz. Bass (a sub): lows, nothing above 320 Hz. Drums (a full loop): the top tamed. Percussion loops: low mids, cut below 180 Hz, the top open for air. Clap and snare: upper mids, 900 Hz to 7 kHz, lows and highs dampened. Hats: upper mids and highs, cut below 1.8 kHz, the top down 3 dB. Synths and stabs: low and upper mids, 280 Hz to 6 kHz. Vocals: 140 Hz to 14 kHz. Effects and textures: upper mids, 400 Hz to 9 kHz, in the background. With the song's style heard, bass, synth and vocal slots follow where its references keep those parts. New channels get one automatically from edit_project. list: every channel's part (guessed from its name and sample when not set), its range, its slot and the proposed one. apply: gives channels (default all) their part's proposed slot, making each its own insert when it shares one or plays into master. set: one channel's part or cuts by hand (lowCutHz, highCutHz in Hz, 0 for none; steep doubles the high cut to 48 dB/octave; topShelfDb tames the top above topShelfHz). Bands 2-4 of the slot EQ stay free for notches and peaks.",
      inputSchema: {
        action: z.enum(["list", "apply", "set"]),
        channels: z.array(z.string()).optional().describe("list and apply: these channels (default all)"),
        channel: z.string().optional().describe("set: the channel"),
        part: z.enum(["kick", "rumble", "drums", "bass", "percussion", "clap", "hats", "synth", "vocal", "fx"]).optional().describe("set: what it plays (starts it from that part's slot)"),
        lowCutHz: z.number().min(0).max(20000).optional(),
        highCutHz: z.number().min(0).max(20000).optional(),
        steep: z.boolean().optional(),
        topShelfDb: z.number().min(-24).max(24).optional(),
        topShelfHz: z.number().min(20).max(20000).optional(),
      },
    },
    ({ action, channels, channel, part, lowCutHz, highCutHz, steep, topShelfDb, topShelfHz }) =>
      attempt(async () => {
        await engine.connect();
        if (action === "list") return engine.call("mix.slots", channels ? { channels } : {});
        if (action === "apply") return engine.call("mix.slots", { ...(channels ? { channels } : {}), apply: true, group: groups.next() });
        return engine.call("mix.setSlot", {
          channel, group: groups.next(),
          ...Object.fromEntries(Object.entries({ part, lowCutHz, highCutHz, steep, topShelfDb, topShelfHz }).filter(([, v]) => v !== undefined)),
        });
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
    ({ startBar, endBar, style, listen, maxParts }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("mix.check", {
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
          ...(style ? { style } : {}),
          ...(listen !== undefined ? { listen } : {}),
          ...(maxParts ? { maxParts } : {}),
        });
      })
  );

  server.registerTool(
    "audition_in_song",
    {
      description: "Tries up to twenty candidate samples in a channel's place inside the song: renders the bars (default the first 8) with each candidate on the channel (loops at their own tempo, through the channel's slot and effects) and as it is now, and says how each sits with the rest: fit 0-100 (100 when the bands it lives in are its own, low when the rest covers it there), covers (the bands where the rest is as loud as it), holds and its level against the mix. Candidates play as loud as the sound they would replace, so the comparison is fair; for bass, synths and vocals, key says whether its notes go with the song's key and the transpose that puts it in. Each has a file to offer_choice as its audition, so the user chooses sounds with the groove around them, not alone; the pick window then has a slider that fades the song under the sound, down to the sound alone. The song isn't changed: put the pick in with edit_project.",
      inputSchema: {
        channel: z.string().describe("A sampler channel already in the song (its first layer is swapped)"),
        paths: z.array(z.string()).min(1).max(20).describe("The candidate samples"),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ channel, paths, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("mix.audition", {
          channel, paths,
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  server.registerTool(
    "analyze_audio",
    {
      description: "The same measurements as render, for any audio file or a time range of it (seconds).",
      inputSchema: {
        path: z.string(),
        startSeconds: z.number().min(0).optional(),
        endSeconds: z.number().min(0).optional(),
      },
      annotations: { readOnlyHint: true },
    },
    ({ path, startSeconds, endSeconds }) => attempt(() => engine.call("analyze", { path, start: startSeconds, end: endSeconds }))
  );

  server.registerTool(
    "search_samples",
    {
      description: "Searches the user's sample library. Every word in query must match the file name, its folders or its category. With style, sound or like (needs the listening models), results are ranked by how they sound: fit (to the style, for each sample's part: 0 an average library sample, 100 as close as the style's references are to each other) and match (to the words or the sample: the share of the library it beats, 0-100); combine them with the filters, e.g. category and loop. Results give the path, a description of the sound, sounds (words for what it sounds like), category, loop, tempo, key (keySource: name, sound or both; heardKey when the sound disagrees with the name), duration, rootKey (detected pitch) where known, keyFit with fitsKey, the user's rating, and taste (0-100, learned from their ratings) once they've rated a few.",
      inputSchema: {
        query: z.string().optional().describe("Words in the file name or folders, e.g. \"kick\" or \"vinyl\""),
        style: z.string().optional().describe("A style name (styles list): rank by fit to it"),
        sound: z.string().optional().describe("What it should sound like, in words, e.g. \"dusty warm chord stab\" or \"rumbling distorted kick\""),
        like: z.string().optional().describe("Path of a sample (or any audio file) it should sound like"),
        role: z.enum(["kick", "hats", "drums", "bass", "music", "kickHit", "hatHit", "percHit"]).optional().describe("The part to judge fit for, if not the sample's category (loops: kick, hats, drums, bass, music; one-shots: kickHit, hatHit, percHit)"),
        category: z.enum(["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "bass", "synth", "vocal", "fx", "drums", "other"]).optional(),
        loop: z.boolean().optional().describe("true for loops only, false for one-shots only"),
        tempo: z.number().optional().describe("BPM; also matches half and double time"),
        key: z.string().optional().describe("In this key (or its relative), e.g. \"Am\", \"F# minor\""),
        fitsKey: z.string().optional().describe("\"song\" or a key: only samples that go with it, each with keyFit.transpose (semitones to put it exactly in key)"),
        minDuration: z.number().optional().describe("Seconds"),
        maxDuration: z.number().optional().describe("Seconds"),
        limit: z.number().int().min(1).max(100).optional().describe("Default 20"),
      },
      annotations: { readOnlyHint: true },
    },
    (args) => attempt(() => engine.call("library.search", args))
  );

  server.registerTool(
    "melody",
    {
      description: "Notes of a melody, placed on the song's grid (ticks from the melody's start, 960 a beat) in the song's key (or key) and a register, ready for addNotes. source voice: what the user hummed or sang in a voice message (the latest, or recording), its tempo worked out from the rhythm (tempo \"song\" takes it as hummed at the song's tempo), snapped to the scale. source reference: a stretch of one of the styles' reference tracks (track: words of its file name; from and length in seconds, default 24 s two fifths in), split into stems and transcribed: part lead (the top line of everything but drums and bass), chords (all of it) or bass; takes several seconds. Gives notes, length (whole bars), heardTempo, heardKey and transposed (semitones moved).",
      inputSchema: {
        source: z.enum(["voice", "reference"]),
        recording: z.string().optional().describe("voice: which voice message (default the latest)"),
        tempo: z.enum(["song"]).optional().describe("voice: it was hummed at the song's tempo"),
        track: z.string().optional().describe("reference: words of the track's file name"),
        style: z.string().optional().describe("reference: the style to look in (default all)"),
        from: z.number().optional().describe("Seconds: where to start (reference), or where the melody starts in the voice message"),
        length: z.number().optional().describe("reference: seconds to take, 2-60 (default 24)"),
        part: z.enum(["lead", "chords", "bass"]).optional().describe("reference: default lead"),
        key: z.string().optional().describe("Move it into this key instead of the song's"),
        register: z.enum(["bass", "chords", "lead", "high"]).optional().describe("Octaves to move it into (bass C1-E3, chords C3-C5, lead C4-C6, high C5-C7)"),
        snapToScale: z.boolean().optional().describe("Snap notes to the key's scale (default: voice yes, reference no)"),
      },
      annotations: { readOnlyHint: true },
    },
    ({ source, ...args }) => attempt(() => engine.call(source === "voice" ? "melody.voice" : "melody.reference", args))
  );

  server.registerTool(
    "describe_sample",
    {
      description: "Full measurements of one audio file: duration, loudness, attack and decay, spectral centroid and band energy (sub, low, mid, high), detected pitch, onset count and tempo, plus its classification and, once GrooveKit can listen, words for what it sounds like and (with style) its fit. Works for files outside the library too.",
      inputSchema: { path: z.string(), style: z.string().optional().describe("A style name: also give its fit") },
      annotations: { readOnlyHint: true },
    },
    ({ path, style }) => attempt(() => engine.call("library.describe", { path, ...(style ? { style } : {}) }))
  );

  server.registerTool(
    "styles",
    {
      description: "The user's styles: folders of reference tracks GrooveKit has heard, each track once into a song map (song_maps); a style is learnt from its maps in use. list (with listening status: models, library indexing, the hearing queue), describe (tempo range, swing, onsets per beat, loudness, spectral balance, each part's level against the mix, and words for what each part sounds like), add (queues a folder's new and changed tracks to be heard in the background, a few minutes per track). download fetches the listening models (about 400 MB): only when the user has agreed to it. references lists the folders in the user's references folder (~/Music/GrooveKit/References, one folder per style, named like its guide): each with its file count, the style heard from it, how many tracks are new or gone since, and its guide; add a folder that has new tracks to hear them.",
      inputSchema: {
        action: z.enum(["list", "references", "describe", "add", "download"]),
        name: z.string().optional().describe("The style, for describe; for add, a name (default: the folder's)"),
        path: z.string().optional().describe("Folder of reference tracks, for add"),
      },
    },
    ({ action, name, path }) =>
      attempt(async () => {
        if (action === "download") return engine.call("listening.download", {});
        if (action === "add") return engine.call("styles.add", { path, ...(name ? { name } : {}) });
        if (action === "list") return { ...((await engine.call("styles.list", {})) as object), listening: await engine.call("listening.status", {}) };
        if (action === "references") return engine.call("styles.references", {});
        return engine.call(`styles.${action}`, { name });
      })
  );

  server.registerTool(
    "song_maps",
    {
      description: "The user's reference tracks, each heard once into a song map: its arrangement bar by bar (the beat grid, sections with their role and bars, which parts play in each: kick, rumble, bass, hats, percussion, music, vocal; energy; changes like a part out for a bar or a filter opening), the figures its music and bass play (two bars of notes each, where the part is loudest: the score shows them in 16ths, figures gives their notes in ticks with the key they're in), and what it sounds like. list: the songs by style, with status (heard, older: heard before arrangements or figures were heard, new, queued...), inUse (the user's tick: the style learns from ticked songs) and tempo; style narrows it. read: one song's map as a score (id from list), to learn how the style's tracks are built before arranging: intro length, when the low end and the hats come in, where breaks fall, how many parts play at once; and, before writing stabs, chords or a bassline, what its lines are like: read two or three maps' figures and write in their rhythm and intervals (a hard techno stab is mostly one note hammered on a few 16ths with a jump to another, not a melody), moving the notes into the song's key (transpose by the difference between the figure's key and the song's, keeping octaves where the part lives) rather than inventing a tune. hear: queues songs (ids) to be heard (again), a few minutes each in the background. setInUse: ticks or unticks songs (ids, inUse), only when the user asks.",
      inputSchema: {
        action: z.enum(["list", "read", "hear", "setInUse"]),
        style: z.string().optional().describe("list: only this style"),
        id: z.string().optional().describe("read: the song, as list gives it (style/title)"),
        ids: z.array(z.string()).optional().describe("hear, setInUse: the songs"),
        inUse: z.boolean().optional().describe("setInUse"),
      },
    },
    ({ action, style, id, ids, inUse }) =>
      attempt(async () => {
        if (action === "list") {
          const r = (await engine.call("maps.list", {})) as { songs: { id: string; style: string; title: string; status: string; inUse: boolean; map?: { tempo: number } }[] };
          const songs = r.songs.filter((s) => !style || s.style.toLowerCase() === style.toLowerCase());
          return songs.map((s) => ({ id: s.id, status: s.status, inUse: s.inUse, ...(s.map ? { tempo: s.map.tempo } : {}) }));
        }
        if (action === "read") {
          const map = (await engine.call("maps.get", { id })) as { style: string; title: string; inUse: boolean; score?: string; track: Record<string, unknown> & { figures?: unknown[] } };
          const { sounds: _, arrangement: __, balance: ___, path: ____, figures, ...measured } = map.track;
          return {
            id, style: map.style, title: map.title, inUse: map.inUse, measured,
            score: map.score ?? "Heard before arrangements were heard: hear it again for its score.",
            figures: figures ?? "Heard before figures were heard: hear it again for what its music and bass play.",
          };
        }
        if (action === "hear") return engine.call("maps.hear", { ids });
        return engine.call("maps.setInUse", { ids, inUse });
      })
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
    ({ action, id, path, loops, limit, paths, tempo, channel, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        const reference = id ? { id } : { path };
        if (action === "find") return engine.call("match.kicks", { ...reference, ...(loops !== undefined ? { loops } : {}), ...(limit ? { limit } : {}) });
        if (action === "audition") return engine.call("match.audition", { ...reference, paths, ...(tempo ? { tempo } : {}) });
        return engine.call("match.channel", {
          ...reference, channel,
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  /** Sounds shown per part (or offer title) since the user last picked one, across rounds. */
  const shownForPart = new Map<string, number>();

  server.registerTool(
    "offer_choice",
    {
      description: "Puts sounds to the user to pick by ear (A, B, C...): as many as they've set in GrooveKit's settings (the system prompt says how many; two to twenty), or as many as they ask for. A window shows each with a play button, next to the reference when there is one, and this waits (up to ten minutes) for their pick. Their pick fits the style from then on; \"none of them\" counts against all. After a first \"none of them\", search once more for the same part with different sounds and offer again (again: true); after a second, go on without that part and tell the user at the end, since they can always ask for it. \"Another round\" (chosen: another round) means none of these and as many new ones again: offer that many more, different from every sound offered for this part so far, as often as asked; the result counts the sounds shown for the part, and once fifty have been without a pick it says to stop and ask the user which way to go instead (other words, another kind of sound, another part). They can also answer Other with a note in their own words (\"only hat loops, not whole drum loops\"): then none was right for this, which says nothing about the sounds' fit, so do what the note asks (search again, another kind of sound, another part) and offer again. Give each candidate the path, the audition file from match_sound or audition_in_song when there is one, and a short label. Candidates without an in-song audition are tried in the song here, so every one plays in time with it from the same bar: in channel's place when you give one (the channel the pick is for, playing its notes), else in a slot of their own: loops over the bars, one-shots and instruments playing notes (the candidate's own, the offer's, or a bar of the part's usual figure). Candidates can be sounds played by notes too: one-shot samples, or instruments (Triad with different presets, a sampler set up another way) with the line they'd play, so the user picks synth sounds and lines by ear in the song as well. Give startBar and endBar for the stretch where the part will play (default the first 8 bars). Returns the chosen index, -1 for none, or that they haven't chosen yet.",
      inputSchema: {
        title: z.string().describe("What's being chosen: \"Which kick for the groove?\""),
        style: z.string().optional().describe("The style the pick is for (the song's)"),
        reference: z.object({ file: z.string(), from: z.number().optional(), title: z.string().optional() }).optional()
          .describe("The reference to compare with: its file and where to play from (seconds), as match_sound gives it"),
        candidates: z.array(z.object({
          path: z.string().optional().describe("A sample"),
          instrument: z.record(z.string(), z.unknown()).optional().describe("Or a whole instrument (a plugin such as Triad with a preset's state, or a sampler), as in addChannel"),
          notes: z.array(z.object({ start: z.number().int().min(0), length: z.number().int().min(1), key: z.number().int().min(0).max(127).optional(), velocity: z.number().int().min(1).max(127).optional() })).optional()
            .describe("What it plays in its own slot (ticks from the bar, one pattern that repeats): its line, chords or rhythm"),
          part: z.enum(["kick", "rumble", "drums", "bass", "percussion", "clap", "hats", "synth", "vocal", "fx"]).optional(),
          audition: z.string().optional(), label: z.string().optional(),
        })).min(2).max(20),
        again: z.boolean().optional().describe("True when this offer follows a \"none of them\" for the same part"),
        channel: z.string().optional().describe("The song's channel the pick is for: candidates are tried in its place, playing its notes"),
        notes: z.array(z.object({ start: z.number().int().min(0), length: z.number().int().min(1), key: z.number().int().min(0).max(127).optional(), velocity: z.number().int().min(1).max(127).optional() })).optional()
          .describe("Without a channel: the notes every one-shot and instrument candidate plays (else a bar of its part's usual figure)"),
        part: z.enum(["kick", "rumble", "drums", "bass", "percussion", "clap", "hats", "synth", "vocal", "fx"]).optional().describe("What the candidates play, for their slot and figure"),
        startBar: z.number().min(1).optional(),
        endBar: z.number().min(1).optional(),
      },
    },
    ({ title, style, reference, candidates, again, channel, notes, part, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        const { id } = (await engine.call("picks.offer", {
          title, candidates, ...(style ? { style } : {}), ...(reference ? { reference } : {}), ...(channel ? { channel } : {}),
          ...(notes ? { notes } : {}), ...(part ? { part } : {}),
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        })) as { id: number };
        // How many sounds the user has been shown for this part without picking one.
        const key = part ?? title;
        const shown = (shownForPart.get(key) ?? 0) + candidates.length;
        shownForPart.set(key, shown);
        for (let waited = 0; waited < 600; waited++) {
          const r = (await engine.call("picks.get", { id })) as { status: string; choice?: number; note?: string; more?: boolean };
          if (r.status === "chosen") {
            if (r.note) return { choice: -1, chosen: "other", note: r.note, next: "None of these was what the user wants: do what their note says and offer again." };
            const chosen = r.choice !== undefined && r.choice >= 0 ? candidates[r.choice] : undefined;
            if (chosen) {
              shownForPart.delete(key);
              return { choice: r.choice, chosen };
            }
            if (r.more) {
              return {
                choice: -1, chosen: "another round", shown,
                next: shown >= 50
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
          await new Promise((resolve) => setTimeout(resolve, 1000));
        }
        return { choice: null, note: "The user hasn't chosen yet; the window stays open. Ask them, or check later with the same offer." };
      })
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
    ({ style, startBar, endBar }) =>
      attempt(async () => {
        await engine.connect();
        return engine.call("styles.compare", {
          name: style,
          ...(startBar !== undefined ? { start: barToTicks(startBar) } : {}),
          ...(endBar !== undefined ? { end: barToTicks(endBar) } : {}),
        });
      })
  );

  server.registerTool(
    "rate_sample",
    {
      description: "Records the user's verdict on a sample: it fits (true) or doesn't (false) a style (or any style without one); null forgets it. Later searches with that style rank it, and sounds like it, up or down.",
      inputSchema: {
        path: z.string(),
        fits: z.boolean().nullable(),
        style: z.string().optional(),
      },
    },
    ({ path, fits, style }) => attempt(() => engine.call("library.rate", { path, fits, ...(style ? { style } : {}) }))
  );

  server.registerTool(
    "notes",
    {
      description: "The user's notes for you: list (each with enabled, and here: whether it applies to the open song), add (text; scope all songs, the default, or song for this one only), update (id with text, enabled or scope) or delete (id).",
      inputSchema: {
        action: z.enum(["list", "add", "update", "delete"]),
        id: z.string().optional(),
        text: z.string().optional(),
        enabled: z.boolean().optional(),
        scope: z.enum(["all", "song"]).optional(),
      },
    },
    ({ action, id, text: note, enabled, scope }) =>
      attempt(() => engine.call(`notes.${action}`, { ...(id ? { id } : {}), ...(note !== undefined ? { text: note } : {}), ...(enabled !== undefined ? { enabled } : {}), ...(scope ? { scope } : {}) }))
  );

  server.registerTool(
    "style_guide",
    {
      description: "Style guides: how to make a style (the feeling, each part's sound with words to search by, the order to build in, the mix, the arrangement, what to avoid) and targets in numbers (tempo, swing, parts at once, each role's level against the kick, low end against highs, loudness). list shows them all and the song's style; get reads one in full (id or name; without either, the song's); save writes the user's version (id with text; without an id, a new guide named by its \"# \" heading), only when the user asks; remove brings an edited guide back to how it shipped, or deletes one of the user's own.",
      inputSchema: {
        action: z.enum(["list", "get", "save", "remove"]),
        id: z.string().optional().describe("A guide's id or name"),
        text: z.string().optional().describe("Markdown, for save, with a ```targets block of JSON"),
      },
    },
    ({ action, id, text }) =>
      attempt(() => engine.call(`guides.${action}`, { ...(id ? { id } : {}), ...(text !== undefined ? { text } : {}) }))
  );

  server.registerTool(
    "sample_library",
    {
      description: "Manages the sample library: status (folders, sample count, scan progress), addFolder (indexes a folder and scans it in the background), removeFolder, or scan (rescans changed files).",
      inputSchema: {
        action: z.enum(["status", "addFolder", "removeFolder", "scan"]),
        path: z.string().optional().describe("Folder, for addFolder and removeFolder"),
      },
    },
    ({ action, path }) => attempt(() => engine.call(`library.${action}`, path ? { path } : {}))
  );

  server.registerTool(
    "plugins",
    {
      description: "The user's VST3 and AU plugins: list (filtered by words in name, manufacturer or category, and by instrument or effect), scan (finds newly installed plugins, takes about a minute; progress via status), or status.",
      inputSchema: {
        action: z.enum(["list", "scan", "status"]).optional().describe("Default list"),
        query: z.string().optional(),
        instrument: z.boolean().optional().describe("true for instruments, false for effects"),
      },
      annotations: { readOnlyHint: false },
    },
    ({ action, query, instrument }) =>
      attempt(() =>
        engine.call(`plugins.${action ?? "list"}`, {
          ...(query ? { query } : {}),
          ...(instrument !== undefined ? { instrument } : {}),
        })
      )
  );

  server.registerTool(
    "describe_plugin",
    {
      description: "Loads a plugin and lists its parameters (id, name, normalized value, display text), plus a pluginRef to use in commands. Instruments like the Digitakt have thousands of parameters: filter with query (words in the name, e.g. \"T1 filter\").",
      inputSchema: {
        uid: z.string().describe("From plugins"),
        query: z.string().optional(),
        limit: z.number().int().min(1).max(1000).optional().describe("Default 200"),
      },
      annotations: { readOnlyHint: true },
    },
    ({ uid, query, limit }) => attempt(() => engine.call("plugins.describe", { uid, ...(query ? { query } : {}), ...(limit ? { limit } : {}) }))
  );

  server.registerTool(
    "list_effects",
    {
      description: "Built-in effects (eq, compressor, multiband, limiter, reverb, delay, phaser, chorus, distortion, exciter, bitcrusher, stereo, lowpass, highpass) with every parameter's id, unit, range, default, choices (for parameters that pick an option by index) and whether it can be automated; defaultMix is where a new one's mix usually starts; presets are starting points (copy their params, and mix, into the effect).",
      annotations: { readOnlyHint: true },
    },
    () => attempt(() => engine.call("listEffects"))
  );
}
