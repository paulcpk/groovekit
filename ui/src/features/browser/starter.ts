// The starter samples: a pack of public-domain drum machines, one-shots and loops the engine
// downloads once into the library's folder. Its status arrives as "starter" notifications
// while it downloads.

import { create } from "zustand";
import { call, engine, listen, useEngine } from "@/state/engine";

export interface StarterStatus {
  installed: boolean;
  downloading: boolean;
  /** 0..1 while downloading. */
  progress: number;
  totalBytes: number;
  files: number;
  folder: string;
  error?: string | null;
}

export const useStarter = create<{ status?: StarterStatus }>(() => ({}));

listen("starter", (status: StarterStatus) => useStarter.setState({ status }));

export async function refreshStarter() {
  try {
    useStarter.setState({ status: await engine.call<StarterStatus>("library.starter") });
  } catch {
    // An engine without the pack (tests, older builds): nothing to offer.
    useStarter.setState({ status: undefined });
  }
}

export const downloadStarter = () => call<StarterStatus>("library.downloadStarter").then((status) => useStarter.setState({ status }));

useEngine.subscribe((s, previous) => {
  if (s.status === "connected" && previous.status !== "connected") void refreshStarter();
});
