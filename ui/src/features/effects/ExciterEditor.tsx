import { useMemo, useRef, useState } from "react";
import { startDrag } from "@/lib/drag";
import { formatParam } from "@/lib/catalogue";
import { ParamMenu, useParamControl } from "./params";
import { useParams, type EditorProps } from "./editors";
import { approach, useAnimationFrame, useMeterStream } from "./live";

const CREAM = "#efe3c4", BRASS = "#c9a45c", INK = "#1b1410";
const SERIF = `"Iowan Old Style", "Palatino", Georgia, serif`;
const MODELS = ["Triode", "Pentode", "Tape", "Xformer", "Console", "Transistor"];

/** The engine's stage (effects::ExciterPlugin::stage), to draw what each model does. */
function stage(model: number, x: number, drive: number, warmth: number) {
  const u = x * drive, bias = 0.35 * warmth;
  switch (model) {
    case 1: { const f = (v: number) => v / Math.pow(1 + Math.pow(Math.abs(v), 2.5), 0.4); return f(u + 0.5 * bias) - f(0.5 * bias); }
    case 2: return (2 / Math.PI) * (Math.atan(u + 0.3 * bias) - Math.atan(0.3 * bias));
    case 4: { const f = (v: number) => { v = Math.max(-1.5, Math.min(1.5, v)); return v - (v * v * v) / 6.75; }; return f(u + 0.2 * bias) - f(0.2 * bias); }
    case 5: { const f = (v: number) => v / Math.sqrt(1 + (v >= 0 ? 1 : 0.5) * v * v); return f(u + 0.5 * bias) - f(0.5 * bias); }
    default: return Math.tanh(u + bias) - Math.tanh(bias);
  }
}

/** The second to seventh harmonics a sine at -6 dBFS comes out with, in dB below the fundamental. */
function harmonics(model: number, driveDb: number, warmth: number) {
  const core = model === 3 ? 0 : model;
  const drive = Math.pow(10, driveDb / 20) * (model === 3 ? 1.8 : 1);
  const n = 256;
  const out = Array.from({ length: n }, (_, i) => stage(core, 0.5 * Math.sin((2 * Math.PI * i) / n), drive, warmth));
  const magnitude = (k: number) => Math.hypot(...[0, 1].map((part) => out.reduce((s, v, i) => s + v * (part ? Math.sin : Math.cos)((2 * Math.PI * k * i) / n), 0)));
  const fundamental = magnitude(1);
  return [2, 3, 4, 5, 6, 7].map((k) => Math.max(-60, 20 * Math.log10(magnitude(k) / fundamental + 1e-9)));
}

/** The exciter as a piece of vintage gear: a tube that glows with the drive and the signal, a VU
    meter on the output, LEDs for the harmonics the chosen stage adds, the model on a rotary
    switch, and chicken-head knobs. */
