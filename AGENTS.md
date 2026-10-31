# Working on GrooveKit

Notes for coding agents (and people) working in this repository.

## After every larger feature: rebuild the app

When a feature is done, the app is rebuilt so it can be opened and used straight away. Running
GrooveKit may be quit for this without asking; there is no unsaved work to protect while a
feature is being built.

1. Commit to `main` and push (no branches or pull requests).
2. Quit a running GrooveKit: `pkill -x groovekit` (its engine follows it out).
3. Build the engine and GrooveKit's own plugins, then the app:
   ```sh
   cmake --build --preset release --target groovekit-engine
   (cd ui && pnpm app:build)
   ```
   The app lands in `ui/src-tauri/target/release/bundle/macos/GrooveKit.app` (and a `.dmg`).
   Check that `Contents/Resources/Plug-Ins` holds every plugin in `build/release/plugins/all`.

## Test engines

A test engine started with `groovekit-engine serve <folder>` adds the folder to the app's recent
projects; start test engines with `GROOVEKIT_APP_DATA=<scratch folder>` so the app's own list
isn't touched, and stop them when done.

## Writing for the product

Never name other music software (other DAWs, plugins or hardware makers' product names) in the
README, the UI, the agent's instructions, code comments or the website.
