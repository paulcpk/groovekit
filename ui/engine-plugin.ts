// Dev server endpoint that finds the GrooveKit engine for the UI: the one serving the project
// (found through the registration every serving engine writes), or a new one started here and
// stopped with the dev server. The desktop shell does the same natively.

import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, realpathSync } from "node:fs";
import { createServer } from "node:net";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import type { Plugin } from "vite";

const registry = join(homedir(), "Library", "Application Support", "GrooveKit", "engines");

interface EngineInfo {
  url: string;
  pid: number;
  project: string;
}

/** The live engine serving a project, if there is one. */
function servingEngine(project: string): EngineInfo | undefined {
  let files: string[] = [];
  try {
    files = readdirSync(registry);
  } catch {
    return undefined;
  }

  for (const file of files) {
    try {
      const info = JSON.parse(readFileSync(join(registry, file), "utf8")) as EngineInfo;
      process.kill(info.pid, 0);
      if (realpathSync(info.project) === realpathSync(project)) return info;
    } catch {
      // Gone, unreadable or another project.
    }
  }
  return undefined;
}

function freePort(): Promise<number> {
  return new Promise((ok, fail) => {
    const server = createServer();
    server.on("error", fail);
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address() as { port: number };
      server.close(() => ok(port));
    });
  });
}

export function enginePlugin(): Plugin {
  const repo = resolve(import.meta.dirname, "..");
  const enginePath = process.env.GROOVEKIT_ENGINE ?? join(repo, "build", "dev", "engine", "groovekit-engine");
  const project = resolve(process.env.GROOVEKIT_PROJECT ?? join(homedir(), "Music", "GrooveKit", "Scratch"));
  let child: ChildProcess | undefined;
  let starting: Promise<EngineInfo> | undefined;

  async function ensureEngine(): Promise<EngineInfo> {
    const running = servingEngine(project);
    if (running) return running;
    if (starting) return starting;

    if (!existsSync(enginePath)) throw new Error(`No engine at ${enginePath}; build it with cmake --build build/dev`);
    mkdirSync(project, { recursive: true });
    const port = await freePort();

    starting = new Promise<EngineInfo>((ok, fail) => {
      child = spawn(enginePath, ["serve", project, "--port", String(port), "--parent-pid", String(process.pid)], { stdio: "inherit" });
      child.on("exit", () => (child = undefined));

      const started = Date.now();
      const poll = setInterval(() => {
        const info = servingEngine(project);
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
