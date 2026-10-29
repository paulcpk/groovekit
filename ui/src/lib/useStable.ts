import { useCallback, useRef } from "react";

/** A callback whose identity never changes but which always calls the latest function given,
    so children that take it can be memoized. */
export function useStable<A extends unknown[], R>(fn: (...args: A) => R): (...args: A) => R {
  const ref = useRef(fn);
  ref.current = fn;
  return useCallback((...args: A) => ref.current(...args), []);
}
