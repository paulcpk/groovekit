import { describe, expect, it } from "vitest";
import { applyPatch } from "../src/engine/patch.ts";

describe("applyPatch", () => {
  const doc = { tempo: 120, channels: [{ id: "a" }, { id: "b" }], mixer: { inserts: [{ id: "master", volumeDb: 0 }] } };

  it("copies only what changes", () => {
    const next = applyPatch(doc, [{ op: "replace", path: "/mixer/inserts/0/volumeDb", value: -6 }]);
    expect(next.mixer.inserts[0].volumeDb).toBe(-6);
    expect(doc.mixer.inserts[0].volumeDb).toBe(0);
    expect(next.channels).toBe(doc.channels);
    expect(next.mixer).not.toBe(doc.mixer);
  });

  it("adds, appends and removes array items", () => {
    let next = applyPatch(doc, [{ op: "add", path: "/channels/1", value: { id: "x" } }]);
    expect(next.channels.map((c) => c.id)).toEqual(["a", "x", "b"]);
    next = applyPatch(next, [{ op: "add", path: "/channels/-", value: { id: "z" } }]);
    expect(next.channels.map((c) => c.id)).toEqual(["a", "x", "b", "z"]);
    next = applyPatch(next, [{ op: "remove", path: "/channels/0" }]);
    expect(next.channels.map((c) => c.id)).toEqual(["x", "b", "z"]);
  });

  it("adds and removes object fields, with escaped keys", () => {
    let next = applyPatch(doc as any, [{ op: "add", path: "/name", value: "Song" }, { op: "add", path: "/a~1b", value: 1 }]);
    expect(next.name).toBe("Song");
    expect(next["a/b"]).toBe(1);
    next = applyPatch(next, [{ op: "remove", path: "/name" }]);
    expect("name" in next).toBe(false);
  });
});
