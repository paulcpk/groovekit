// Taste: styles and their reference tracks' maps, melodies from them, style guides, the user's notes.

import { z } from "zod";
import { attempt, defined, longRunning, type Extra, type Tools } from "./shared.ts";

export function registerStyleTools({ server, engine }: Tools) {
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
        if (action === "add") return engine.call("styles.add", { path, ...defined({ name }) });
        if (action === "list") {
          const [styles, listening] = await Promise.all([engine.call<object>("styles.list", {}), engine.call("listening.status", {})]);
          return { ...styles, listening };
        }
        if (action === "references") return engine.call("styles.references", {});
        return engine.call(`styles.${action}`, { name });
      })
  );

  server.registerTool(
    "song_maps",
    {
      description: "The user's reference tracks, each heard once into a song map: its arrangement bar by bar (the beat grid, sections with their role and bars, which parts play in each: kick, rumble, bass, hats, percussion, music, vocal; energy; changes like a part out for a bar or a filter opening), the figures its music and bass play (two bars of notes each, where the part is loudest, with their key), and what it sounds like. list: the songs by style, with status (heard, older: heard before arrangements or figures were heard, new, queued...), inUse (the user's tick: the style learns from ticked songs) and tempo; style narrows it. read: one song's map as a score (id from list) with its figures as notes in ticks (figures narrows them to music or bass, or leaves them out). hear: queues songs (ids) to be heard (again), a few minutes each in the background. setInUse: ticks or unticks songs (ids, inUse), only when the user asks.",
      inputSchema: {
        action: z.enum(["list", "read", "hear", "setInUse"]),
        style: z.string().optional().describe("list: only this style"),
        id: z.string().optional().describe("read: the song, as list gives it (style/title)"),
        figures: z.enum(["all", "music", "bass", "none"]).optional().describe("read: which figures to include (default all)"),
        ids: z.array(z.string()).optional().describe("hear, setInUse: the songs"),
        inUse: z.boolean().optional().describe("setInUse"),
      },
    },
    ({ action, style, id, figures: which, ids, inUse }) =>
      attempt(async () => {
        if (action === "list") {
          const r = await engine.call<{ songs: { id: string; style: string; title: string; status: string; inUse: boolean; map?: { tempo: number } }[] }>("maps.list", {});
          const songs = r.songs.filter((s) => !style || s.style.toLowerCase() === style.toLowerCase());
          return songs.map((s) => ({ id: s.id, status: s.status, inUse: s.inUse, ...(s.map ? { tempo: s.map.tempo } : {}) }));
        }
        if (action === "read") {
          type Figure = { part: string };
          const map = await engine.call<{ style: string; title: string; inUse: boolean; score?: string; track: Record<string, unknown> & { figures?: Figure[] } }>("maps.get", { id });
          const { sounds: _, arrangement: __, balance: ___, path: ____, figures, ...measured } = map.track;
          const wanted = which ?? "all";
          return {
            id, style: map.style, title: map.title, inUse: map.inUse, measured,
            score: map.score ?? "Heard before arrangements were heard: hear it again for its score.",
            ...(wanted === "none" ? {} : {
              figures: figures
                ? wanted === "all" ? figures : figures.filter((f) => f.part === wanted)
                : "Heard before figures were heard: hear it again for what its music and bass play.",
            }),
          };
        }
        if (action === "hear") return engine.call("maps.hear", { ids });
        return engine.call("maps.setInUse", { ids, inUse });
      })
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
    ({ source, ...args }, extra: Extra) =>
      attempt(() => longRunning(extra, "transcribing", (signal) => engine.call(source === "voice" ? "melody.voice" : "melody.reference", defined(args), { signal })))
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
    ({ action, id, text }) => attempt(() => engine.call(`guides.${action}`, defined({ id, text })))
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
    ({ action, id, text: note, enabled, scope }) => attempt(() => engine.call(`notes.${action}`, defined({ id, text: note, enabled, scope })))
  );
}
