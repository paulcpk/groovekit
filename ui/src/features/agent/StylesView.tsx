import { ChevronDown, ChevronRight, Plus, Trash2 } from "lucide-react";
import { useState } from "react";
import { Dialog } from "@/components/dialog";
import { cn } from "@/lib/cn";
import { execute, useEngine } from "@/state/engine";
import { openWindow } from "@/state/windows";
import { deleteGuide, unhideGuide, useStyleGuides, type GuideSummary } from "./guides";

/** The style guides: how the agent makes each style. The song's style comes first; any opens to
    read and edit, and one can be made the song's style or taken out of the list. */
export function StylesView() {
  const { guides, loaded } = useStyleGuides();
  const connected = useEngine((s) => s.status === "connected");
  const [deleting, setDeleting] = useState<GuideSummary>();
  const [showHidden, setShowHidden] = useState(false);
  const song = guides.find((g) => g.song);
  const others = guides.filter((g) => !g.song && !g.hidden);
  const hidden = guides.filter((g) => !g.song && g.hidden);

  return (
    <div className="flex h-full flex-col">
      <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-2 py-2">
        <p className="px-1 pb-2 text-xs leading-relaxed text-dim">
          How the agent makes each style: the feeling, the parts and their sound, the mix, and what to avoid. It follows the
          song's style; edit any guide to make it yours.
        </p>
        {song && (
          <>
            <div className="px-1 pb-1 text-2xs uppercase tracking-wide text-faint">This song</div>
            <GuideCard guide={song} onDelete={setDeleting} />
          </>
        )}
        {loaded && (
          <div className={cn("px-1 pb-1 text-2xs uppercase tracking-wide text-faint", song && "mt-3")}>{song ? "Other styles" : "Styles"}</div>
        )}
        <div className="flex flex-col gap-1.5">
          {others.map((g) => <GuideCard key={g.id} guide={g} onDelete={setDeleting} />)}
        </div>
        {hidden.length > 0 && (
          <div className="mt-3">
            <button type="button" onClick={() => setShowHidden(!showHidden)} className="flex items-center gap-1 px-1 text-2xs uppercase tracking-wide text-faint hover:text-dim">
              {showHidden ? <ChevronDown size={11} /> : <ChevronRight size={11} />} Hidden ({hidden.length})
            </button>
            {showHidden && hidden.map((g) => (
              <div key={g.id} className="flex h-7 items-center gap-2 px-1">
                <span className="min-w-0 flex-1 truncate text-xs text-faint">{g.name}</span>
                <button type="button" onClick={() => void unhideGuide(g.id)} className="h-5 rounded-[4px] px-1.5 text-2xs text-dim hover:bg-control hover:text-fg">Show again</button>
              </div>
            ))}
          </div>
        )}
      </div>
      <div className="shrink-0 p-2 pt-1">
        <button type="button" disabled={!connected} onClick={() => openWindow({ kind: "guide", guide: "" })}
          className="flex h-8 w-full items-center justify-center gap-1.5 rounded-[8px] bg-raised text-xs text-dim ring-1 ring-line transition-colors hover:text-fg hover:ring-agent/50 disabled:opacity-40">
          <Plus size={12} /> New style guide
        </button>
      </div>

      <Dialog open={!!deleting} onOpenChange={(open) => !open && setDeleting(undefined)} title={`Delete ${deleting?.name ?? "this style"}?`} className="w-[380px]">
        {deleting && (
          <>
            <p className="text-xs leading-relaxed text-dim">
              {deleting.builtIn
                ? "It comes with GrooveKit, so it's hidden rather than deleted: you can show it again from the bottom of the list."
                : "It's one of your own: the guide is deleted for good."}
              {deleting.song && " This song keeps it as its style until you pick another."}
            </p>
            <div className="mt-4 flex justify-end gap-2">
              <button type="button" onClick={() => setDeleting(undefined)} className="h-7 rounded-[4px] px-3 text-xs text-dim hover:bg-control hover:text-fg">Cancel</button>
              <button type="button" onClick={() => void deleteGuide(deleting.id).finally(() => setDeleting(undefined))}
                className="h-7 rounded-[4px] bg-rec px-3 text-xs text-white hover:brightness-110">{deleting.builtIn ? "Hide it" : "Delete it"}</button>
            </div>
          </>
        )}
      </Dialog>
    </div>
  );
}

/** A guide's card. Its buttons keep their place when hidden, so hovering moves nothing. */
function GuideCard({ guide, onDelete }: { guide: GuideSummary; onDelete: (g: GuideSummary) => void }) {
  return (
    <div role="button" tabIndex={0} onClick={() => openWindow({ kind: "guide", guide: guide.id })}
      onKeyDown={(e) => e.key === "Enter" && openWindow({ kind: "guide", guide: guide.id })}
      className={cn("group cursor-pointer rounded-[8px] bg-raised px-2.5 py-2 ring-1 transition-colors hover:ring-agent/50",
        guide.song ? "ring-agent/60" : "ring-line")}>
      <div className="flex h-5 items-center gap-1.5">
        <span className="min-w-0 flex-1 truncate text-xs text-fg">{guide.name}</span>
        {guide.edited && <span className="shrink-0 text-2xs text-faint">{guide.builtIn ? "edited" : "yours"}</span>}
        {!guide.song && (
          <button type="button" title="Make it this song's style"
            onClick={(e) => {
              e.stopPropagation();
              void execute({ type: "setStyle", style: guide.id }, { label: `Style: ${guide.name}` });
            }}
            className="invisible h-5 shrink-0 rounded-[4px] px-1.5 text-2xs text-dim hover:bg-control hover:text-fg group-hover:visible group-focus-within:visible">Use</button>
        )}
        <button type="button" title={guide.builtIn ? "Hide this style" : "Delete this style"}
          onClick={(e) => {
            e.stopPropagation();
            onDelete(guide);
          }}
          className="invisible flex h-5 w-5 shrink-0 items-center justify-center rounded-[4px] text-faint hover:bg-control hover:text-rec group-hover:visible group-focus-within:visible">
          <Trash2 size={11} />
        </button>
      </div>
      <div className="mt-0.5 line-clamp-2 text-2xs leading-relaxed text-dim">{guide.summary}</div>
    </div>
  );
}
