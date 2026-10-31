// JSON-RPC client for the GrooveKit engine API (docs/protocol.md), shared by the MCP server and
// the app: requests with timeouts, notifications by kind, a mirror of the song document, and for
// the app a connection that finds its engine and comes back by itself.

import type { Project } from "@groovekit/schema/project";
import type { Command, PatchOp } from "@groovekit/schema/protocol";

export class EngineError extends Error {
  readonly code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}

/** How long a call may take before it fails: renders and hearings take a while; nothing takes longer than this. */
export const DEFAULT_CALL_TIMEOUT_MS = 600_000;

/** How long calls may wait with nothing from the engine before it's probed, and how long the
    probe may go unanswered. Long calls (a render, hearing a track) still get their answer, since
    the engine answers the probe meanwhile. */
const SILENCE_MS = 20_000;
const PROBE_MS = 10_000;
const RETRY_MS = 1_500;

export type Status = "connecting" | "connected" | "disconnected";

export interface CallOptions {
  /** Fails the call after this long; the engine keeps working on it. */
  timeoutMs?: number;
  /** Fails the call when aborted (the client gave up on the tool), so nothing waits on it. */
  signal?: AbortSignal;
}

export interface EngineClientOptions {
  /** Where to connect, or how to find out each time (the app asks its shell or dev server). */
  url: string | (() => Promise<string>);
  /** Who this client is to the engine's history: "agent", "ui"... */
  origin?: string;
  /** Keep connecting after the connection drops or can't be made (the app); without it, a call
      makes one attempt and fails if the engine isn't there (the MCP server). */
  reconnect?: boolean;
  /** Keep `document` in step with the engine's song (default true; the app keeps its own mirror). */
  mirror?: boolean;
  /** Told when the connection comes and goes, with why it went. */
  onStatus?: (status: Status, error?: string) => void;
}

//==============================================================================
/** Results of the methods the clients read fields of; the rest are `unknown` until typed. */
export interface Summary {
  name?: string;
  tempo: number;
  key?: string;
  style?: string;
  lengthBars: number;
  channels: { id: string; name: string; sample?: string }[];
  patterns: { id: string; notesPerChannel: Record<string, number> }[];
  [key: string]: unknown;
}

export interface HistoryEntry {
  id: number;
  parent?: number;
  time: number;
  origin: string;
  group?: string;
  label: string;
}

export interface SlotInfo {
  channel: string;
  name: string;
  part: string;
  range: unknown;
  guessed: boolean;
  insert: string;
  slot: unknown;
  proposed: unknown;
}

export interface Pick {
  status: "waiting" | "chosen";
  choice?: number;
  note?: string;
  more?: boolean;
}

export interface MapListing {
  id: string;
  style: string;
  title: string;
  status: string;
  inUse: boolean;
  map?: { tempo: number };
}

export interface SongMap {
  id: string;
  style: string;
  title: string;
  inUse: boolean;
  score?: string;
  track: Record<string, unknown> & { figures?: { part: string }[]; voices?: unknown };
}

export interface TransportState {
  playing: boolean;
  recording: boolean;
  position: number;
  seconds: number;
  mode?: "song" | "pattern";
  pattern?: string;
  metronome?: boolean;
  countInBars?: number;
  time?: number;
  outputLatencyMs?: number;
}

type Params = Record<string, unknown>;

/** The engine's methods the clients call by name, with what each takes and gives. Methods built
    from a variable (`transport.${action}`) go through the untyped overload. */
