#include "WindowOrder.h"

#import <AppKit/AppKit.h>

namespace gk
{

namespace
{
    /** Allowed beside a full-screen window, on the Space it's shown on. */
    constexpr auto alongside = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorMoveToActiveSpace;
}

void runAsAccessory()
{
    [[NSApplication sharedApplication] setActivationPolicy: NSApplicationActivationPolicyAccessory];
}

void prepareForSpaces (void* nativeView)
{
    if (auto* view = static_cast<NSView*> (nativeView))
        [[view window] setCollectionBehavior: [[view window] collectionBehavior] | alongside];
}

void orderFrontRegardless (void* nativeView)
{
    if (auto* view = static_cast<NSView*> (nativeView))
    {
        // A window stays on the Space it was first shown on, and a background process ordering it
        // to the front doesn't move it to the Space in use: shown while the UI is full screen (a
        // Space of its own), it would come up on the desktop, out of sight. Joining every Space for
        // the moment it's ordered in puts it on the one in use, the UI's full-screen Space too,
        // where it then stays. (Joining every Space and moving to the active one can't go together.)
        auto* window = [view window];
        const auto behaviour = ([window collectionBehavior] | alongside) & ~NSWindowCollectionBehaviorCanJoinAllSpaces;
        [window setCollectionBehavior: (behaviour & ~NSWindowCollectionBehaviorMoveToActiveSpace) | NSWindowCollectionBehaviorCanJoinAllSpaces];
        [window orderFrontRegardless];
        [window setCollectionBehavior: behaviour];
    }
}

void orderOut (void* nativeView)
{
    if (auto* view = static_cast<NSView*> (nativeView))
        [[view window] orderOut: nil];
}

int orderedIndex (void* nativeView)
{
    auto* view = static_cast<NSView*> (nativeView);
    auto* window = view != nullptr ? [view window] : nil;
    return window != nil && [window isVisible] ? static_cast<int> ([window orderedIndex]) : -1;
}

} // namespace gk
