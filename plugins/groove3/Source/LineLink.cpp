#include "LineLink.h"
#include "Line.h"

namespace groove3
{

LineLink::~LineLink()
{
    // Deleted while GrooveKit still knew it (it lets go first when it can).
    if (auto* h = std::exchange (host, nullptr))
        h->detach();
}

Steinberg::tresult PLUGIN_API LineLink::queryInterface (const Steinberg::TUID iid, void** obj)
{
    if (Steinberg::FUnknownPrivate::iidEqual (iid, gkmachine::IMachine::getIid())
        || Steinberg::FUnknownPrivate::iidEqual (iid, Steinberg::FUnknown_iid))
    {
        *obj = static_cast<gkmachine::IMachine*> (this);
        return Steinberg::kResultOk;
    }

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

int32_t LineLink::Extensions::queryIAudioProcessor (const Steinberg::TUID iid, void** obj)
{
    // Owned by the processor, so counting references means nothing (addRef is a no-op).
    if (Steinberg::FUnknownPrivate::iidEqual (iid, gkmachine::IMachine::getIid()))
        return link.queryInterface (iid, obj);

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

void PLUGIN_API LineLink::setHost (gkmachine::IHost* newHost)
{
    host = newHost;
    linked = host != nullptr;
    playStep = -1;
    hostPlaying = false;

    if (host == nullptr)
    {
        pattern = {};
        notes.clear();
    }
    else
    {
        host->setStepTicks (stepTicks);
    }

    ++changes;
}

void PLUGIN_API LineLink::showPattern (const gkmachine::Pattern& p)
{
    pattern.exists = p.exists != 0;
    pattern.name = juce::String::fromUTF8 (p.name != nullptr ? p.name : "");
    pattern.lengthSteps = std::max (1, p.lengthSteps);
    pattern.stepsPerBar = std::max (1, p.stepsPerBar);
    ++changes;
}

void PLUGIN_API LineLink::showNotes (const gkmachine::Note* shown, gkmachine::int32 count)
{
    notes.assign (shown, shown + std::max (0, count));
    ++changes;
}

void PLUGIN_API LineLink::setPlayhead (gkmachine::int32 step, gkmachine::int32 playing)
{
    if (step == playStep && (playing != 0) == hostPlaying)
        return;

    playStep = step;
    hostPlaying = playing != 0;
    ++changes;
}

} // namespace groove3
