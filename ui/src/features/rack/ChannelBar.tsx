import type { Channel, Project } from "@groovekit/schema/project";
import { ChevronDown } from "lucide-react";
import { Knob } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { Automatable } from "@/features/playlist/Automatable";
import { useAutomated } from "@/lib/automation";
import { useLiveControl } from "@/lib/liveControl";
import { formatDb, formatPan, insertName } from "@/lib/model";
import { execute } from "@/state/engine";
import { routeToNewInsert } from "./channels";

/** The inserts a channel can play into, as dropdown items. */
export function InsertItems({ doc, channel }: { doc: Project; channel: Channel }) {
  const current = channel.insert ?? "master";
  return (
    <>
      <DropdownLabel>Plays into mixer insert</DropdownLabel>
      {doc.mixer.inserts.map((insert, i) => (
        <DropdownItem key={insert.id} checked={insert.id === current}
          onSelect={() => void execute({ type: "updateChannel", channel: channel.id, changes: { insert: insert.id === "master" ? null : insert.id } },
            { label: `Route ${channel.name || channel.id} to ${insertName(insert, i)}` })}>
          {i === 0 ? "" : `${i} · `}{insertName(insert, i)}
        </DropdownItem>
      ))}
      <DropdownSeparator />
      <DropdownItem onSelect={() => void routeToNewInsert(doc, channel)}>New insert</DropdownItem>
    </>
  );
}

/** Where a channel plays and how loud: its mixer insert, volume and pan. Right-click the knobs
    to automate them. */
export function ChannelBar({ doc, channel }: { doc: Project; channel: Channel }) {
  const volume = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { volumeDb: Math.round(v * 10) / 10 } }), "Channel volume");
  const pan = useLiveControl((v) => ({ type: "updateChannel", channel: channel.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan channel");
  const volumeShown = useAutomated({ scope: "channel", channel: channel.id, param: "volume" }, channel.volumeDb ?? 0);
  const panShown = useAutomated({ scope: "channel", channel: channel.id, param: "pan" }, channel.pan ?? 0);
  const index = doc.mixer.inserts.findIndex((i) => i.id === (channel.insert ?? "master"));

  return (
    <div className="flex items-center gap-3 rounded-md bg-raised px-2 py-1.5">
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <span className="text-2xs text-faint">Mixer insert</span>
        <Dropdown>
          <DropdownTrigger asChild>
            <button type="button" data-no-drag
              className="flex h-6 min-w-0 items-center gap-1 rounded-[4px] bg-control px-2 text-left text-xs text-fg hover:bg-hover">
              <span className="text-dim tabular">{index <= 0 ? "M" : index}</span>
              <span className="min-w-0 flex-1 truncate">{insertName(doc.mixer.inserts[index], index)}</span>
              <ChevronDown size={11} className="shrink-0 text-dim" />
            </button>
          </DropdownTrigger>
          <DropdownContent>
            <InsertItems doc={doc} channel={channel} />
          </DropdownContent>
        </Dropdown>
      </div>
      <Automatable target={{ scope: "channel", channel: channel.id, param: "volume" }} value={channel.volumeDb ?? 0}>
        <Knob size={28} label="Volume" value={volumeShown} min={-60} max={12} defaultValue={0} format={(v) => `${formatDb(v)} dB`}
          onChange={volume.onChange} onCommit={volume.onCommit} />
      </Automatable>
      <Automatable target={{ scope: "channel", channel: channel.id, param: "pan" }} value={channel.pan ?? 0}>
        <Knob size={28} label="Pan" value={panShown} min={-1} max={1} defaultValue={0} bipolar format={formatPan}
          onChange={pan.onChange} onCommit={pan.onCommit} />
      </Automatable>
    </div>
  );
}
