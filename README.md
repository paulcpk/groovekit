# GrooveKit

An agent-first DAW: FL Studio's workflow, a clean minimal look, and AI agents as first-class producers.
The human is the director; the agent does arrangement, mixing, automation and mastering.

Status: early development (v0, headless engine). See [docs/architecture.md](docs/architecture.md) and [docs/roadmap.md](docs/roadmap.md).

## Building (macOS)

Requirements: Xcode Command Line Tools, CMake ≥ 3.22, Ninja, Node ≥ 24 and pnpm.

```sh
git submodule update --init --recursive
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## License

AGPLv3. GrooveKit builds on [JUCE 8](https://juce.com) (AGPLv3) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) (GPLv3).
