import { describe, expect, it } from "vitest";
import { itemsFor, normalise } from "../src/features/spotlight/commands";

const known = { instruments: [], projects: [{ path: "/songs/Night Drive", name: "Night Drive", modified: 0 }], samples: [] };
const first = (text: string) => itemsFor(text, known)[0];

describe("the command box", () => {
  it("runs a command named exactly, said or typed", () => {
    expect(first("play")).toMatchObject({ kind: "command", label: "Play or pause" });
    expect(first("Play.")).toMatchObject({ kind: "command", label: "Play or pause" });
    expect(first("open the mixer")).toMatchObject({ kind: "command", label: "Mixer" });
    expect(first("Tempo 128.")).toMatchObject({ kind: "command", label: "Tempo 128" });
    expect(first("set the tempo to 132.5")).toMatchObject({ kind: "command", label: "Tempo 132.5" });
    expect(first("open night drive")).toMatchObject({ kind: "project", label: "Open Night Drive" });
  });

  it("asks the agent when in doubt, with what partly matches under it", () => {
    expect(first("add a new kick")).toMatchObject({ kind: "agent", label: "add a new kick" });
    expect(first("play something darker")).toMatchObject({ kind: "agent" });
    const items = itemsFor("pia", known);
    expect(items[0].kind).toBe("agent");
    expect(items.some((i) => i.label === "Piano roll")).toBe(true);
  });

  it("puts what's said into plain words", () => {
    expect(normalise("  Tempo, 128.5 BPM! ")).toBe("tempo 128.5 bpm");
  });
});
