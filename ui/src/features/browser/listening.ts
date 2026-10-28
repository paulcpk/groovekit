// GrooveKit's ears in the UI: the listening models, how much of the library has been heard, the
// user's styles (folders of reference tracks), kept up to date by "listening" notifications, and
// the references folder that holds them.

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
  /** The style guide it goes with, by name. */
  guide?: string | null;
}

/** A folder of reference tracks in the references folder, against the style heard from it. */
export interface ReferenceFolder {
  name: string;
  path: string;
  files: number;
  style?: string | null;
  /** Tracks not heard yet (new or changed), and heard ones no longer there. */
  unheard: number;
  gone: number;
  guide?: string | null;
}

export interface References {
  folder: string;
  exists: boolean;
  folders: ReferenceFolder[];
}

interface ListeningState {
  status?: ListeningStatus;
  styles: Style[];
  references?: References;
}

export const useListening = create<ListeningState>(() => ({ styles: [] }));

async function refreshStyles() {
  const r = await engine.call<{ styles: Style[] }>("styles.list");
  useListening.setState({ styles: r.styles });
  await refreshReferences();
}

/** Reads the references folder again (its folders change on disk without the engine knowing). */
export async function refreshReferences() {
  const references = await engine.call<References>("styles.references");
  useListening.setState({ references });
  return references;
}

export const createReferences = () => call<References>("styles.createReferences").then((references) => useListening.setState({ references }));

/** A folder in the references folder that has tracks GrooveKit hasn't heard, or has lost some. */
export const needsHearing = (f: ReferenceFolder) => f.files > 0 && (!f.style || f.unheard > 0 || f.gone > 0);

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

/** The user's verdict on a sample for a style; null forgets it. */
export const rateSample = (path: string, style: string, fits: boolean | null) => call("library.rate", { path, style, fits });
