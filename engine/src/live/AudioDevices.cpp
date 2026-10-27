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

juce::String defaultAudioDeviceName (bool input)
{
   #if JUCE_MAC
    const AudioObjectPropertyAddress address { input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
                                               kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof (device);

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr || device == kAudioObjectUnknown)
        return {};

    const AudioObjectPropertyAddress nameAddress { kAudioDevicePropertyDeviceNameCFString, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef name = nullptr;
    size = sizeof (name);

    if (AudioObjectGetPropertyData (device, &nameAddress, 0, nullptr, &size, &name) != noErr || name == nullptr)
        return {};

    const auto result = juce::String::fromCFString (name);
    CFRelease (name);
    return result;
   #else
    juce::ignoreUnused (input);
    return {};
   #endif
}

struct DefaultOutputWatcher::Impl
{
    explicit Impl (std::function<void()> f) : onChange (std::move (f))
    {
       #if JUCE_MAC
        AudioObjectAddPropertyListener (kAudioObjectSystemObject, &address, listener, this);
       #endif
    }

    ~Impl()
    {
        *alive = false;
       #if JUCE_MAC
        AudioObjectRemovePropertyListener (kAudioObjectSystemObject, &address, listener, this);
       #endif
    }

   #if JUCE_MAC
    // Called on a CoreAudio thread.
    static OSStatus listener (AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* data)
    {
        auto self = static_cast<Impl*> (data);
        juce::MessageManager::callAsync ([self, alive = self->alive]
        {
            if (*alive)
                self->onChange();
        });
        return noErr;
    }

    const AudioObjectPropertyAddress address { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
   #endif

    std::function<void()> onChange;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

DefaultOutputWatcher::DefaultOutputWatcher (std::function<void()> onChange) : impl (std::make_unique<Impl> (std::move (onChange))) {}
DefaultOutputWatcher::~DefaultOutputWatcher() = default;

bool hasBluetoothInput (const juce::AudioDeviceManager& devices)
{
    return isBluetoothAudioDevice (devices.getAudioDeviceSetup().inputDeviceName);
}

juce::String applyAudioSetup (juce::AudioDeviceManager& devices, juce::AudioDeviceManager::AudioDeviceSetup setup, bool chosen, std::string& notice)
{
    if (isBluetoothAudioDevice (setup.inputDeviceName))
    {
        notice = setup.inputDeviceName.toStdString() + "'s microphone isn't used: it would switch the headset to call quality";
        setup.inputDeviceName = {};
    }

    auto error = devices.setAudioDeviceSetup (setup, chosen);

    // An input and an output that share no sample rate can't run together: play without the input.
    if (error.isNotEmpty() && setup.inputDeviceName.isNotEmpty())
    {
        const auto input = setup.inputDeviceName;
        setup.inputDeviceName = {};

        if (devices.setAudioDeviceSetup (setup, chosen).isEmpty())
        {
            notice = input.toStdString() + " and " + setup.outputDeviceName.toStdString()
                   + " can't run together (" + error.toStdString() + "), so there's no input. Pick an input that works with this output to record.";
            return {};
        }
    }

    return error;
}

} // namespace gk
