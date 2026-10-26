export const HEADER_W = 200;
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

/** The sample being dragged from the browser. Kept here too, since WebKit doesn't always pass
    custom types through the drag data. */
let draggedSample: DraggedSample | undefined;

export function startSampleDrag(e: React.DragEvent, sample: DraggedSample) {
  draggedSample = sample;
  e.dataTransfer.setData(SAMPLE_MIME, JSON.stringify(sample));
  e.dataTransfer.setData("text/plain", sample.path);
  e.dataTransfer.effectAllowed = "copy";
}

export function endSampleDrag() {
  draggedSample = undefined;
}

export function isSampleDrag(e: React.DragEvent) {
  return draggedSample !== undefined || e.dataTransfer.types.includes(SAMPLE_MIME);
}

/** The sample a drop carries, if it's one from the browser. */
export function droppedSample(e: React.DragEvent): DraggedSample | undefined {
  const data = e.dataTransfer.getData(SAMPLE_MIME);
  const sample = data ? (JSON.parse(data) as DraggedSample) : draggedSample;
  draggedSample = undefined;
  return sample;
}
