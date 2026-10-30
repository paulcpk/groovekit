import { ChevronRight, Folder, FolderOpen, FolderOpenDot, Music, SlidersHorizontal, Waves } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { StarterSamples } from "./StarterSamples";
import { OWN_PLUGINS, ownPlugins, type PluginInfo } from "@/features/rack/PluginPicker";
import { openProject } from "@/features/shell/ProjectDialog";
import { cn } from "@/lib/cn";
import { effectNames } from "@/lib/catalogue";
import { uniqueId } from "@/lib/model";
import { engine, execute, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { ROW_H, Sample, SampleRow } from "./SampleRow";

export interface Listing {
  folders: { path: string; name: string; samples: number }[];
  samples: Sample[];
}

interface ProjectEntry { path: string; name: string; modified: number }
interface EffectPreset { name: string; params: Record<string, number>; mix?: number }

/** One line of the tree. Keys name what's expanded; sample folders use their path. */
export type Row =
  | { kind: "root"; key: "projects" | "presets" | "samples"; name: string; depth: 0 }
  | { kind: "project"; project: ProjectEntry; current: boolean; depth: number }
  | { kind: "plugin"; plugin: PluginInfo; key: string; depth: number }
  | { kind: "category"; key: string; name: string; depth: number }
  | { kind: "pluginPreset"; plugin: PluginInfo; preset: string; depth: number }
  | { kind: "effectKind"; key: string; effect: string; name: string; depth: number }
  | { kind: "effectPreset"; effect: string; preset: EffectPreset; user: boolean; depth: number }
  | { kind: "note"; text: string; depth: number }
  | { kind: "folder"; path: string; name: string; samples: number; depth: number; parent?: string }
  | { kind: "sample"; sample: Sample; depth: number; parent: string };

export const EXPANDED_KEY = "groovekit.browser.expanded";

export function loadExpanded(): Set<string> {
  try {
    const saved = new Set(JSON.parse(localStorage.getItem(EXPANDED_KEY) ?? "[]") as string[]);
    // Saved before the tree had its levels: the samples start open, as they did.
    if (!["projects", "presets", "samples"].some((k) => saved.has(k))) saved.add("samples");
    return saved;
  } catch {
    return new Set(["samples"]);
  }
}

const presetName = (full: string) => (full.includes(": ") ? full.slice(full.indexOf(": ") + 2) : full);
const presetCategory = (full: string) => (full.includes(": ") ? full.slice(0, full.indexOf(": ")) : "Sounds");

/** Projects, presets and the sample library as one tree, each a level of its own: projects open,
    presets go into the song (a synth's as a new channel, an effect's onto the selected insert),
    samples play. Folders open one level at a time. */
export function BrowserTree({ playing, preview, autoPlay }: { playing?: string; preview: (s: Sample) => void; autoPlay: boolean }) {
  const connected = useEngine((s) => s.status === "connected");
  const scanning = useEngine((s) => s.libraryProgress?.running);
  const project = useEngine((s) => s.project);
  const [listings, setListings] = useState<Map<string, Listing>>(new Map());
  const [projects, setProjects] = useState<ProjectEntry[]>();
  const [plugins, setPlugins] = useState<PluginInfo[]>();
  const [pluginPresets, setPluginPresets] = useState<Map<string, string[]>>(new Map());
  const [effectPresets, setEffectPresets] = useState<Map<string, { factory: EffectPreset[]; user: EffectPreset[] }>>(new Map());
  const [expanded, setExpanded] = useState(loadExpanded);
  const [cursor, setCursor] = useState(0);
  const [scrollTop, setScrollTop] = useState(0);
  const [height, setHeight] = useState(400);
  const box = useRef<HTMLDivElement>(null);
  const loading = useRef(new Set<string>());

  const once = useCallback((key: string, run: () => Promise<void>) => {
    if (loading.current.has(key)) return;
    loading.current.add(key);
    run().catch(() => {}).finally(() => loading.current.delete(key));
  }, []);

  const load = useCallback((path: string) => once(`folder:${path}`, () =>
    engine.call<Listing>("library.list", path ? { path } : {}).then((l) => setListings((m) => new Map(m).set(path, l)))), [once]);

  // Start over when connecting and when a scan finishes, since folders may have changed.
  useEffect(() => {
    if (!connected || scanning) return;
    setListings(new Map());
    load("");
  }, [connected, scanning === true, load]);

  useEffect(() => {
    if (connected && scanning && !listings.has("")) load("");
  }, [connected, scanning, listings, load]);

  // The library's folders start open, the first time.
  const root = listings.get("");
  useEffect(() => {
    if (root && root.folders.length && !expanded.has("samples:opened")) {
      setExpanded((e) => new Set([...e, "samples:opened", ...root.folders.map((f) => f.path)]));
    }
  }, [root]);

  // Projects are listed again whenever another one opens; plugins once.
  useEffect(() => {
    if (!connected || !expanded.has("projects")) return;
    once(`projects:${project}`, async () => {
      const [browsed, recent] = await Promise.all([
        engine.call<{ projects: ProjectEntry[] }>("project.browse"),
        engine.call<{ projects: ProjectEntry[] }>("project.recent"),
      ]);
      const byPath = new Map<string, ProjectEntry>();
      for (const p of [...recent.projects, ...browsed.projects]) byPath.set(p.path, p);
      setProjects([...byPath.values()].sort((a, b) => b.modified - a.modified));
    });
  }, [connected, expanded.has("projects"), project, once]);

  useEffect(() => {
    if (!connected || !expanded.has("presets") || plugins) return;
    once("plugins", async () => {
      const r = await engine.call<{ plugins: PluginInfo[] }>("plugins.list", {});
      setPlugins(ownPlugins(r.plugins));
    });
  }, [connected, expanded.has("presets"), plugins, once]);

  useEffect(() => {
    try {
      localStorage.setItem(EXPANDED_KEY, JSON.stringify([...expanded]));
    } catch {
      // Storage can be unavailable; the tree works without it.
    }
    if (!connected) return;
    for (const key of expanded) {
      if (key.startsWith("plugin:")) {
        const uid = key.slice(7);
        if (!pluginPresets.has(uid)) once(key, () => engine.call<{ presets: string[] }>("plugins.describe", { uid, limit: 1 })
          .then((r) => setPluginPresets((m) => new Map(m).set(uid, r.presets ?? []))));
      } else if (key.startsWith("effect:")) {
        const kind = key.slice(7);
        if (!effectPresets.has(kind)) once(key, () => engine.call<{ factory: EffectPreset[]; user: EffectPreset[] }>("presets.list", { kind })
          .then((r) => setEffectPresets((m) => new Map(m).set(kind, r))));
      } else if (!key.includes(":") && !["projects", "presets", "samples", "effects"].includes(key) && !listings.has(key)) {
        load(key);
      }
    }
  }, [expanded, connected, listings, pluginPresets, effectPresets, load, once]);

  const rows = useMemo(() => {
    const out: Row[] = [];

    out.push({ kind: "root", key: "projects", name: "Projects", depth: 0 });
    if (expanded.has("projects")) {
      if (!projects) out.push({ kind: "note", text: "Loading…", depth: 1 });
      else if (projects.length === 0) out.push({ kind: "note", text: "No projects yet", depth: 1 });
      for (const p of projects ?? []) out.push({ kind: "project", project: p, current: p.path === project, depth: 1 });
    }

    out.push({ kind: "root", key: "presets", name: "Presets", depth: 0 });
    if (expanded.has("presets")) {
      for (const p of plugins ?? []) {
        const key = `plugin:${p.uid}`;
        out.push({ kind: "plugin", plugin: p, key, depth: 1 });
        if (!expanded.has(key)) continue;
        const names = pluginPresets.get(p.uid);
        if (!names) {
          out.push({ kind: "note", text: "Loading…", depth: 2 });
          continue;
        }
        const categories = [...new Set(names.filter((n) => n !== "Init").map(presetCategory))];
        for (const c of categories) {
          const ckey = `${key}:${c}`;
          out.push({ kind: "category", key: ckey, name: c, depth: 2 });
          if (!expanded.has(ckey)) continue;
          for (const n of names) if (n !== "Init" && presetCategory(n) === c) out.push({ kind: "pluginPreset", plugin: p, preset: n, depth: 3 });
        }
      }
      out.push({ kind: "category", key: "effects", name: "Effects", depth: 1 });
      if (expanded.has("effects")) {
        for (const [effect, name] of Object.entries(effectNames)) {
          const key = `effect:${effect}`;
          out.push({ kind: "effectKind", key, effect, name, depth: 2 });
          if (!expanded.has(key)) continue;
          const list = effectPresets.get(effect);
          if (!list) out.push({ kind: "note", text: "Loading…", depth: 3 });
          else if (list.factory.length + list.user.length === 0) out.push({ kind: "note", text: "No presets", depth: 3 });
          for (const preset of list?.factory ?? []) out.push({ kind: "effectPreset", effect, preset, user: false, depth: 3 });
          for (const preset of list?.user ?? []) out.push({ kind: "effectPreset", effect, preset, user: true, depth: 3 });
        }
      }
    }

    out.push({ kind: "root", key: "samples", name: "Samples", depth: 0 });
    if (expanded.has("samples")) {
      const walk = (path: string, depth: number) => {
        const l = listings.get(path);
        if (!l) return;
        for (const f of l.folders) {
          out.push({ kind: "folder", ...f, depth, parent: path || undefined });
          if (expanded.has(f.path)) walk(f.path, depth + 1);
        }
        for (const sample of l.samples) out.push({ kind: "sample", sample, depth, parent: path });
      };
      walk("", 1);
    }

    return out;
  }, [listings, expanded, projects, project, plugins, pluginPresets, effectPresets]);

  useEffect(() => {
    const el = box.current;
    if (!el) return;
    const observer = new ResizeObserver(() => setHeight(el.clientHeight));
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const toggle = (key: string, open?: boolean) => setExpanded((e) => {
    const next = new Set(e);
    if (open ?? !next.has(key)) next.add(key);
    else next.delete(key);
    return next;
  });

  const keyOf = (row: Row): string | undefined => {
    switch (row.kind) {
      case "root": return row.key;
      case "plugin": case "category": case "effectKind": return row.key;
      case "folder": return row.path;
      default: return undefined;
    }
  };

  /** What a line does when it's chosen: a project opens, a preset goes into the song, a sample plays. */
  const activate = (row: Row) => {
    const doc = useEngine.getState().doc;
    if (row.kind === "project") {
      if (!row.current) void openProject(row.project.path);
    } else if (row.kind === "pluginPreset" && doc) {
      const { plugin, preset } = row;
      const ref = { format: plugin.format, uid: plugin.uid, name: plugin.name, manufacturer: plugin.manufacturer, preset };
      if (plugin.instrument) {
        const id = uniqueId(plugin.name, doc.channels);
        void execute({ type: "addChannel", channel: { id, name: `${plugin.name} ${presetName(preset)}`, instrument: { type: "plugin", plugin: ref } } },
          { label: `Add ${plugin.name} ${presetName(preset)}` }).then(() => setUi({ selectedChannel: id }));
      } else {
        const insertId = useUi.getState().selectedInsert ?? "master";
        const insert = doc.mixer.inserts.find((i) => i.id === insertId);
        if (!insert) return;
        void execute({ type: "addEffect", insert: insertId, effect: { type: "plugin", id: uniqueId(plugin.name, insert.effects ?? []), plugin: ref } },
          { label: `Add ${plugin.name} ${presetName(preset)}` }).then(() => toast(`${plugin.name}: ${presetName(preset)} on ${insert.name ?? insertId}`));
      }
    } else if (row.kind === "effectPreset" && doc) {
      const insertId = useUi.getState().selectedInsert ?? "master";
      const insert = doc.mixer.inserts.find((i) => i.id === insertId);
      if (!insert) return;
      const { effect, preset } = row;
      void execute({ type: "addEffect", insert: insertId,
        effect: { type: "builtin", id: uniqueId(effect, insert.effects ?? []), kind: effect as never, params: preset.params, ...(preset.mix !== undefined ? { mix: preset.mix } : {}) } },
        { label: `Add ${effectNames[effect]} ${preset.name}` }).then(() => toast(`${effectNames[effect]}: ${preset.name} on ${insert.name ?? insertId}`));
    } else if (row.kind === "sample") {
      preview(row.sample);
    } else {
      const key = keyOf(row);
      if (key) toggle(key);
    }
  };

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
    const key = keyOf(row);

    if (e.key === "ArrowDown") moveTo(cursor + 1);
    else if (e.key === "ArrowUp") moveTo(cursor - 1);
    else if (e.key === "Enter") activate(row);
    else if (e.key === "ArrowRight" && key) {
      if (!expanded.has(key)) toggle(key, true);
      else moveTo(cursor + 1);
    } else if (e.key === "ArrowLeft") {
      if (key && expanded.has(key)) toggle(key, false);
      else {
        // Up to the line this one hangs from.
        for (let i = cursor - 1; i >= 0; --i) if (rows[i].depth < row.depth) { moveTo(i); break; }
      }
    }
  };

  const first = Math.max(0, Math.floor(scrollTop / ROW_H) - 10);
  const last = Math.min(rows.length, Math.ceil((scrollTop + height) / ROW_H) + 10);
  const libraryEmpty = root !== undefined && root.folders.length === 0;

  return (
    <div ref={box} tabIndex={0} onKeyDown={onKeyDown} onScroll={(e) => setScrollTop(e.currentTarget.scrollTop)}
      className="min-h-0 flex-1 overflow-y-auto outline-none">
      <div className="relative" style={{ height: rows.length * ROW_H + 8 + (libraryEmpty && expanded.has("samples") ? 90 : 0) }}>
        <div className="absolute inset-x-0" style={{ top: first * ROW_H + 4 }}>
          {rows.slice(first, last).map((row, j) => {
            const i = first + j;
            const selected = i === cursor;
            const select = () => {
              setCursor(i);
              box.current?.focus();
            };

            if (row.kind === "sample") {
              return (
                <SampleRow key={row.sample.path} sample={row.sample} depth={row.depth + 1} selected={selected} playing={playing === row.sample.path}
                  onClick={() => {
                    select();
                    preview(row.sample);
                  }} />
              );
            }

            const key = keyOf(row);
            const open = key !== undefined && expanded.has(key);
            const line = (icon: React.ReactNode, label: React.ReactNode, right?: React.ReactNode, extra?: { title?: string; onDoubleClick?: () => void; dim?: boolean }) => (
              <div key={`${row.kind}:${key ?? i}`} title={extra?.title}
                onClick={() => {
                  select();
                  if (key) toggle(key);
                }}
                onDoubleClick={extra?.onDoubleClick}
                className={cn("flex cursor-default items-center gap-1 pr-2 text-xs", selected ? "bg-control" : "hover:bg-raised", extra?.dim && "text-faint")}
                style={{ height: ROW_H, paddingLeft: 4 + row.depth * 14 }}>
                {key ? <ChevronRight size={11} className={cn("shrink-0 text-faint transition-transform", open && "rotate-90")} /> : <span className="w-[11px] shrink-0" />}
                {icon}
                <span className={cn("min-w-0 flex-1 truncate", extra?.dim ? "" : "text-fg")}>{label}</span>
                {right && <span className="shrink-0 text-2xs text-faint tabular">{right}</span>}
              </div>
            );

            switch (row.kind) {
              case "root": {
                const icon = row.key === "projects" ? <FolderOpenDot size={12} className="shrink-0 text-accent" /> : row.key === "presets"
                  ? <SlidersHorizontal size={12} className="shrink-0 text-accent" /> : <Waves size={12} className="shrink-0 text-accent" />;
                return line(icon, <span className="font-semibold">{row.name}</span>);
              }
              case "project":
                return line(<Music size={12} className={cn("shrink-0", row.current ? "text-accent" : "text-dim")} />, row.project.name,
                  row.current ? "open" : new Date(row.project.modified).toLocaleDateString(),
                  { title: `${row.project.path}\nDouble-click or Enter to open`, onDoubleClick: () => activate(row) });
              case "plugin":
                return line(open ? <FolderOpen size={12} className="shrink-0 text-accent" /> : <Folder size={12} className="shrink-0 text-dim" />,
                  <>{row.plugin.name} <span className="text-2xs text-faint">{OWN_PLUGINS[row.plugin.name]}</span></>);
              case "category":
              case "effectKind":
                return line(open ? <FolderOpen size={12} className="shrink-0 text-accent" /> : <Folder size={12} className="shrink-0 text-dim" />, row.name);
              case "pluginPreset":
                return line(<span className="h-1.5 w-1.5 shrink-0 rounded-full bg-agent" />, presetName(row.preset), undefined,
                  { title: row.plugin.instrument ? "Double-click or Enter: a new channel with this sound" : "Double-click or Enter: onto the selected insert", onDoubleClick: () => activate(row) });
              case "effectPreset":
                return line(<span className="h-1.5 w-1.5 shrink-0 rounded-full bg-agent" />, row.preset.name, row.user ? "yours" : undefined,
                  { title: "Double-click or Enter: onto the selected insert", onDoubleClick: () => activate(row) });
              case "note":
                return line(<span className="w-3 shrink-0" />, row.text, undefined, { dim: true });
              case "folder":
                return line(open ? <FolderOpen size={12} className="shrink-0 text-accent" /> : <Folder size={12} className="shrink-0 text-dim" />, row.name, row.samples.toLocaleString(), { title: row.path });
            }
          })}
          {libraryEmpty && expanded.has("samples") && (
            <div className="flex flex-col items-center gap-2 p-4 text-center text-2xs text-dim">
              <span>The library is empty. Download the starter samples, or add a folder below.</span>
              <StarterSamples brief />
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
