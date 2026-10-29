// Where every file of the starter pack's sources goes, and what it's called. Folders are by what
// a sound is (Kicks, Snares, Hats...), then by the machine it came from (808, TR-8, DR-5...), and
// a file's name says both: "808 Kick Tone 25 Decay 50", "DR-5 Snare Dance", "Analog Hat Open 02".
// A loop's name ends with its tempo, which the library reads.
//
// Each source has a function from a file's path inside its checkout (and its duration) to the
// place it goes in the pack, as { folder, name } (name without extension), or null to leave it
// out. Any file not mapped is an error, so nothing slips through unnamed.

/** The machines, as the READ ME explains them. */
export const machines = {
  "808": "the 1980 analogue drum machine behind hip hop, electro and techno: a deep booming kick, a crisp snare, clicky hats and cowbell. Tone, decay, snappy and tune are its knobs, sampled at several settings.",
  "TR-8": "a 2014 machine that models the classic 606, 707, 727, 808 and 909 drum machines. The model is in each name: 909 kicks punch, 707 sounds are clean and dry, 727 is Latin percussion, 606 is thin and snappy.",
  "DR-5": "a 1990s drum machine with a wide set of sampled acoustic, electronic and world percussion sounds.",
  "RD-6": "a recent analogue machine in the 606 tradition: snappy kicks, trashy snares and thin hats.",
  "RX5": "a 1986 digital drum machine with punchy, slightly gritty 12-bit kits and orchestral extras.",
  "Analog": "drums synthesized and recorded through analogue gear: a modular synth, an analogue compressor. Clean, modern electronic hits.",
  "Various": "assorted electronic one-shots, named by how they sound.",
  "Tabla": "the strokes of the Indian hand drum, named by their traditional bols (Na, Te, Ghe, Tun...).",
};

/** The pack's words for a source's name: "808_Closed_HiHat_Long" -> "808 Hat Closed Long".
    The sound's type comes first, then what kind it is; a leading model number (the TR-8's
    606, 707, 727, 808 and 909) stays in front. */
function words(raw) {
  let s = raw.replace(/\.[A-Za-z0-9]+$/, "").replace(/[_-]+/g, " ").trim();
  const swaps = [
    [/\bTR ?808\b/g, "808"], [/\bTR ?909\b/g, "909"], [/\bCR ?78\b/g, "CR-78"],
    [/\bBass Drum\b/g, "Kick"], [/\bAttackBD\b/g, "Kick Attack"], [/\bAttackSD\b/g, "Snare Attack"],
    [/\bSnare Drum\b/g, "Snare"], [/\bSnare Dum\b/g, "Snare"], [/\bRim Shot Snare\b/g, "Snare Rimshot"],
    [/\bHiHat\b/g, "Hat"], [/\bHi hat\b/gi, "Hat"], [/\bHand Clap\b/g, "Clap"], [/\bRim Shot\b/g, "Rimshot"],
    [/\bNoiseTom\b/g, "Noise Tom"], [/\bVibra slap\b/g, "Vibraslap"], [/\bE Bass\b/g, "Electric Bass"],
    [/\bProcess Roto\b/g, "Roto Tom Process"], [/^Mallet Crash$/, "Crash Mallet"],
  ];
  for (const [from, to] of swaps) s = s.replace(from, to);
  let model = "";
  const m = /^(\d{3}) (.*)$/.exec(s);
  if (m) [, model, s] = m;
  s = s.replace(/\b(Closed|Open|Pedal) Hat\b/, "Hat $1");
  // "Club Kick" -> "Kick Club", "Ambient Tom 1" -> "Tom Ambient 1", "Crash Cymbal 1" -> "Cymbal Crash 1".
  s = s.replace(/^(.*?) ?\b(Kick|Snare|Clap|Hat|Tom|Conga|Cowbell|Side Stick|Rimshot|Cymbal)\b( .*)?$/, (_m, before, type, after) =>
    `${type} ${before}${after ?? ""}`).replace(/\s+/g, " ").trim();
  s = s.replace(/^Hat (\w+) (Closed|Open|Pedal)\b/, "Hat $2 $1");
  s = s.replace(/^Cymbal (Crash|Ride|Splash|China)\b/, "$1");
  s = s.replace(/^Cymbal Rock Ride\b/, "Ride Rock").replace(/^Cymbal Flat Ride$/, "Ride Flat").replace(/^Cymbal Brush Hit$/, "Cymbal Brush");
  s = s.replace(/^(Low|Mid|High|Floor|Open High|Mute High) (Timbale|Bongo|Agogo)\b/, "$2 $1");
  s = s.replace(/^(Short|Long) (Whistle|Guiro)\b/, "$2 $1");
  return `${model} ${s}`.replace(/\s+/g, " ").trim();
}

