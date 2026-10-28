import { Check, ChevronDown, ChevronRight, Ellipsis, Minus, Pause, Play, Search, X } from "lucide-react";
import { useEffect, useMemo, useState } from "react";
import { IconButton, Tabs } from "@/components/controls";
import {
  ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuTrigger,
  Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger,
} from "@/components/menu";
import { inDesktopApp } from "@/engine/client";
import { cn } from "@/lib/cn";
import { pickFile, pickSavePath } from "@/lib/files";
import { useEngine } from "@/state/engine";
import { openWindow } from "@/state/windows";
import {
  clearQueue, duration, exportMaps, forget, hear, hearNew, importMaps, needsHearing, pauseHearing, refreshMaps, removeReferences,
  resumeHearing, setInUse, unqueue, unskip, useAgentView, useMaps, type Song,
} from "./maps";

/** The reference tracks GrooveKit has heard into maps: the songs by style, with which are in use,
    and the hearing queue. */
export function MapsView() {
  const view = useAgentView((s) => s.maps);
  const connected = useEngine((s) => s.status === "connected");
  const left = useMaps((s) => (s.hearing?.queued.length ?? 0) + (s.hearing?.current ? 1 : 0));

  useEffect(() => {
    if (connected) void refreshMaps().catch(() => {});
  }, [connected]);

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 items-center px-2 pt-1.5">
        <Tabs<"songs" | "processing"> value={view} onChange={(maps) => useAgentView.setState({ maps })}
          items={[{ value: "songs", label: "Songs", title: "Every reference track, by style" },
            { value: "processing", label: <span className="flex items-center gap-1">Processing{left > 0 && <span className="tabular text-agent">{left}</span>}</span>,
              title: "The songs being heard" }]} />
      </div>
      <div className="min-h-0 flex-1">{view === "songs" ? <Songs /> : <Processing />}</div>
    </div>
  );
}

