#include "HostLink.h"
#include "Sequencer.h"

namespace rhythm
{

HostLink::HostLink (juce::AudioProcessorValueTreeState& s) : state (s)
{
    state.addParameterListener (id::scale, this);
}

HostLink::~HostLink()
{
    state.removeParameterListener (id::scale, this);
    cancelPendingUpdate();

    // Deleted while GrooveKit still knew it (it lets go first when it can).
    if (auto* h = std::exchange (host, nullptr))
        h->detach();
}

bool HostLink::isInsideGrooveKit()
{
    // In process: the host is the engine's executable (in the app as in a build).
    static const bool inside = juce::File (juce::PluginHostType::getHostPath()).getFileName().startsWith ("groovekit-engine");
    return inside;
}

int HostLink::ticksForScale (int scale)
{
    switch (scale)
    {
        case 1:  return 320;    // 8th triplets
        case 2:  return 160;    // 16th triplets
        case 3:  return 120;    // 32nds
        default: return 240;    // 16ths
    }
}

Steinberg::tresult PLUGIN_API HostLink::queryInterface (const Steinberg::TUID iid, void** obj)
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

int32_t HostLink::Extensions::queryIAudioProcessor (const Steinberg::TUID iid, void** obj)
{
    // Owned by the processor, so counting references means nothing (addRef is a no-op).
    if (Steinberg::FUnknownPrivate::iidEqual (iid, gkmachine::IMachine::getIid()))
        return link.queryInterface (iid, obj);

    *obj = nullptr;
    return Steinberg::kNoInterface;
}

void PLUGIN_API HostLink::setHost (gkmachine::IHost* newHost)
{
    host = newHost;
    linked = host != nullptr;
    playStep = -1;
    hostPlaying = false;

    if (host == nullptr)
        pattern = {};

    ++changes;
    sendStepTicks();
}

void PLUGIN_API HostLink::showPattern (const gkmachine::Pattern& p)
{
    pattern.exists = p.exists != 0;
    pattern.name = juce::String::fromUTF8 (p.name != nullptr ? p.name : "");
    pattern.stepTicks = std::max (1, p.stepTicks);
    pattern.lengthSteps = std::max (1, p.lengthSteps);
    pattern.stepsPerBar = std::max (1, p.stepsPerBar);
    pattern.swing = p.swing;
    pattern.hits.assign (p.hits, p.hits + std::max (0, p.numHits));
    ++changes;
}

void PLUGIN_API HostLink::setPlayhead (gkmachine::int32 step, gkmachine::int32 playing)
{
    if (step == playStep && (playing != 0) == hostPlaying)
        return;

    playStep = step;
    hostPlaying = playing != 0;
    ++changes;
}

void HostLink::parameterChanged (const juce::String&, float)
{
    // From any thread (automation comes on the audio thread).
    triggerAsyncUpdate();
}

void HostLink::handleAsyncUpdate()
{
    sendStepTicks();
}

void HostLink::sendStepTicks()
{
    if (host != nullptr)
        host->setStepTicks (ticksForScale (juce::roundToInt (state.getRawParameterValue (id::scale)->load())));
}

} // namespace rhythm
