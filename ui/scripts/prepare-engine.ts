// Copies the engine build into src-tauri/binaries under the name Tauri bundles sidecars by:
// groovekit-engine-<target triple>, and GrooveKit's own plugins (the Triad synth) into
// src-tauri/plugins, which the app carries in Resources/Plug-Ins. Uses the release build if
// there is one, else the debug build.

import { execSync } from "node:child_process";
import { copyFileSync, cpSync, existsSync, mkdirSync, rmSync } from "node:fs";
import { join, resolve } from "node:path";

const repo = resolve(import.meta.dirname, "..", "..");
const triple = execSync("rustc -vV").toString().match(/host: (\S+)/)![1];
const preset = ["release", "dev"].find((p) => existsSync(join(repo, "build", p, "engine", "groovekit-engine")));
const source = preset && join(repo, "build", preset, "engine", "groovekit-engine");

if (!source) {
  console.error("No engine build; run cmake --build --preset release (or dev) first.");
  process.exit(1);
}

const target = join(repo, "ui", "src-tauri", "binaries", `groovekit-engine-${triple}`);
mkdirSync(join(target, ".."), { recursive: true });
copyFileSync(source, target);
console.log(`Engine for the app: ${source}`);

const plugin = join(repo, "build", preset!, "plugins", "triad", "Triad_artefacts", preset === "release" ? "Release" : "Debug", "VST3", "Triad.vst3");
if (!existsSync(plugin)) {
  console.error(`No Triad build at ${plugin}; build the engine target, which builds it too.`);
  process.exit(1);
}

const plugins = join(repo, "ui", "src-tauri", "plugins");
rmSync(plugins, { recursive: true, force: true });
mkdirSync(plugins, { recursive: true });
cpSync(plugin, join(plugins, "Triad.vst3"), { recursive: true, verbatimSymlinks: true });
console.log(`Plugins for the app: ${plugin}`);
