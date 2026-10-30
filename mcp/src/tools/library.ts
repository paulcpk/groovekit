// The user's sounds: the sample library, their verdicts, plugins and the built-in effects.

import { z } from "zod";
import { attempt, defined, type Tools } from "./shared.ts";

export function registerLibraryTools({ server, engine }: Tools) {
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
    (args) => attempt(() => engine.call("library.search", defined(args)))
  );

  server.registerTool(
    "describe_sample",
    {
      description: "Full measurements of one audio file: duration, loudness, attack and decay, spectral centroid and band energy (sub, low, mid, high), detected pitch, onset count and tempo, plus its classification and, once GrooveKit can listen, words for what it sounds like and (with style) its fit. Works for files outside the library too.",
      inputSchema: { path: z.string(), style: z.string().optional().describe("A style name: also give its fit") },
      annotations: { readOnlyHint: true },
    },
    ({ path, style }) => attempt(() => engine.call("library.describe", defined({ path, style })))
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
    ({ path, fits, style }) => attempt(() => engine.call("library.rate", { path, fits, ...defined({ style }) }))
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
    ({ action, path }) => attempt(() => engine.call(`library.${action}`, defined({ path })))
  );

  server.registerTool(
    "plugins",
    {
      description: "The user's VST3 and AU plugins and GrooveKit's own: list (filtered by words in name, manufacturer or category, and by instrument or effect), scan (finds newly installed plugins, takes about a minute; progress via status), or status.",
      inputSchema: {
        action: z.enum(["list", "scan", "status"]).optional().describe("Default list"),
        query: z.string().optional(),
        instrument: z.boolean().optional().describe("true for instruments, false for effects"),
      },
    },
    ({ action, query, instrument }) => attempt(() => engine.call(`plugins.${action ?? "list"}`, defined({ query, instrument })))
  );

  server.registerTool(
    "describe_plugin",
    {
      description: "Loads a plugin and lists its presets and parameters (id, name, normalized value, display text), plus a pluginRef to use in commands. Instruments like the Digitakt have thousands of parameters: filter with query (words in the name, e.g. \"T1 filter\").",
      inputSchema: {
        uid: z.string().describe("From plugins"),
        query: z.string().optional(),
        limit: z.number().int().min(1).max(1000).optional().describe("Default 200"),
      },
      annotations: { readOnlyHint: true },
    },
    ({ uid, query, limit }) => attempt(() => engine.call("plugins.describe", defined({ uid, query, limit })))
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