export function ExciterEditor(props: EditorProps) {
  const p = useParams(props);
  const model = Math.round(p("model")), drive = p("driveDb"), warmth = p("warmth") / 100;
  const stream = useMeterStream(props.insert.id, props.effect.id, 2);
  const overtones = useMemo(() => harmonics(model, drive, warmth), [model, drive, warmth]);
  const needle = useRef<SVGGElement>(null), glow = useRef<HTMLDivElement>(null), leds = useRef<HTMLDivElement>(null), lamp = useRef<HTMLDivElement>(null);
  const anim = useRef({ vu: -24, level: 0 });

  useAnimationFrame((now, dt) => {
    const a = anim.current;
    // 0 VU at -14 dBFS RMS, where loud electronic music sits; a VU needle takes about 300 ms to get there.
    const vu = Math.max(-24, Math.min(4, stream.value(0, 1) + 14));
    a.vu = approach(a.vu, vu, dt, 65);
    const level = Math.max(0, Math.min(1, (stream.value(0, 0) + 40) / 40));
    a.level = approach(a.level, level, dt, 90);
    needle.current?.setAttribute("transform", `rotate(${vuAngle(a.vu)} 110 104)`);
    const flicker = 0.97 + 0.03 * Math.sin(now / 37) * Math.sin(now / 91);
    if (glow.current) glow.current.style.opacity = String((0.35 + 0.25 * (drive / 24) + 0.45 * a.level) * flicker);
    leds.current?.style.setProperty("--level", String(0.45 + 0.55 * a.level));
    if (lamp.current) lamp.current.style.opacity = String(0.6 + 0.4 * a.level * flicker);
  });

  return (
    <div className="flex overflow-hidden rounded-md shadow-[0_6px_18px_rgba(0,0,0,0.5)]">
      <Cheek />
      <div className="relative flex-1 px-5 pt-4 pb-3" style={{
        background: "repeating-linear-gradient(90deg, rgba(255,255,255,0.012) 0 1px, transparent 1px 3px), linear-gradient(180deg, #3a2e25 0%, #2a211b 55%, #221a15 100%)",
        boxShadow: "inset 0 1px 0 rgba(255,235,200,0.12), inset 0 -2px 6px rgba(0,0,0,0.5)",
      }}>
        {[[8, 8], [8, -8], [-8, 8], [-8, -8]].map(([x, y], i) => <Screw key={i} x={x} y={y} />)}

        <div className="mb-3 flex items-baseline justify-between px-2">
          <div style={{ fontFamily: SERIF, color: CREAM, letterSpacing: "0.32em", fontSize: 15, textShadow: "0 1px 0 #000" }}>EXCITER</div>
          <div className="flex items-center gap-2" style={{ fontFamily: SERIF, color: BRASS, fontSize: 9, letterSpacing: "0.2em" }}>
            GROOVEKIT · TYPE EX-1
            <div ref={lamp} className="h-2.5 w-2.5 rounded-full" style={{ background: "radial-gradient(circle at 35% 35%, #ffd9a0, #ff7a1a 55%, #8a2a00)", boxShadow: "0 0 8px #ff7a1a" }} />
          </div>
        </div>

        <div className="flex items-stretch gap-3">
          <Tube glowRef={glow} />
          <VuMeter needleRef={needle} />
          <div ref={leds} className="flex flex-1 flex-col justify-between rounded-[5px] px-3 py-2.5" style={{ background: "#120d0a", boxShadow: "inset 0 2px 6px rgba(0,0,0,0.8), 0 1px 0 rgba(255,235,200,0.08)" }}>
            <div style={{ fontFamily: SERIF, color: BRASS, fontSize: 9, letterSpacing: "0.2em" }}>HARMONICS</div>
            <div className="flex flex-1 items-end justify-between gap-1.5 pt-2">
              {overtones.map((db, i) => {
                const lit = Math.round(((db + 60) / 60) * 10);
                return (
                  <div key={i} className="flex flex-1 flex-col items-center gap-1">
                    <div className="flex w-full flex-col-reverse gap-[2px]">
                      {Array.from({ length: 10 }, (_, s) => (
                        <div key={s} className="h-[5px] rounded-[1px] transition-[background,box-shadow] duration-300"
                          style={s < lit
                            ? { background: s >= 8 ? "#ff5a3c" : s >= 6 ? "#ffb347" : "#ffcf7a", boxShadow: "0 0 5px rgba(255,170,70,calc(var(--level) * 0.9))", opacity: "var(--level)" }
                            : { background: "#2a1d14" }} />
                      ))}
                    </div>
                    <div style={{ fontFamily: SERIF, color: CREAM, fontSize: 9, opacity: 0.7 }}>{i + 2}</div>
                  </div>
                );
              })}
            </div>
          </div>
        </div>

        <div className="mt-4 flex items-end justify-between gap-2">
          <ModelSwitch {...props} />
          <RetroKnob {...props} id="driveDb" name="Drive" />
          <RetroKnob {...props} id="warmth" name="Warmth" />
          <RetroKnob {...props} id="harmonics" name="Presence" />
          <RetroKnob {...props} id="harmonicsHz" name="Freq" />
          <RetroKnob {...props} id="body" name="Body" />
          <RetroKnob {...props} id="outputDb" name="Output" />
        </div>
      </div>
      <Cheek />
    </div>
  );
}

//==============================================================================
function Cheek() {
  return <div className="w-4 shrink-0" style={{
    background: "repeating-linear-gradient(180deg, rgba(0,0,0,0.18) 0 2px, transparent 2px 7px), repeating-linear-gradient(176deg, rgba(255,200,140,0.06) 0 1px, transparent 1px 11px), linear-gradient(90deg, #3d2213, #5a3218 45%, #2c180c)",
    boxShadow: "inset -1px 0 0 rgba(0,0,0,0.6), inset 1px 0 0 rgba(255,200,150,0.12)",
  }} />;
}