export interface Methods {
  hello: { params: { origin?: string }; result: { server: string; protocol: number; project?: string } };
  execute: { params: { commands: Command[]; group?: string; label?: string }; result: { entry: number; label: string } };
  undo: { params: { wholeGroup?: boolean }; result: { changed: boolean; head: number | null } };
  redo: { params: { wholeGroup?: boolean }; result: { changed: boolean; head: number | null } };
  get: { params: { path?: string }; result: unknown };
  getSummary: { params: Params; result: Summary };
  getHistory: { params: Params; result: { entries: HistoryEntry[]; head: number | null } };
  listEffects: { params: Params; result: Record<string, unknown> };
  render: { params: Params; result: { path: string; format: string; analysis: Record<string, unknown>; gainDb?: number; stems?: unknown[] } };
  analyze: { params: { path: string; start?: number; end?: number }; result: Record<string, unknown> };
  analyzeMix: { params: { start?: number; end?: number; tailSeconds?: number }; result: Record<string, unknown> };
  master: { params: Params; result: { inputGainDb: number; integratedLufs: number; truePeakDb: number; reachedTarget: boolean; renders: number; passes: number } };
  "transport.state": { params: Params; result: TransportState };
  "transport.play": { params: Params; result: TransportState };
  "transport.stop": { params: Params; result: TransportState };
  "transport.setMode": { params: { mode: "song" | "pattern"; pattern?: string }; result: unknown };
  "transport.setLoop": { params: { start?: number; end?: number }; result: unknown };
  "transport.setPosition": { params: { position: number | undefined }; result: unknown };
  "project.recent": { params: Params; result: { projects: { path: string; name: string; modified: number }[]; current?: string } };
  "record.inputs": { params: Params; result: { inputs: unknown[] } };
  "record.alignment": { params: Params; result: unknown };
  "record.start": { params: Params; result: unknown };
  "record.stop": { params: { group?: string }; result: unknown };
  "record.calibrate": { params: { input: string; bars: number }; result: unknown };
  "audio.devices": { params: Params; result: unknown };
  "audio.setDevice": { params: { input?: string; output?: string }; result: unknown };
  "midi.devices": { params: Params; result: unknown };
  "midi.setClock": { params: { output: string; enabled: boolean }; result: unknown };
  "library.search": { params: Params; result: { results: Record<string, unknown>[]; total?: number } };
  "library.describe": { params: { path: string; style?: string }; result: Record<string, unknown> };
  "library.rate": { params: { path: string; fits: boolean | null; style?: string }; result: unknown };
  "plugins.describe": { params: { uid: string; query?: string; limit?: number }; result: Record<string, unknown> };
  "listening.status": { params: Params; result: Record<string, unknown> };
  "listening.download": { params: Params; result: Record<string, unknown> };
  "styles.list": { params: Params; result: { styles: unknown[]; progress?: unknown } };
  "styles.references": { params: Params; result: unknown };
  "styles.add": { params: { path?: string; name?: string }; result: unknown };
  "styles.compare": { params: { name: string; start?: number; end?: number }; result: Record<string, unknown> };
  "maps.list": { params: Params; result: { songs: MapListing[] } };
  "maps.get": { params: { id?: string }; result: SongMap };
  "maps.hear": { params: { ids?: string[] }; result: unknown };
  "maps.setInUse": { params: { ids?: string[]; inUse?: boolean }; result: unknown };
  "voices.library": { params: { style?: string }; result: { style: string; library: unknown; text: string } };
  "voices.song": { params: Params; result: { voices: unknown; channels: string[]; bars: number; text: string } };
  "voices.check": { params: { style?: string; key?: string }; result: { style: string; findings: string[]; text: string } };
  "voices.write": { params: Params; result: { candidates: Record<string, unknown>[]; style: string; key?: string } };
  "match.kicks": { params: Params; result: unknown };
  "match.audition": { params: Params; result: unknown };
  "match.channel": { params: Params; result: unknown };
  "picks.offer": { params: Params; result: { id: number } };
  "picks.get": { params: { id: number }; result: Pick };
  "picks.list": { params: Params; result: { offers: { id: number }[] } };
  "picks.choose": { params: { id: number; choice: number; note?: string; more?: boolean }; result: unknown };
  "mix.slots": { params: Params; result: { channels: SlotInfo[]; applied?: unknown; style?: string } };
  "mix.setSlot": { params: Params; result: unknown };
  "mix.check": { params: Params; result: Record<string, unknown> };
  "mix.audition": { params: Params; result: { candidates: Record<string, unknown>[] } & Record<string, unknown> };
}

type Pending = { method: string; resolve: (v: unknown) => void; reject: (e: Error) => void; done: () => void };
type Listener = (params: any) => void;
type AnyListener = (method: string, params: any) => void;

//==============================================================================
export class EngineClient {
  /** Where the client connected, or is connecting, last. */
  url = "";
  readonly origin: string;
  /** The engine's song, kept in step through its notifications (unless mirror is off). */
  document: Project | undefined;
  /** What hello said: the engine's protocol version and the project it serves. */
  hello: Methods["hello"]["result"] | undefined;

  private readonly options: EngineClientOptions;
  private socket: WebSocket | undefined;
  private connecting: Promise<void> | undefined;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private listeners = new Map<string, Set<Listener>>();
  private anyListeners = new Set<AnyListener>();
  private retry: ReturnType<typeof setTimeout> | undefined;
  private watchdog: ReturnType<typeof setInterval> | undefined;
  private stopped = false;
  private lastMessage = 0;
  private probing = false;
  private silent = false;

  constructor(options: EngineClientOptions | string, origin = "agent") {
    this.options = typeof options === "string" ? { url: options, origin } : options;
    this.origin = this.options.origin ?? origin;
    if (typeof this.options.url === "string") this.url = this.options.url;
  }

  get connected() {
    return this.socket?.readyState === WebSocket.OPEN;
  }

  /** Connects (or reconnects) and waits for the first snapshot and hello. One attempt. */
  connect(): Promise<void> {
    if (this.connected && this.hello) return Promise.resolve();
    if (this.connecting) return this.connecting;

    this.connecting = this.open().finally(() => {
      this.connecting = undefined;
    });

    return this.connecting;
  }

  /** Connects and keeps the connection up, trying again whenever it goes (reconnect mode). */
  start() {
    this.stopped = false;
    this.connect().catch(() => {});
  }

  /** Closes, and stops trying again. */
  stop() {
    this.stopped = true;
    clearTimeout(this.retry);
    clearInterval(this.watchdog);
    this.socket?.close();
  }

