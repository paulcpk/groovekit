import { useLayoutEffect, useRef, useState } from "react";

/** An element's height, kept up to date. */
export function useHeight<T extends HTMLElement>(fallback = 100) {
  const ref = useRef<T>(null);
  const [height, setHeight] = useState(fallback);

  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const observer = new ResizeObserver(() => setHeight(el.clientHeight));
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  return [ref, height] as const;
}