//==============================================================================
function Songs() {
  const { songs, loaded, references, error } = useMaps();
  const [filter, setFilter] = useState("");
  const [closed, setClosed] = useState<Set<string>>(() => new Set(readClosed()));
  const [message, setMessage] = useState<string>();

  const shown = useMemo(() => {
    const words = filter.trim().toLowerCase();
    return words ? songs.filter((s) => `${s.style} ${s.title}`.toLowerCase().includes(words)) : songs;
  }, [songs, filter]);

  const styles = useMemo(() => {
    const groups = new Map<string, Song[]>();
    for (const s of shown) groups.set(s.style, [...(groups.get(s.style) ?? []), s]);
    return [...groups];
  }, [shown]);

  const ticked = songs.filter((s) => s.inUse);
  const toHear = songs.filter(needsHearing);
  const mapped = songs.filter((s) => s.map);

  useEffect(() => {
    if (!message) return;
    const t = setTimeout(() => setMessage(undefined), 6000);
    return () => clearTimeout(t);
  }, [message]);

  const toggleStyle = (style: string) => setClosed((c) => {
    const next = new Set(c);
    if (!next.delete(style)) next.add(style);
    try { localStorage.setItem(CLOSED, JSON.stringify([...next])); } catch { /* remembered for this session only */ }
    return next;
  });

  const exportTicked = () => {
    const ids = ticked.filter((s) => s.map).map((s) => s.id);
    const styleNames = [...new Set(ticked.filter((s) => s.map).map((s) => s.style))];
    const name = `${styleNames.length === 1 ? styleNames[0] : "GrooveKit"} maps.zip`;
    const folder = references?.replace(/\/[^/]*$/, "");
    void pickSavePath("Export song maps", folder ? `${folder}/${name}` : name, ["zip"]).then((path) => {
      if (path) void exportMaps(ids, path).then((r) => setMessage(`Exported ${r.count} map${r.count === 1 ? "" : "s"} to ${r.path.replace(/^.*\//, "")}`), () => {});
    });
  };

  const importZip = () => {
    void pickFile("Import song maps", ["zip"]).then((path) => {
      if (path) void importMaps(path).then((r) =>
        setMessage(`Added ${r.added} map${r.added === 1 ? "" : "s"}${r.kept ? `; kept your own for ${r.kept}` : ""}`), () => {});
    });
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 items-center gap-1 px-2 py-1.5">
        <div className="flex h-6 min-w-0 flex-1 items-center gap-1.5 rounded-[4px] bg-bg px-1.5 ring-1 ring-line focus-within:ring-agent/50">
          <Search size={11} className="shrink-0 text-faint" />
          <input value={filter} onChange={(e) => setFilter(e.target.value)} placeholder="Find a song" spellCheck={false}
            onKeyDown={(e) => e.stopPropagation()}
            className="min-w-0 flex-1 bg-transparent text-xs text-fg outline-none placeholder:text-faint" />
          {filter && <button type="button" onClick={() => setFilter("")} className="text-faint hover:text-fg"><X size={11} /></button>}
        </div>
        <Dropdown>
          <DropdownTrigger asChild>
            <IconButton size="sm" title="Tick, hear, export and import"><Ellipsis size={13} /></IconButton>
          </DropdownTrigger>
          <DropdownContent align="end">
            <DropdownItem onSelect={() => void setInUse(shown.map((s) => s.id), true)}>Tick {filter ? "these" : "all"}</DropdownItem>
            <DropdownItem onSelect={() => void setInUse(shown.map((s) => s.id), false)}>Untick {filter ? "these" : "all"}</DropdownItem>
            <DropdownSeparator />
            <DropdownItem disabled={!ticked.some((s) => s.file)} onSelect={() => void hear(ticked.filter((s) => s.file).map((s) => s.id))}>
              Hear the ticked songs ({ticked.filter((s) => s.file).length})
            </DropdownItem>
            <DropdownItem disabled={!toHear.length} onSelect={() => void hearNew()}>Hear new and changed songs ({toHear.length})</DropdownItem>
            <DropdownSeparator />
            <DropdownItem disabled={!inDesktopApp || !ticked.some((s) => s.map)} onSelect={exportTicked}>
              Export the ticked maps ({ticked.filter((s) => s.map).length})…
            </DropdownItem>
            <DropdownItem disabled={!inDesktopApp} onSelect={importZip}>Import maps…</DropdownItem>
          </DropdownContent>
        </Dropdown>
      </div>

      <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-1 pb-2">
        {error && <p className="px-2 py-2 text-xs leading-relaxed text-rec">{error}</p>}
        {loaded && !error && songs.length === 0 && (
          <p className="px-2 py-2 text-xs leading-relaxed text-dim">
            No reference tracks yet. Put tracks you love in {references ?? "the references folder"}, one folder per style; GrooveKit hears each
            once into a map, and learns the style from the maps you tick.
          </p>
        )}
        {styles.map(([style, list]) => (
          <StyleGroup key={style} style={style} songs={list} open={!closed.has(style) || !!filter} onToggle={() => toggleStyle(style)} />
        ))}
      </div>

      <div className="shrink-0 border-t border-line px-2.5 py-1.5 text-2xs text-dim">
        {message ?? (
          <span className="tabular">
            {mapped.filter((s) => s.inUse).length} of {mapped.length} maps in use
            {toHear.length > 0 && <> · <button type="button" className="text-agent hover:text-fg" onClick={() => void hearNew()}>hear {toHear.length} new</button></>}
          </span>
        )}
      </div>
    </div>
  );
}

const CLOSED = "groovekit.mapsClosedStyles";

function readClosed(): string[] {
  try {
    return JSON.parse(localStorage.getItem(CLOSED) ?? "[]");
  } catch {
    return [];
  }
}

