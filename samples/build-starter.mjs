#!/usr/bin/env node
// Builds the starter sample pack: public-domain drum machines and one-shots, pulled from their
// repositories at pinned commits, renamed into one set (see starter-map.mjs), converted to FLAC
// and zipped for the app to download.
//
//   node samples/build-starter.mjs [--out build/starter] [--offline]
//
// Needs git, ffmpeg, ffprobe and zip. The result is <out>/GrooveKit-Starter.zip, holding one
// folder, "GrooveKit Starter", with LICENSES.txt, READ ME.txt and starter.json inside. Publish
// the zip as a release asset (samples/README.md) and put its size in the engine's StarterPack.cpp.

import { execFileSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { counters, machines, sources } from "./starter-map.mjs";

/** Goes up when the pack's contents change in a way the app should fetch again. */
export const PACK_VERSION = 1;
const PACK_NAME = "GrooveKit Starter";

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, "..");
const args = process.argv.slice(2);
const outDir = path.resolve(repoRoot, args.includes("--out") ? args[args.indexOf("--out") + 1] : "build/starter");
const offline = args.includes("--offline");
const srcDir = path.join(outDir, "src");
const packDir = path.join(outDir, PACK_NAME);

const run = (cmd, cmdArgs, opts = {}) => execFileSync(cmd, cmdArgs, { stdio: ["ignore", "pipe", "inherit"], encoding: "utf8", ...opts }).trim();

for (const tool of ["git", "ffmpeg", "ffprobe", "zip"]) {
  try { run("which", [tool]); } catch { console.error(`${tool} is needed (brew install ${tool})`); process.exit(1); }
}

// 1. The sources, at their commits.
fs.mkdirSync(srcDir, { recursive: true });
for (const s of sources) {
  const dir = path.join(srcDir, s.id);
  const at = fs.existsSync(path.join(dir, ".git")) ? run("git", ["-C", dir, "rev-parse", "HEAD"]) : "";
  if (at === s.commit) continue;
  if (offline) throw new Error(`${s.id} isn't at ${s.commit} in ${dir} and --offline was given`);
  console.log(`Fetching ${s.title}…`);
  fs.rmSync(dir, { recursive: true, force: true });
  if (s.sparse) {
    run("git", ["clone", "-q", "--filter=blob:none", "--sparse", s.repo, dir]);
    run("git", ["-C", dir, "sparse-checkout", "set", s.sparse]);
  } else {
    run("git", ["clone", "-q", "--filter=blob:none", s.repo, dir]);
  }
  run("git", ["-C", dir, "checkout", "-q", s.commit]);
}

// 2. Where every file goes.
const walk = (dir) => fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
  e.isDirectory() ? walk(path.join(dir, e.name)) : [path.join(dir, e.name)]);
const duration = (file) => Number(run("ffprobe", ["-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", file]));

