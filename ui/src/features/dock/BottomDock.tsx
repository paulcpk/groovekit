import { Tabs } from "@/components/controls";
import { ChannelRack } from "@/features/rack/ChannelRack";
import { Mixer } from "@/features/mixer/Mixer";
import { PianoRoll } from "@/features/piano/PianoRoll";
import { setUi, useUi, type BottomPanel } from "@/state/ui";

export function BottomDock() {
  const panel = useUi((s) => s.bottomPanel);

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-8 shrink-0 items-center border-b border-line px-2" data-tour="dock">
        <Tabs<BottomPanel>
          value={panel}
          onChange={(p) => setUi({ bottomPanel: p })}
          items={[
            { value: "rack", label: "Channel rack", title: "F6" },
            { value: "piano", label: "Piano roll", title: "F7" },
            { value: "mixer", label: "Mixer", title: "F9" },
          ]}
        />
      </div>
      <div className="min-h-0 flex-1">
        {panel === "rack" && <ChannelRack />}
        {panel === "piano" && <PianoRoll />}
        {panel === "mixer" && <Mixer />}
      </div>
    </div>
  );
}
