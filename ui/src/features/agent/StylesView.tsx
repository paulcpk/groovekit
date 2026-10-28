import { Plus } from "lucide-react";
import { cn } from "@/lib/cn";
import { execute, useEngine } from "@/state/engine";
import { openWindow } from "@/state/windows";
import { useStyleGuides, type GuideSummary } from "./guides";

/** The style guides: how the agent makes each style. The song's style comes first; any opens to
    read and edit, and one can be made the song's style. */
export function StylesView() {
  const { guides, loaded } = useStyleGuides();
  const connected = useEngine((s) => s.status === "connected");
  const song = guides.find((g) => g.song);
  const others = guides.filter((g) => !g.song);

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
            <GuideCard guide={song} />
          </>
        )}
        {loaded && (
          <div className={cn("px-1 pb-1 text-2xs uppercase tracking-wide text-faint", song && "mt-3")}>{song ? "Other styles" : "Styles"}</div>
        )}
        <div className="flex flex-col gap-1.5">
          {others.map((g) => <GuideCard key={g.id} guide={g} />)}
        </div>
      </div>
      <div className="shrink-0 p-2 pt-1">
        <button type="button" disabled={!connected} onClick={() => openWindow({ kind: "guide", guide: "" })}
          className="flex h-8 w-full items-center justify-center gap-1.5 rounded-[8px] bg-raised text-xs text-dim ring-1 ring-line transition-colors hover:text-fg hover:ring-agent/50 disabled:opacity-40">
          <Plus size={12} /> New style guide
        </button>
      </div>
    </div>
  );
}

function GuideCard({ guide }: { guide: GuideSummary }) {
  return (
    <div role="button" tabIndex={0} onClick={() => openWindow({ kind: "guide", guide: guide.id })}
      onKeyDown={(e) => e.key === "Enter" && openWindow({ kind: "guide", guide: guide.id })}
      className={cn("group cursor-pointer rounded-[8px] bg-raised px-2.5 py-2 ring-1 transition-colors hover:ring-agent/50",
        guide.song ? "ring-agent/60" : "ring-line")}>
      <div className="flex items-center gap-1.5">
        <span className="flex-1 truncate text-xs text-fg">{guide.name}</span>
        {guide.edited && <span className="text-2xs text-faint">{guide.builtIn ? "edited" : "yours"}</span>}
        {!guide.song && (
          <button type="button" title="Make it this song's style"
            onClick={(e) => {
              e.stopPropagation();
              void execute({ type: "setStyle", style: guide.id }, { label: `Style: ${guide.name}` });
            }}
            className="hidden h-5 rounded-[4px] px-1.5 text-2xs text-dim hover:bg-control hover:text-fg group-hover:block">Use</button>
        )}
      </div>
      <div className="mt-0.5 line-clamp-2 text-2xs leading-relaxed text-dim">{guide.summary}</div>
    </div>
  );
}
