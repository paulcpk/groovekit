import { RotateCw, Trash2 } from "lucide-react";
import { Fragment, useEffect, useState } from "react";
import { IconButton, ToggleChip } from "@/components/controls";
import { ArrangementView } from "./ArrangementView";
import { KickMatches } from "./KickMatches";
import { FloatingWindow } from "@/components/window";
import { useStopPreviewsOnClose } from "@/features/shell/actions";
import { noteName } from "@/lib/model";
import { closeWindow, type OpenWindow } from "@/state/windows";
import { forget, getMap, hear, setInUse, useMaps, type Figure, type SongMap, type Voice } from "./maps";

/** The parts the hearing levels against the whole mix. */
const PARTS: [string, string][] = [["kick", "Kick and rumble"], ["bass", "Bass"], ["drums", "Drums"], ["hats", "Hats"], ["music", "Music"]];

const SOUNDS: Record<string, string> = {
  mix: "mix", drums: "drums", kick: "kick and rumble", hats: "hats", bass: "bass", music: "music", kickHit: "kick hits", hatHit: "hat hits", percHit: "other hits",
};

const date = (ms: number) => new Date(ms).toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" });

/** A song's map: what the hearing found in it, whether the style learns from it, and hearing it
    again or forgetting it. */
export function MapWindow({ window: w, id }: { window: OpenWindow; id: string }) {
  const song = useMaps((s) => s.songs.find((x) => x.id === id));
  useStopPreviewsOnClose();
  const [map, setMap] = useState<SongMap | null>();

  // Read again when it's heard again or ticked.
  useEffect(() => {
    void getMap(id).then(setMap, () => setMap(null));
  }, [id, song?.map?.heardAt, song?.inUse]);

  const title = map?.title ?? id.replace(/^[^/]*\//, "");
  const t = map?.track;

  return (
    <FloatingWindow window={w} width={540} accent="var(--color-agent)"
      title={<><span className="truncate">{title}</span><span className="shrink-0 text-2xs font-normal text-faint">Song map</span></>}
      extra={map && (
        <div className="flex items-center gap-1">
          <ToggleChip on={map.inUse} tone="agent" label={map.inUse ? "In use" : "Not in use"}
            title={map.inUse ? "The style learns from this song; click to leave it out" : "Click to let the style learn from it"}
            onClick={() => void setInUse([id], !map.inUse)} />
          <IconButton size="sm" title={song?.file ? "Hear it again" : "Its audio isn't on this Mac"} disabled={!song?.file || song.status === "hearing" || song.status === "queued"}
            onClick={() => void hear([id])}><RotateCw size={12} /></IconButton>
          <IconButton size="sm" title="Forget: delete the map and don't hear it again"
            onClick={() => void forget([id]).then(() => closeWindow(w.key))}><Trash2 size={12} /></IconButton>
        </div>
      )}>
      <div className="overflow-y-auto px-4 py-3 text-xs text-fg select-text">
        {map === undefined && <div className="text-faint">…</div>}
        {map === null && <div className="text-faint">This song has no map (yet).</div>}
        {map && t && (
          <>
            <div className="text-2xs text-dim">
              {map.style} · {map.source === "imported" ? "imported" : `heard ${date(map.heardAt)}`}
              {!map.current && <span className="text-accent"> · heard by an older hearing</span>}
              {song?.status === "queued" && " · queued to be heard again"}
              {song?.status === "hearing" && <span className="text-agent"> · being heard again</span>}
            </div>

            <div className="mt-3 grid grid-cols-4 gap-1.5">
              <Measure label="Tempo" value={t.tempo > 0 ? t.tempo.toFixed(1) : "?"} unit="BPM" />
              <Measure label="Swing" value={t.swing !== undefined ? `${Math.round(t.swing * 100)}` : "–"} unit={t.swing !== undefined ? "%" : ""} />
              <Measure label="Hits a beat" value={t.density > 0 ? t.density.toFixed(1) : "–"} unit="in the highs" />
              <Measure label="Loudness" value={t.lufs.toFixed(1)} unit="LUFS" />
            </div>

            <Section title="Parts against the mix">
              {PARTS.filter(([role]) => t.levels[role] !== undefined).map(([role, label]) => (
                <div key={role} className="flex h-5 items-center gap-2">
                  <span className="w-24 shrink-0 text-dim">{label}</span>
                  <div className="h-1.5 flex-1 overflow-hidden rounded-full bg-control">
                    <div className="h-full rounded-full bg-agent/70" style={{ width: `${Math.max(0, Math.min(1, (t.levels[role] + 40) / 40)) * 100}%` }} />
                  </div>
                  <span className="w-14 shrink-0 text-right text-2xs text-dim tabular">{t.levels[role].toFixed(1)} dB</span>
                </div>
              ))}
            </Section>

            {t.balance.length > 0 && (
              <Section title="Spectral balance, low to high">
                <Balance bands={t.balance} />
              </Section>
            )}

            <Section title="Sounds heard">
              <div className="flex flex-wrap gap-1">
                {Object.entries(t.sounds).filter(([, n]) => n > 0).map(([role, n]) => (
                  <span key={role} className="rounded-[3px] bg-control px-1.5 py-0.5 text-2xs text-dim">{SOUNDS[role] ?? role} <span className="text-faint">×{n}</span></span>
                ))}
              </div>
            </Section>

            {t.kick && (
              <Section title="Kick and rumble">
                <KickMatches id={id} kick={t.kick} file={song?.file}
                  from={t.arrangement ? t.arrangement.downbeat + t.kick.bars[0] * ((4 * 60) / t.arrangement.tempo) : undefined} />
              </Section>
            )}

            <Section title="Arrangement">
              {t.arrangement && t.arrangement.bars > 0 ? (
                <>
                  <ArrangementView arrangement={t.arrangement} file={song?.file} />
                  {t.arrangement.changes.length > 0 && (
                    <div className="mt-2 flex flex-col gap-0.5 text-2xs text-dim">
                      {t.arrangement.changes.slice(0, 12).map((c, i) => (
                        <span key={i}><span className="tabular text-faint">Bar {c.bar + 1}</span> · {c.part} {c.what}</span>
                      ))}
                      {t.arrangement.changes.length > 12 && <span className="text-faint">and {t.arrangement.changes.length - 12} more</span>}
                    </div>
                  )}
                  {map.score && (
                    <details className="mt-2">
                      <summary className="cursor-pointer text-2xs text-faint hover:text-dim">As the agent reads it</summary>
                      <pre className="mt-1 overflow-x-auto rounded-[6px] bg-bg p-2 font-mono text-[10px] leading-relaxed text-dim ring-1 ring-line">{map.score}</pre>
                    </details>
                  )}
                </>
              ) : (
                <p className="text-2xs leading-relaxed text-faint">
                  {t.arrangement ? "No beat could be heard in this track." : "Heard by an older hearing, without the arrangement: hear it again for its sections, parts per bar and energy."}
                </p>
              )}
            </Section>

            {t.voices && t.voices.voices.length > 0 && (
              <Section title="Voices">
                <p className="mb-2 text-2xs leading-relaxed text-faint">
                  Every melodic part through the track{t.voices.key ? ` (${t.voices.key.key})` : ""}, each as the figures it repeats: the agent writes its parts from these, in the song's key.
                </p>
                <div className="flex flex-col gap-3">
                  {t.voices.voices.map((v, i) => <VoiceView key={i} voice={v} />)}
                </div>
                <Together voices={t.voices.voices} pairs={t.voices.pairs} />
              </Section>
            )}

            {!t.voices && t.figures && t.figures.length > 0 && (
              <Section title="Figures">
                <p className="mb-2 text-2xs leading-relaxed text-faint">What the music and the bass play where they're loudest, two bars each: the agent writes its lines in these rhythms and intervals.</p>
                <div className="flex flex-col gap-2">
                  {t.figures.map((f, i) => <FigureView key={i} figure={f} />)}
                </div>
              </Section>
            )}

            <div className="mt-3 truncate text-2xs text-faint" title={song?.file ?? t.path}>{song?.file ?? t.path}</div>
          </>
        )}
      </div>
    </FloatingWindow>
  );
}

/** A figure as a small piano roll: a row per note it uses, 16ths across its bars. */
function FigureView({ figure: f }: { figure: Figure }) {
  return (
    <Roll notes={f.notes} bars={f.bars} color={f.part === "bass" ? "#5ec4a9" : "#a78bfa"}
      label={f.part} detail={`bars ${f.bar + 1}–${f.bar + f.bars}${f.section ? ` · ${f.section}` : ""}${f.key ? ` · ${f.key.key}` : ""}`} />
  );
}

/** A voice: what it is, then its runs, each figure as a roll with the bars it repeats over. */
function VoiceView({ voice: v }: { voice: Voice }) {
  const shown = 4;
  const color = v.part === "bass" ? "#5ec4a9" : v.part === "drums" ? "#f2a33a" : "#a78bfa";
  return (
    <div>
      <div className="mb-1 flex flex-wrap items-baseline gap-x-2 text-2xs">
        <span className="font-medium text-fg">{v.role}</span>
        <span className="text-faint">
          {noteName(v.range[0])}{v.range[1] !== v.range[0] ? `–${noteName(v.range[1])}` : ""}
          {v.sound ? ` · ${v.sound}` : ""}{v.chord >= 1.5 ? ` · chords of ${Math.round(v.chord)}` : ""} · {Math.round(v.notesPerBar)} note{Math.round(v.notesPerBar) === 1 ? "" : "s"} a bar
        </span>
      </div>
      <div className="flex flex-col gap-1.5">
        {v.runs.slice(0, shown).map((r, i) =>
          r.change?.startsWith("as bars") ? (
            <div key={i} className="text-2xs text-faint">bars {r.from + 1}–{r.to}: {r.change}</div>
          ) : (
            <Roll key={i} bars={r.bars} color={color}
              notes={r.notes.map(([step, steps, key, velocity]) => ({ start: step * 240, length: steps * 240, key, velocity }))}
              detail={`bars ${r.from + 1}–${r.to}${r.change ? ` · ${r.change}` : ""}${r.variations ? ` · ${r.variations} fill${r.variations > 1 ? "s" : ""}` : ""}`} />
          ),
        )}
        {v.runs.length > shown && <span className="text-2xs text-faint">and {v.runs.length - shown} more</span>}
      </div>
    </div>
  );
}

/** How the voices go together, the telling pairs: one answering another, or playing in its gaps. */
function Together({ voices, pairs }: { voices: Voice[]; pairs: { a: number; b: number; bars: number; together: number; fills: number; answerAfter?: number }[] }) {
  const lines = pairs
    .filter((p) => p.bars >= 4 && (p.answerAfter !== undefined || p.together >= 0.6 || p.fills >= 0.8))
    .slice(0, 8)
    .map((p) => {
      const a = `${voices[p.a].role} ${p.a + 1}`, b = `${voices[p.b].role} ${p.b + 1}`;
      return p.answerAfter !== undefined ? `${b} answers ${a}, ${p.answerAfter} 16ths after its phrase`
        : p.together >= 0.6 ? `${b} plays with ${a}` : `${b} plays in the gaps of ${a}`;
    });
  if (lines.length === 0) return null;
  return (
    <div className="mt-2 flex flex-col gap-0.5 text-2xs text-dim">
      {lines.map((l, i) => <span key={i}>{l}</span>)}
    </div>
  );
}

function Roll({ notes, bars, color, label, detail }: {
  notes: { start: number; length: number; key: number; velocity: number }[]; bars: number; color: string; label?: string; detail: string;
}) {
  const keys = [...new Set(notes.map((n) => n.key))].sort((a, b) => b - a);
  const steps = bars * 16;
  return (
    <div>
      <div className="mb-1 flex items-baseline gap-2 text-2xs">
        {label && <span className="font-medium text-fg">{label}</span>}
        <span className="text-faint">{detail}</span>
      </div>
      <div className="grid gap-px rounded-[4px] bg-bg p-1 ring-1 ring-line" style={{ gridTemplateColumns: `28px repeat(${steps}, minmax(0, 1fr))` }}>
        {keys.map((key) => (
          <Fragment key={key}>
            <span className="pr-1 text-right font-mono text-[9px] leading-[10px] text-faint">{noteName(key)}</span>
            {Array.from({ length: steps }, (_, step) => {
              const note = notes.find((n) => n.key === key && n.start <= step * 240 && n.start + n.length > step * 240);
              const starts = note !== undefined && note.start === step * 240;
              return (
                <span key={step} className="h-[10px]"
                  style={{
                    background: note ? color : step % 16 === 0 ? "#1f1f24" : step % 4 === 0 ? "#19191d" : "#151518",
                    opacity: note ? 0.45 + 0.55 * note.velocity / 127 : 1,
                    borderLeft: starts ? "1px solid #0c0c0e" : undefined,
                    borderRadius: starts ? "2px 0 0 2px" : undefined,
                  }} />
              );
            })}
          </Fragment>
        ))}
      </div>
    </div>
  );
}

function Measure({ label, value, unit }: { label: string; value: string; unit: string }) {
  return (
    <div className="rounded-[6px] bg-bg px-2 py-1.5 ring-1 ring-line">
      <div className="text-2xs text-faint">{label}</div>
      <div className="text-sm text-fg tabular">{value}</div>
      <div className="truncate text-2xs text-faint">{unit}</div>
    </div>
  );
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="mt-4">
      <div className="mb-1.5 text-2xs uppercase tracking-wide text-faint">{title}</div>
      {children}
    </div>
  );
}

/** The mix's level per band, as bars against the loudest. */
function Balance({ bands }: { bands: number[] }) {
  const top = Math.max(...bands);
  return (
    <div className="flex h-14 items-end gap-0.5 rounded-[6px] bg-bg p-1.5 ring-1 ring-line" title={bands.map((b) => `${b.toFixed(1)} dB`).join(", ")}>
      {bands.map((b, i) => (
        <div key={i} className="flex-1 rounded-t-[2px] bg-agent/60" style={{ height: `${Math.max(4, Math.min(1, (b - top + 36) / 36) * 100)}%` }} />
      ))}
    </div>
  );
}