/** A loop's tempo from how long it lasts: the whole number of 4/4 bars that puts it between
    80 and 170 BPM, nearest 125 if several do. */
export function loopTempo(seconds) {
  const options = [1, 2, 4, 8, 16].map((bars) => (bars * 4 * 60) / seconds).filter((t) => t >= 80 && t <= 170);
  if (options.length === 0) return null;
  return Math.round(options.sort((a, b) => Math.abs(a - 125) - Math.abs(b - 125))[0]);
}

const withTempo = (name, seconds, known) => `${name} ${known ?? loopTempo(seconds) ?? ""}bpm`.replace(/ bpm$/, "");

// The 808 set: two-letter sound codes, then the knob settings the sample was taken at.
const fischer = (file) => {
  const m = /^[a-z]+8\/([A-Z]{2})(\d{2})?(\d{2})?\.WAV$/i.exec(file);
  if (!m) return null;
  const [, code, a, b] = m;
  const n = (v) => String(Number(v));
  const toms = { HT: "High", MT: "Mid", LT: "Low" }, congas = { HC: "High", MC: "Mid", LC: "Low" };
  switch (code) {
    case "BD": return { folder: "Kicks/808", name: `808 Kick Tone ${n(a)} Decay ${n(b)}` };
    case "SD": return { folder: "Snares/808", name: `808 Snare Tone ${n(a)} Snappy ${n(b)}` };
    case "CP": return { folder: "Claps/808", name: "808 Clap" };
    case "CH": return { folder: "Hats/808", name: "808 Hat Closed" };
    case "OH": return { folder: "Hats/808", name: `808 Hat Open Decay ${n(a)}` };
    case "CY": return { folder: "Cymbals/808", name: `808 Cymbal Tone ${n(a)} Decay ${n(b)}` };
    case "CB": return { folder: "Percussion/808", name: "808 Cowbell" };
    case "CL": return { folder: "Percussion/808", name: "808 Claves" };
    case "MA": return { folder: "Percussion/808", name: "808 Maracas" };
    case "RS": return { folder: "Percussion/808", name: "808 Rimshot" };
    default:
      if (toms[code]) return { folder: "Toms/808", name: `808 Tom ${toms[code]} Tune ${n(a)}` };
      if (congas[code]) return { folder: "Percussion/808", name: `808 Conga ${congas[code]} Tune ${n(a)}` };
      return null;
  }
};

/** The top folder for a cleaned name, by what the name says, else by the source's folder. */
function kindOf(name, sourceFolder) {
  if (/\bKick\b/.test(name)) return "Kicks";
  if (/\bSnare\b/.test(name)) return "Snares";
  if (/\b(Rimshot|Side Stick)\b/.test(name)) return "Percussion";
  if (/\bClap\b/.test(name)) return "Claps";
  if (/\bHat\b/.test(name)) return "Hats";
  if (/\b(Crash|Ride|Splash|China|Cymbal)\b/.test(name)) return "Cymbals";
  if (/\bTom\b/.test(name)) return "Toms";
  return { BD: "Kicks", SD: "Snares", CLAP: "Claps", HATS: "Hats", TOMS: "Toms" }[sourceFolder] ?? "Percussion";
}

// MckAudio's packs: TR8/BD/008_808_Bass_Drum_Long_2.wav and the like.
const mck = (file, seconds, counters) => {
  const m = /^(TR8|DR5|RD6|RX5)\/([A-Z]+)\/(\d+)(?:_(.+))?\.wav$/.exec(file);
  if (!m) return null;
  const [, machine, folder, , rawName] = m;
  const label = { TR8: "TR-8", DR5: "DR-5", RD6: "RD-6", RX5: "RX5" }[machine];

  if (machine === "RD6") {
    // Numbered only (the files don't say which hats are closed or open).
    const kind = { BD: "Kick", CLAP: "Clap", HATS: "Hat", SD: "Snare", TOMS: "Tom" }[folder];
    const n = counters.next(`${label} ${kind}`);
    return { folder: `${kindOf(kind, folder)}/${label}`, name: `${label} ${kind} ${String(n).padStart(2, "0")}` };
  }

  if (machine === "DR5" && folder === "MISC") return { folder: "FX/DR-5", name: `DR-5 Misc ${counters.next("DR-5 Misc")}` };
  if (machine === "DR5" && folder === "FX") return { folder: "FX/DR-5", name: `DR-5 ${rawName.replace(/_/g, " ")}` };

  let name = words(rawName);

  if (machine === "RX5" && folder === "MISC") {
    if (/^(Hey|Wao|Ooo)$/.test(name)) return { folder: "FX/RX5", name: `RX5 Voice ${name}` };
    if (/^(Glass Crash|Gun|Door Slam)$/.test(name)) return { folder: "FX/RX5", name: `RX5 ${name}` };
    if (/^FM Percussion/.test(name)) return { folder: "Percussion/RX5", name: `RX5 ${name.replace("Percussion", "Perc")}` };
    if (/Bass/.test(name)) return { folder: "Bass", name: `RX5 ${name}` };
    return { folder: "Synth", name: `RX5 ${name}` };   // DX Orchestra, DX Marimba, DX Clavinet, guitars
  }
  if (machine === "RX5" && name === "Timpani") return { folder: "Percussion/RX5", name: "RX5 Timpani" };
  if (name === "Claps") name = "Clap";

  const kind = kindOf(name, folder);
  return { folder: `${kind}/${label}`, name: `${label} ${name}` };
};

