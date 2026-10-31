#include "WindowOrder.h"

#import <AppKit/AppKit.h>

namespace gk
{

void orderFrontRegardless (void* nativeView)
{
    if (auto* view = static_cast<NSView*> (nativeView))
    {
        // A window stays on the Space it was first shown on: shown again while the UI is full
        // screen (a Space of its own), it would come up behind it on the desktop. Moved to the
        // Space in use instead, and allowed beside a full-screen window there.
        auto* window = [view window];
        [window setCollectionBehavior: [window collectionBehavior] | NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenAuxiliary];
        [window orderFrontRegardless];
    }
}

} // namespace gk