function Screw({ x, y }: { x: number; y: number }) {
  return <div className="absolute h-2.5 w-2.5 rounded-full" style={{
    [x > 0 ? "left" : "right"]: Math.abs(x), [y > 0 ? "top" : "bottom"]: Math.abs(y),
    background: "radial-gradient(circle at 35% 30%, #d8c8a8, #6b5a44 60%, #2a2018)",
    boxShadow: "0 1px 1px rgba(0,0,0,0.7)",
  }}><div className="absolute top-1/2 left-[2px] h-px w-[6px] -translate-y-1/2 rotate-45 bg-black/60" /></div>;
}

/** -20 to +3 VU across the dial, spaced like a real meter's: by voltage, so the top end spreads out. */
function vuAngle(vu: number) {
  const volts = (db: number) => Math.pow(10, db / 20);
  return -48 + ((volts(Math.max(-24, Math.min(4, vu))) - volts(-20)) / (volts(3) - volts(-20))) * 96;
}

function VuMeter({ needleRef }: { needleRef: React.Ref<SVGGElement> }) {
  const marks = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
  const at = (vu: number, r: number) => {
    const a = (vuAngle(vu) * Math.PI) / 180;
    return [110 + r * Math.sin(a), 104 - r * Math.cos(a)];
  };
  const arc = (from: number, to: number, r: number) => {
    const [x0, y0] = at(from, r), [x1, y1] = at(to, r);
    return `M ${x0} ${y0} A ${r} ${r} 0 0 1 ${x1} ${y1}`;
  };

  return (
    <div className="rounded-[6px] p-1.5" style={{ background: "linear-gradient(180deg, #4a3a2c, #241a13)", boxShadow: "inset 0 1px 0 rgba(255,235,200,0.15), 0 2px 4px rgba(0,0,0,0.6)" }}>
      <svg width={220} height={112} className="block rounded-[3px]">
        <defs>
          <radialGradient id="vu-face" cx="50%" cy="100%" r="100%">
            <stop offset="0" stopColor="#fff6dc" />
            <stop offset="1" stopColor="#e7d6a8" />
          </radialGradient>
          <linearGradient id="vu-glass" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#fff" stopOpacity={0.35} />
            <stop offset="0.45" stopColor="#fff" stopOpacity={0.04} />
            <stop offset="1" stopColor="#000" stopOpacity={0.15} />
          </linearGradient>
        </defs>
        <rect width={220} height={112} fill="url(#vu-face)" />
        <path d={arc(-20, 0, 78)} stroke={INK} strokeWidth={1.2} fill="none" />
        <path d={arc(0, 3, 78)} stroke="#b8261c" strokeWidth={4} fill="none" />
        {marks.map((m) => {
          const [x0, y0] = at(m, 78), [x1, y1] = at(m, m % 5 === 0 || m >= 0 ? 88 : 84), [tx, ty] = at(m, 97);
          return (
            <g key={m}>
              <line x1={x0} y1={y0} x2={x1} y2={y1} stroke={m > 0 ? "#b8261c" : INK} strokeWidth={1.2} />
              <text x={tx} y={ty + 3} fontSize={8.5} textAnchor="middle" fill={m > 0 ? "#b8261c" : INK} fontFamily={SERIF}>{m > 0 ? `+${m}` : Math.abs(m)}</text>
            </g>
          );
        })}
        <text x={110} y={74} fontSize={15} textAnchor="middle" fill={INK} fontFamily={SERIF} letterSpacing={2}>VU</text>
        <g ref={needleRef} transform={`rotate(${vuAngle(-20)} 110 104)`}>
          <line x1={110} y1={104} x2={110} y2={22} stroke="#000" strokeOpacity={0.18} strokeWidth={2.5} transform="translate(2 2)" />
          <line x1={110} y1={104} x2={110} y2={22} stroke={INK} strokeWidth={1.4} />
        </g>
        <rect x={80} y={98} width={60} height={14} rx={3} fill="#2a1d14" />
        <rect width={220} height={112} fill="url(#vu-glass)" />
      </svg>
    </div>
  );
}

