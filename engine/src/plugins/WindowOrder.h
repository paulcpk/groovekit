#pragma once

namespace gk
{

/** Puts the window holding this native view (an NSView) in front of every app's windows, without
    making the engine the active app. A background process can't make itself active (macOS 14 on),
    and plain ordering to the front only orders among the engine's own windows; the UI's window
    keeps the keyboard, and a click in this one makes the engine active. It comes up on the Space in
    use, over the UI when that's full screen. */
void orderFrontRegardless (void* nativeView);

/** Takes the window holding this native view off the screen without closing it (it stays open,
    and comes back with orderFrontRegardless). */
void orderOut (void* nativeView);

/** Where the window holding this native view is among the engine's windows, 0 in front; -1 when
    it isn't on screen. */
int orderedIndex (void* nativeView);

} // namespace gk
