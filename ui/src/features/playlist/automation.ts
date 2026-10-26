import type { AutomationClip, AutomationTarget, Project } from "@groovekit/schema/project";
import { firstEmptyLane, songEnd, ticksPerBar, uniqueId } from "@/lib/model";
import { execute } from "@/state/engine";
import { setUi } from "@/state/ui";
import { targetLabel } from "@/lib/automation";

/** An automation clip for a parameter over the song (or 8 bars), on the first empty track below
    the song's clips. */
export async function createAutomation(doc: Project, target: AutomationTarget, value: number) {
  const length = Math.max(8 * ticksPerBar(doc), songEnd(doc));
  const label = targetLabel(target, doc);
  const track = firstEmptyLane(doc, Math.max(0, ...doc.playlist.clips.map((c) => c.track)));
  const clip: AutomationClip = {
    type: "automation", id: uniqueId("auto", doc.playlist.clips), track, start: 0, length, target,
    points: [{ time: 0, value }, { time: length, value }],
  };

  await execute([
    ...(doc.playlist.tracks[track]?.name ? [] : [{ type: "updateTrack" as const, track, changes: { name: label } }]),
    { type: "addClip", clip },
  ], { label: `Automate ${label}` });
  setUi({ selectedClips: [clip.id] });
}