function StyleGroup({ style, songs, open, onToggle }: { style: string; songs: Song[]; open: boolean; onToggle: () => void }) {
  const inUse = songs.filter((s) => s.inUse).length;
  const tick = inUse === songs.length ? true : inUse === 0 ? false : "some";
  const heard = songs.filter((s) => s.map).length;
  const toHear = songs.filter(needsHearing).length;

  return (
    <div className="mb-1">
      <div role="button" tabIndex={0} onClick={onToggle} onKeyDown={(e) => e.key === "Enter" && onToggle()}
        className="group flex h-7 cursor-pointer items-center gap-1.5 rounded-[4px] px-1.5 hover:bg-hover">
        {open ? <ChevronDown size={12} className="shrink-0 text-faint" /> : <ChevronRight size={12} className="shrink-0 text-faint" />}
        <Tick state={tick} title={tick === true ? "Untick the whole style" : "Tick the whole style"}
          onChange={(on) => void setInUse(songs.map((s) => s.id), on)} />
        <span className="min-w-0 flex-1 truncate text-xs text-fg">{style}</span>
        <span className="shrink-0 text-2xs text-faint tabular">{heard}/{songs.length} heard</span>
        {toHear > 0 && (
          <button type="button" title={`Hear the ${toHear} new or changed song${toHear === 1 ? "" : "s"}`}
            onClick={(e) => {
              e.stopPropagation();
              void hearNew(style);
            }}
            className="shrink-0 rounded-[3px] px-1 text-2xs text-agent hover:bg-control hover:text-fg">Hear {toHear}</button>
        )}
      </div>
      {open && songs.map((s) => <SongRow key={s.id} song={s} />)}
    </div>
  );
}

const STATUS: Record<Song["status"], { label: string; className: string; title: string }> = {
  heard: { label: "", className: "", title: "Heard" },
  older: { label: "older", className: "text-accent", title: "Heard by an older version of the hearing; hear it again for what's new" },
  changed: { label: "changed", className: "text-accent", title: "The file changed since it was heard" },
  new: { label: "new", className: "text-dim", title: "Not heard yet" },
  queued: { label: "queued", className: "text-faint", title: "Waiting to be heard" },
  hearing: { label: "hearing", className: "text-agent", title: "Being heard now" },
  failed: { label: "failed", className: "text-rec", title: "Couldn't be heard" },
  skipped: { label: "skipped", className: "text-faint", title: "Forgotten: not heard until you ask for it" },
  imported: { label: "imported", className: "text-agent/80", title: "A map someone shared; the audio isn't on this Mac" },
  gone: { label: "no audio", className: "text-faint", title: "Heard here, but the audio is no longer in the references folder" },
};

function SongRow({ song: s }: { song: Song }) {
  const status = STATUS[s.status];
  const open = () => s.map && openWindow({ kind: "map", id: s.id });

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div role="button" tabIndex={0} onClick={open} onKeyDown={(e) => e.key === "Enter" && open()}
          title={[s.title, s.error ?? status.title].join("\n")}
          className={cn("flex h-6 items-center gap-1.5 rounded-[4px] pl-6 pr-1.5", s.map ? "cursor-pointer hover:bg-hover" : "cursor-default")}>
          <Tick state={s.inUse} title={s.inUse ? "In use: the style learns from it. Click to leave it out" : "Not in use. Click to use it"}
            onChange={(on) => void setInUse([s.id], on)} />
          <span className={cn("min-w-0 flex-1 truncate text-xs", s.inUse ? "text-fg" : "text-faint", s.status === "skipped" && "line-through")}>{s.title}</span>
          {status.label && <span className={cn("shrink-0 text-2xs", status.className, s.status === "hearing" && "animate-pulse")}>{status.label}</span>}
          {s.map && s.map.tempo > 0 && <span className="w-12 shrink-0 text-right text-2xs text-faint tabular">{Math.round(s.map.tempo)} BPM</span>}
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem disabled={!s.map} onSelect={open}>Open map</ContextMenuItem>
        <ContextMenuItem onSelect={() => void setInUse([s.id], !s.inUse)}>{s.inUse ? "Untick (leave out)" : "Tick (use)"}</ContextMenuItem>
        <ContextMenuSeparator />
        {s.status === "queued" ? (
          <ContextMenuItem onSelect={() => void unqueue([s.id])}>Take out of the queue</ContextMenuItem>
        ) : (
          <ContextMenuItem disabled={!s.file || s.status === "hearing"} onSelect={() => void hear([s.id])}>{s.map ? "Hear again" : "Hear"}</ContextMenuItem>
        )}
        {s.status === "skipped" ? (
          <ContextMenuItem onSelect={() => void unskip([s.id])}>Don't skip</ContextMenuItem>
        ) : (
          <ContextMenuItem disabled={s.status === "hearing"} onSelect={() => void forget([s.id])}>
            Forget{s.map ? " (delete the map)" : ""}
          </ContextMenuItem>
        )}
        <ContextMenuSeparator />
        <ContextMenuItem danger disabled={!s.file || s.status === "hearing"} onSelect={() => void removeReferences([s.id])}>
          Move out of the references
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}

