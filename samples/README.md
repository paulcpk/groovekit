# Starter samples

The starter pack GrooveKit offers to download: public-domain drum machines, one-shots and loops, renamed into one set so a beginner can find a kick and someone who knows the machines can go straight to the 808 folder. The app fetches it as a zip from a GitHub release into `~/Music/Samples/GrooveKit Starter` and adds the folder to the library (Browser → Library folders → Download starter samples).

`starter-map.mjs` says where every source file goes and what it's called; `build-starter.mjs` pulls the sources at pinned commits, converts them to FLAC (lossless, about half the size of WAV) and zips the result, stored rather than compressed since FLAC has done the shrinking.

## Sources

| Source | Licence | What |
|---|---|---|
| [MckAudio sample packs](https://github.com/MckAudio/MckSamplePacks) | CC0 | TR-8 (its 606, 707, 727, 808 and 909 models), DR-5, RD-6 and RX5 |
| [TR-808 samples by Michael Fischer](https://github.com/tidalcycles/sounds-tr808-fischer) | CC0 | the 808 at several tone, decay, snappy and tune settings |
| [uzu-drumkit](https://github.com/tidalcycles/uzu-drumkit) | Unlicense | synthesized and analogue-processed drums ("Analog" in the pack) |
| [Sonic Pi samples](https://github.com/sonic-pi-net/sonic-pi) | CC0 | Arovane's loops, bass, synth, ambient, FX and tabla; the acoustic kit and guitars are left out |

Folder and file names say which machine a sound was recorded from; the makers of those machines aren't involved. The pack's `LICENSES.txt` carries every source's licence text and the Sonic Pi README with its Freesound links.

## Building and publishing

Needs git, ffmpeg, ffprobe and zip (`brew install ffmpeg`).

```sh
node samples/build-starter.mjs            # -> build/starter/GrooveKit-Starter.zip
```

The build prints the zip's size. To publish a new pack:

1. Raise `PACK_VERSION` in `build-starter.mjs` when the contents changed (the app offers the download again to anyone with an older pack).
2. Build, then attach the zip to a release tagged `starter-samples-v<version>`:
   ```sh
   gh release create starter-samples-v1 build/starter/GrooveKit-Starter.zip --title "Starter samples v1" --notes "..."
   ```
3. Put the version, URL and size in `engine/src/library/StarterPack.cpp` (`packVersion`, `packUrl`, `packBytes`, `packFiles`); the engine refuses a download whose size differs.

To try a pack before publishing, point the engine at the local zip:

```sh
GROOVEKIT_STARTER_URL=file:///path/to/GrooveKit-Starter.zip GROOVEKIT_STARTER_BYTES=<size> build/dev/engine/groovekit-engine serve <project>
```
