import type { AutomationClip, Project } from "@groovekit/schema/project";
import { describe, expect, it } from "vitest";
import { automationValueAt } from "../src/lib/automation";

const target = { scope: "insert", insert: "master", param: "volume" } as const;

function song(clips: Partial<AutomationClip>[], rows: Project["playlist"]["rows"] = []): Project {
  return {
    schemaVersion: 5, tempo: 120, channels: [], patterns: [],
    playlist: { rows, clips: clips.map((c, i) => ({ type: "automation", id: `a${i}`, row: 0, start: 0, length: 3840, target, points: [], ...c }) as AutomationClip) },
    mixer: { inserts: [{ id: "master" }] },
  } as unknown as Project;
}

describe("automation at the playhead", () => {
  it("follows a clip's points, holding before and after", () => {
    const doc = song([{ start: 3840, points: [{ time: 0, value: -20 }, { time: 1920, value: 0 }] }]);
    expect(automationValueAt(doc, target, 0)).toBe(-20);
    expect(automationValueAt(doc, target, 3840 + 960)).toBeCloseTo(-10);
    expect(automationValueAt(doc, target, 9000)).toBe(0);
    expect(automationValueAt(doc, { ...target, param: "pan" }, 0)).toBeUndefined();
  });

  it("bends with a curve, the way the engine plays it", () => {
    const doc = song([{ points: [{ time: 0, value: 0, curve: 0.5 }, { time: 1920, value: 1 }] }]);
    const middle = automationValueAt(doc, target, 960)!;
    expect(middle).not.toBeCloseTo(0.5);
    expect(middle).toBeGreaterThan(0);
    expect(middle).toBeLessThan(1);
  });

  it("holds a clip's last value until the next clip", () => {
    const doc = song([
      { start: 0, length: 1920, points: [{ time: 0, value: -30 }, { time: 960, value: -10 }] },
      { start: 7680, points: [{ time: 0, value: 0 }] },
    ]);
    expect(automationValueAt(doc, target, 5000)).toBe(-10);
  });

  it("leaves out muted clips and rows, and rows a solo leaves out", () => {
    const points = [{ time: 0, value: -12 }];
    expect(automationValueAt(song([{ points, mute: true }]), target, 0)).toBeUndefined();
    expect(automationValueAt(song([{ points }], [{ mute: true }]), target, 0)).toBeUndefined();
    expect(automationValueAt(song([{ points }], [{}, { solo: true }]), target, 0)).toBeUndefined();
    expect(automationValueAt(song([{ points }], [{ solo: true }]), target, 0)).toBe(-12);
  });
});