function Tick({ state, onChange, title }: { state: boolean | "some"; onChange: (on: boolean) => void; title?: string }) {
  return (
    <button type="button" role="checkbox" aria-checked={state === "some" ? "mixed" : state} title={title}
      onClick={(e) => {
        e.stopPropagation();
        onChange(state !== true);
      }}
      className={cn("flex h-3.5 w-3.5 shrink-0 items-center justify-center rounded-[3px] ring-1 transition-colors",
        state ? "bg-agent text-black ring-agent" : "ring-faint hover:ring-dim")}>
      {state === true ? <Check size={10} strokeWidth={3} /> : state === "some" ? <Minus size={10} strokeWidth={3} /> : null}
    </button>
  );
}

//==============================================================================
/** The stretches hearTrack hears in each song. */
const STRETCHES = 6;

function Processing() {
  const hearing = useMaps((s) => s.hearing);
  const toHear = useMaps((s) => s.songs.filter(needsHearing).length);
  const [, tick] = useState(0);

  // The current song's time, every second.
  useEffect(() => {
    if (!hearing?.running) return;
    const t = setInterval(() => tick((n) => n + 1), 1000);
    return () => clearInterval(t);
  }, [hearing?.running]);

  if (!hearing) return <div className="p-3 text-xs text-faint">…</div>;

  const { current, queued, recent, paused, running, ready, secondsPerSong } = hearing;
  const left = queued.length + (current ? 1 : 0);
  const eta = secondsPerSong > 0 ? secondsPerSong * (queued.length + (current ? 1 - current.progress : 0)) : 0;
  const state = paused ? "Paused" : running ? "Hearing" : left > 0 ? (ready ? "Starting…" : "Waiting for the listening models") : "Nothing to hear";

  return (
    <div className="flex h-full flex-col">
      <div className="shrink-0 px-2 pb-2 pt-1.5">
        <div className="rounded-[8px] bg-raised px-2.5 py-2 ring-1 ring-line">
          <div className="flex items-center gap-1.5">
            <span className={cn("h-1.5 w-1.5 shrink-0 rounded-full", running ? "animate-pulse bg-agent" : paused ? "bg-accent" : "bg-faint")} />
            <span className="flex-1 text-xs text-fg">{state}</span>
            {left > 0 && (
              paused || !running ? (
                <IconButton size="sm" title="Carry on hearing" disabled={!ready} onClick={() => void resumeHearing()}><Play size={12} /></IconButton>
              ) : (
                <IconButton size="sm" title="Pause after this stretch; the song starts over when you carry on" onClick={() => void pauseHearing()}><Pause size={12} /></IconButton>
              )
            )}
            {queued.length > 0 && (
              <button type="button" onClick={() => void clearQueue()} className="h-5 rounded-[3px] px-1.5 text-2xs text-dim hover:bg-control hover:text-fg">Clear queue</button>
            )}
          </div>
          {left > 0 && (
            <div className="mt-0.5 pl-3 text-2xs text-dim tabular">
              {left} song{left === 1 ? "" : "s"} left{eta > 0 && ` · about ${duration(eta)}`}
              {!ready && " · download them in the Browser's Listening section"}
            </div>
          )}
          {left === 0 && toHear > 0 && (
            <button type="button" onClick={() => void hearNew()} className="mt-1 pl-3 text-2xs text-agent hover:text-fg">
              Hear {toHear} new or changed song{toHear === 1 ? "" : "s"}
            </button>
          )}

          {current && (
            <div className="mt-2 rounded-[6px] bg-bg px-2 py-1.5 ring-1 ring-line">
              <div className="flex items-baseline gap-1.5">
                <span className="min-w-0 flex-1 truncate text-xs text-fg" title={current.title}>{current.title}</span>
                <span className="shrink-0 text-2xs text-faint tabular">{duration((Date.now() - current.started) / 1000)}</span>
              </div>
              <div className="text-2xs text-faint">{current.style}</div>
              <div className="mt-1.5 flex gap-0.5">
                {Array.from({ length: STRETCHES }, (_, i) => {
                  const done = current.progress * STRETCHES;
                  return (
                    <div key={i} className="h-1 flex-1 overflow-hidden rounded-full bg-control">
                      <div className={cn("h-full bg-agent", i >= Math.floor(done) && i < Math.ceil(done + 0.001) && running && "animate-pulse")}
                        style={{ width: `${Math.min(1, Math.max(0, done - i)) * 100}%` }} />
                    </div>
                  );
                })}
              </div>
              <div className="mt-1 text-2xs text-dim tabular">
                Stretch {Math.min(STRETCHES, Math.floor(current.progress * STRETCHES) + 1)} of {STRETCHES}: split into drums, bass and music, then listened to
              </div>
            </div>
          )}
        </div>
      </div>

      <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-1 pb-2">
        {queued.length > 0 && (
          <>
            <div className="px-2 pb-1 text-2xs uppercase tracking-wide text-faint">Next</div>
            {queued.map((item, i) => (
              <div key={item.id} className="group flex h-6 items-center gap-1.5 rounded-[4px] px-2 hover:bg-hover" title={`${item.style} / ${item.title}`}>
                <span className="w-5 shrink-0 text-right text-2xs text-faint tabular">{i + 1}</span>
                <span className="min-w-0 flex-1 truncate text-xs text-dim">{item.title}</span>
                <span className="max-w-[35%] shrink-0 truncate text-2xs text-faint">{item.style}</span>
                <button type="button" title="Take out of the queue" onClick={() => void unqueue([item.id])}
                  className="shrink-0 text-faint opacity-0 hover:text-fg group-hover:opacity-100"><X size={11} /></button>
              </div>
            ))}
          </>
        )}
        {recent.length > 0 && (
          <>
            <div className="mt-2 px-2 pb-1 text-2xs uppercase tracking-wide text-faint">Done</div>
            {recent.map((item) => (
              <div key={`${item.id}@${item.at}`} role="button" tabIndex={0}
                onClick={() => item.ok && openWindow({ kind: "map", id: item.id })}
                className={cn("flex h-6 items-center gap-1.5 rounded-[4px] px-2", item.ok && "cursor-pointer hover:bg-hover")}
                title={item.error ?? `${item.style} / ${item.title}`}>
                {item.ok ? <Check size={11} className="shrink-0 text-ok" /> : <X size={11} className="shrink-0 text-rec" />}
                <span className="min-w-0 flex-1 truncate text-xs text-dim">{item.title}</span>
                <span className={cn("shrink-0 truncate text-2xs tabular", item.ok ? "text-faint" : "max-w-[45%] text-rec")}>
                  {item.ok ? duration(item.seconds) : item.error}
                </span>
              </div>
            ))}
          </>
        )}
        {queued.length === 0 && recent.length === 0 && !current && (
          <p className="px-2 py-1 text-xs leading-relaxed text-dim">
            Each song is heard once: split into drums, bass and music, then listened to part by part. It takes half a minute or so a song,
            and carries on after a restart.
          </p>
        )}
      </div>
    </div>
  );
}
