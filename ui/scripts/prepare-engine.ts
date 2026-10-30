// Copies the engine build into src-tauri/binaries under the name Tauri bundles sidecars by:
// groovekit-engine-<target triple>, and GrooveKit's own plugins (Triad, Quartet, Sextet, Chamber, Echo) into
// src-tauri/plugins, which the app carries in Resources/Plug-Ins. Uses the release build if
// there is one, else the debug build.

import { execSync } from "node:child_process";
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
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

// GrooveKit's own plugins, collected by the build into one folder.
const built = join(repo, "build", preset!, "plugins", "all");
const bundles = existsSync(built) ? readdirSync(built).filter((f) => f.endsWith(".vst3")) : [];
if (bundles.length === 0) {
  console.error(`No plugin builds in ${built}; build the engine target, which builds them too.`);
  process.exit(1);
}

const plugins = join(repo, "ui", "src-tauri", "plugins");
rmSync(plugins, { recursive: true, force: true });
mkdirSync(plugins, { recursive: true });
for (const bundle of bundles) cpSync(join(built, bundle), join(plugins, bundle), { recursive: true, verbatimSymlinks: true });
console.log(`Plugins for the app: ${bundles.join(", ")} from ${built}`);
