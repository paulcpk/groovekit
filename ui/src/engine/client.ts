// The engine connection as the app makes it: the client is the MCP server's (mcp/src/engine.ts),
// with reconnects on; what's the app's own is how it finds the engine.

export { EngineClient, EngineError, type Status } from "@groovekit/mcp/src/engine.ts";

export const inDesktopApp = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

/** The engine protocol this UI needs (hello's protocol): 2 for song maps and sound matching. */
export const ENGINE_PROTOCOL = 2;

/** Where to connect: ?engine=ws://... in the address, the desktop app's engine, the dev server's
    engine finder, or the default port. */
export async function findEngineUrl(): Promise<string> {
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