function Tube({ glowRef }: { glowRef: React.Ref<HTMLDivElement> }) {
  return (
    <div className="relative flex w-[70px] shrink-0 items-end justify-center">
      <div ref={glowRef} className="pointer-events-none absolute inset-x-0 top-6 bottom-6 rounded-full blur-xl"
        style={{ background: "radial-gradient(ellipse at 50% 60%, rgba(255,140,40,0.9), rgba(255,90,10,0.35) 45%, transparent 70%)" }} />
      <svg width={56} height={118} className="relative">
        <defs>
          <linearGradient id="tube-glass" x1="0" x2="1">
            <stop offset="0" stopColor="#fff" stopOpacity={0.25} />
            <stop offset="0.3" stopColor="#fff" stopOpacity={0.05} />
            <stop offset="0.8" stopColor="#fff" stopOpacity={0.02} />
            <stop offset="1" stopColor="#fff" stopOpacity={0.18} />
          </linearGradient>
          <radialGradient id="tube-filament" cx="50%" cy="50%" r="50%">
            <stop offset="0" stopColor="#ffe2a8" />
            <stop offset="0.5" stopColor="#ff8a24" />
            <stop offset="1" stopColor="#ff5a00" stopOpacity={0} />
          </radialGradient>
        </defs>
        <path d="M 10 100 L 10 26 Q 10 6 28 6 Q 46 6 46 26 L 46 100 Z" fill="rgba(200,190,170,0.06)" stroke="rgba(230,220,200,0.35)" />
        <rect x={17} y={36} width={22} height={46} rx={2} fill="#3a3530" stroke="#5a544c" />
        <rect x={20} y={40} width={16} height={38} fill="#1e1a17" />
        <ellipse cx={28} cy={59} rx={7} ry={15} fill="url(#tube-filament)" />
        <line x1={28} y1={14} x2={28} y2={36} stroke="#8a8278" strokeWidth={1} />
        <path d="M 10 26 Q 10 6 28 6 Q 46 6 46 26 L 46 100 L 10 100 Z" fill="url(#tube-glass)" />
        <rect x={6} y={100} width={44} height={14} rx={2} fill="#1a1512" stroke="#3a2e25" />
        {[14, 22, 30, 38].map((x) => <rect key={x} x={x} y={103} width={3} height={8} fill={BRASS} opacity={0.6} />)}
      </svg>
    </div>
  );
}

//==============================================================================
/** A chicken-head knob on a dial of ticks: drag up and down (Shift for fine), double-click for the
    default, right-click to reset or automate. */
function RetroKnob({ insert, effect, id, name }: EditorProps & { id: string; name: string }) {
  const { p, value, toKnob, fromKnob, live } = useParamControl(insert, effect, id, name);
  const [dragging, setDragging] = useState<number | null>(null);
  if (!p) return null;

  const k = dragging ?? toKnob(value);
  const t = (k - toKnob(p.min)) / (toKnob(p.max) - toKnob(p.min) || 1);
  const angle = -135 + t * 270;
  const size = 62, c = size / 2;

  return (
    <ParamMenu insert={insert} effect={effect} id={id}>
      <div className="flex flex-col items-center gap-1" title={p.description}>
        <svg width={size} height={size} className="cursor-ns-resize"
          onDoubleClick={() => live.onCommit(toKnob(p.default))}
          onPointerDown={(e) => {
            if (e.button !== 0) return;
            const k0 = toKnob(value), lo = toKnob(p.min), hi = toKnob(p.max);
            let next = k0;
            startDrag(e, (_dx, dy, ev) => {
              next = Math.min(hi, Math.max(lo, k0 - (dy / 160) * (hi - lo) * (ev.shiftKey ? 0.1 : 1)));
              setDragging(next);
              live.onChange(next);
            }, (_dx, _dy, _ev, moved) => {
              setDragging(null);
              if (moved) live.onCommit(next);
            }, "ns-resize");
          }}>
          <defs>
            <radialGradient id={`skirt-${id}`} cx="40%" cy="35%" r="70%">
              <stop offset="0" stopColor="#4a4038" />
              <stop offset="1" stopColor="#0d0a08" />
            </radialGradient>
          </defs>
          {Array.from({ length: 11 }, (_, i) => {
            const a = ((-135 + i * 27) * Math.PI) / 180;
            return <line key={i} x1={c + 25 * Math.sin(a)} y1={c - 25 * Math.cos(a)} x2={c + 29 * Math.sin(a)} y2={c - 29 * Math.cos(a)}
              stroke={CREAM} strokeOpacity={i / 10 <= t + 0.001 ? 0.95 : 0.35} strokeWidth={i % 5 === 0 ? 1.6 : 1} />;
          })}
          <circle cx={c} cy={c + 1.5} r={19} fill="#000" opacity={0.5} />
          <circle cx={c} cy={c} r={19} fill={`url(#skirt-${id})`} stroke="#000" />
          <g transform={`rotate(${angle} ${c} ${c})`} style={{ transition: dragging === null ? "transform 0.18s ease-out" : undefined }}>
            <path d={`M ${c - 6} ${c + 9} L ${c - 3.2} ${c - 20} Q ${c} ${c - 24} ${c + 3.2} ${c - 20} L ${c + 6} ${c + 9} Q ${c} ${c + 13} ${c - 6} ${c + 9} Z`}
              fill="#16110d" stroke="#3a3029" />
            <line x1={c} y1={c - 19} x2={c} y2={c - 6} stroke={CREAM} strokeWidth={1.8} strokeLinecap="round" />
          </g>
        </svg>
        <div style={{ fontFamily: SERIF, color: CREAM, fontSize: 10, letterSpacing: "0.18em", textTransform: "uppercase" }}>{name}</div>
        <div className="tabular" style={{ color: BRASS, fontSize: 9, opacity: dragging !== null ? 1 : 0.75 }}>{formatParam(fromKnob(k), p.unit)}</div>
      </div>
    </ParamMenu>
  );
}

