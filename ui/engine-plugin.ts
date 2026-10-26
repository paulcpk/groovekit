// Dev server endpoint that finds the GrooveKit engine for the UI: the one serving the project
// (found through engine.json, which every serving engine writes), or a new one started here and
// stopped with the dev server. The desktop shell will do the same natively.

import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import type { Plugin } from "vite";

const discoveryFile = join(homedir(), "Library", "Application Support", "GrooveKit", "engine.json");

interface EngineInfo {
  url: string;
  pid: number;
  project: string;
}

function readRunning(): EngineInfo | undefined {
  try {
    const info = JSON.parse(readFileSync(discoveryFile, "utf8")) as EngineInfo;
    process.kill(info.pid, 0);
    return info;
  } catch {
    return undefined;
  }
}

export function enginePlugin(): Plugin {
  const repo = resolve(import.meta.dirname, "..");
  const enginePath = process.env.GROOVEKIT_ENGINE ?? join(repo, "build", "dev", "engine", "groovekit-engine");
  const project = resolve(process.env.GROOVEKIT_PROJECT ?? join(homedir(), "Music", "GrooveKit", "Scratch"));
  let child: ChildProcess | undefined;
  let starting: Promise<EngineInfo> | undefined;

  async function ensureEngine(): Promise<EngineInfo> {
    const running = readRunning();
    if (running && resolve(running.project) === project) return running;
    if (starting) return starting;

    if (!existsSync(enginePath)) throw new Error(`No engine at ${enginePath}; build it with cmake --build build/dev`);

    starting = new Promise<EngineInfo>((ok, fail) => {
      child = spawn(enginePath, ["serve", project, "--port", "7878", "--parent-pid", String(process.pid)], { stdio: "inherit" });
      child.on("exit", () => (child = undefined));

      const started = Date.now();
      const poll = setInterval(() => {
        const info = readRunning();
        if (info && info.pid === child?.pid) {
          clearInterval(poll);
          ok(info);
        } else if (!child || Date.now() - started > 60000) {
          clearInterval(poll);
          fail(new Error("The engine didn't start"));
        }
      }, 200);
    }).finally(() => (starting = undefined));

    return starting;
  }

  return {
    name: "groovekit-engine",
    configureServer(server) {
      server.middlewares.use("/__engine", async (_req, res) => {
        res.setHeader("Content-Type", "application/json");
        try {
          const info = await ensureEngine();
          res.end(JSON.stringify({ url: info.url, project: info.project }));
        } catch (e) {
          res.statusCode = 503;
          res.end(JSON.stringify({ error: String((e as Error).message) }));
        }
      });
      server.httpServer?.on("close", () => child?.kill("SIGTERM"));
    },
  };
}