// uzu-drumkit: bd/10_bd_switchangel.wav; the folder says the sound.
const uzu = (file, seconds, counters) => {
  const m = /^([a-z]+)\/\d+_.*\.wav$/.exec(file);
  if (!m) return null;
  const kinds = {
    bd: ["Kicks", "Kick"], sd: ["Snares", "Snare"], cp: ["Claps", "Clap"], hh: ["Hats", "Hat Closed"], oh: ["Hats", "Hat Open"],
    cr: ["Cymbals", "Crash"], rd: ["Cymbals", "Ride"], ht: ["Toms", "Tom High"], mt: ["Toms", "Tom Mid"], lt: ["Toms", "Tom Low"],
    rim: ["Percussion", "Rimshot"], cb: ["Percussion", "Cowbell"], sh: ["Percussion", "Shaker"], tb: ["Percussion", "Tambourine"],
    misc: ["FX", "FX"],
  };
  if (m[1] === "brk") return { folder: "Loops", name: withTempo("Analog Break Amen", seconds) };
  const entry = kinds[m[1]];
  if (!entry) return null;
  const [top, kind] = entry;
  const n = counters.next(`Analog ${kind}`);
  return { folder: `${top}/Analog`, name: `Analog ${kind} ${String(n).padStart(2, "0")}` };
};

