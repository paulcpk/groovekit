import { forwardRef, useEffect, useId, useRef, useState } from "react";
import { cn } from "@/lib/cn";
import { startDrag } from "@/lib/drag";

type ButtonProps = React.ButtonHTMLAttributes<HTMLButtonElement> & {
  active?: boolean;
  tone?: "default" | "accent" | "rec" | "agent";
  size?: "sm" | "md";
};

/** The glow colour of a lit control in each tone. */
const litColor = { accent: "var(--color-accent)", rec: "var(--color-rec)", agent: "var(--color-agent)" } as const;

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button(
  { active, tone = "default", size = "md", className, style, ...props }, ref) {
  const activeTone = {
    default: "bg-hover text-fg gk-key",
    accent: "gk-lit text-black",
    rec: "gk-lit text-white",
    agent: "gk-lit text-black",
  }[tone];
  // Buttons drawn on a control background (rather than only on hover) are keys.
  const keyed = !active && /(^|\s)bg-control(\s|$)/.test(className ?? "");

  return (
    <button
      ref={ref}
      type="button"
      className={cn(
        "gk-press inline-flex shrink-0 items-center justify-center gap-1.5 rounded-[4px] whitespace-nowrap transition-colors disabled:opacity-40",
        size === "sm" ? "h-[22px] px-1.5 text-xs" : "h-7 px-2.5 text-sm",
        active ? activeTone : "text-dim hover:bg-control hover:text-fg",
        keyed && "gk-key",
        className,
      )}
      style={active && tone !== "default" ? ({ "--lit": litColor[tone], ...style } as React.CSSProperties) : style}
      {...props}
    />
  );
});

export function IconButton({ className, size = "md", ...props }: ButtonProps) {
  return <Button size={size} className={cn(size === "sm" ? "w-[22px] px-0" : "w-7 px-0", className)} {...props} />;
}

/** Small square toggles for mute/solo-like states. */
export function ToggleChip({ on, onClick, label, title, tone = "accent", className }: {
  on: boolean; onClick: (e: React.MouseEvent) => void; label: string; title?: string; tone?: "accent" | "rec" | "dim"; className?: string;
}) {
  const onClass = tone === "rec" ? "text-white" : tone === "dim" ? "text-fg" : "text-black";
  const lit = tone === "rec" ? "var(--color-rec)" : tone === "dim" ? "var(--color-faint)" : "var(--color-accent)";
  return (
    <button
      type="button"
      title={title}
      onClick={onClick}
      onPointerDown={(e) => e.stopPropagation()}
      style={on ? ({ "--lit": lit } as React.CSSProperties) : undefined}
      className={cn("gk-press h-[18px] min-w-[18px] rounded-[3px] px-1 text-2xs font-semibold leading-none", on ? cn("gk-lit", onClass) : "gk-key bg-control text-dim hover:text-fg", className)}
    >
      {label}
    </button>
  );
}

