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

} // namespace gk