/** The model on a rotary switch: click a name, or drag the knob. */
function ModelSwitch({ insert, effect }: EditorProps) {
  const { value, set } = useParamControl(insert, effect, "model", "Model");
  const index = Math.round(value);
  const angleOf = (i: number) => -112 + (i * 224) / (MODELS.length - 1);
  const size = 150, c = size / 2, r = 18;

  return (
    <ParamMenu insert={insert} effect={effect} id="model">
      <div className="relative -mb-1 shrink-0" style={{ width: size, height: size - 30 }}>
        <svg width={size} height={size - 30} className="absolute inset-0 overflow-visible">
          {MODELS.map((m, i) => {
            const a = (angleOf(i) * Math.PI) / 180;
            const tx = c + 50 * Math.sin(a) * 1.12, ty = c + 4 - 50 * Math.cos(a);
            return (
              <g key={m} className="cursor-pointer" onClick={() => set(i)}>
                <title>{["Triode", "Pentode", "Tape", "Transformer", "Console", "Transistor"][i]}</title>
                <line x1={c + 24 * Math.sin(a)} y1={c + 4 - 24 * Math.cos(a)} x2={c + 29 * Math.sin(a)} y2={c + 4 - 29 * Math.cos(a)} stroke={CREAM} strokeOpacity={i === index ? 1 : 0.4} />
                <text x={tx} y={ty + 3} fontSize={7.5} textAnchor="middle" fill={i === index ? "#ffcf7a" : CREAM} fillOpacity={i === index ? 1 : 0.6}
                  fontFamily={SERIF} letterSpacing={0.6} style={{ textShadow: i === index ? "0 0 6px rgba(255,170,70,0.8)" : undefined }}>{m.toUpperCase()}</text>
              </g>
            );
          })}
          <circle cx={c} cy={c + 5.5} r={r} fill="#000" opacity={0.5} />
          <circle cx={c} cy={c + 4} r={r} fill="#17120e" stroke="#000" />
          <g style={{ transform: `rotate(${angleOf(index)}deg)`, transformOrigin: `${c}px ${c + 4}px`, transition: "transform 0.4s cubic-bezier(0.3, 1.5, 0.5, 1)" }}>
            <rect x={c - 4} y={c + 4 - r - 2} width={8} height={2 * r + 4} rx={4} fill="#2a221c" stroke="#4a3e34" />
            <line x1={c} y1={c + 4 - r} x2={c} y2={c - 4} stroke={CREAM} strokeWidth={1.8} strokeLinecap="round" />
          </g>
        </svg>
        <div className="absolute inset-x-0 bottom-0 text-center" style={{ fontFamily: SERIF, color: CREAM, fontSize: 10, letterSpacing: "0.18em" }}>MODEL</div>
      </div>
    </ParamMenu>
  );
}
