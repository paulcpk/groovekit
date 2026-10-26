import type { DraggedSample } from "@/features/playlist/geometry";
import { call, toast, useEngine } from "@/state/engine";
import { addSamplerChannel } from "@/features/rack/channels";

const audioFile = /\.(wav|aiff?|flac|mp3|ogg|m4a|caf)$/i;
const extensions = ["wav", "aif", "aiff", "flac", "mp3", "ogg", "m4a", "caf"];

/** True while files (from Finder) are dragged over the window. */
export function isFileDrag(e: React.DragEvent) {
  return e.dataTransfer.types.includes("Files");
}

async function toBase64(file: File) {
  const bytes = new Uint8Array(await file.arrayBuffer());
  let binary = "";
  for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary);
}

/** Copies dropped audio files into the project's samples folder; the web view has their bytes, not their paths. */
export async function importDroppedFiles(e: React.DragEvent): Promise<DraggedSample[]> {
  const files = [...e.dataTransfer.files].filter((f) => audioFile.test(f.name));
  if (!files.length) {
    toast("Drop audio files: WAV, AIFF, FLAC, MP3, OGG");
    return [];
  }
  const samples: DraggedSample[] = [];
  for (const file of files) {
    const { path } = await call<{ path: string }>("project.importData", { name: file.name, data: await toBase64(file) });
    samples.push({ path });
  }
  return samples;
}

/** Picks audio files with the system's dialog (desktop app), copies them into the project and adds a channel for each. */
export async function importAudio() {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const picked = await open({ title: "Import audio", multiple: true, filters: [{ name: "Audio", extensions }] });
  const paths = Array.isArray(picked) ? picked : picked ? [picked] : [];

  for (const source of paths) {
    const { path } = await call<{ path: string }>("project.importFile", { path: source });
    const doc = useEngine.getState().doc;
    if (doc) await addSamplerChannel(doc, { path });
  }
  if (paths.length) toast(`Imported ${paths.length} file${paths.length === 1 ? "" : "s"} into the project's samples folder`);
}

/** Copies every sample the song uses from outside the project folder into it. */
export async function collectSamples() {
  const r = await call<{ copied: number }>("project.collectSamples");
  toast(r.copied ? `Copied ${r.copied} sample${r.copied === 1 ? "" : "s"} into the project` : "Every sample is already in the project");
}
