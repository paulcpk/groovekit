import { AudioLines, Check, ChevronDown, ChevronRight, Folder, FolderOpen, FolderPlus, Headphones, Heart, Play, RefreshCw, Repeat, Search, Sparkles, Square, ThumbsDown, ThumbsUp, X } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Button, Empty, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { endSampleDrag, startSampleDrag, type DraggedSample } from "@/features/playlist/geometry";
import { addSamplerChannel } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { fileName, noteName } from "@/lib/model";
import { previewSample, stopPreview } from "@/features/shell/actions";
import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";
import { call, engine, useEngine } from "@/state/engine";
import { addStyle, canListen, downloadModels, needsHearing, rateSample, refreshReferences, removeStyle, useListening, useListeningStatus, type ListeningStatus, type ReferenceFolder } from "./listening";

interface Sample {
  path: string;
  description?: string;
  category: string;
  loop: boolean;
  tempo?: number;
  key?: string;
  /** Where the key comes from: the file name, the sound, or both agreeing. */
  keySource?: "name" | "sound" | "name and sound";
  /** What it sounds like, when that's not the name's key. */
  heardKey?: string;
  durationSeconds: number;
  rootKey?: number;
  /** How it goes with the song's key (when filtering by it): semitones to put it exactly in key. */
  keyFit?: { fits: boolean; transpose: number };
  /** Ranked by sound: fit to the style (0 an average library sample, 100 like the references), match to the words or sample (0-100). */
  fit?: number;
  match?: number;
  rating?: "fits" | "doesn't fit";
  /** 0-100: how likely you are to say it fits, learned from your ratings. */
  taste?: number;
  /** What it sounds like, in words. */
  sounds?: string[];
}

const categories = ["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums", "bass", "synth", "vocal", "fx"];

const ROW_H = 26;

const dragged = (s: Sample): DraggedSample => ({ path: s.path, durationSeconds: s.durationSeconds, loop: s.loop, tempo: s.tempo, category: s.category });

function addChannel(s: Sample) {
  const doc = useEngine.getState().doc;
  if (doc) void addSamplerChannel(doc, dragged(s));
}

/** How well a sample fits the style: up to about 100 for as close as the references are to each other. */
function Fit({ fit }: { fit: number }) {
  return (
    <span title={`Fit to the style: ${fit} (0 is an average sample of your library, 100 as close as your reference tracks are to each other)`}
      className={cn("w-7 shrink-0 text-right text-2xs tabular", fit >= 60 ? "text-ok" : fit >= 30 ? "text-fg" : fit >= 0 ? "text-dim" : "text-faint")}>
      {fit}
    </span>
  );
}

/** "Am", "F#": short key names. */
function shortKey(key: string) {
  const [tonic, mode] = key.split(" ");
  return mode === "minor" ? `${tonic}m` : tonic;
}

/** A sample's key (heard ones in italics), or a one-shot's note, and with the song's key, the
    semitones that put it in key. */
function KeyLabel({ sample: s }: { sample: Sample }) {
  const label = s.key ? shortKey(s.key) : s.rootKey !== undefined ? noteName(s.rootKey) : undefined;
  if (!label) return null;
  const title = s.key
    ? `${s.key}${s.keySource === "sound" ? " (heard)" : s.keySource === "name and sound" ? " (named, and sounds it)" : " (from its name)"}${s.heardKey ? `; sounds like ${s.heardKey}` : ""}`
    : `Its note: ${noteName(s.rootKey!)}`;
  const shift = s.keyFit?.transpose;
  return (
    <span className="flex shrink-0 items-baseline gap-1 text-2xs" title={shift ? `${title}. ${shift > 0 ? "+" : ""}${shift} semitones put it in the song's key.` : title}>
      <span className={cn("text-faint", s.keySource === "sound" && "italic", s.heardKey && "underline decoration-dotted")}>{label}</span>
      {shift !== undefined && shift !== 0 && <span className="text-accent tabular">{shift > 0 ? "+" : ""}{shift}</span>}
    </span>
  );
}

