// Copies the engine build into src-tauri/binaries under the name Tauri bundles sidecars by:
// groovekit-engine-<target triple>. Uses the release build if there is one, else the debug build.

import { execSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { join, resolve } from "node:path";

const repo = resolve(import.meta.dirname, "..", "..");
const triple = execSync("rustc -vV").toString().match(/host: (\S+)/)![1];
const candidates = ["release", "dev"].map((preset) => join(repo, "build", preset, "engine", "groovekit-engine"));
const source = candidates.find(existsSync);

if (!source) {
  console.error("No engine build; run cmake --build --preset release (or dev) first.");
  process.exit(1);
}

const target = join(repo, "ui", "src-tauri", "binaries", `groovekit-engine-${triple}`);
mkdirSync(join(target, ".."), { recursive: true });
copyFileSync(source, target);
console.log(`Engine for the app: ${source}`);
