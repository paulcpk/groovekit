#include "AudioDevices.h"

#include <vector>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

namespace gk
{

bool isBluetoothAudioDevice (const juce::String& name)
{
   #if JUCE_MAC
    if (name.isEmpty())
        return false;

    const auto address = [] (AudioObjectPropertySelector selector)
    {
        return AudioObjectPropertyAddress { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    };

    const auto devicesAddress = address (kAudioHardwarePropertyDevices);
    UInt32 size = 0;

    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size) != noErr)
        return false;

    std::vector<AudioObjectID> devices (size / sizeof (AudioObjectID));

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size, devices.data()) != noErr)
        return false;

    for (auto device : devices)
    {
        // The name JUCE lists the device by.
        const auto nameAddress = address (kAudioDevicePropertyDeviceNameCFString);
        CFStringRef deviceName = nullptr;
        size = sizeof (deviceName);

        if (AudioObjectGetPropertyData (device, &nameAddress, 0, nullptr, &size, &deviceName) != noErr || deviceName == nullptr)
            continue;

        const bool matches = juce::String::fromCFString (deviceName) == name;
        CFRelease (deviceName);

        if (! matches)
            continue;

        const auto transportAddress = address (kAudioDevicePropertyTransportType);
        UInt32 transport = 0;
        size = sizeof (transport);

        if (AudioObjectGetPropertyData (device, &transportAddress, 0, nullptr, &size, &transport) == noErr
            && (transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE))
            return true;
    }
   #else
    juce::ignoreUnused (name);
   #endif

    return false;
}

bool hasUnchosenBluetoothInput (const juce::AudioDeviceManager& devices)
{
    const auto input = devices.getAudioDeviceSetup().inputDeviceName;

    if (! isBluetoothAudioDevice (input))
        return false;

    // The settings last chosen explicitly (audio.setDevice), or restored from them.
    auto chosen = devices.createStateXml();
    return chosen == nullptr || chosen->getStringAttribute ("audioInputDeviceName") != input;
}

} // namespace gk
