import { Popover } from "radix-ui";
import { Check } from "lucide-react";
import { useState } from "react";
import { cn } from "@/lib/cn";
import { palette } from "@/lib/model";

/** A small square in a part's colour; clicking it picks another: one of the palette, or any
    colour. onChange gets null to go back to the part's own colour. */
export function ColorSwatch({ color, chosen, onChange, title = "Colour", size = 12, className }: {
  /** The colour shown (the chosen one, or the part's own). */
  color: string;
  /** Whether a colour was chosen rather than given. */
  chosen: boolean;
  onChange: (color: string | null) => void;
  title?: string;
  size?: number;
  className?: string;
}) {
  const [open, setOpen] = useState(false);
  const [custom, setCustom] = useState(color);

  const pick = (c: string | null) => {
    onChange(c);
    setOpen(false);
  };

  return (
    <Popover.Root open={open} onOpenChange={(o) => {
      setOpen(o);
      if (o) setCustom(color);
    }}>
      <Popover.Trigger asChild>
        <button type="button" title={title} onPointerDown={(e) => e.stopPropagation()} onDoubleClick={(e) => e.stopPropagation()}
          className={cn("shrink-0 rounded-[3px] ring-1 ring-black/40 transition-transform hover:scale-110 hover:ring-fg/60", className)}
          style={{ width: size, height: size, background: color }} />
      </Popover.Trigger>
      <Popover.Portal>
        <Popover.Content side="bottom" align="start" sideOffset={4} onClick={(e) => e.stopPropagation()}
          className="z-50 rounded-lg border border-line bg-raised p-2 shadow-[0_12px_32px_rgb(0_0_0/0.5)]">
          <div className="grid grid-cols-6 gap-1">
            {palette.map((c) => (
              <button key={c} type="button" title={c} onClick={() => pick(c)}
                className="flex h-5 w-5 items-center justify-center rounded-[3px] ring-1 ring-black/40 hover:ring-fg/70" style={{ background: c }}>
                {chosen && c.toLowerCase() === color.toLowerCase() && <Check size={11} strokeWidth={3} className="text-black/70" />}
              </button>
            ))}
          </div>
          <div className="mt-2 flex items-center gap-1.5">
            <label className="flex h-6 flex-1 cursor-pointer items-center gap-1.5 rounded-[4px] bg-control px-1.5 text-2xs text-dim hover:text-fg" title="Any colour">
              <input type="color" value={custom} onChange={(e) => setCustom(e.target.value)} onBlur={() => custom !== color && onChange(custom)}
                className="h-3.5 w-3.5 cursor-pointer appearance-none rounded-[2px] border-0 bg-transparent p-0 [&::-webkit-color-swatch]:rounded-[2px] [&::-webkit-color-swatch]:border-0 [&::-webkit-color-swatch-wrapper]:p-0" />
              Custom
            </label>
            {custom !== color && (
              <button type="button" onClick={() => pick(custom)} className="h-6 rounded-[4px] bg-agent px-2 text-2xs text-black hover:brightness-110">Apply</button>
            )}
            {chosen && (
              <button type="button" onClick={() => pick(null)} className="h-6 rounded-[4px] px-1.5 text-2xs text-dim hover:bg-control hover:text-fg" title="Back to its own colour">
                Reset
              </button>
            )}
          </div>
        </Popover.Content>
      </Popover.Portal>
    </Popover.Root>
  );
}
