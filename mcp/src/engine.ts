// JSON-RPC client for the GrooveKit engine API (docs/protocol.md), with a mirror of the song document.

import type { Project } from "@groovekit/schema/project";
import type { PatchOp } from "@groovekit/schema/protocol";

export class EngineError extends Error {
  readonly code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}

type Pending = { resolve: (v: unknown) => void; reject: (e: Error) => void };

export class EngineClient {
  readonly url: string;
  readonly origin: string;
  document: Project | undefined;

  private socket: WebSocket | undefined;
  private connecting: Promise<void> | undefined;
  private nextId = 1;
  private pending = new Map<number, Pending>();

  constructor(url: string, origin = "agent") {
    this.url = url;
    this.origin = origin;
  }

  /** Connects (or reconnects) and waits for the first snapshot. */
  connect(): Promise<void> {
    if (this.socket?.readyState === WebSocket.OPEN && this.document) return Promise.resolve();
    if (this.connecting) return this.connecting;

    this.connecting = new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url);
      let ready = false;

      socket.onmessage = (event) => {
        const message = JSON.parse(String(event.data));

        if (message.method === "snapshot") {
          this.document = message.params.document;
          if (!ready) {
            ready = true;
            this.socket = socket;
            this.request("hello", { origin: this.origin }).then(() => resolve(), reject);
          }
        } else if (message.method === "patch") {
          if (this.document) applyPatch(this.document, message.params.ops);
        } else if (message.id !== undefined) {
          const p = this.pending.get(message.id);
          this.pending.delete(message.id);
          if (!p) return;
          if (message.error) p.reject(new EngineError(message.error.code, message.error.message));
          else p.resolve(message.result);
        }
      };

      socket.onerror = () => {
        if (!ready) reject(new Error(`Can't connect to the GrooveKit engine at ${this.url}. Is \`groovekit-engine serve\` running?`));
      };

      socket.onclose = () => {
        this.socket = undefined;
        this.document = undefined;
        for (const p of this.pending.values()) p.reject(new Error("The engine connection closed"));
        this.pending.clear();
      };
    }).finally(() => {
      this.connecting = undefined;
    });

    return this.connecting;
  }

  async call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    await this.connect();
    return this.request(method, params) as Promise<T>;
  }

  close() {
    this.socket?.close();
  }

  private request(method: string, params: Record<string, unknown>): Promise<unknown> {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.socket!.send(JSON.stringify({ jsonrpc: "2.0", id, method, params }));
    });
  }
}

//==============================================================================
/** Applies JSON Patch add/remove/replace in place, as the engine does. */
export function applyPatch(doc: unknown, ops: PatchOp[]) {
  for (const op of ops) {
    const keys = op.path.split("/").slice(1).map((k) => k.replace(/~1/g, "/").replace(/~0/g, "~"));
    const last = keys.pop()!;
    let parent: any = doc;
    for (const k of keys) parent = parent[Array.isArray(parent) ? Number(k) : k];

    if (Array.isArray(parent)) {
      const index = last === "-" ? parent.length : Number(last);
      if (op.op === "add") parent.splice(index, 0, op.value);
      else if (op.op === "remove") parent.splice(index, 1);
      else parent[index] = op.value;
    } else if (op.op === "remove") {
      delete parent[last];
    } else {
      parent[last] = op.value;
    }
  }
}

export function ticksPerBar(doc: Project | undefined): number {
  const ts = doc?.timeSignature ?? {};
  return (960 * 4 * (ts.numerator ?? 4)) / (ts.denominator ?? 4);
}
