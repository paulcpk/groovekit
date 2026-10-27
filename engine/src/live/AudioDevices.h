#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace gk
{

/** True if the named audio device is connected over Bluetooth (AirPods and other headsets).
    Opening such a device's microphone switches it to call quality, mono at 16-24 kHz, for
    everything it plays, and splits it into an input and an output device of the same name, so
    the engine never uses one as an input. */
bool isBluetoothAudioDevice (const juce::String& name);

/** True if the input is a Bluetooth headset's microphone (macOS makes it the default input
    along with the output, and JUCE opens the defaults). */
bool hasBluetoothInput (const juce::AudioDeviceManager&);

/** Opens a device setup, without a Bluetooth microphone as its input and, if the input and
    output can't run together (no common sample rate), without the input. notice then says what
    was left out. chosen saves it as the user's setup. Returns the error if nothing could open. */
juce::String applyAudioSetup (juce::AudioDeviceManager&, juce::AudioDeviceManager::AudioDeviceSetup, bool chosen, std::string& notice);

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
