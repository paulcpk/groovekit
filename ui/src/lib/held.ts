import { useEffect, useRef, useState } from "react";

/** Whether every value set in `part` is in `whole` (numbers to within a millionth). */
export function contains(whole: unknown, part: unknown): boolean {
  if (typeof part === "number" && typeof whole === "number") return Math.abs(part - whole) < 1e-6;
  if (part && typeof part === "object") {
    if (!whole || typeof whole !== "object") return false;
    return Object.entries(part).every(([k, v]) => v === undefined || contains((whole as Record<string, unknown>)[k], v));
  }
  return part === whole;
}

/** A value edited here and saved by the engine: what was last set shows until the saved value
    catches up with it, so a control doesn't jump back to an older value (or through the ones in
    between) while the engine's answers arrive. Gives up waiting after two seconds. */
export function useHeld<T>(saved: T, key?: string): [T, (value: T) => void] {
  const [held, setHeld] = useState<{ value: T; key?: string } | undefined>(undefined);
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined);
  // A value held for something else (another layer, say) doesn't count.
  const current = held && held.key === key ? held.value : undefined;

  useEffect(() => {
    if (held && (held.key !== key || contains(saved, held.value))) setHeld(undefined);
  }, [saved, held, key]);

  useEffect(() => () => clearTimeout(timer.current), []);

  const hold = (value: T) => {
    setHeld({ value, key });
    clearTimeout(timer.current);
    timer.current = setTimeout(() => setHeld(undefined), 2000);
  };

  return [current ?? saved, hold];
}
