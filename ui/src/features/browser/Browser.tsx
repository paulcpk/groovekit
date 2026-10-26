import { ChevronRight, Folder, FolderOpen, FolderPlus, Play, RefreshCw, Repeat, Search, Square, X } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Button, Empty, IconButton } from "@/components/controls";
import { endSampleDrag, startSampleDrag, type DraggedSample } from "@/features/playlist/geometry";
import { addSamplerChannel } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { fileName } from "@/lib/model";
import { previewSample, stopPreview } from "@/features/shell/actions";
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

const ROW_H = 26;

const dragged = (s: Sample): DraggedSample => ({ path: s.path, durationSeconds: s.durationSeconds, loop: s.loop, tempo: s.tempo, category: s.category });

function addChannel(s: Sample) {
  const doc = useEngine.getState().doc;
  if (doc) void addSamplerChannel(doc, dragged(s));
}

/** One sample: click to hear it, drag it to the playlist or channel rack, double-click for a channel. */
function SampleRow({ sample: s, depth = 0, selected, playing, onClick }: {
  sample: Sample; depth?: number; selected: boolean; playing: boolean; onClick: () => void;
}) {
  return (
    <div
      draggable
      onDragStart={(e) => startSampleDrag(e, dragged(s))}
      onDragEnd={endSampleDrag}
      onClick={onClick}
      onDoubleClick={() => addChannel(s)}
      title={`${s.path}\nDrag to the playlist or channel rack. Double-click adds a channel.`}
      className={cn("group flex cursor-grab items-center gap-1.5 pr-2 text-xs", selected ? "bg-control" : "hover:bg-raised")}
      style={{ height: ROW_H, paddingLeft: 8 + depth * 14 }}
    >
      <span className={cn("w-3 shrink-0", playing ? "text-accent" : "text-faint opacity-0 group-hover:opacity-100")}>
        {playing ? <Square size={9} fill="currentColor" /> : <Play size={10} fill="currentColor" />}
      </span>
      <span className="min-w-0 flex-1 truncate text-fg">{fileName(s.path)}</span>
      {s.loop && s.tempo && <span className="shrink-0 text-2xs text-faint tabular">{Math.round(s.tempo)}</span>}
      {s.key && <span className="shrink-0 text-2xs text-faint">{s.key}</span>}
      {/* In the tree, the folders say what kind of sound it is; names get the room. */}
      {depth === 0 && <span className="shrink-0 rounded-[2px] bg-bg px-1 text-[9px] text-dim">{s.category}</span>}
    </div>
  );
}

/** Auditions one sample at a time; Space and the stop button stop it too. */
function usePreview() {
  const playing = useEngine((s) => s.previewing);
  const preview = useCallback((s: Sample) => void previewSample(s.path, s.durationSeconds), []);
  return { playing, preview };
}

/** What's being auditioned, with a way to stop it. */
function NowPreviewing() {
  const playing = useEngine((s) => s.previewing);
  if (!playing) return null;
  return (
    <div className="flex h-7 shrink-0 items-center gap-1.5 border-t border-line bg-raised px-2 text-xs">
      <span className="h-1.5 w-1.5 shrink-0 animate-pulse rounded-full bg-accent" />
      <span className="min-w-0 flex-1 truncate text-fg" title={playing}>{fileName(playing)}</span>
      <IconButton size="sm" title="Stop (Space)" onClick={() => void stopPreview()}><Square size={10} fill="currentColor" /></IconButton>
    </div>
  );
}

export function Browser() {
  const connected = useEngine((s) => s.status === "connected");
  const progress = useEngine((s) => s.libraryProgress);
  const [query, setQuery] = useState("");
  const [category, setCategory] = useState<string>();
  const [loops, setLoops] = useState<boolean | undefined>();
  const [results, setResults] = useState<Sample[]>([]);
  const [size, setSize] = useState<number>();
  const [cursor, setCursor] = useState(0);
  const { playing, preview } = usePreview();
  // With nothing to search for, the library shows as its folders.
  const filtering = query.trim() !== "" || category !== undefined || loops !== undefined;

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

      {!filtering ? (
        <FolderTree playing={playing} preview={preview} />
      ) : (
        <div className="min-h-0 flex-1 overflow-y-auto py-1">
          {results.length === 0 ? (
            <Empty>{size === 0 ? "The library is empty. Add a folder below." : "No samples match."}</Empty>
          ) : results.map((s, i) => (
            <SampleRow key={s.path} sample={s} selected={i === cursor} playing={playing === s.path}
              onClick={() => {
                setCursor(i);
                preview(s);
              }} />
          ))}
        </div>
      )}

      <NowPreviewing />
      <LibraryFolders />
    </div>
  );
}

interface Listing {
  folders: { path: string; name: string; samples: number }[];
  samples: Sample[];
}

type Row =
  | { kind: "folder"; path: string; name: string; samples: number; depth: number; parent?: string }
  | { kind: "sample"; sample: Sample; depth: number; parent: string };

const EXPANDED_KEY = "groovekit.browser.expanded";

function loadExpanded(): Set<string> {
  try {
    return new Set(JSON.parse(localStorage.getItem(EXPANDED_KEY) ?? "[]"));
  } catch {
    return new Set();
  }
}

