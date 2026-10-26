#include "EditGraveyard.h"

namespace gk
{

namespace
{
    constexpr juce::uint32 keepMs = 1000;
}

EditGraveyard& EditGraveyard::getInstance()
{
    static EditGraveyard graveyard;
    return graveyard;
}

void EditGraveyard::bury (std::unique_ptr<tracktion::Edit> edit)
{
    if (edit == nullptr)
        return;

    edit->getTransport().stop (false, true);
    graves.push_back ({ std::move (edit), juce::Time::getMillisecondCounter() });

    if (! isTimerRunning())
        startTimer (250);
}

void EditGraveyard::hold()
{
    ++holds;
}

void EditGraveyard::release()
{
    holds = std::max (0, holds - 1);
}

void EditGraveyard::timerCallback()
{
    if (holds > 0)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    graves.erase (std::remove_if (graves.begin(), graves.end(),
                                  [now] (const Grave& g) { return now - g.buriedAt >= keepMs; }),
                  graves.end());

    if (graves.empty())
        stopTimer();
}

void EditGraveyard::flush()
{
    stopTimer();

    if (graves.empty())
        return;

    if (auto mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
        mm->runDispatchLoopUntil (static_cast<int> (keepMs / 2));

    graves.clear();
}

} // namespace gk
