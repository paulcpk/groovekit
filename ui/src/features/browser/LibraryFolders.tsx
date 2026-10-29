import { FolderPlus, RefreshCw, X } from "lucide-react";
import { useEffect, useState } from "react";
import { Button, IconButton } from "@/components/controls";

import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";
import { call, engine, useEngine } from "@/state/engine";

export function LibraryFolders() {
  const connected = useEngine((s) => s.status === "connected");
  const progress = useEngine((s) => s.libraryProgress);
  const [folders, setFolders] = useState<string[]>([]);
  const [adding, setAdding] = useState(false);

  const refresh = () => engine.call<{ folders: string[]; progress: any }>("library.status").then((r) => {
    setFolders(r.folders);
    if (r.progress) useEngine.setState({ libraryProgress: r.progress });
  }, () => {});

  useEffect(() => {
    if (connected) void refresh();
  }, [connected, progress?.running]);

  return (
    <div className="border-t border-line p-2 text-2xs text-dim">
      <div className="mb-1 flex items-center gap-1">
        <span className="flex-1 uppercase tracking-wide text-faint">Library folders</span>
        <IconButton size="sm" title="Rescan" onClick={() => void call("library.scan")}><RefreshCw size={11} className={progress?.running ? "animate-spin" : ""} /></IconButton>
        <IconButton size="sm" title="Add folder" onClick={() => {
          if (!inDesktopApp) return setAdding(true);
          void pickFolder("Add a sample folder", folders[0]).then((path) => {
            if (path) void call("library.addFolder", { path }).then(refresh);
          });
        }}><FolderPlus size={12} /></IconButton>
      </div>
      {folders.map((f) => (
        <div key={f} className="group flex items-center gap-1" title={f}>
          <span className="min-w-0 flex-1 truncate">{f.replace(/^\/Users\/[^/]+/, "~")}</span>
          <button type="button" className="text-faint opacity-0 group-hover:opacity-100 hover:text-rec" title="Remove folder"
            onClick={() => void call("library.removeFolder", { path: f }).then(refresh)}><X size={10} /></button>
        </div>
      ))}
      {progress?.running && <div className="mt-1 text-faint">Scanning… {progress.analysed + progress.unchanged} of {progress.filesFound}</div>}
      {adding && (
        <form className="mt-1 flex gap-1" onSubmit={(e) => {
          e.preventDefault();
          const path = new FormData(e.currentTarget).get("path") as string;
          setAdding(false);
          if (path.trim()) void call("library.addFolder", { path: path.trim().replace(/^~/, folders[0]?.match(/^\/Users\/[^/]+/)?.[0] ?? "~") }).then(refresh);
        }}>
          <input autoFocus name="path" placeholder="/Users/you/Samples" onKeyDown={(e) => e.stopPropagation()}
            className="h-6 min-w-0 flex-1 rounded-[3px] bg-bg px-1.5 text-xs text-fg outline-none ring-1 ring-accent" />
          <Button size="sm" type="submit">Add</Button>
        </form>
      )}
    </div>
  );
}
