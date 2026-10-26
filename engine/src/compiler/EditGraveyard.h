#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

/** Deletes discarded Edits a little later instead of straight away.

    Audio Units deliver parameter notifications later on the run loop (CAEventReceiver). If the
    plugin is gone by then, the notification calls into a dead listener and the process aborts
    with std::bad_function_call. Keeping discarded Edits for a second lets those notifications
    arrive while their plugins still exist.
*/
class EditGraveyard : private juce::Timer
{
public:
    static EditGraveyard& getInstance();

    void bury (std::unique_ptr<tracktion::Edit>);

    /** Lets pending notifications arrive, then deletes every Edit. Call before the Engine goes. */
    void flush();

private:
    struct Grave
    {
        std::unique_ptr<tracktion::Edit> edit;
        juce::uint32 buriedAt;
    };

    std::vector<Grave> graves;

    void timerCallback() override;
};

} // namespace gk
