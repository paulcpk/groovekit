#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace gk
{

/** True if the named audio device is connected over Bluetooth (AirPods and other headsets).
    Opening such a device's microphone switches it to call quality, mono at 16-24 kHz,
    for everything it plays, so the engine only uses one as input when asked to. */
bool isBluetoothAudioDevice (const juce::String& name);

/** True if the input is a Bluetooth headset's microphone that isn't the input last chosen
    (macOS makes it the default input along with the output, and JUCE opens the defaults). */
bool hasUnchosenBluetoothInput (const juce::AudioDeviceManager&);

/** The name of macOS's default output (or input) device, as JUCE lists it; empty if there's none. */
juce::String defaultAudioDeviceName (bool input);

/** Calls back on the message thread whenever macOS's default output device changes (headphones
    connected, AirPods taken out, another output picked in the menu bar). */
class DefaultOutputWatcher
{
public:
    explicit DefaultOutputWatcher (std::function<void()> onChange);
    ~DefaultOutputWatcher();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gk
