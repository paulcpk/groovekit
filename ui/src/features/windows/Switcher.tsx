import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { targetIcon, targetLabel } from "./labels";
import { commitSwitcher, switcherLayout, useSwitcher } from "./switcherState";
import type { WindowTarget } from "@/state/windows";

/** The bar of open windows while Cmd is held after Cmd+Left/Right: a small picture of each with
    its name, the chosen one ringed. In the windows' layer, under them: GrooveKit's own windows
    sit in their tiles themselves (FloatingWindow), live. */
export function Switcher() {
  const { open, targets, index, tiles } = useSwitcher();
  if (!open || !targets.length) return null;

  const first = tiles[targets[0]], last = tiles[targets[targets.length - 1]];
  const { pad } = switcherLayout;

  return (
    <>
      <div className="pointer-events-auto absolute inset-0" style={{ zIndex: 0 }} onClick={() => commitSwitcher()} />
      <div className="pointer-events-none absolute rounded-2xl border border-line bg-panel/90 shadow-[0_24px_70px_rgb(0_0_0/0.6)] backdrop-blur-md"
        style={{ zIndex: 1, left: first.x - pad, top: first.y - pad, width: last.x + last.width - first.x + pad * 2, height: first.height + pad * 2 }} />
      {targets.map((target, i) => <Tile key={target} target={target} selected={i === index} index={i} />)}
    </>
  );
}

function Tile({ target, selected, index }: { target: WindowTarget; selected: boolean; index: number }) {
  const tile = useSwitcher((s) => s.tiles[target]);
  const picture = useSwitcher((s) => s.pictures[target]);
  const label = useEngine((s) => targetLabel(target, s.doc));
  const { label: labelHeight, inset } = switcherLayout;
  const Icon = targetIcon(target);
  const ours = !target.startsWith("plugin:") && target !== "main";

  return (
    <button type="button" title={label}
      className={cn("pointer-events-auto absolute flex flex-col rounded-xl outline-none", selected ? "bg-hover ring-2 ring-accent" : "hover:bg-hover/60")}
      style={{ zIndex: 1, left: tile.x, top: tile.y, width: tile.width, height: tile.height }}
      onPointerEnter={() => useSwitcher.setState({ index })}
      onClick={() => commitSwitcher(target)}>
      <div className="relative min-h-0 flex-1" style={{ margin: inset, marginBottom: 0 }}>
        {target === "main" ? <ArrangementPicture /> : picture ? (
          <img src={picture} alt="" draggable={false} className="absolute inset-0 h-full w-full rounded-md object-contain" />
        ) : !ours && (
          <div className="absolute inset-0 flex items-center justify-center rounded-md bg-bg/60 text-dim"><Icon size={26} strokeWidth={1.6} /></div>
        )}
      </div>
      <div className={cn("flex shrink-0 items-center justify-center gap-1 truncate px-1.5 text-[11px]", selected ? "text-fg" : "text-dim")} style={{ height: labelHeight }}>
        <Icon size={11} className="shrink-0" /><span className="truncate">{label}</span>
      </div>
    </button>
  );
}

/** The app's own window, drawn small: the top bar, the rows of the arrangement with their clips. */
function ArrangementPicture() {
  const rows = [[0.04, 0.5, 0.56, 0.9], [0.04, 0.9], [0.3, 0.5, 0.62, 0.82], [0.12, 0.38, 0.5, 0.74], [0.56, 0.96]];
  const colors = ["var(--color-accent)", "#7aa2f7", "#9ece6a", "#e0af68", "#bb9af7"];
  return (
    <div className="absolute inset-0 flex flex-col gap-[3px] overflow-hidden rounded-md border border-line bg-bg p-[5px]">
      <div className="h-[6px] shrink-0 rounded-sm bg-raised" />
      <div className="flex min-h-0 flex-1 flex-col justify-around">
        {rows.map((clips, r) => (
          <div key={r} className="relative h-[14%] rounded-[2px] bg-panel">
            {Array.from({ length: clips.length / 2 }, (_, i) => (
              <div key={i} className="absolute inset-y-0 rounded-[2px] opacity-80"
                style={{ left: `${clips[i * 2] * 100}%`, width: `${(clips[i * 2 + 1] - clips[i * 2]) * 100}%`, background: colors[r] }} />
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}
