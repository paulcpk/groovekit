#include "WindowOrder.h"

#import <AppKit/AppKit.h>

namespace gk
{

void orderFrontRegardless (void* nativeView)
{
    if (auto* view = static_cast<NSView*> (nativeView))
        [[view window] orderFrontRegardless];
}

} // namespace gk
