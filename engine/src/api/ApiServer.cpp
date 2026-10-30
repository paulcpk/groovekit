#include "ApiServer.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <juce_events/juce_events.h>

namespace gk
{

struct ApiServer::Impl
{
    Impl (ApiHandler& h, int port)
        : handler (&h), server (port, "127.0.0.1")
    {
        server.disablePerMessageDeflate();

        server.setOnConnectionCallback ([this, alive = alive] (std::weak_ptr<ix::WebSocket> weak, std::shared_ptr<ix::ConnectionState> state)
        {
            auto ws = weak.lock();

            if (ws == nullptr)
                return;

            ws->setOnMessageCallback ([this, alive, weak, id = state->getId()] (const ix::WebSocketMessagePtr& msg)
            {
                // Runs on a network thread: hand the work to the message thread.
                juce::MessageManager::callAsync ([this, alive, weak, id, type = msg->type, text = std::move (msg->str)]
                {
                    if (*alive)
                        handle (weak, id, type, text);
                });
            });
        });

        attach();
    }

    ~Impl()
    {
        *alive = false;

        if (handler != nullptr)
            handler->setBroadcast ({});
    }

    void attach()
    {
        handler->setBroadcast ([this] (const std::string& message)
        {
            for (auto& [id, client] : clients)
                client.send (message);
        });
    }

    void setHandler (ApiHandler& h)
    {
        handler = &h;
        attach();

        for (auto& [id, client] : clients)
            client.send (handler->getSnapshotMessage());
    }

    void handle (std::weak_ptr<ix::WebSocket> weak, const std::string& id, ix::WebSocketMessageType type, const std::string& text)
    {
        switch (type)
        {
            case ix::WebSocketMessageType::Open:
            {
                auto& client = clients[id];
                client.send = [weak] (const std::string& message)
                {
                    if (auto ws = weak.lock())
                        ws->sendText (message);
                };

                if (handler != nullptr)
                    client.send (handler->getSnapshotMessage());

                break;
            }

            case ix::WebSocketMessageType::Message:
                if (auto it = clients.find (id); it != clients.end() && handler != nullptr)
                    handler->handleMessage (text, it->second);

                break;

            case ix::WebSocketMessageType::Close:
                clients.erase (id);
                break;

            default:
                break;
        }
    }

    ApiHandler* handler;
    ix::WebSocketServer server;
    std::map<std::string, ApiHandler::Client> clients; // message thread only
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

ApiServer::ApiServer (ApiHandler& handler, int p)
    : impl (std::make_unique<Impl> (handler, p)), port (p)
{
}

ApiServer::~ApiServer()
{
    stop();
}

void ApiServer::setHandler (ApiHandler& handler)
{
    impl->setHandler (handler);
}

void ApiServer::detachHandler()
{
    impl->handler = nullptr;
}

void ApiServer::start()
{
    auto [ok, error] = impl->server.listen();

    if (! ok)
        throw std::runtime_error ("Can't listen on port " + std::to_string (port) + ": " + error);

    impl->server.start();
}

void ApiServer::stop()
{
    impl->server.stop();
}

} // namespace gk
