import { Heart, Play, Sparkles, Square, ThumbsDown, ThumbsUp } from "lucide-react";

import { endSampleDrag, startSampleDrag, type DraggedSample } from "@/features/playlist/geometry";
import { addSamplerChannel } from "@/features/rack/channels";
import { cn } from "@/lib/cn";
import { fileName, noteName } from "@/lib/model";

import { useEngine } from "@/state/engine";

export interface Sample {
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

export const ROW_H = 26;

export const dragged = (s: Sample): DraggedSample => ({ path: s.path, durationSeconds: s.durationSeconds, loop: s.loop, tempo: s.tempo, category: s.category });

export function addChannel(s: Sample) {
  const doc = useEngine.getState().doc;
  if (doc) void addSamplerChannel(doc, dragged(s));
}

/** How well a sample fits the style: up to about 100 for as close as the references are to each other. */
export function Fit({ fit }: { fit: number }) {
  return (
    <span title={`Fit to the style: ${fit} (0 is an average sample of your library, 100 as close as your reference tracks are to each other)`}
      className={cn("w-7 shrink-0 text-right text-2xs tabular", fit >= 60 ? "text-ok" : fit >= 30 ? "text-fg" : fit >= 0 ? "text-dim" : "text-faint")}>
      {fit}
    </span>
  );
}

/** "Am", "F#": short key names. */
export function shortKey(key: string) {
  const [tonic, mode] = key.split(" ");
  return mode === "minor" ? `${tonic}m` : tonic;
}

/** A sample's key (heard ones in italics), or a one-shot's note, and with the song's key, the
    semitones that put it in key. */
export function KeyLabel({ sample: s }: { sample: Sample }) {
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
export function SampleRow({ sample: s, depth = 0, selected, playing, onClick, style, onRate, onSimilar }: {
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
