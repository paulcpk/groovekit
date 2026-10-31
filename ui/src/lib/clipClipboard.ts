import type { Clip } from "@groovekit/schema/project";

/** Playlist clips copied with ⌘C, pasted with ⌘V as clips of their own (new ids, the same rows
    and patterns, channels or automation). */
let clips: Clip[] = [];

export function copyClips(copied: Clip[]) {
  clips = copied.map((c) => ({ ...c }));
}

export function copiedClips(): Clip[] {
  return clips.map((c) => ({ ...c }));
}
