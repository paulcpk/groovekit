import { Check, FolderPlus, Map as MapIcon, X } from "lucide-react";
import { useState } from "react";
import { Button, IconButton } from "@/components/controls";
import { Dialog } from "@/components/dialog";

import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";

import { openMaps } from "@/features/agent/maps";
import { addStyle, stopUsingStyle, downloadModels, needsHearing, useListeningStatus, type ListeningStatus, type ReferenceFolder } from "./listening";

/** What hearing a references folder would do. */
export function hearLabel(f: ReferenceFolder) {
  if (!f.style) return `Hear ${f.name} (${f.files} track${f.files === 1 ? "" : "s"})`;
  const changes = [f.unheard && `${f.unheard} new`, f.gone && `${f.gone} gone`].filter(Boolean).join(", ");
  return `Update ${f.name} (${changes})`;
}

export const megabytes = (bytes: number) => `${Math.round(bytes / 1_000_000)} MB`;

/** Letting GrooveKit hear the library and the user's reference tracks: the one-time download,
    then how much has been heard, and the styles with their analysis. */
export function Listening({ status }: { status?: ListeningStatus }) {
  const { styles, references } = useListeningStatus();
  const [removing, setRemoving] = useState<string>();
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
        <IconButton size="sm" title="Your reference songs and their maps" onClick={() => openMaps("songs")}><MapIcon size={12} /></IconButton>
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
          <button type="button" title="Stop using this style" onClick={() => setRemoving(s.name)}
            className="invisible shrink-0 text-faint hover:text-rec group-hover:visible"><X size={10} /></button>
        </div>
      ))}

      <Dialog open={!!removing} onOpenChange={(open) => !open && setRemoving(undefined)} title={`Stop using ${removing ?? "this style"}?`} className="w-[380px]">
        <p className="text-xs leading-relaxed text-dim">
          Its songs are unticked in Maps, so searches stop ranking by it and the agent stops learning from it. Their maps are kept:
          tick them again in Maps to bring the style back.
        </p>
        <div className="mt-4 flex justify-end gap-2">
          <button type="button" onClick={() => setRemoving(undefined)} className="h-7 rounded-[4px] px-3 text-xs text-dim hover:bg-control hover:text-fg">Cancel</button>
          <button type="button" onClick={() => void stopUsingStyle(removing!).finally(() => setRemoving(undefined))}
            className="h-7 rounded-[4px] bg-rec px-3 text-xs text-white hover:brightness-110">Stop using it</button>
        </div>
      </Dialog>

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
        <button type="button" title="See every song being heard" onClick={() => openMaps("processing")}
          className="mt-1 flex flex-col gap-1 rounded-[4px] text-left hover:text-fg">
          <span>Listening to {analysis.style}… {analysis.analysed ?? 0} of {analysis.tracks ?? "?"} tracks</span>
          <div className="h-1 w-full overflow-hidden rounded-full bg-bg"><div className="h-full bg-agent transition-[width]" style={{ width: `${((analysis.analysed ?? 0) / Math.max(1, analysis.tracks ?? 1)) * 100}%` }} /></div>
        </button>
      )}
      {analysis.error && !analysis.running && <div className="mt-1 text-rec">{analysis.error}</div>}
      {models.ready && styles.length === 0 && !analysis.running && (
        <div className="text-faint">Add a folder of tracks you love as a style; GrooveKit learns how its kicks, hats, bass and music sound.</div>
      )}
    </div>
  );
}