// Sonic Pi's samples, named one by one; the drum_ (acoustic kit), guit_ and burp ones stay out.
const title = (s) => s.split("_").map((w) => w[0].toUpperCase() + w.slice(1)).join(" ");
// Sonic Pi's names are often made-up words ("bd_zome", "hat_gnu"): a sound keeps its word when it
// says something about it, and is numbered otherwise.
const telling = {
  bd: { 808: "808 Style", boom: "Boom", chip: "Chip", fat: "Fat", haus: "Haus", jazz: "Jazz", pure: "Pure", tek: "Tek" },
  sn: { dub: "Dub" },
  hat: { metal: "Metal", noiz: "Noise", raw: "Raw", snap: "Snap", tap: "Tap" },
};
const sonicPi = (file, seconds, counters) => {
  const name = file.replace(/\.flac$/, "");
  const [prefix, ...rest] = name.split("_");
  const tail = rest.join("_");
  const one = (folder, n) => ({ folder, name: n });
  const wordOrNumber = (kind) => telling[prefix][tail] ? `${kind} ${telling[prefix][tail]}` : `${kind} ${String(counters.next(`Various ${kind}`)).padStart(2, "0")}`;

  switch (prefix) {
    case "bd": return one("Kicks/Various", wordOrNumber("Kick"));
    case "sn": return one("Snares/Various", wordOrNumber("Snare"));
    case "hat": return one("Hats/Various", wordOrNumber("Hat"));
    case "ride": return one("Cymbals/Various", `Ride ${title(tail)}`);
    case "bass": return one("Bass", `Bass ${title(tail).replace(/\bDnb\b/, "DnB")}`);
    case "ambi": return one("Ambient", title(tail));
    case "tabla": return one("Percussion/Tabla", `Tabla ${title(tail).replace(/(\D)(\d)$/, "$1 $2")}`);
    case "vinyl": return one("FX", `Vinyl ${title(tail)}`);
    case "glitch": return tail.startsWith("bass") ? one("Bass", "Bass Glitch G") : one("FX", `Glitch ${title(tail).replace(/(\D)(\d)$/, "$1 $2")}`);
    case "mehackit": return one("FX", title(tail).replace(/(\D)(\d)$/, "$1 $2"));
    case "arovane": return one("Loops", withTempo(`Arovane ${title(tail)}`, seconds));
    case "loop": {
      if (tail === "drone_g_97") return one("Loops", "Drone G 97bpm");
      const names = { amen: "Amen Break", amen_full: "Amen Break Full", "3d_printer": "3D Printer", mehackit1: "Mehackit 1", mehackit2: "Mehackit 2", perc1: "Perc 1", perc2: "Perc 2" };
      return one("Loops", withTempo(names[tail] ?? title(tail), seconds));
    }
    case "drum": return tail === "cowbell" ? one("Percussion/Various", "Cowbell") : null;
    case "misc": return tail === "burp" ? null : one("FX", tail === "cineboom" ? "Cinematic Boom" : title(tail));
    case "guit": return null;
    case "perc": {
      const fx = { door: "Door", impact1: "Impact 1", impact2: "Impact 2", swash: "Swash", swoosh: "Swoosh" };
      if (fx[tail]) return one("FX", fx[tail]);
      return one("Percussion/Various", { bell: "Bell 1", bell2: "Bell 2", snap: "Snap 1", snap2: "Snap 2", till: "Till" }[tail] ?? title(tail));
    }
    case "elec": {
      if (/kick$/.test(tail)) return one("Kicks/Various", `Kick ${title(tail.replace(/_kick$/, ""))}`);
      if (tail === "snare") return one("Snares/Various", "Snare Electro");
      if (/snare$/.test(tail)) return one("Snares/Various", `Snare ${{ filt: "Filtered", hi: "High", lo: "Low", mid: "Mid" }[tail.replace(/_snare$/, "")]}`);
      if (tail === "fuzz_tom") return one("Toms/Various", "Tom Fuzz");
      if (tail === "cymbal") return one("Cymbals/Various", "Cymbal Electro");
      if (tail === "tick" || tail === "wood") return one("Percussion/Various", title(tail));
      return one("Synth", title(tail).replace(/^Blip2$/, "Blip 2"));
    }
    case "tbd": {
      if (tail === "fxbed_loop") return one("Ambient", "FX Bed");
      if (tail.startsWith("pad")) return one("Synth", `Pad ${tail.slice(-1)}`);
      if (tail === "highkey_c4") return one("Synth", "High Key C4");
      if (tail === "voctone") return one("Synth", "Vocal Tone");
      if (tail === "perc_blip") return one("Percussion/Various", "Perc Blip");
      if (tail === "perc_hat") return one("Hats/Various", "Hat Perc");
      if (tail.startsWith("perc_tap")) return one("Percussion/Various", `Tap ${tail.slice(-1)}`);
      return null;
    }
    default: return null;
  }
};

/** The sources, pinned to commits so the pack builds the same every time. `files` lists what
    to take from the checkout (paths relative to `root`). */
export const sources = [
  {
    id: "mck", title: "MckAudio sample packs", repo: "https://github.com/MckAudio/MckSamplePacks", commit: "5db40e8fe26785c256845a5bb38921b2654a887f",
    licence: "CC0 1.0", credit: "MckAudio", root: ".", pattern: /^(TR8|DR5|RD6|RX5)\/.*\.wav$/, map: mck,
  },
  {
    id: "fischer", title: "TR-808 samples by Michael Fischer", repo: "https://github.com/tidalcycles/sounds-tr808-fischer", commit: "85fbecf1bec32553395625ea659e2a56dfd7c0e1",
    licence: "CC0 1.0", credit: "Michael Fischer, packaged by the TidalCycles project", root: ".", pattern: /^[a-z]+8\/.*\.WAV$/i, map: fischer,
  },
  {
    id: "uzu", title: "uzu-drumkit", repo: "https://github.com/tidalcycles/uzu-drumkit", commit: "2f3e05c70ab4d73ad053a1467adec89bd27377a0",
    licence: "The Unlicense (public domain)", credit: "Mot4i, Switch Angel and Ludens", root: ".", pattern: /^[a-z]+\/.*\.wav$/, map: uzu,
  },
  {
    id: "sonicpi", title: "Sonic Pi samples", repo: "https://github.com/sonic-pi-net/sonic-pi", commit: "df6f17e4625b0451fa2af8accf2e1e641c7ad458",
    sparse: "etc/samples", root: "etc/samples",
    licence: "CC0 1.0", credit: "Uwe Zahn (Arovane) for the arovane_ samples; the rest from freesound.org, each placed in the public domain by its author (see the Sonic Pi README below)",
    pattern: /\.flac$/, map: sonicPi,
  },
];

/** Numbers sounds of a kind in the order their source lists them. */
export function counters() {
  const counts = new Map();
  return { next: (key) => { const n = (counts.get(key) ?? 0) + 1; counts.set(key, n); return n; } };
}
