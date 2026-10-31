import { FolderPlus, RefreshCw, X } from "lucide-react";
import { useEffect, useState } from "react";
import { Button, IconButton } from "@/components/controls";

import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";
import { call, engine, useEngine } from "@/state/engine";
import { downloadModels, megabytes, type ListeningStatus } from "./listening";
import { StarterSamples } from "./StarterSamples";

/** The sample library: its folders, and where GrooveKit is with them: finding the files, then
    (once its listening models are here, a one-time download) hearing each one, so the agent and
    the search can pick by sound. Reference tracks are heard in the agent's Maps. */
export function SampleLibrary({ status }: { status?: ListeningStatus }) {
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

  const models = status?.models, index = status?.index;
  const hearing = !!models?.ready && !!index?.running;

  return (
    <div className="border-t border-line p-2 text-2xs text-dim">
      <div className="mb-1 flex items-center gap-1">
        <span className="flex-1 uppercase tracking-wide text-faint">Sample library</span>
        <IconButton size="sm" title="Look for new and changed samples" onClick={() => void call("library.scan")}><RefreshCw size={11} className={progress?.running ? "animate-spin" : ""} /></IconButton>
        <IconButton size="sm" title="Add a sample folder" onClick={() => {
          if (!inDesktopApp) return setAdding(true);
          void pickFolder("Add a sample folder", folders[0]).then((path) => {
            if (path) void call("library.addFolder", { path }).then(refresh);
          });
        }}><FolderPlus size={12} /></IconButton>
      </div>
      {/* With no folders at all, the empty library's message makes the offer instead. */}
      {folders.length > 0 && <StarterSamples />}
      {folders.map((f) => (
        <div key={f} className="group flex items-center gap-1" title={f}>
          <span className="min-w-0 flex-1 truncate">{f.replace(/^\/Users\/[^/]+/, "~")}</span>
          <button type="button" className="text-faint opacity-0 group-hover:opacity-100 hover:text-rec" title="Remove folder"
            onClick={() => void call("library.removeFolder", { path: f }).then(refresh)}><X size={10} /></button>
        </div>
      ))}
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

      {progress?.running && <Progress label={`Finding samples… ${(progress.analysed + progress.unchanged).toLocaleString()} of ${progress.filesFound.toLocaleString()}`} done={(progress.analysed + progress.unchanged) / Math.max(1, progress.filesFound)} />}

      {models && !models.ready && !models.downloading && folders.length > 0 && (
        <div className="mt-1.5 flex flex-col gap-1.5">
          <span>Let GrooveKit hear your samples (and the tracks you love, in the agent's Maps), so it picks by sound. A one-time {megabytes(models.totalBytes)} download; everything stays on this Mac.</span>
          {models.error && <span className="text-rec">{models.error}</span>}
          <Button size="sm" className="self-start" onClick={() => void downloadModels()}>Download</Button>
        </div>
      )}
      {models?.downloading && <Progress label={`Downloading the listening models… ${Math.round(models.progress * 100)}% of ${megabytes(models.totalBytes)}`} done={models.progress} />}
      {hearing && <Progress label={`Hearing your samples… ${index!.embedded.toLocaleString()} of ${index!.total.toLocaleString()}`} done={index!.embedded / Math.max(1, index!.total)} />}
      {models?.ready && index && !index.running && !progress?.running && index.samples > 0 && (
        <div className="mt-1 text-faint" title="The agent and the search can find these by how they sound">
          {index.samples.toLocaleString()} samples heard{index.failed > 0 && `, ${index.failed.toLocaleString()} couldn't be read`}
        </div>
      )}
    </div>
  );
}

function Progress({ label, done }: { label: string; done: number }) {
  return (
    <div className="mt-1 flex flex-col gap-1">
      <span>{label}</span>
      <div className="h-1 overflow-hidden rounded-full bg-bg"><div className="h-full bg-agent transition-[width]" style={{ width: `${Math.min(1, done) * 100}%` }} /></div>
    </div>
  );
}