const plan = [];          // { from, to, source }
const taken = new Map();  // target -> from, to catch two files wanting one name
const unmapped = [];
for (const s of sources) {
  const root = path.join(srcDir, s.id, s.root);
  const count = counters();
  for (const file of walk(root).sort()) {
    const rel = path.relative(root, file);
    if (!s.pattern.test(rel)) continue;
    const place = s.map(rel, duration(file), count);
    if (!place) { unmapped.push(`${s.id}/${rel}`); continue; }
    if (!/^[A-Za-z0-9 .'#-]+$/.test(place.name)) throw new Error(`Odd name "${place.name}" for ${s.id}/${rel}`);
    // Two source files with one name (the TR-8 set has a pair) are told apart by a number.
    let to = path.join(packDir, place.folder, `${place.name}.flac`);
    for (let n = 2; taken.has(to); ++n) to = path.join(packDir, place.folder, `${place.name} ${n}.flac`);
    taken.set(to, `${s.id}/${rel}`);
    plan.push({ from: file, to, source: s });
  }
}

// The Sonic Pi acoustic kit, guitars and a burp are left out on purpose; anything else unmapped
// is a mistake in the map.
const expectedOut = /^sonicpi\/(drum_(?!cowbell)|guit_|misc_burp)/;
const surprises = unmapped.filter((f) => !expectedOut.test(f));
if (surprises.length) { console.error("Not mapped:\n  " + surprises.join("\n  ")); process.exit(1); }

// 3. Convert (or copy FLAC as it is), a few at a time, skipping files already done.
fs.rmSync(packDir, { recursive: true, force: true });
fs.mkdirSync(packDir, { recursive: true });
const convert = ({ from, to }) => new Promise((resolve, reject) => {
  fs.mkdirSync(path.dirname(to), { recursive: true });
  if (from.toLowerCase().endsWith(".flac")) { fs.copyFileSync(from, to); return resolve(); }
  const p = spawn("ffmpeg", ["-v", "error", "-y", "-i", from, "-map_metadata", "-1", "-compression_level", "8", to], { stdio: ["ignore", "ignore", "inherit"] });
  p.on("exit", (code) => (code === 0 ? resolve() : reject(new Error(`ffmpeg failed on ${from}`))));
});
let next = 0;
await Promise.all(Array.from({ length: Math.min(8, os.cpus().length) }, async () => {
  while (next < plan.length) await convert(plan[next++]);
}));

// 4. What's in the pack, and whose it is.
const byFolder = new Map();
for (const { to } of plan) {
  const top = path.relative(packDir, to).split(path.sep)[0];
  byFolder.set(top, (byFolder.get(top) ?? 0) + 1);
}
const folderOrder = ["Kicks", "Snares", "Claps", "Hats", "Cymbals", "Toms", "Percussion", "Bass", "Synth", "FX", "Ambient", "Loops"];
const folderNotes = {
  Kicks: "bass drums", Snares: "snares, with and without the snappy wires", Claps: "hand claps", Hats: "hi-hats, closed (short) and open (ringing)",
  Cymbals: "crashes, rides and splashes", Toms: "toms, low to high", Percussion: "everything else you hit: rimshots, cowbells, congas, shakers, tabla",
  Bass: "bass hits and plucks", Synth: "bells, blips, pads and keys", FX: "risers, glitches, reverses, noises", Ambient: "drones, hums and textures to lay under a track",
  Loops: "beats and textures that repeat; the tempo is in the name, so GrooveKit fits them to your song",
};

const readMe = `${PACK_NAME}
${"=".repeat(PACK_NAME.length)}

A starter set of ${plan.length} drum sounds, one-shots and loops for GrooveKit, all in the public
domain. Folders say what a sound is, subfolders say which machine it came from, and every file
name says both, so searching "808" finds every 808 sound and the Kicks folder holds every kick.

Folders
${folderOrder.filter((f) => byFolder.has(f)).map((f) => `  ${f.padEnd(12)} ${folderNotes[f]} (${byFolder.get(f)})`).join("\n")}

The machines
${Object.entries(machines).map(([name, text]) => `  ${name.padEnd(8)} ${text}`).join("\n")}

Names
  Machine, then the sound, then what sets it apart: "808 Kick Tone 25 Decay 50" is the 808's kick
  with its tone knob at 25 % and its decay at 50 %; "TR-8 909 Hat Open Short" is the TR-8's 909
  model, open hat, short setting; "Analog Snare 03" is the third analogue snare. Loops end with
  their tempo.

Licence
  Everything here is public domain (CC0 or the Unlicense): use it in anything, no credit needed.
  LICENSES.txt says where each sound came from and who made it. Model numbers name the machines
  the sounds were recorded from; the machines' makers aren't involved in this pack.
`;
fs.writeFileSync(path.join(packDir, "READ ME.txt"), readMe);

let licenses = `${PACK_NAME}: sources and licences
${"=".repeat(PACK_NAME.length + 30)}

Every sound in this pack is in the public domain. The sources, with the commit each was taken
from, and their own licence texts:

`;
for (const s of sources) {
  const dir = path.join(srcDir, s.id);
  const files = plan.filter((p) => p.source === s).length;
  licenses += `${s.title}\n${"-".repeat(s.title.length)}\n${s.repo} at ${s.commit}\nLicence: ${s.licence}\nBy: ${s.credit}\nFiles taken: ${files}\n\n`;
  const licenceFile = fs.readdirSync(dir).find((f) => /^(LICENSE|LICENCE|UNLICENSE)(\..*)?$/i.test(f));
  if (licenceFile) licenses += fs.readFileSync(path.join(dir, licenceFile), "utf8").trim() + "\n\n";
  if (s.id === "sonicpi") licenses += "The Sonic Pi samples README, with the original Freesound links:\n\n" + fs.readFileSync(path.join(dir, s.root, "README.md"), "utf8").trim() + "\n\n";
}
fs.writeFileSync(path.join(packDir, "LICENSES.txt"), licenses);

fs.writeFileSync(path.join(packDir, "starter.json"), JSON.stringify({
  name: PACK_NAME, version: PACK_VERSION, files: plan.length, built: new Date().toISOString().slice(0, 10),
  sources: sources.map((s) => ({ title: s.title, repo: s.repo, commit: s.commit, licence: s.licence })),
}, null, 2) + "\n");

// 5. One zip, stored rather than compressed: FLAC has done the shrinking.
const zip = path.join(outDir, "GrooveKit-Starter.zip");
fs.rmSync(zip, { force: true });
run("zip", ["-q", "-0", "-r", "-X", zip, PACK_NAME], { cwd: outDir });
const bytes = fs.statSync(zip).size;
const sha = createHash("sha256").update(fs.readFileSync(zip)).digest("hex");

console.log(`\n${plan.length} files in ${packDir}`);
for (const f of folderOrder) if (byFolder.has(f)) console.log(`  ${f.padEnd(12)} ${byFolder.get(f)}`);
console.log(`\n${zip}\n  ${bytes} bytes (${(bytes / 1e6).toFixed(1)} MB), sha256 ${sha}`);
console.log(`\nStarterPack.cpp wants: packVersion ${PACK_VERSION}, zipBytes ${bytes}`);
