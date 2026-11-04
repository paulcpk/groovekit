import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { targetIcon, targetLabel } from "./labels";
import { commitSwitcher, useSwitcher } from "./switcherState";

/** The bar of open windows while Cmd is held after Cmd+Left/Right: an icon for each, the chosen
    one named under them. Over everything, as Cmd+Tab's is. */
export function Switcher() {
  const { open, targets, index } = useSwitcher();
  const doc = useEngine((s) => s.doc);
  if (!open) return null;

  return (
    <div className="pointer-events-none fixed inset-0 z-[70] flex items-center justify-center">
      <div className="pointer-events-auto flex max-w-[calc(100vw-64px)] flex-col items-center gap-2 rounded-2xl border border-line bg-panel/90 p-3 shadow-[0_24px_70px_rgb(0_0_0/0.6)] backdrop-blur-md">
        <div className="flex max-w-full items-center gap-1.5 overflow-hidden">
          {targets.map((target, i) => {
            const Icon = targetIcon(target);
            return (
              <button key={target} type="button" title={targetLabel(target, doc)}
                className={cn("flex h-[72px] w-[72px] shrink-0 items-center justify-center rounded-xl text-dim",
                  i === index ? "bg-hover text-fg ring-2 ring-accent" : "hover:bg-hover/60")}
                onPointerEnter={() => useSwitcher.setState({ index: i })}
                onClick={() => commitSwitcher(target)}>
                <Icon size={30} strokeWidth={1.6} className={target === "main" ? "text-accent" : undefined} />
              </button>
            );
          })}
        </div>
        <div className="max-w-full truncate px-2 text-sm text-fg">{targets[index] ? targetLabel(targets[index], doc) : ""}</div>
      </div>
    </div>
  );
}
