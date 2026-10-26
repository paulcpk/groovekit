import type { AudioClip } from "@groovekit/schema/project";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { audioSource } from "@/lib/model";
import { openWindow } from "@/state/windows";
import { usePeaks } from "@/lib/peaks";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

/** Gain, pan and fades of one audio clip. */
export function AudioClipDialog() {
  const id = useUi((s) => s.clipEditor);
  const clip = useEngine((s) => s.doc?.playlist.clips.find((c) => c.id === id && c.type === "audio")) as AudioClip | undefined;
  const tempo = useEngine((s) => s.doc?.tempo ?? 120);
  const name = useEngine((s) => (clip && s.doc ? audioSource(s.doc, clip).channel?.name : undefined));
  const peaks = usePeaks(useEngine((s) => (clip && s.doc ? audioSource(s.doc, clip).sample : undefined)));

  const change = (changes: Record<string, unknown>, label: string) => {
    if (clip) void execute({ type: "updateClip", clip: clip.id, changes }, { label, group: `clip-${clip.id}` });
  };

  const seconds = clip ? (clip.length * 60) / (tempo * 960) : 0;
  const row = "grid grid-cols-[110px_1fr] items-center gap-3 text-xs";

  return (
    <Dialog open={!!clip} onOpenChange={(o) => !o && setUi({ clipEditor: undefined })} title={name || "Audio clip"} className="w-[420px]">
      {clip && (
        <div className="flex flex-col gap-3">
          <div className={row}>
            <span className="text-dim">Gain</span>
            <DragNumber value={clip.gainDb ?? 0} min={-48} max={24} step={0.1} format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(1)} dB`}
              onCommit={(v) => change({ gainDb: Math.round(v * 10) / 10 }, "Clip gain")} className="text-fg" />
          </div>
          <div className={row}>
            <span className="text-dim">Pan</span>
            <DragNumber value={clip.pan ?? 0} min={-1} max={1} step={0.01} format={(v) => (Math.abs(v) < 0.005 ? "Centre" : `${Math.round(Math.abs(v) * 100)}% ${v < 0 ? "L" : "R"}`)}
              onCommit={(v) => change({ pan: Math.round(v * 100) / 100 }, "Clip pan")} className="text-fg" />
          </div>
          <div className={row}>
            <span className="text-dim">Fade in</span>
            <DragNumber value={clip.fadeIn ?? 0} min={0} max={seconds} step={0.01} format={(v) => `${v.toFixed(2)} s`}
              onCommit={(v) => change({ fadeIn: Math.round(v * 100) / 100 }, "Fade in")} className="text-fg" />
          </div>
          <div className={row}>
            <span className="text-dim">Fade out</span>
            <DragNumber value={clip.fadeOut ?? 0} min={0} max={seconds} step={0.01} format={(v) => `${v.toFixed(2)} s`}
              onCommit={(v) => change({ fadeOut: Math.round(v * 100) / 100 }, "Fade out")} className="text-fg" />
          </div>
          <div className="flex items-center justify-between gap-2 rounded-md bg-raised px-2.5 py-2 text-xs">
            <span className="text-dim">Reverse, stretch, envelope and filter belong to the audio channel, for all its clips.</span>
            <Button size="sm" className="shrink-0 bg-control text-fg" onClick={() => {
              setUi({ clipEditor: undefined });
              openWindow({ kind: "channel", channel: clip.channel });
            }}>Open</Button>
          </div>
          {peaks && <div className="text-2xs text-faint">The file is {peaks.durationSeconds.toFixed(2)} s long; the clip plays {seconds.toFixed(2)} s of the song.</div>}
        </div>
      )}
    </Dialog>
  );
}
