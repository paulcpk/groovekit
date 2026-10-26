import { FolderPlus, Play, RefreshCw, Repeat, Search, Square, X } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { Button, Empty, IconButton } from "@/components/controls";
import { SAMPLE_MIME, type DraggedSample } from "@/features/playlist/geometry";
import { addSamplerChannel } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { fileName } from "@/lib/model";
import { call, engine, useEngine } from "@/state/engine";

interface Sample {
  path: string;
  description?: string;
  category: string;
  loop: boolean;
  tempo?: number;
  key?: string;
  durationSeconds: number;
  rootKey?: number;
}

const categories = ["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums", "bass", "synth", "vocal", "fx"];

export function Browser() {
  const connected = useEngine((s) => s.status === "connected");
  const progress = useEngine((s) => s.libraryProgress);
  const [query, setQuery] = useState("");
  const [category, setCategory] = useState<string>();
  const [loops, setLoops] = useState<boolean | undefined>();
  const [results, setResults] = useState<Sample[]>([]);
  const [size, setSize] = useState<number>();
  const [playing, setPlaying] = useState<string>();
  const [cursor, setCursor] = useState(0);
  const list = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!connected) return;
    const timer = setTimeout(() => {
      engine.call<{ results: Sample[]; librarySize: number }>("library.search", {
        query, limit: 200, ...(category ? { category } : {}), ...(loops !== undefined ? { loop: loops } : {}),
      }).then((r) => {
        setResults(r.results);
        setSize(r.librarySize);
        setCursor(0);
      }, () => {});
    }, 150);
    return () => clearTimeout(timer);
  }, [connected, query, category, loops, progress?.running]);

  const preview = (s: Sample) => {
    if (playing === s.path) {
      setPlaying(undefined);
      void call("preview.stop");
      return;
    }
    setPlaying(s.path);
    void call("preview.play", { path: s.path });
    setTimeout(() => setPlaying((p) => (p === s.path ? undefined : p)), Math.min(30, s.durationSeconds) * 1000);
  };

  const dragged = (s: Sample): DraggedSample => ({ path: s.path, durationSeconds: s.durationSeconds, loop: s.loop, tempo: s.tempo, category: s.category });

  return (
    <div className="flex h-full flex-col">
      <div className="flex flex-col gap-2 border-b border-line p-2">
        <div className="flex h-7 items-center gap-1.5 rounded-[4px] bg-bg px-2">
          <Search size={12} className="text-faint" />
          <input
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === "ArrowDown") setCursor((c) => Math.min(results.length - 1, c + 1));
              if (e.key === "ArrowUp") setCursor((c) => Math.max(0, c - 1));
              if (e.key === "Enter" && results[cursor]) preview(results[cursor]);
            }}
            placeholder={size ? `Search ${size.toLocaleString()} samples` : "Search samples"}
            className="min-w-0 flex-1 bg-transparent text-sm text-fg outline-none placeholder:text-faint"
          />
          {query && <button type="button" onClick={() => setQuery("")} className="text-faint hover:text-fg"><X size={12} /></button>}
        </div>
        <div className="flex flex-wrap gap-1">
          {categories.map((c) => (
            <button key={c} type="button" onClick={() => setCategory(category === c ? undefined : c)}
              className={cn("h-5 rounded-[3px] px-1.5 text-2xs", category === c ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}>
              {c}
            </button>
          ))}
          <button type="button" onClick={() => setLoops(loops === true ? false : loops === false ? undefined : true)}
            title="Loops / one-shots / both"
            className={cn("flex h-5 items-center gap-1 rounded-[3px] px-1.5 text-2xs", loops !== undefined ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}>
            <Repeat size={9} /> {loops === true ? "loops" : loops === false ? "one-shots" : "all"}
          </button>
        </div>
      </div>

      <div ref={list} className="min-h-0 flex-1 overflow-y-auto py-1">
        {results.length === 0 ? (
          <Empty>{size === 0 ? "The library is empty. Add a folder below." : "No samples match."}</Empty>
        ) : results.map((s, i) => (
          <div
            key={s.path}
            draggable
            onDragStart={(e) => {
              e.dataTransfer.setData(SAMPLE_MIME, JSON.stringify(dragged(s)));
              e.dataTransfer.effectAllowed = "copy";
            }}
            onClick={() => {
              setCursor(i);
              preview(s);
            }}
            onDoubleClick={() => {
              const doc = useEngine.getState().doc;
              if (doc) void addSamplerChannel(doc, dragged(s));
            }}
            title={`${s.path}\nDrag to the playlist or channel rack. Double-click adds a channel.`}
            className={cn("group flex h-[26px] cursor-grab items-center gap-1.5 px-2 text-xs", i === cursor ? "bg-control" : "hover:bg-raised")}
          >
            <span className={cn("w-3 shrink-0", playing === s.path ? "text-accent" : "text-faint opacity-0 group-hover:opacity-100")}>
              {playing === s.path ? <Square size={9} fill="currentColor" /> : <Play size={10} fill="currentColor" />}
            </span>
            <span className="min-w-0 flex-1 truncate text-fg">{fileName(s.path)}</span>
            {s.loop && s.tempo && <span className="shrink-0 text-2xs text-faint tabular">{Math.round(s.tempo)}</span>}
            {s.key && <span className="shrink-0 text-2xs text-faint">{s.key}</span>}
            <span className="shrink-0 rounded-[2px] bg-bg px-1 text-[9px] text-dim">{s.category}</span>
          </div>
        ))}
      </div>

      <LibraryFolders />
    </div>
  );
}

function LibraryFolders() {
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
        <IconButton size="sm" title="Add folder" onClick={() => setAdding(true)}><FolderPlus size={12} /></IconButton>
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
