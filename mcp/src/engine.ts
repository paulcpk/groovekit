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

/** How long a call may take before it fails: renders and hearings take a while; nothing takes longer than this. */
export const DEFAULT_CALL_TIMEOUT_MS = 600_000;

export interface CallOptions {
  /** Fails the call after this long; the engine keeps working on it. */
  timeoutMs?: number;
  /** Fails the call when aborted (the client gave up on the tool), so nothing waits on it. */
  signal?: AbortSignal;
}

type Pending = { method: string; resolve: (v: unknown) => void; reject: (e: Error) => void; done: () => void };
type Listener = (params: any) => void;

export class EngineClient {
  readonly url: string;
  readonly origin: string;
  document: Project | undefined;

  private socket: WebSocket | undefined;
  private connecting: Promise<void> | undefined;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private listeners = new Map<string, Set<Listener>>();

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
      const cannot = () => new Error(`Can't connect to the GrooveKit engine at ${this.url}. Is \`groovekit-engine serve\` running?`);

      socket.onmessage = (event) => {
        const message = JSON.parse(String(event.data));

        if (message.method === "snapshot") {
          this.document = message.params.document;
          if (!ready) {
            ready = true;
            this.socket = socket;
            this.request("hello", { origin: this.origin }, {}).then(() => resolve(), reject);
          }
        } else if (message.method === "patch") {
          if (this.document) applyPatch(this.document, message.params.ops);
        } else if (message.method !== undefined) {
          for (const listener of this.listeners.get(message.method) ?? []) listener(message.params);
        } else if (message.id !== undefined) {
          const p = this.pending.get(message.id);
          this.pending.delete(message.id);
          if (!p) return;
          p.done();
          if (message.error) p.reject(new EngineError(message.error.code, message.error.message));
          else p.resolve(message.result);
        }
      };

      socket.onerror = () => {
        if (!ready) reject(cannot());
      };

      socket.onclose = () => {
        // Closed before the snapshot (the engine refused or died while starting): the connect fails
        // instead of leaving every later call waiting on it.
        if (!ready) reject(cannot());
        if (this.socket === socket) {
          this.socket = undefined;
          this.document = undefined;
        }
        for (const p of this.pending.values()) {
          p.done();
          p.reject(new Error("The engine connection closed"));
        }
        this.pending.clear();
      };
    }).finally(() => {
      this.connecting = undefined;
    });

    return this.connecting;
  }

  async call<T = unknown>(method: string, params: Record<string, unknown> = {}, options: CallOptions = {}): Promise<T> {
    await this.connect();
    return this.request(method, params, options) as Promise<T>;
  }

  /** Listens to the engine's notifications of one kind (docs/protocol.md), e.g. "picks"; returns the unsubscribe. */
  on(method: string, listener: Listener): () => void {
    let set = this.listeners.get(method);
    if (!set) this.listeners.set(method, (set = new Set()));
    set.add(listener);
    return () => {
      set.delete(listener);
    };
  }

  close() {
    this.socket?.close();
  }

  private request(method: string, params: Record<string, unknown>, options: CallOptions): Promise<unknown> {
    const id = this.nextId++;
    const { timeoutMs = DEFAULT_CALL_TIMEOUT_MS, signal } = options;

    return new Promise((resolve, reject) => {
      const fail = (error: Error) => {
        if (!this.pending.delete(id)) return;
        done();
        reject(error);
      };
      const onAbort = () => fail(new Error(`${method} was cancelled`));
      const timer = setTimeout(() => fail(new Error(`The engine didn't answer ${method} within ${Math.round(timeoutMs / 1000)} s`)), timeoutMs);
      const done = () => {
        clearTimeout(timer);
        signal?.removeEventListener("abort", onAbort);
      };

      if (signal?.aborted) {
        clearTimeout(timer);
        reject(new Error(`${method} was cancelled`));
        return;
      }
      signal?.addEventListener("abort", onAbort, { once: true });
      this.pending.set(id, { method, resolve, reject, done });
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
