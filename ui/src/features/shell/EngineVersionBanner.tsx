import { TriangleAlert } from "lucide-react";
import { useState } from "react";
import { ENGINE_PROTOCOL, inDesktopApp } from "@/engine/client";
import { useEngine } from "@/state/engine";

/** Shown when the app joined an engine older than itself: one already serving the song, started
    by Claude Code or an older GrooveKit, which lacks newer features (the Maps tab stays empty).
    Restarting it starts the app's own. */
export function EngineVersionBanner() {
  const protocol = useEngine((s) => (s.status === "connected" ? s.protocol : undefined));
  const [restarting, setRestarting] = useState(false);

  if (protocol === undefined || protocol >= ENGINE_PROTOCOL) return null;

  const restart = () => {
    setRestarting(true);
    void import("@tauri-apps/api/core").then(({ invoke }) => invoke("restart_engine")).finally(() => setRestarting(false));
  };

  return (
    <div className="flex shrink-0 items-center gap-2 border-t border-line bg-accent/10 px-3 py-1.5 text-xs text-fg">
      <TriangleAlert size={13} className="shrink-0 text-accent" />
      <span className="min-w-0 flex-1">
        This song is open in an older GrooveKit engine (started by Claude Code or an older app), so newer parts like the Maps tab don't work.
        {inDesktopApp ? " Restart it to use this version's engine." : " Quit it, and Claude Code's GrooveKit tools, to use this version."}
      </span>
      {inDesktopApp && (
        <button type="button" disabled={restarting} onClick={restart}
          className="h-6 shrink-0 rounded-[4px] bg-accent px-2.5 text-xs text-black hover:brightness-110 disabled:opacity-50">
          {restarting ? "Restarting…" : "Restart the engine"}
        </button>
      )}
    </div>
  );
}
