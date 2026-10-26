#pragma once

#include "session/Session.h"

namespace gk
{

class EngineContext;

/** JSON-RPC 2.0 over any message transport. Lives on the message thread.

    Requests: execute, undo, redo, get, getSummary, getHistory, render, analyze, listEffects, hello.
    Notifications to clients: "snapshot" on connect, "patch" after every change from any client.
    See docs/protocol.md.
*/
class ApiHandler
{
public:
    using Send = std::function<void (const std::string& message)>;

    struct Client
    {
        std::string origin = "client";
        Send send;
    };

    ApiHandler (Session&, EngineContext&);
    ~ApiHandler();

    /** Called for every change; the server sends the message to all clients. */
    void setBroadcast (Send);

    /** The notification a client gets when it connects. */
    std::string getSnapshotMessage() const;

    /** Handles one incoming message and sends the response through client.send. */
    void handleMessage (const std::string& text, Client&);

    /** The JSON-RPC result for one method call. Throws on errors. */
    model::Json call (const std::string& method, const model::Json& params, Client&);

private:
    Session& session;
    EngineContext& context;
    Send broadcast;
    int listenerId = 0;
    std::string lastOrigin;
    int renderCount = 0;

    model::Json render (const model::Json& params);
};

} // namespace gk
