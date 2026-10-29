// JSON-RPC 2.0 client for the GrooveKit engine (docs/protocol.md). Reconnects by itself.

export class EngineError extends Error {
  readonly code: number;

  constructor(code: number, message: string) {
    super(message);
    this.code = code;
  }
}

type Pending = { resolve: (v: unknown) => void; reject: (e: Error) => void };
export type Status = "connecting" | "connected" | "disconnected";

export interface ClientEvents {
  status(status: Status, error?: string): void;
  notification(method: string, params: any): void;
}

export const inDesktopApp = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

/** How long calls may wait with nothing from the engine before it's probed, and how long the
    probe may go unanswered. Long calls (a render, hearing a track) still get their answer, since
    the engine answers the probe meanwhile. */
const SILENCE_MS = 20000;
const PROBE_MS = 10000;

/** The engine protocol this UI needs (hello's protocol): 2 for song maps and sound matching. */
export const ENGINE_PROTOCOL = 2;

/** Where to connect: ?engine=ws://... in the address, the desktop app's engine, the dev server's
    engine finder, or the default port. */
async function findEngineUrl(): Promise<string> {
  const fromQuery = new URLSearchParams(location.search).get("engine");
  if (fromQuery) return fromQuery;

  if (inDesktopApp) {
    const { invoke } = await import("@tauri-apps/api/core");
    return invoke<string>("engine_url");
  }

  try {
    const response = await fetch("/__engine");
    const info = await response.json();
    if (info.url) return info.url;
    if (info.error) throw new Error(info.error);
  } catch (e) {
    if (e instanceof Error && !(e instanceof TypeError) && e.message) throw e;
  }

  return "ws://127.0.0.1:7878";
}

export class EngineClient {
  url = "";
  private socket: WebSocket | undefined;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private retry: ReturnType<typeof setTimeout> | undefined;
  private stopped = false;
  /** Watches for an engine that stopped answering: when calls wait and nothing arrives for a
      while, a cheap call is tried; if that gets no answer either, the connection is dropped so
      the calls fail and a new connection is tried. */
  private watchdog: ReturnType<typeof setInterval> | undefined;
  private lastMessage = 0;
  private probing = false;
  private silent = false;

  constructor(private readonly origin: string, private readonly events: ClientEvents) {}

  start() {
    this.stopped = false;
    void this.open();
  }

  stop() {
    this.stopped = true;
    clearTimeout(this.retry);
    clearInterval(this.watchdog);
    this.socket?.close();
  }

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
        void this.call("transport.state").catch(() => {});
      }
    }, 5000);
  }

  get connected() {
    return this.socket?.readyState === WebSocket.OPEN;
  }

  call<T = any>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.connected) return Promise.reject(new Error("Not connected to the engine"));

    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (v: unknown) => void, reject });
      this.socket!.send(JSON.stringify({ jsonrpc: "2.0", id, method, params }));
    });
  }

  private async open() {
    this.events.status("connecting");

    try {
      this.url = await findEngineUrl();
    } catch (e) {
      this.scheduleRetry((e as Error).message);
      return;
    }

    const socket = new WebSocket(this.url);
    let welcomed = false;

    socket.onmessage = (event) => {
      this.lastMessage = Date.now();
      this.probing = false;
      const message = JSON.parse(String(event.data));

      if (message.id !== undefined && !message.method) {
        const p = this.pending.get(message.id);
        this.pending.delete(message.id);
        if (!p) return;
        if (message.error) p.reject(new EngineError(message.error.code, message.error.message));
        else p.resolve(message.result);
        return;
      }

      if (message.method === "snapshot" && !welcomed) {
        welcomed = true;
        this.socket = socket;
        this.watch(socket);
        this.call("hello", { origin: this.origin })
          .then((hello) => {
            this.events.notification("hello", hello);
            this.events.status("connected");
          })
          .catch(() => {});
      }

      this.events.notification(message.method, message.params);
    };

    socket.onclose = () => {
      if (this.socket === socket) this.socket = undefined;
      const why = this.silent ? "The engine stopped answering" : "The engine connection closed";
      for (const p of this.pending.values()) p.reject(new Error(why));
      this.pending.clear();
      this.scheduleRetry(welcomed ? (this.silent ? "The engine stopped answering" : "The engine stopped") : `Can't reach the engine at ${this.url}`);
    };
  }

  private scheduleRetry(error: string) {
    if (this.stopped) return;
    this.events.status("disconnected", error);
    clearTimeout(this.retry);
    this.retry = setTimeout(() => void this.open(), 1500);
  }
}
