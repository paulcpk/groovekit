import { FolderOpen } from "lucide-react";
import { useEffect, useState } from "react";
import { Button } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";
import { call, engine, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { confirmUnsaved } from "./SaveChanges";

export interface RecentProject {
  path: string;
  name: string;
  modified: number;
}

const input = "h-7 min-w-0 flex-1 rounded-[4px] bg-bg px-2 text-sm text-fg outline-none ring-1 ring-line focus:ring-accent";

export function recentProjects() {
  return engine.call<{ projects: RecentProject[] }>("project.recent").then((r) => r.projects);
}

/** Opens a project folder; the engine switches every client over to it. Unsaved changes in
    this one are offered to be saved first. */
export async function openProject(path: string) {
  if (path !== useEngine.getState().project && !(await confirmUnsaved("open"))) return;
  return call("project.open", { path }).then(() => setUi({ projectDialog: undefined }));
}

/** Every change is written to the project folder as it's made; saving marks the point the song
    was saved at, which leaving without saving goes back to. */
export async function saveProject() {
  await call("project.save");
  const project = useEngine.getState().project;
  toast(project ? `Saved · ${project.replace(/^\/Users\/[^/]+/, "~")}` : "Saved");
}


/** In the desktop app, Open project goes straight to the system's folder picker. */
export async function browseForProject() {
  if (!inDesktopApp) {
    setUi({ projectDialog: "open" });
    return;
  }
  const project = useEngine.getState().project;
  const path = await pickFolder("Open project", project && parentOf(project));
  if (path) await openProject(path);
}

function parentOf(path: string) {
  return path.replace(/\/[^/]+\/?$/, "") || "/";
}

function timeAgo(ms: number) {
  const minutes = Math.round((Date.now() - ms) / 60000);
  if (minutes < 1) return "just now";
  if (minutes < 60) return `${minutes} min ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours} h ago`;
  return new Date(ms).toLocaleDateString();
}

export function ProjectDialog() {
  const mode = useUi((s) => s.projectDialog);
  const close = () => setUi({ projectDialog: undefined });

  return (
    <Dialog open={mode !== undefined} onOpenChange={(o) => !o && close()} title={{ new: "New project", open: "Open project", saveAs: "Save project as" }[mode ?? "new"]}
      className="w-[480px]">
      {mode === "new" ? <NewProject /> : mode === "saveAs" ? <NewProject saveAs /> : mode === "open" ? <OpenProject /> : null}
    </Dialog>
  );
}

/** Creates a project, or with saveAs, copies this one (recordings and all) and opens the copy. */
function NewProject({ saveAs = false }: { saveAs?: boolean }) {
  const project = useEngine((s) => s.project);
  const songName = useEngine((s) => s.doc?.name);
  const [name, setName] = useState(saveAs ? `${songName || project?.split("/").pop() || "Untitled"} copy` : "Untitled");
  const [location, setLocation] = useState(project ? parentOf(project) : "");
  const [busy, setBusy] = useState(false);

  const create = async () => {
    const folder = name.trim();
    if (!folder || !location.trim()) return;
    if (!saveAs && !(await confirmUnsaved("open"))) return;
    setBusy(true);
    call(saveAs ? "project.saveAs" : "project.new", { path: `${location.trim().replace(/\/$/, "")}/${folder}` })
      .then(() => setUi({ projectDialog: undefined }), () => {})
      .finally(() => setBusy(false));
  };

  return (
    <form className="flex flex-col gap-3" onSubmit={(e) => { e.preventDefault(); void create(); }}>
      <label className="flex items-center gap-3 text-xs">
        <span className="w-16 text-dim">Name</span>
        <input data-autofocus value={name} onChange={(e) => setName(e.target.value)} onFocus={(e) => e.target.select()}
          onKeyDown={(e) => e.stopPropagation()} className={input} />
      </label>
      <label className="flex items-center gap-3 text-xs">
        <span className="w-16 text-dim">Location</span>
        <input value={location} onChange={(e) => setLocation(e.target.value)} onKeyDown={(e) => e.stopPropagation()} className={input} />
        {inDesktopApp && (
          <Button size="sm" className="bg-control text-fg" onClick={() => void pickFolder("Where to keep the project", location).then((p) => p && setLocation(p))}>
            Choose…
          </Button>
        )}
      </label>
      <div className="text-2xs text-faint">
        {saveAs ? "The project folder, with its recordings and history, is copied to a new folder of this name, and the copy is opened."
          : "A folder named after the project is created there. It holds the song, its history and recordings."}
      </div>
      <div className="flex justify-end gap-2">
        <Button size="sm" onClick={() => setUi({ projectDialog: undefined })}>Cancel</Button>
        <Button size="sm" type="submit" active tone="accent" disabled={busy || !name.trim()}>{saveAs ? "Save" : "Create"}</Button>
      </div>
    </form>
  );
}

function OpenProject() {
  const current = useEngine((s) => s.project);
  const [projects, setProjects] = useState<RecentProject[]>();
  const [path, setPath] = useState("");

  useEffect(() => {
    recentProjects().then(setProjects, () => setProjects([]));
  }, []);

  return (
    <div className="flex flex-col gap-3">
      <div className="text-2xs uppercase tracking-wide text-faint">Recent</div>
      <div className="flex max-h-[300px] flex-col overflow-y-auto">
        {!projects ? <div className="text-xs text-faint">Loading…</div>
          : projects.length === 0 ? <div className="text-xs text-faint">No projects yet.</div>
          : projects.map((p) => (
            <button key={p.path} type="button" disabled={p.path === current} title={p.path}
              className="flex items-center gap-2 rounded-[4px] px-2 py-1.5 text-left hover:bg-control disabled:opacity-50"
              onClick={() => void openProject(p.path)}>
              <span className="truncate text-sm text-fg">{p.name}</span>
              <span className="min-w-0 flex-1 truncate text-2xs text-faint">{parentOf(p.path)}</span>
              <span className="shrink-0 text-2xs text-faint">{p.path === current ? "open" : timeAgo(p.modified)}</span>
            </button>
          ))}
      </div>
      <form className="flex items-center gap-2" onSubmit={(e) => { e.preventDefault(); if (path.trim()) void openProject(path.trim()); }}>
        <input data-autofocus value={path} onChange={(e) => setPath(e.target.value)} placeholder="/path/to/project" onKeyDown={(e) => e.stopPropagation()} className={input} />
        {inDesktopApp && (
          <Button size="sm" className="bg-control text-fg" onClick={() => void browseForProject()}><FolderOpen size={12} /> Browse…</Button>
        )}
        <Button size="sm" type="submit" disabled={!path.trim()}>Open</Button>
      </form>
    </div>
  );
}
