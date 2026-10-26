import type { AutomationClip, AutomationTarget, Project } from "@groovekit/schema/project";
import { songEnd, ticksPerBar, uniqueId } from "@/lib/model";
import { execute } from "@/state/engine";
import { setUi } from "@/state/ui";
import { targetLabel } from "@/lib/automation";

/** An automation clip for a parameter over the song (or 8 bars), on a new track at the bottom. */
export async function createAutomation(doc: Project, target: AutomationTarget, value: number) {
  const length = Math.max(8 * ticksPerBar(doc), songEnd(doc));
  const label = targetLabel(target, doc);
  const track = uniqueId("automation", doc.playlist.tracks);
  const clip: AutomationClip = {
    type: "automation", id: uniqueId("auto", doc.playlist.clips), track, start: 0, length, target,
    points: [{ time: 0, value }, { time: length, value }],
  };

  await execute([
    { type: "addTrack", track: { id: track, name: label } },
    { type: "addClip", clip },
  ], { label: `Automate ${label}` });
  setUi({ selectedClips: [clip.id] });
}
