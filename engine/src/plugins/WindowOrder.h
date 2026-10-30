#pragma once

namespace gk
{

/** Puts the window holding this native view (an NSView) in front of every app's windows, without
    making the engine the active app. A background process can't make itself active (macOS 14 on),
    and plain ordering to the front only orders among the engine's own windows; the UI's window
    keeps the keyboard, and a click in this one makes the engine active. */
void orderFrontRegardless (void* nativeView);

} // namespace gk
