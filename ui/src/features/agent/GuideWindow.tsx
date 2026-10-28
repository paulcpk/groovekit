import { Pencil, RotateCcw, Trash2 } from "lucide-react";
import { useEffect, useState } from "react";
import Markdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { IconButton, ToggleChip } from "@/components/controls";
import { FloatingWindow } from "@/components/window";
import { execute, useEngine } from "@/state/engine";
import { closeWindow, openWindow, type OpenWindow } from "@/state/windows";
import { getGuide, removeGuide, saveGuide, useGuides, type Guide } from "./guides";
import { MARKDOWN } from "./markdown";

type Range = [number, number];
interface Targets {
  tempo?: Range; swing?: Range; partsAtOnce?: number; topAtOnce?: number; lowVsHigh?: Range; highHitsPerBeat?: Range; loudness?: Range;
  levels?: Record<string, Range>;
}

const range = ([a, b]: Range, unit = "") => (a === b ? `${a}${unit}` : `${a} to ${b}${unit}`);

/** The numbers a guide aims for (its targets block), read out. */
function TargetsTable({ targets: raw }: { targets: Record<string, unknown> }) {
  const t = raw as Targets;
  const rows: [string, string][] = [];
  if (t.tempo) rows.push(["Tempo", range(t.tempo, " BPM")]);
  if (t.swing) rows.push(["Swing", range(t.swing, "%")]);
  if (t.partsAtOnce) rows.push(["Parts at once", `up to ${t.partsAtOnce}`]);
  if (t.topAtOnce) rows.push(["Top parts at once", `up to ${t.topAtOnce} (hats, percussion, claps, effects)`]);
  if (t.lowVsHigh) rows.push(["Low end over highs", range(t.lowVsHigh, " dB")]);
  if (t.highHitsPerBeat) rows.push(["Hits per beat in the highs", range(t.highHitsPerBeat)]);
  if (t.loudness) rows.push(["Loudness", range(t.loudness, " LUFS")]);
  for (const [role, r] of Object.entries(t.levels ?? {})) rows.push([`${role[0].toUpperCase()}${role.slice(1)} against the kick`, range(r, " dB")]);
  if (!rows.length) return null;
  return (
    <div className="mt-4 rounded-[6px] bg-bg p-2.5 ring-1 ring-line">
      <div className="mb-1.5 text-2xs uppercase tracking-wide text-faint">Targets</div>
      <div className="grid grid-cols-[auto_1fr] gap-x-4 gap-y-0.5 text-[11px]">
        {rows.map(([k, v]) => (
          <div key={k} className="contents"><span className="text-dim">{k}</span><span className="tabular text-fg">{v}</span></div>
        ))}
      </div>
    </div>
  );
}

const NEW_GUIDE = `# My style

What it should feel like: what moves the body, what the ears hold on to.

## The parts

- Kick: how it should sound, in words to search the library by.
- Bass: ...

## Mix and arrangement

How loud each part sits, how many play at once, how it builds.

## Avoid

What makes it sound wrong.

\`\`\`targets
{
  "tempo": [120, 130],
  "partsAtOnce": 6
}
\`\`\`
`;

/** A style guide, to read and to edit: the user's version replaces the built-in one (Reset brings
    it back), and guides of their own can be added and deleted. A song using it is shown and set
    from here. */
export function GuideWindow({ window: w, id }: { window: OpenWindow; id: string }) {
  const style = useEngine((s) => s.doc?.style);
  const listed = useGuides((s) => s.guides.find((g) => g.id === id));
  const [guide, setGuide] = useState<Guide>();
  const [draft, setDraft] = useState<string | undefined>(id ? undefined : NEW_GUIDE);
  const editing = draft !== undefined;

  // Read again whenever it changes (here, by the agent, or by Claude Code).
  useEffect(() => {
    if (id) void getGuide(id).then(setGuide, () => {});
  }, [id, listed?.edited, listed?.summary, listed?.name]);

  const save = () => {
    if (draft === undefined) return;
    void saveGuide(draft, id || undefined).then((saved) => {
      setDraft(undefined);
      if (!id) {
        closeWindow(w.key);
        openWindow({ kind: "guide", guide: saved.id });
      } else setGuide(saved);
    }, () => {});
  };

  const remove = () => {
    void removeGuide(id).then((r) => {
      if (r.removed === "deleted") {
        closeWindow(w.key);
        if (style === id) void execute({ type: "setStyle" }, { label: "No style" });
      } else void getGuide(id).then(setGuide, () => {});
    }, () => {});
  };

  const isSong = !!id && style === id;
  const title = id ? guide?.name ?? listed?.name ?? id : "New style guide";

  return (
    <FloatingWindow window={w} width={600} accent="var(--color-agent)"
      title={<><span className="truncate">{title}</span><span className="text-2xs font-normal text-faint">Style guide</span></>}
      extra={
        <div className="flex items-center gap-1">
          {id && !editing && (
            <ToggleChip on={isSong} label={isSong ? "This song's style" : "Use for this song"}
              title={isSong ? "The agent follows this guide in this song; click to clear" : "Make it the style the agent follows in this song"}
              onClick={() => void execute(isSong ? { type: "setStyle" } : { type: "setStyle", style: id }, { label: isSong ? "No style" : `Style: ${title}` })} />
          )}
          {id && !editing && guide && (
            <IconButton size="sm" title="Edit" onClick={() => setDraft(guide.text)}><Pencil size={12} /></IconButton>
          )}
          {id && !editing && guide?.builtIn && guide.edited && (
            <IconButton size="sm" title="Back to how it shipped" onClick={remove}><RotateCcw size={12} /></IconButton>
          )}
          {id && !editing && guide && !guide.builtIn && (
            <IconButton size="sm" title="Delete this guide" onClick={remove}><Trash2 size={12} /></IconButton>
          )}
        </div>
      }>
      {editing ? (
        <div className="flex flex-col gap-2 p-3">
          <textarea value={draft} onChange={(e) => setDraft(e.target.value)} autoFocus spellCheck={false}
            onKeyDown={(e) => {
              e.stopPropagation();
              if (e.key === "s" && (e.metaKey || e.ctrlKey)) {
                e.preventDefault();
                save();
              }
            }}
            className="h-[60vh] w-full resize-none rounded-[6px] bg-bg p-2.5 font-mono text-[11px] leading-relaxed text-fg outline-none ring-1 ring-line focus:ring-agent/50" />
          <div className="flex items-center gap-2">
            <span className="flex-1 text-2xs text-faint">Markdown. The first heading is its name; the targets block gives the numbers the agent aims for.</span>
            <button type="button" onClick={() => (id ? setDraft(undefined) : closeWindow(w.key))} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:text-fg">Cancel</button>
            <button type="button" disabled={!draft.trim()} onClick={save} className="h-6 rounded-[4px] bg-agent px-2 text-xs text-black disabled:opacity-40">Save</button>
          </div>
        </div>
      ) : (
        <div className="px-4 py-3 text-xs leading-relaxed text-fg select-text">
          {guide ? (
            <>
              {guide.builtIn && guide.edited && <div className="mb-2 text-2xs text-faint">Your version. Reset brings back the one GrooveKit came with.</div>}
              <Markdown remarkPlugins={[remarkGfm]} components={MARKDOWN}>{guide.text.replace(/```targets[\s\S]*?```\n?/, "")}</Markdown>
              <TargetsTable targets={guide.targets} />
            </>
          ) : (
            <div className="text-faint">…</div>
          )}
        </div>
      )}
    </FloatingWindow>
  );
}
