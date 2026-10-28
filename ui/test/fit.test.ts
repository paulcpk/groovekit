import { describe, expect, it } from "vitest";
import type { Project } from "@groovekit/schema/project";
import { barRange, barsAt, describeFit, fitCommands, fitSpeed, formatBars, suggestedBars, tempoFor } from "../src/lib/fit.ts";

const song = (tempo: number) => ({ schemaVersion: 5, tempo, channels: [], patterns: [], playlist: { rows: [], clips: [] }, mixer: { inserts: [] } }) as Project;

describe("fitting samples to the song", () => {
  it("fills the chosen bars at any tempo", () => {
    // A 4-bar loop at 90 BPM is 10.667 s long.
    const seconds = (4 * 4 * 60) / 90;
    const doc = song(150);
    expect(barsAt(doc, seconds, 90)).toBeCloseTo(4);
    expect(fitSpeed(doc, 90)).toBeCloseTo(150 / 90);
    expect(tempoFor(doc, seconds, 8)).toBeCloseTo(180);
    expect(fitSpeed(doc, tempoFor(doc, seconds, 8))).toBeCloseTo(150 / 180);
    expect(describeFit(doc, 90, false)).toBe("1.67× faster, pitch kept");
    expect(describeFit(doc, 90, true)).toBe("1.67× faster, +8.8 st");
    expect(describeFit(doc, 180, true)).toBe("1.20× slower, −3.2 st");
  });

  it("suggests the nearest power of two and keeps within eight times either way", () => {
    // 2 s at 140 BPM is 1.17 bars.
    expect(suggestedBars(song(140), 2)).toBe(1);
    expect(suggestedBars(song(120), 7)).toBe(4);
    const range = barRange(song(120), 2);
    expect(range.min).toBeCloseTo(0.125);
    expect(range.max).toBeCloseTo(8);
    expect(fitSpeed(song(120), 5)).toBe(8);
    expect(formatBars(0.5)).toBe("1/2 bar");
    expect(formatBars(4)).toBe("4 bars");
  });

  it("resizes the clips that played the whole part", () => {
    const doc = song(120);
    doc.channels.push({ id: "loop", group: "arrangement", instrument: { type: "sampler", layers: [{ id: "sample", sample: "/loop.wav", sourceTempo: 120 }] } });
    doc.playlist.clips.push(
      { type: "audio", id: "whole", row: 0, start: 0, length: 7680, channel: "loop" },
      { type: "audio", id: "cut", row: 0, start: 7680, length: 1920, channel: "loop" },
    );
    // 4 s at 120 BPM is 2 bars; made 4.
    const commands = fitCommands(doc, doc.channels[0], 4, { sourceTempo: tempoFor(doc, 4, 4), resample: true });
    expect(commands).toEqual([
      { type: "updateSamplerLayer", channel: "loop", layer: "sample", changes: { sourceTempo: 240, resample: true } },
      { type: "updateClip", clip: "whole", changes: { length: 15360 } },
    ]);
  });
});
