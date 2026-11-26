#include "JackServer.h"

#include <dlfcn.h>
#include <cstdint>

namespace djr
{
namespace JackServer
{

namespace
{
    using Client = void*;
    using OpenFn = Client (*)(const char*, int, int*, ...);
    using CloseFn = int (*)(Client);
    using GetSizeFn = std::uint32_t (*)(Client);
    using SetSizeFn = int (*)(Client, std::uint32_t);
    using GetPortsFn = const char** (*)(Client, const char*, const char*, unsigned long);
    using FreeFn = void (*)(void*);

    constexpr int jackNoStartServer = 0x01;
    constexpr unsigned long jackPortIsInput = 0x1;
    constexpr unsigned long jackPortIsOutput = 0x2;
    constexpr unsigned long jackPortIsPhysical = 0x4;
    constexpr auto jackDefaultAudioType = "32 bit float mono audio";

    struct Api
    {
        OpenFn open = nullptr;
        CloseFn close = nullptr;
        GetSizeFn getSize = nullptr;
        SetSizeFn setSize = nullptr;
        GetPortsFn getPorts = nullptr;
        FreeFn free = nullptr;

        bool isValid() const noexcept { return open != nullptr && close != nullptr && getSize != nullptr && setSize != nullptr; }
    };

    const Api& api()
    {
        static const Api loaded = []
        {
            Api result;

            // The same library JUCE's JACK device loads - on a PipeWire system
            // the ld cache points libjack.so.0 at PipeWire's implementation.
            auto* handle = dlopen("libjack.so.0", RTLD_LAZY | RTLD_NOLOAD);

            if (handle == nullptr)
                handle = dlopen("libjack.so.0", RTLD_LAZY);

            if (handle == nullptr)
                handle = dlopen("libjack.so", RTLD_LAZY);

            if (handle == nullptr)
                return result;

            result.open = reinterpret_cast<OpenFn>(dlsym(handle, "jack_client_open"));
            result.close = reinterpret_cast<CloseFn>(dlsym(handle, "jack_client_close"));
            result.getSize = reinterpret_cast<GetSizeFn>(dlsym(handle, "jack_get_buffer_size"));
            result.setSize = reinterpret_cast<SetSizeFn>(dlsym(handle, "jack_set_buffer_size"));
            result.getPorts = reinterpret_cast<GetPortsFn>(dlsym(handle, "jack_get_ports"));
            result.free = reinterpret_cast<FreeFn>(dlsym(handle, "jack_free"));
            return result;
        }();

        return loaded;
    }

    /** A short-lived client of our own: JUCE keeps its client to itself. */
    class ScopedClient
    {
    public:
        ScopedClient()
        {
            if (api().isValid())
            {
                int status = 0;
                client = api().open("DJR_Studio-latency", jackNoStartServer, &status);
            }
        }

        ~ScopedClient()
        {
            if (client != nullptr)
                api().close(client);
        }

        Client get() const noexcept { return client; }

    private:
        Client client = nullptr;
    };
}

std::optional<int> getBufferSize()
{
    ScopedClient client;

    if (client.get() == nullptr)
        return std::nullopt;

    return static_cast<int>(api().getSize(client.get()));
}

bool setBufferSize(int frames)
{
    if (frames <= 0)
        return false;

    ScopedClient client;
    return client.get() != nullptr && api().setSize(client.get(), static_cast<std::uint32_t>(frames)) == 0;
}

juce::String findHardwareClient(bool playback)
{
    ScopedClient client;

    if (client.get() == nullptr || api().getPorts == nullptr)
        return {};

    // A port that plays sound is one we write into: a JACK *input*.
    const auto flags = jackPortIsPhysical | (playback ? jackPortIsInput : jackPortIsOutput);
    const auto** ports = api().getPorts(client.get(), nullptr, jackDefaultAudioType, flags);

    if (ports == nullptr)
        return {};

    juce::String name;

    if (ports[0] != nullptr)
        name = juce::String::fromUTF8(ports[0]).upToLastOccurrenceOf(":", false, false);

    if (api().free != nullptr)
        api().free(ports);

    return name;
}

} // namespace JackServer
} // namespace djr