/** The library as the folders it's kept in, opened one level at a time. */
function FolderTree({ playing, preview }: { playing?: string; preview: (s: Sample) => void }) {
  const connected = useEngine((s) => s.status === "connected");
  const scanning = useEngine((s) => s.libraryProgress?.running);
  const [listings, setListings] = useState<Map<string, Listing>>(new Map());
  const [expanded, setExpanded] = useState(loadExpanded);
  const [cursor, setCursor] = useState(0);
  const [scrollTop, setScrollTop] = useState(0);
  const [height, setHeight] = useState(400);
  const box = useRef<HTMLDivElement>(null);
  const loading = useRef(new Set<string>());

  const load = useCallback((path: string) => {
    if (loading.current.has(path)) return;
    loading.current.add(path);
    engine.call<Listing>("library.list", path ? { path } : {})
      .then((l) => setListings((m) => new Map(m).set(path, l)), () => {})
      .finally(() => loading.current.delete(path));
  }, []);

  // Start over when connecting and when a scan finishes, since folders may have changed.
  useEffect(() => {
    if (!connected || scanning) return;
    setListings(new Map());
    load("");
  }, [connected, scanning === true, load]);

  // The engine catches up on changes with a scan whenever it starts; show what's indexed meanwhile.
  useEffect(() => {
    if (connected && scanning && !listings.has("")) load("");
  }, [connected, scanning, listings, load]);

  // The root's folders start open.
  const root = listings.get("");
  useEffect(() => {
    if (root && expanded.size === 0 && root.folders.length) setExpanded(new Set(root.folders.map((f) => f.path)));
  }, [root]);

  useEffect(() => {
    try {
      localStorage.setItem(EXPANDED_KEY, JSON.stringify([...expanded]));
    } catch {
      // Storage can be unavailable; the tree works without it.
    }
    for (const path of expanded) if (!listings.has(path)) load(path);
  }, [expanded, listings, load]);

  const rows = useMemo(() => {
    const out: Row[] = [];
    const walk = (path: string, depth: number) => {
      const l = listings.get(path);
      if (!l) return;
      for (const f of l.folders) {
        out.push({ kind: "folder", ...f, depth, parent: path || undefined });
        if (expanded.has(f.path)) walk(f.path, depth + 1);
      }
      for (const sample of l.samples) out.push({ kind: "sample", sample, depth, parent: path });
    };
    walk("", 0);
    return out;
  }, [listings, expanded]);

  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const observer = new ResizeObserver(() => setHeight(el.clientHeight));
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const toggle = (path: string, open?: boolean) => setExpanded((e) => {
    const next = new Set(e);
    if (open ?? !next.has(path)) next.add(path);
    else next.delete(path);
    return next;
  });

  const moveTo = (i: number) => {
    const index = Math.max(0, Math.min(rows.length - 1, i));
    setCursor(index);
    const el = box.current;
    if (!el) return;
    if (index * ROW_H < el.scrollTop) el.scrollTop = index * ROW_H;
    else if ((index + 1) * ROW_H > el.scrollTop + el.clientHeight) el.scrollTop = (index + 1) * ROW_H - el.clientHeight;
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    const row = rows[cursor];
    if (!row) return;
    const handled = ["ArrowDown", "ArrowUp", "ArrowRight", "ArrowLeft", "Enter"].includes(e.key);
    if (!handled) return;
    e.preventDefault();
    e.stopPropagation();

    if (e.key === "ArrowDown") moveTo(cursor + 1);
    else if (e.key === "ArrowUp") moveTo(cursor - 1);
    else if (e.key === "Enter") row.kind === "folder" ? toggle(row.path) : preview(row.sample);
    else if (e.key === "ArrowRight" && row.kind === "folder") {
      if (!expanded.has(row.path)) toggle(row.path, true);
      else moveTo(cursor + 1);
    } else if (e.key === "ArrowLeft") {
      if (row.kind === "folder" && expanded.has(row.path)) toggle(row.path, false);
      else if (row.parent) moveTo(rows.findIndex((r) => r.kind === "folder" && r.path === row.parent));
    }
  };

  if (!root) return <div className="min-h-0 flex-1" />;
  if (root.folders.length === 0) return <Empty>The library is empty. Add a folder below.</Empty>;

  // Only the rows in view are drawn; a folder can hold thousands of samples.
  const first = Math.max(0, Math.floor(scrollTop / ROW_H) - 10);
  const last = Math.min(rows.length, Math.ceil((scrollTop + height) / ROW_H) + 10);

  return (
    <div ref={box} tabIndex={0} onKeyDown={onKeyDown} onScroll={(e) => setScrollTop(e.currentTarget.scrollTop)}
      className="min-h-0 flex-1 overflow-y-auto outline-none">
      <div className="relative" style={{ height: rows.length * ROW_H + 8 }}>
        <div className="absolute inset-x-0" style={{ top: first * ROW_H + 4 }}>
          {rows.slice(first, last).map((row, j) => {
            const i = first + j;
            if (row.kind === "sample") {
              return (
                <SampleRow key={row.sample.path} sample={row.sample} depth={row.depth + 1} selected={i === cursor} playing={playing === row.sample.path}
                  onClick={() => {
                    setCursor(i);
                    preview(row.sample);
                  }} />
              );
            }
            const open = expanded.has(row.path);
            return (
              <div key={row.path} title={row.path}
                onClick={() => {
                  setCursor(i);
                  toggle(row.path);
                }}
                className={cn("flex cursor-default items-center gap-1 pr-2 text-xs", i === cursor ? "bg-control" : "hover:bg-raised")}
                style={{ height: ROW_H, paddingLeft: 4 + row.depth * 14 }}>
                <ChevronRight size={11} className={cn("shrink-0 text-faint transition-transform", open && "rotate-90")} />
                {open ? <FolderOpen size={12} className="shrink-0 text-accent" /> : <Folder size={12} className="shrink-0 text-dim" />}
                <span className="min-w-0 flex-1 truncate text-fg">{row.name}</span>
                <span className="shrink-0 text-2xs text-faint tabular">{row.samples.toLocaleString()}</span>
              </div>
            );
          })}
        </div>
      </div>
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
