#pragma once

namespace gk
{

/** Puts the window holding this native view (an NSView) in front of every app's windows, without
    making the engine the active app. A background process can't make itself active (macOS 14 on),
    and plain ordering to the front only orders among the engine's own windows; the UI's window
    keeps the keyboard, and a click in this one makes the engine active. It comes up on the Space in
    use, over the UI when that's full screen. */
void orderFrontRegardless (void* nativeView);

/** Makes the engine an accessory app: no Dock icon or menu bar of its own, and its windows (the
    plugins') may come up beside another app's full-screen window, the UI's. Run from inside the
    app's bundle, the engine would otherwise count as an ordinary app, whose windows macOS keeps
    off a full-screen Space that isn't its own. */
void runAsAccessory();

/** Readies a new window (before it's first shown) to come up beside a full-screen window: the UI's,
    when it's full screen. */
void prepareForSpaces (void* nativeView);

/** Takes the window holding this native view off the screen without closing it (it stays open,
    and comes back with orderFrontRegardless). */
void orderOut (void* nativeView);

/** Where the window holding this native view is among the engine's windows, 0 in front; -1 when
    it isn't on screen. */
int orderedIndex (void* nativeView);

} // namespace gk
