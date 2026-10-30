import { AudioLines, Headphones, Repeat, Search, Sparkles, Square, X } from "lucide-react";
import { useCallback, useEffect, useRef, useState } from "react";
import { Empty, IconButton } from "@/components/controls";

import { cn } from "@/lib/cn";
import { fileName } from "@/lib/model";
import { previewSample, stopPreview } from "@/features/shell/actions";

import { engine, useEngine } from "@/state/engine";

import { canListen, rateSample, useListeningStatus } from "./listening";
import { BrowserTree } from "./BrowserTree";
import { LibraryFolders } from "./LibraryFolders";
import { Listening } from "./ListeningPanel";
import { Sample, SampleRow, shortKey } from "./SampleRow";
import { StylePicker } from "./StylePicker";

export const categories = ["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums", "bass", "synth", "vocal", "fx"];

/** Auditions one sample at a time; Space and the stop button stop it too. */
export function usePreview() {
  const playing = useEngine((s) => s.previewing);
  const preview = useCallback((s: Sample) => void previewSample(s.path, s.durationSeconds), []);
  return { playing, preview };
}

/** What's being auditioned, with a way to stop it. */
export function NowPreviewing() {
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

export const STYLE_KEY = "groovekit.browser.style";
export const AUTOPLAY_KEY = "groovekit.browser.autoplay";

/** Whether moving through samples with the arrow keys plays each one, remembered between sessions. */
export function useAutoPlay() {
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

export function loadStyle() {
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
        <BrowserTree playing={playing} preview={preview} autoPlay={autoPlay} />
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
