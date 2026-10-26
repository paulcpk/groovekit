import { describe, expect, it } from "vitest";
import type { Project } from "@groovekit/schema/project";
import { formatPosition, patternLength, snapTicks, ticksPerBar, uniqueId } from "../src/lib/model.ts";
import { dbToFader, faderToDb } from "../src/components/fader.tsx";

const doc = { schemaVersion: 3, tempo: 120, channels: [], patterns: [], playlist: { tracks: [], clips: [] }, mixer: { inserts: [] } } as Project;

describe("model helpers", () => {
  it("formats positions as bar.beat.16th", () => {
    expect(formatPosition(0, doc)).toBe("1.1.1");
    expect(formatPosition(3840 + 960 + 240, doc)).toBe("2.2.2");
    expect(formatPosition(2880, { ...doc, timeSignature: { numerator: 3, denominator: 4 } })).toBe("2.1.1");
  });

  it("finds pattern lengths as the engine does", () => {
    expect(patternLength({ id: "p", notes: [] }, doc)).toBe(3840);
    expect(patternLength({ id: "p", notes: [{ channel: "a", start: 3840, length: 240 }] }, doc)).toBe(7680);
    expect(patternLength({ id: "p", length: 1920, notes: [] }, doc)).toBe(1920);
  });

  it("makes unique ids", () => {
    expect(uniqueId("Kick 01.wav", [])).toBe("kick-01-wav");
    expect(uniqueId("kick", [{ id: "kick" }, { id: "kick-2" }])).toBe("kick-3");
  });

  it("resolves snap to bars", () => {
    expect(snapTicks(0, doc)).toBe(ticksPerBar(doc));
    expect(snapTicks(240, doc)).toBe(240);
  });

  it("maps fader positions and dB both ways", () => {
    for (const db of [-60, -20, -6, 0, 6, 24]) expect(faderToDb(dbToFader(db))).toBeCloseTo(db, 0);
    expect(dbToFader(-200)).toBe(0);
    expect(faderToDb(0)).toBe(-100);
  });
});
