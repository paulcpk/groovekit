import { readdirSync, readFileSync, existsSync } from "node:fs";
import { join } from "node:path";
import { Ajv2020 } from "ajv/dist/2020.js";
import { describe, expect, it } from "vitest";
import type { Project } from "../generated/project.ts";

const schema = JSON.parse(readFileSync(join(import.meta.dirname, "..", "project.schema.json"), "utf8"));
const validate = new Ajv2020({ discriminator: true, allErrors: true }).compile<Project>(schema);
const songsDir = join(import.meta.dirname, "..", "..", "tests", "songs");

describe("test songs match the schema", () => {
  const songs = readdirSync(songsDir).filter((d) => existsSync(join(songsDir, d, "song.json")));

  it("finds songs", () => expect(songs.length).toBeGreaterThan(0));

  for (const song of songs) {
    it(song, () => {
      const data = JSON.parse(readFileSync(join(songsDir, song, "song.json"), "utf8"));
      const ok = validate(data);
      expect(validate.errors ?? []).toEqual([]);
      expect(ok).toBe(true);
    });
  }
});

describe("the schema rejects bad input", () => {
  const minimal = (): Record<string, any> => ({
    schemaVersion: 1,
    tempo: 120,
    channels: [],
    patterns: [],
    playlist: { tracks: [], clips: [] },
    mixer: { inserts: [{ id: "master" }] },
  });

  it("accepts a minimal project", () => expect(validate(minimal())).toBe(true));

  it("rejects unknown properties", () => {
    const p = minimal();
    p.mixer.inserts[0].volme = 1;
    expect(validate(p)).toBe(false);
  });

  it("rejects unknown clip types", () => {
    const p = minimal();
    p.playlist.clips.push({ type: "video", id: "v", track: "t", start: 0, length: 1 });
    expect(validate(p)).toBe(false);
  });
});
