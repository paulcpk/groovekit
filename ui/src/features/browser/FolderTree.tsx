import { ChevronRight, Folder, FolderOpen } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Empty } from "@/components/controls";

import { cn } from "@/lib/cn";

import { engine, useEngine } from "@/state/engine";

import { ROW_H, Sample, SampleRow } from "./SampleRow";

export interface Listing {
  folders: { path: string; name: string; samples: number }[];
  samples: Sample[];
}

export type Row =
  | { kind: "folder"; path: string; name: string; samples: number; depth: number; parent?: string }
  | { kind: "sample"; sample: Sample; depth: number; parent: string };

export const EXPANDED_KEY = "groovekit.browser.expanded";

export function loadExpanded(): Set<string> {
  try {
    return new Set(JSON.parse(localStorage.getItem(EXPANDED_KEY) ?? "[]"));
  } catch {
    return new Set();
  }
}

/** The library as the folders it's kept in, opened one level at a time. */
export function FolderTree({ playing, preview, autoPlay }: { playing?: string; preview: (s: Sample) => void; autoPlay: boolean }) {
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
