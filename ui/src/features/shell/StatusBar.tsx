import { Bot } from "lucide-react";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";

/** Engine connection, and the latest change with who made it. */
export function StatusBar() {
  const status = useEngine((s) => s.status);
  const project = useEngine((s) => s.project);
  const last = useEngine((s) => s.changes[0]);
  const library = useEngine((s) => s.libraryProgress);
  const agent = last && last.origin !== "ui";

  return (
    <div className="flex h-6 shrink-0 items-center gap-3 border-t border-line bg-panel px-3 text-2xs text-faint">
      <span>{status === "connected" ? `Engine · ${project ?? ""}` : status === "connecting" ? "Connecting…" : "Engine not reachable"}</span>
      {library?.running && <span>Scanning samples · {library.analysed + library.unchanged}/{library.filesFound}</span>}
      <div className="flex-1" />
      {last && (
        <button
          type="button"
          className={cn("flex items-center gap-1.5 truncate hover:text-fg", agent && "text-agent")}
          onClick={() => setUi({ sidebarOpen: true, sidebarTab: "history" })}
        >
          {agent && <Bot size={11} />}
          <span className="truncate">
            {last.kind === "undo" ? "Undo: " : last.kind === "redo" ? "Redo: " : ""}{last.label}
          </span>
          <span className="text-faint">· {last.origin}</span>
        </button>
      )}
    </div>
  );
}