//==============================================================================
/** A rotary control. Drag up and down (Shift for fine steps), double-click for the default. */
export function Knob({
  value, min, max, defaultValue, onChange, onCommit, format, size = 28, bipolar, color = "var(--color-accent)", title, label,
}: {
  value: number; min: number; max: number; defaultValue?: number;
  onChange?: (v: number) => void; onCommit: (v: number) => void;
  format?: (v: number) => string; size?: number; bipolar?: boolean; color?: string; title?: string; label?: string;
}) {
  const [live, setLive] = useState<number | null>(null);
  const id = useId();
  const shown = live ?? value;
  const t = (Math.min(max, Math.max(min, shown)) - min) / (max - min || 1);
  const start = -135, sweep = 270;
  const angle = start + t * sweep;
  const origin = bipolar ? start + 0.5 * sweep : start;
  const r = size / 2 - 3;
  const c = size / 2;

  const arc = (a0: number, a1: number) => {
    const [from, to] = a0 < a1 ? [a0, a1] : [a1, a0];
    const p = (a: number) => [c + r * Math.sin((a * Math.PI) / 180), c - r * Math.cos((a * Math.PI) / 180)];
    const [x0, y0] = p(from), [x1, y1] = p(to);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${to - from > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };

  const text = format ? format(shown) : shown.toFixed(2);

  return (
    <div className="flex flex-col items-center gap-0.5" title={title ? `${title}: ${text}` : text}>
      <svg
        width={size} height={size}
        className="cursor-ns-resize"
        onDoubleClick={() => defaultValue !== undefined && onCommit(defaultValue)}
        onPointerDown={(e) => {
          e.stopPropagation();
          const v0 = value;
          let v = v0;
          startDrag(e, (_dx, dy, ev) => {
            const range = (max - min) * (ev.shiftKey ? 0.1 : 1);
            v = Math.min(max, Math.max(min, v0 - (dy / 150) * range));
            setLive(v);
            onChange?.(v);
          }, (_dx, _dy, _ev, moved) => {
            setLive(null);
            if (moved) onCommit(v);
          }, "ns-resize");
        }}
      >
        <defs>
          {/* A domed cap, lit from above like the pads. */}
          <radialGradient id={`${id}-cap`} cx="50%" cy="30%" r="75%">
            <stop offset="0" stopColor="#4a4a53" />
            <stop offset="0.6" stopColor="#2e2e35" />
            <stop offset="1" stopColor="#1c1c21" />
          </radialGradient>
        </defs>
        <path d={arc(start, start + sweep)} stroke="#2a2a30" strokeWidth={3} fill="none" strokeLinecap="round" />
        {Math.abs(angle - origin) > 0.5 && (
          <path d={arc(origin, angle)} stroke={color} strokeWidth={3} fill="none" strokeLinecap="round"
            style={{ filter: `drop-shadow(0 0 2px color-mix(in srgb, ${color} 70%, transparent))` }} />
        )}
        <circle cx={c} cy={c + 0.6} r={Math.max(2, r - 3.5)} fill="rgb(0 0 0 / 0.5)" />
        <circle cx={c} cy={c} r={Math.max(2, r - 3.5)} fill={`url(#${id}-cap)`} stroke="rgb(0 0 0 / 0.55)" strokeWidth={0.75} />
        <path d={`M ${c - (r - 5)} ${c - 0.6} A ${r - 5} ${r - 5} 0 0 1 ${c + (r - 5)} ${c - 0.6}`} stroke="rgb(255 255 255 / 0.12)" strokeWidth={0.75} fill="none" />
        <line
          x1={c + (r - 9) * Math.sin((angle * Math.PI) / 180) * 0.2} y1={c - (r - 9) * Math.cos((angle * Math.PI) / 180) * 0.2}
          x2={c + (r - 5) * Math.sin((angle * Math.PI) / 180)} y2={c - (r - 5) * Math.cos((angle * Math.PI) / 180)}
          stroke="#e6e6ea" strokeWidth={1.5} strokeLinecap="round"
        />
      </svg>
      {label !== undefined && <div className="max-w-[64px] truncate text-2xs text-dim">{label}</div>}
    </div>
  );
}

//==============================================================================
/** A number you drag (up/down) or double-click to type. */
export function DragNumber({
  value, min, max, step = 1, pixelsPerStep = 3, format, parse, onCommit, className, title,
}: {
  value: number; min: number; max: number; step?: number; pixelsPerStep?: number;
  format?: (v: number) => string; parse?: (s: string) => number; onCommit: (v: number) => void; className?: string; title?: string;
}) {
  const [live, setLive] = useState<number | null>(null);
  const [editing, setEditing] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const shown = live ?? value;
  const text = format ? format(shown) : String(shown);

  useEffect(() => {
    if (editing) input.current?.select();
  }, [editing]);

  if (editing) {
    return (
      <input
        ref={input}
        defaultValue={String(value)}
        className={cn("w-16 rounded-[3px] bg-bg px-1 text-fg outline-none ring-1 ring-accent tabular", className)}
        onBlur={(e) => {
          setEditing(false);
          const v = parse ? parse(e.target.value) : Number(e.target.value);
          if (Number.isFinite(v)) onCommit(Math.min(max, Math.max(min, v)));
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "Escape") setEditing(false);
          e.stopPropagation();
        }}
      />
    );
  }

  return (
    <span
      title={title}
      className={cn("cursor-ns-resize tabular", className)}
      onDoubleClick={() => setEditing(true)}
      onPointerDown={(e) => {
        const v0 = value;
        let v = v0;
        startDrag(e, (_dx, dy, ev) => {
          const steps = Math.round(-dy / (ev.shiftKey ? pixelsPerStep * 5 : pixelsPerStep));
          v = Math.min(max, Math.max(min, v0 + steps * step));
          setLive(v);
        }, (_dx, _dy, _ev, moved) => {
          setLive(null);
          if (moved && v !== v0) onCommit(v);
        }, "ns-resize");
      }}
    >
      {text}
    </span>
  );
}

//==============================================================================
export function Tabs<T extends string>({ value, onChange, items, className }: {
  value: T; onChange: (v: T) => void; items: { value: T; label: React.ReactNode; title?: string }[]; className?: string;
}) {
  return (
    <div className={cn("flex items-center gap-0.5", className)}>
      {items.map((item) => (
        <button
          key={item.value}
          type="button"
          title={item.title}
          onClick={() => onChange(item.value)}
          className={cn(
            "h-6 rounded-[4px] px-2 text-xs transition-colors",
            value === item.value ? "gk-key bg-control text-fg" : "text-dim hover:text-fg",
          )}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}

export function Panel({ className, ...props }: React.HTMLAttributes<HTMLDivElement>) {
  return <div className={cn("bg-panel", className)} {...props} />;
}

export function Empty({ children, className }: { children: React.ReactNode; className?: string }) {
  return <div className={cn("flex h-full items-center justify-center p-6 text-center text-xs text-faint", className)}>{children}</div>;
}