/** One sample: click to hear it, drag it to the playlist or channel rack, double-click for a channel.
    With a style, it can be marked as fitting or not; any sample can lead to sounds like it. */
function SampleRow({ sample: s, depth = 0, selected, playing, onClick, style, onRate, onSimilar }: {
  sample: Sample; depth?: number; selected: boolean; playing: boolean; onClick: () => void;
  style?: string; onRate?: (fits: boolean | null) => void; onSimilar?: () => void;
}) {
  const sounds = s.sounds?.length ? `\nSounds ${s.sounds.join(", ")}` : "";
  return (
    <div
      draggable
      onDragStart={(e) => startSampleDrag(e, dragged(s))}
      onDragEnd={endSampleDrag}
      onClick={onClick}
      onDoubleClick={() => addChannel(s)}
      title={`${s.path}${sounds}\nDrag to the playlist or channel rack. Double-click adds a channel.`}
      className={cn("group flex cursor-grab items-center gap-1.5 pr-2 text-xs", selected ? "bg-control" : "hover:bg-raised")}
      style={{ height: ROW_H, paddingLeft: 8 + depth * 14 }}
    >
      <span className={cn("w-3 shrink-0", playing ? "text-accent" : "text-faint opacity-0 group-hover:opacity-100")}>
        {playing ? <Square size={9} fill="currentColor" /> : <Play size={10} fill="currentColor" />}
      </span>
      <span className="min-w-0 flex-1 truncate text-fg">{fileName(s.path)}</span>
      {s.loop && s.tempo && <span className="shrink-0 text-2xs text-faint tabular">{Math.round(s.tempo)}</span>}
      <KeyLabel sample={s} />
      {/* In the tree, the folders say what kind of sound it is; names get the room. */}
      {depth === 0 && <span className="shrink-0 rounded-[2px] bg-bg px-1 text-[9px] text-dim">{s.category}</span>}
      {onSimilar && (
        <button type="button" title="Find sounds like this" onClick={(e) => { e.stopPropagation(); onSimilar(); }}
          className="shrink-0 text-faint opacity-0 hover:text-agent group-hover:opacity-100"><Sparkles size={11} /></button>
      )}
      {style && onRate && (
        <span className={cn("flex shrink-0 items-center gap-0.5", !s.rating && "opacity-0 group-hover:opacity-100")}>
          <button type="button" title={`Fits ${style}`} onClick={(e) => { e.stopPropagation(); onRate(s.rating === "fits" ? null : true); }}
            className={cn(s.rating === "fits" ? "text-ok" : "text-faint hover:text-fg")}><ThumbsUp size={11} /></button>
          <button type="button" title={`Doesn't fit ${style}`} onClick={(e) => { e.stopPropagation(); onRate(s.rating === "doesn't fit" ? null : false); }}
            className={cn(s.rating === "doesn't fit" ? "text-rec" : "text-faint hover:text-fg")}><ThumbsDown size={11} /></button>
        </span>
      )}
      {s.taste !== undefined && s.taste >= 70 && !s.rating && (
        <span title={`Your taste: ${s.taste}. It sounds like the samples you said fit`} className="shrink-0 text-agent"><Heart size={10} fill="currentColor" /></span>
      )}
      {s.fit !== undefined && <Fit fit={s.fit} />}
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

const STYLE_KEY = "groovekit.browser.style";
const AUTOPLAY_KEY = "groovekit.browser.autoplay";

/** Whether moving through samples with the arrow keys plays each one, remembered between sessions. */
function useAutoPlay() {
  const [on, setOn] = useState(() => {
    try {
      return localStorage.getItem(AUTOPLAY_KEY) !== "0";
    } catch {
      return true;
    }
  });
  const toggle = () => {
    setOn(!on);
    try {
      localStorage.setItem(AUTOPLAY_KEY, on ? "0" : "1");
    } catch {
      // Storage can be unavailable; the switch still holds for now.
    }
  };
  return [on, toggle] as const;
}

function loadStyle() {
  try {
    return localStorage.getItem(STYLE_KEY) ?? undefined;
  } catch {
    return undefined;
  }
}

export function Browser() {
  const connected = useEngine((s) => s.status === "connected");
  const progress = useEngine((s) => s.libraryProgress);
  const { status, styles } = useListeningStatus();
  const listening = canListen(status);
  const [query, setQuery] = useState("");
  const [bySound, setBySound] = useState(false);
  const [category, setCategory] = useState<string>();
  const [loops, setLoops] = useState<boolean | undefined>();
  const songKey = useEngine((s) => s.doc?.key);
  const [inKey, setInKey] = useState(false);
  const fitsKey = inKey && !!songKey;
  const [chosenStyle, setChosenStyle] = useState(loadStyle);
  const [like, setLike] = useState<Sample>();
  const [results, setResults] = useState<Sample[]>([]);
  const [size, setSize] = useState<number>();
  const [cursor, setCursor] = useState(0);
  const [version, setVersion] = useState(0);
  const { playing, preview } = usePreview();
  const [autoPlay, toggleAutoPlay] = useAutoPlay();
  const resultsBox = useRef<HTMLDivElement>(null);

  // The arrow keys move through the results (from the search field or the list), playing each
  // sample they land on while auto-play is on, and keep it in view.
  const moveResult = (i: number) => {
    const next = Math.max(0, Math.min(results.length - 1, i));
    setCursor(next);
    if (autoPlay && next !== cursor && results[next]) preview(results[next]);
    (resultsBox.current?.children[next] as HTMLElement | undefined)?.scrollIntoView({ block: "nearest" });
  };

  // Ranking by sound needs the models and a heard library; a removed style no longer applies.
  const style = listening && chosenStyle && styles.some((s) => s.name === chosenStyle) ? chosenStyle : undefined;
  const soundQuery = listening && bySound;
  const likePath = listening ? like?.path : undefined;
  // With nothing to search for, the library shows as its folders.
  const filtering = query.trim() !== "" || category !== undefined || loops !== undefined || !!style || !!likePath || fitsKey;

  const chooseStyle = (name?: string) => {
    setChosenStyle(name);
    try {
      if (name) localStorage.setItem(STYLE_KEY, name);
      else localStorage.removeItem(STYLE_KEY);
    } catch {
      // Storage can be unavailable; the choice still holds for now.
    }
  };

  useEffect(() => {
    if (!connected) return;
    const timer = setTimeout(() => {
      const words = query.trim();
      engine.call<{ results: Sample[]; librarySize: number }>("library.search", {
        limit: 200,
        ...(soundQuery ? (words ? { sound: words } : {}) : { query }),
        ...(category ? { category } : {}),
        ...(loops !== undefined ? { loop: loops } : {}),
        ...(style ? { style } : {}),
        ...(likePath ? { like: likePath } : {}),
        ...(fitsKey ? { fitsKey: "song" } : {}),
      }).then((r) => {
        setResults(r.results);
        setSize(r.librarySize);
        setCursor(0);
      }, () => {});
    }, soundQuery ? 350 : 150);
    return () => clearTimeout(timer);
  }, [connected, query, soundQuery, category, loops, style, likePath, fitsKey, songKey, version, progress?.running, status?.index.running]);

  const rate = (s: Sample, fits: boolean | null) => {
    if (style) void rateSample(s.path, style, fits).then(() => setVersion((v) => v + 1));
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex flex-col gap-2 border-b border-line p-2">
        <div className="flex h-7 items-center gap-1.5 rounded-[4px] bg-bg px-2">
          <button type="button" disabled={!listening} onClick={() => setBySound(!bySound)}
            title={!listening ? "Searching by sound needs GrooveKit to listen first (below)" : soundQuery ? "Searching by how samples sound: describe it. Click to search names." : "Searching names. Click to describe how it should sound."}
            className={cn("shrink-0", soundQuery ? "text-agent" : "text-faint hover:text-fg disabled:hover:text-faint")}>
            {soundQuery ? <AudioLines size={12} /> : <Search size={12} />}
          </button>
          <input
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === "ArrowDown" || e.key === "ArrowUp") {
                e.preventDefault();
                moveResult(cursor + (e.key === "ArrowDown" ? 1 : -1));
              }
              if (e.key === "Enter" && results[cursor]) preview(results[cursor]);
            }}
            placeholder={soundQuery ? "Describe a sound: dusty warm chord stab" : size ? `Search ${size.toLocaleString()} samples` : "Search samples"}
            className="min-w-0 flex-1 bg-transparent text-sm text-fg outline-none placeholder:text-faint"
          />
          {query && <button type="button" onClick={() => setQuery("")} className="text-faint hover:text-fg"><X size={12} /></button>}
          <button type="button" onClick={toggleAutoPlay}
            title={autoPlay ? "Auto-play is on: the arrow keys play each sample they land on. Click to turn it off." : "Auto-play: play each sample as the arrow keys land on it"}
            className={cn("shrink-0", autoPlay ? "text-accent" : "text-faint hover:text-fg")}>
            <Headphones size={12} />
          </button>
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
          {songKey && (
            <button type="button" onClick={() => setInKey(!inKey)}
              title={`Only samples that go with ${songKey} (the same notes, a fifth away, or a note in its scale), with the semitones that put each exactly in key`}
              className={cn("h-5 rounded-[3px] px-1.5 text-2xs", fitsKey ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}>
              in {shortKey(songKey)}
            </button>
          )}
        </div>
        {listening && (
          <div className="flex min-w-0 items-center gap-1">
            <StylePicker style={style} styles={styles.map((s) => s.name)} onChange={chooseStyle} />
            {like && (
              <span className="flex h-5 min-w-0 items-center gap-1 rounded-[3px] bg-control pl-1.5 pr-1 text-2xs text-dim" title={like.path}>
                <Sparkles size={9} className="shrink-0 text-agent" />
                <span className="min-w-0 truncate">like {fileName(like.path)}</span>
                <button type="button" title="Stop finding sounds like it" onClick={() => setLike(undefined)} className="shrink-0 text-faint hover:text-fg"><X size={10} /></button>
              </span>
            )}
          </div>
        )}
      </div>

      {!filtering ? (
        <FolderTree playing={playing} preview={preview} autoPlay={autoPlay} />
      ) : (
        <div ref={resultsBox} tabIndex={0} className="min-h-0 flex-1 overflow-y-auto py-1 outline-none"
          onKeyDown={(e) => {
            if (e.key !== "ArrowDown" && e.key !== "ArrowUp" && e.key !== "Enter") return;
            e.preventDefault();
            e.stopPropagation();
            if (e.key === "Enter") {
              if (results[cursor]) preview(results[cursor]);
              return;
            }
            moveResult(cursor + (e.key === "ArrowDown" ? 1 : -1));
          }}>
          {results.length === 0 ? (
            <Empty>{size === 0 ? "The library is empty. Add a folder below." : "No samples match."}</Empty>
          ) : results.map((s, i) => (
            <SampleRow key={s.path} sample={s} selected={i === cursor} playing={playing === s.path}
              style={style} onRate={(fits) => rate(s, fits)} onSimilar={listening ? () => setLike(s) : undefined}
              onClick={() => {
                setCursor(i);
                preview(s);
                resultsBox.current?.focus();
              }} />
          ))}
        </div>
      )}

      <NowPreviewing />
      <Listening status={status} />
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
function FolderTree({ playing, preview, autoPlay }: { playing?: string; preview: (s: Sample) => void; autoPlay: boolean }) {
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
    const row = rows[index];
    if (autoPlay && index !== cursor && row?.kind === "sample") preview(row.sample);
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
                    box.current?.focus();
                  }} />
              );
            }
            const open = expanded.has(row.path);
            return (
              <div key={row.path} title={row.path}
                onClick={() => {
                  setCursor(i);
                  toggle(row.path);
                  box.current?.focus();
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

/** Ranks the browser by fit to one of the user's styles, or adds one: the folders in the
    references folder GrooveKit hasn't heard (or that changed) first, or any folder. */
function StylePicker({ style, styles, onChange }: { style?: string; styles: string[]; onChange: (name?: string) => void }) {
  const { references, status } = useListening();
  const running = !!status?.styles.running;
  const toHear = references?.folders.filter(needsHearing) ?? [];

  const add = () => {
    if (!inDesktopApp) return;
    void pickFolder("Choose a folder of reference tracks", references?.exists ? references.folder : undefined).then((path) => {
      if (path) void addStyle(path).then((r) => onChange(r.name));
    });
  };

  return (
    <Dropdown onOpenChange={(open) => open && void refreshReferences().catch(() => {})}>
      <DropdownTrigger asChild>
        <button type="button" title="Rank samples by how well they fit a style: a folder of reference tracks GrooveKit has listened to"
          className={cn("flex h-5 shrink-0 items-center gap-1 rounded-[3px] px-1.5 text-2xs", style ? "bg-agent text-black" : "bg-control text-dim hover:text-fg")}>
          <AudioLines size={9} /> {style ?? "Style"} <ChevronDown size={9} />
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Rank by fit to</DropdownLabel>
        {styles.map((name) => (
          <DropdownItem key={name} checked={name === style} onSelect={() => onChange(name)}>{name}</DropdownItem>
        ))}
        {styles.length === 0 && <DropdownItem disabled>No styles yet</DropdownItem>}
        {style && <DropdownItem onSelect={() => onChange(undefined)}>No style</DropdownItem>}
        {toHear.length > 0 && (
          <>
            <DropdownSeparator />
            <DropdownLabel>In your references folder</DropdownLabel>
            {toHear.map((f) => (
              <DropdownItem key={f.path} disabled={running} onSelect={() => void addStyle(f.path)}>
                {hearLabel(f)}
              </DropdownItem>
            ))}
          </>
        )}
        <DropdownSeparator />
        <DropdownItem disabled={!inDesktopApp} onSelect={add}>Add a style from a folder of tracks…</DropdownItem>
      </DropdownContent>
    </Dropdown>
  );
}

/** What hearing a references folder would do. */
function hearLabel(f: ReferenceFolder) {
  if (!f.style) return `Hear ${f.name} (${f.files} track${f.files === 1 ? "" : "s"})`;
  const changes = [f.unheard && `${f.unheard} new`, f.gone && `${f.gone} gone`].filter(Boolean).join(", ");
  return `Update ${f.name} (${changes})`;
}

const megabytes = (bytes: number) => `${Math.round(bytes / 1_000_000)} MB`;

/** Letting GrooveKit hear the library and the user's reference tracks: the one-time download,
    then how much has been heard, and the styles with their analysis. */
function Listening({ status }: { status?: ListeningStatus }) {
  const { styles, references } = useListeningStatus();
  if (!status) return null;
  const { models, index, styles: analysis } = status;
  const toHear = references?.folders.filter(needsHearing) ?? [];

  const addFolder = () => {
    if (inDesktopApp)
      void pickFolder("Choose a folder of reference tracks", references?.exists ? references.folder : undefined).then((path) => path && void addStyle(path));
  };

  return (
    <div className="border-t border-line p-2 text-2xs text-dim">
      <div className="mb-1 flex items-center gap-1">
        <span className="flex-1 uppercase tracking-wide text-faint">Listening</span>
        {models.ready && (
          <IconButton size="sm" title="Add a style: a folder of reference tracks" disabled={analysis.running || !inDesktopApp} onClick={addFolder}>
            <FolderPlus size={12} />
          </IconButton>
        )}
      </div>

      {!models.ready && !models.downloading && (
        <div className="flex flex-col gap-1.5">
          <span>Let GrooveKit hear your samples and the tracks you love, so it can pick sounds that fit. A one-time {megabytes(models.totalBytes)} download; everything stays on this Mac.</span>
          {models.error && <span className="text-rec">{models.error}</span>}
          <Button size="sm" className="self-start" onClick={() => void downloadModels()}>Download</Button>
        </div>
      )}

      {models.downloading && (
        <div className="flex flex-col gap-1">
          <span>Downloading… {Math.round(models.progress * 100)}% of {megabytes(models.totalBytes)}</span>
          <div className="h-1 overflow-hidden rounded-full bg-bg"><div className="h-full bg-agent transition-[width]" style={{ width: `${models.progress * 100}%` }} /></div>
        </div>
      )}

      {models.ready && index.running && (
        <div className="mb-1 flex flex-col gap-1">
          <span>Listening to your library… {index.embedded.toLocaleString()} of {index.total.toLocaleString()}</span>
          <div className="h-1 overflow-hidden rounded-full bg-bg"><div className="h-full bg-agent transition-[width]" style={{ width: `${(index.embedded / Math.max(1, index.total)) * 100}%` }} /></div>
        </div>
      )}

      {models.ready && styles.map((s) => (
        <div key={s.name} className="group flex items-center gap-1" title={s.folder}>
          <Check size={10} className="shrink-0 text-ok" />
          <span className="min-w-0 flex-1 truncate text-fg">{s.name}</span>
          <span className="shrink-0 text-faint tabular">{s.tracks} tracks{s.tempo ? `, ${Math.round(s.tempo.min)}–${Math.round(s.tempo.max)} BPM` : ""}</span>
          <button type="button" className="text-faint opacity-0 group-hover:opacity-100 hover:text-rec" title="Remove style"
            onClick={() => void removeStyle(s.name)}><X size={10} /></button>
        </div>
      ))}

      {models.ready && toHear.map((f) => (
        <div key={f.path} className="flex items-center gap-1" title={f.path}>
          <FolderPlus size={10} className="shrink-0 text-agent" />
          <span className="min-w-0 flex-1 truncate text-fg">{f.name}</span>
          <span className="shrink-0 text-faint tabular">
            {f.style ? [f.unheard && `${f.unheard} new`, f.gone && `${f.gone} gone`].filter(Boolean).join(", ") : `${f.files} tracks, not heard yet`}
          </span>
          <button type="button" className="text-agent hover:text-fg disabled:opacity-40" disabled={analysis.running}
            onClick={() => void addStyle(f.path)}>Hear</button>
        </div>
      ))}

      {analysis.running && (
        <div className="mt-1 flex flex-col gap-1">
          <span>Listening to {analysis.style}… {analysis.analysed ?? 0} of {analysis.tracks ?? "?"} tracks</span>
          <div className="h-1 overflow-hidden rounded-full bg-bg"><div className="h-full bg-agent transition-[width]" style={{ width: `${((analysis.analysed ?? 0) / Math.max(1, analysis.tracks ?? 1)) * 100}%` }} /></div>
        </div>
      )}
      {analysis.error && !analysis.running && <div className="mt-1 text-rec">{analysis.error}</div>}
      {models.ready && styles.length === 0 && !analysis.running && (
        <div className="text-faint">Add a folder of tracks you love as a style; GrooveKit learns how its kicks, hats, bass and music sound.</div>
      )}
    </div>
  );
}
