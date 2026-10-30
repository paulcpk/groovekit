// Hearing the song as numbers, playing it, mastering it and recording hardware into it.

import { z } from "zod";
import { attempt, defined, longRunning, type Extra, type Tools } from "./shared.ts";

export function registerAudioTools({ server, engine, groups, barRange }: Tools) {
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
    ({ startBar, endBar, ...rest }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "rendering", async (signal) => engine.call("render", { ...(await barRange(startBar, endBar)), ...defined(rest) }, { signal }))
      )
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
    ({ targetLufs, ceilingDb, releaseMs, startBar, endBar }, extra: Extra) =>
      attempt(async () =>
        longRunning(extra, "mastering", async (signal) =>
          engine.call("master", { ...(await barRange(startBar, endBar)), ...defined({ targetLufs, ceilingDb, releaseMs }), group: groups.next() }, { signal })
        )
      )
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
    ({ path, startSeconds, endSeconds }) => attempt(() => engine.call("analyze", defined({ path, start: startSeconds, end: endSeconds })))
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
        if (action === "play" && pattern) {
          await engine.call("transport.setMode", { mode: "pattern", pattern });
          return engine.call("transport.play");
        }
        if (action === "play" && (fromBar !== undefined || loopBars)) await engine.call("transport.setMode", { mode: "song" });
        if (action === "play") {
          const loop = loopBars ? await barRange(loopBars[0], loopBars[1]) : {};
          await engine.call("transport.setLoop", loop);
          if (fromBar !== undefined || loopBars) await engine.call("transport.setPosition", { position: (await barRange(fromBar ?? loopBars![0])).start });
        }
        return engine.call(`transport.${action}`);
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
        type Inputs = { inputs: unknown[] };
        switch (args.action) {
          case "devices": {
            const [audio, inputs, midi] = await Promise.all([engine.call("audio.devices"), engine.call<Inputs>("record.inputs"), engine.call("midi.devices")]);
            return { audio, inputs: inputs.inputs, midi };
          }
          case "setup": {
            if (args.inputDevice) await engine.call("audio.setDevice", { input: args.inputDevice });
            if (args.clockOutput) await engine.call("midi.setClock", { output: args.clockOutput, enabled: true });
            const [inputs, alignment] = await Promise.all([engine.call<Inputs>("record.inputs"), engine.call("record.alignment")]);
            return { inputs: inputs.inputs, alignment };
          }
          case "start": {
            await engine.connect();
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
}
