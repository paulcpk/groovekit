#pragma once

#include "ApiHandler.h"

#include <memory>

namespace gk
{

/** Serves the ApiHandler over WebSockets on localhost.

    Network threads hand every message to the JUCE message thread, where the session
    lives, so the handler never runs concurrently with itself.
*/
class ApiServer
{
public:
    ApiServer (ApiHandler&, int port);
    ~ApiServer();

    /** Throws if the port can't be opened. */
    void start();
    void stop();

    int getPort() const     { return port; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int port;
};

} // namespace gk
