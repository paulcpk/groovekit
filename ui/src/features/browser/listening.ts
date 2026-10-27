// GrooveKit's ears in the UI: the listening models, how much of the library has been heard, and
// the user's styles (folders of reference tracks), kept up to date by "listening" notifications.

import { useEffect } from "react";
import { create } from "zustand";
import { call, engine, listen, useEngine } from "@/state/engine";

export interface ListeningStatus {
  models: { ready: boolean; downloading: boolean; progress: number; totalBytes: number; error?: string };
  index: { running: boolean; total: number; embedded: number; failed: number; samples: number; librarySize: number };
  styles: { running: boolean; names: string[]; style?: string; tracks?: number; analysed?: number; failed?: number; error?: string };
}

export interface Style {
  name: string;
  folder: string;
  tracks: number;
  tempo?: { median: number; min: number; max: number };
}

interface ListeningState {
  status?: ListeningStatus;
  styles: Style[];
}

export const useListening = create<ListeningState>(() => ({ styles: [] }));

async function refreshStyles() {
  const r = await engine.call<{ styles: Style[] }>("styles.list");
  useListening.setState({ styles: r.styles });
}

let lastStyleNames = "";

listen("listening", (status: ListeningStatus) => {
  useListening.setState({ status });

  // A style was added, analysed or removed.
  const names = status.styles.names.join("\n") + (status.styles.running ? "" : "\n.");
  if (names !== lastStyleNames) {
    lastStyleNames = names;
    void refreshStyles().catch(() => {});
  }
});

/** The listening status, fetched when the engine connects. */
export function useListeningStatus() {
  const connected = useEngine((s) => s.status === "connected");

  useEffect(() => {
    if (!connected) return;
    void engine.call<ListeningStatus>("listening.status").then((status) => useListening.setState({ status }), () => {});
    void refreshStyles().catch(() => {});
  }, [connected]);

  return useListening();
}

/** Whether searches can rank by sound: the models are in and some of the library has been heard. */
export function canListen(status?: ListeningStatus) {
  return !!status?.models.ready && status.index.samples > 0;
}

export const downloadModels = () => call<ListeningStatus>("listening.download").then((status) => useListening.setState({ status }));

export const addStyle = (path: string) => call<{ name: string }>("styles.add", { path });

export const removeStyle = (name: string) => call("styles.remove", { name }).then(refreshStyles);

/** The user's verdict on a sample for a style; null forgets it. */
export const rateSample = (path: string, style: string, fits: boolean | null) => call("library.rate", { path, style, fits });