  close() {
    this.stop();
  }

  call<M extends keyof Methods>(method: M, params?: Methods[M]["params"], options?: CallOptions): Promise<Methods[M]["result"]>;
  // Methods not in the map (or made from a variable) give whatever the engine says.
  call<T = any>(method: string, params?: Record<string, unknown>, options?: CallOptions): Promise<T>;
  async call(method: string, params: Record<string, unknown> = {}, options: CallOptions = {}): Promise<unknown> {
    // The app's calls fail at once while the engine is away (it shows that and tries again by
    // itself); the server's wait for one attempt to connect.
    if (this.options.reconnect) {
      if (!this.connected) throw new Error("Not connected to the engine");
    } else {
      await this.connect();
    }

    return this.request(method, params, options);
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

  /** Listens to every notification, hello included; returns the unsubscribe. */
  onAny(listener: AnyListener): () => void {
    this.anyListeners.add(listener);
    return () => {
      this.anyListeners.delete(listener);
    };
  }

  private notify(method: string, params: unknown) {
    for (const listener of this.listeners.get(method) ?? []) listener(params);
    for (const listener of this.anyListeners) listener(method, params);
  }

  private async open(): Promise<void> {
    this.options.onStatus?.("connecting");

    try {
      this.url = typeof this.options.url === "string" ? this.options.url : await this.options.url();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      this.scheduleRetry(message);
      throw new Error(message);
    }

    return new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url);
      let ready = false;
      const cannot = () => new Error(`Can't connect to the GrooveKit engine at ${this.url}. Is \`groovekit-engine serve\` running?`);

      socket.onmessage = (event) => {
        this.lastMessage = Date.now();
        this.probing = false;
        const message = JSON.parse(String(event.data));

        if (message.id !== undefined && !message.method) {
          const p = this.pending.get(message.id);
          this.pending.delete(message.id);
          if (!p) return;
          p.done();
          if (message.error) p.reject(new EngineError(message.error.code, message.error.message));
          else p.resolve(message.result);
          return;
        }

        if (message.method === "snapshot") {
          if (this.options.mirror !== false) this.document = message.params.document;
          if (!ready) {
            ready = true;
            this.socket = socket;
            this.watch(socket);
            this.request("hello", { origin: this.origin }, {}).then(
              (hello) => {
                this.hello = hello as Methods["hello"]["result"];
                this.notify("hello", hello);
                this.options.onStatus?.("connected");
                resolve();
              },
              (error) => {
                reject(error);
                socket.close();
              },
            );
          }
        } else if (message.method === "patch") {
          if (this.document) applyPatch(this.document, message.params.ops);
        }

        if (message.method !== undefined) this.notify(message.method, message.params);
      };

      socket.onerror = () => {
        if (!ready) reject(cannot());
      };

      socket.onclose = () => {
        // Closed before the snapshot (the engine refused or died while starting): the connect
        // fails instead of leaving every later call waiting on it.
        if (!ready) reject(cannot());
        if (this.socket === socket) {
          this.socket = undefined;
          this.hello = undefined;
          if (this.options.mirror !== false) this.document = undefined;
          clearInterval(this.watchdog);
        }
        const why = this.silent ? "The engine stopped answering" : "The engine connection closed";
        for (const p of this.pending.values()) {
          p.done();
          p.reject(new Error(why));
        }
        this.pending.clear();
        this.scheduleRetry(ready ? (this.silent ? "The engine stopped answering" : "The engine stopped") : `Can't reach the engine at ${this.url}`);
      };
    });
  }

  private scheduleRetry(error: string) {
    this.options.onStatus?.("disconnected", error);
    if (!this.options.reconnect || this.stopped) return;
    clearTimeout(this.retry);
    this.retry = setTimeout(() => this.connect().catch(() => {}), RETRY_MS);
  }

  /** Watches for an engine that stopped answering: when calls wait and nothing arrives for a
      while, a cheap call is tried; if that gets no answer either, the connection is dropped so
      the calls fail (and, in reconnect mode, a new connection is tried). */
  private watch(socket: WebSocket) {
    clearInterval(this.watchdog);
    this.lastMessage = Date.now();
    this.probing = false;
    this.silent = false;
    this.watchdog = setInterval(() => {
      if (this.socket !== socket || this.pending.size === 0) {
        this.probing = false;
        return;
      }
      const quiet = Date.now() - this.lastMessage;
      if (quiet > SILENCE_MS + PROBE_MS && this.probing) {
        this.silent = true;
        socket.close();
      } else if (quiet > SILENCE_MS && !this.probing) {
        this.probing = true;
        this.request("transport.state", {}, { timeoutMs: PROBE_MS }).catch(() => {});
      }
    }, 5000);
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
      if (!this.socket) {
        clearTimeout(timer);
        reject(new Error("Not connected to the engine"));
        return;
      }
      signal?.addEventListener("abort", onAbort, { once: true });
      this.pending.set(id, { method, resolve, reject, done });
      this.socket.send(JSON.stringify({ jsonrpc: "2.0", id, method, params }));
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
