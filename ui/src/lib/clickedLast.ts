import { useEffect, useRef } from "react";

/** Whether a key is typed into a text field, where it's the field's and not a shortcut. */
export function typing(e: KeyboardEvent) {
  const t = e.target as HTMLElement | null;
  return !!t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA" || t.isContentEditable);
}

/** Whether the panel in `root` is what was clicked last, so its editing keys apply to it and no
    other panel. Picking from a menu (one it opened, say) leaves this as it was. */
export function useClickedLast(root: React.RefObject<HTMLElement | null>) {
  const active = useRef(false);
  useEffect(() => {
    const onDown = (e: PointerEvent) => {
      const target = e.target as Element;
      if (target.closest?.("[role=menu]")) return;
      active.current = !!root.current?.contains(target);
    };
    window.addEventListener("pointerdown", onDown, true);
    return () => window.removeEventListener("pointerdown", onDown, true);
  }, [root]);
  return active;
}
