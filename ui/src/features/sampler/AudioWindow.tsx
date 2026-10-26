import type { AudioInstrument, Channel } from "@groovekit/schema/project";
import { useState } from "react";
import { DragNumber, Tabs, ToggleChip } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { useLiveControl } from "@/lib/liveControl";
import { colorFor, fileName } from "@/lib/model";
import { call, execute, useEngine } from "@/state/engine";
import type { OpenWindow } from "@/state/windows";
import { EnvelopeGraph } from "./EnvelopeGraph";
import { FilterTab } from "./SamplerWindow";
import { Waveform } from "./Waveform";

type Tab = "sample" | "volume" | "filter";

/** An audio channel: the sample its playlist clips play, and the envelope and filter every clip
    plays through. */
export function AudioWindow({ window: w, channel, audio }: { window: OpenWindow; channel: Channel; audio: AudioInstrument }) {
  const [tab, setTab] = useState<Tab>("sample");
  const color = colorFor(channel.id, channel.color);
  const clips = useEngine((s) => s.doc?.playlist.clips.filter((c) => c.type === "audio" && c.channel === channel.id).length ?? 0);
  const tempo = useEngine((s) => s.doc?.tempo ?? 120);
  const set = (changes: Partial<AudioInstrument> | Record<string, unknown>, label: string) =>
    void execute({ type: "updateInstrument", channel: channel.id, changes }, { label });
  const envelope = useLiveControl<AudioInstrument["envelope"]>((env) => ({ type: "updateInstrument", channel: channel.id, changes: { envelope: env } }), "Volume envelope");

  return (
    <FloatingWindow window={w} width={520} accent={color}
      title={<><span className="h-3 w-1 shrink-0 rounded-sm" style={{ background: color }} /><span className="truncate">{channel.name || channel.id}</span><span className="text-2xs font-normal text-faint">Audio channel</span></>}>
      <div className="flex flex-col gap-3 p-3">
        <div className="flex items-center gap-2">
          <Tabs<Tab> value={tab} onChange={setTab} items={[{ value: "sample", label: "Sample" }, { value: "volume", label: "Volume" }, { value: "filter", label: "Filter" }]} />
          <div className="flex-1" />
          <span className="text-2xs text-faint">{clips} clip{clips === 1 ? "" : "s"} in the playlist</span>
        </div>

        {tab === "sample" && (
          <>
            <div className="truncate text-xs text-fg" title={audio.sample}>{fileName(audio.sample)}</div>
            <Waveform path={audio.sample} start={0} end={undefined} reverse={!!audio.reverse} color={color}
              onPress={() => void call("preview.play", { path: audio.sample })}
              onRelease={() => void call("preview.stop")} />
            <div className="flex flex-wrap items-center gap-2 text-2xs text-dim">
              <ToggleChip on={!!audio.reverse} label="Reverse" onClick={() => set({ reverse: !audio.reverse }, audio.reverse ? "Play forwards" : "Reverse")} />
              <ToggleChip on={audio.sourceTempo !== undefined} label="Stretch to tempo" title="Time-stretch the clips to the song's tempo, keeping their pitch"
                onClick={() => set({ sourceTempo: audio.sourceTempo !== undefined ? null : tempo }, audio.sourceTempo !== undefined ? "Don't stretch" : "Stretch to tempo")} />
              {audio.sourceTempo !== undefined ? (
                <span className="flex items-center gap-1">recorded at
                  <DragNumber value={audio.sourceTempo} min={20} max={999} step={0.1} format={(v) => `${v.toFixed(1)} BPM`} className="text-fg"
                    onCommit={(v) => set({ sourceTempo: Math.round(v * 10) / 10 }, "Source tempo")} />
                </span>
              ) : <span className="text-faint">plays at its own speed</span>}
            </div>
            <div className="text-2xs text-faint">Press the waveform to listen. Reverse and stretch apply to every clip of this channel; Make unique on a clip gives it a channel of its own.</div>
          </>
        )}

        {tab === "volume" && (
          <>
            <EnvelopeGraph label="Volume over each clip" envelope={audio.envelope} color={color}
              onChange={envelope.onChange} onCommit={envelope.onCommit} />
            <div className="text-2xs text-faint">Each clip is shaped like a held note: the attack starts where the clip starts, and the release ends where it ends.</div>
          </>
        )}

        {tab === "filter" && <FilterTab channel={channel} filter={audio.filter} color={color} />}
      </div>
    </FloatingWindow>
  );
}
