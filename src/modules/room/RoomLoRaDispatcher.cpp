#include "RoomLoRaDispatcher.h"

#if ROOM_SERVER_ENABLED

namespace roomserver {

void RoomLoRaDispatcher::start()
{
    started = true;
}

std::optional<LoRaIncomingMessage> RoomLoRaDispatcher::parseIncomingDm(const std::string &rawPayload, uint32_t fromNode) const
{
    RoomMessage msg;
    if (!RoomMessageCodec::parse(rawPayload, msg))
        return std::nullopt;

    LoRaIncomingMessage incoming;
    incoming.message = std::move(msg);
    incoming.fromNode = fromNode;
    return incoming;
}

void RoomLoRaDispatcher::scheduleFallback(const std::string &roomName, const std::string &payload, uint32_t toNode)
{
    if (!started)
        return;
    pendingFallback_.push_back(LoRaOutgoingDm{roomName, RoomMessageCodec::compose(roomName, payload), toNode});
}

void RoomLoRaDispatcher::scheduleLocalInject(const RoomMessage &message)
{
    if (!started)
        return;
    pendingLocalInjects_.push_back(message);
}

void RoomLoRaDispatcher::scheduleResyncRequest(const std::string &payload)
{
    if (!started)
        return;
    pendingResync_.push_back(payload);
}

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
