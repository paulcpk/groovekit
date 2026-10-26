export const HEADER_W = 172;
export const RULER_H = 30;
export const LOOP_STRIP_H = 11;
export const CLIP_HEADER_H = 14;

export interface Geometry {
  pxPerTick: number;
  trackHeight: number;
  /** Snap step in ticks. */
  snap: number;
  bar: number;
  beat: number;
}

/** The sample mime type the browser drags, with JSON describing the sample. */
export const SAMPLE_MIME = "application/x-groovekit-sample";

export interface DraggedSample {
  path: string;
  durationSeconds?: number;
  loop?: boolean;
  tempo?: number;
  category?: string;
}
