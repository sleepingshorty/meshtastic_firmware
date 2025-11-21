#pragma once

#include "configuration.h"

#if ROOM_SERVER_ENABLED

#include "RoomMessage.h"
#include "RoomServerConfig.h"
#include "RoomStateStore.h"
#include <optional>
#include <vector>

namespace roomserver {

struct LoRaIncomingMessage {
    RoomMessage message;
    uint32_t fromNode = 0;
};

struct LoRaOutgoingDm {
    std::string roomName;
    std::string payload;
    uint32_t toNode = 0;
};

class RoomLoRaDispatcher
{
  public:
    RoomLoRaDispatcher(RoomServerConfig &config, RoomStateStore &state) : config_(config), state_(state) {}

    void start();

    std::optional<LoRaIncomingMessage> parseIncomingDm(const std::string &rawPayload, uint32_t fromNode) const;

    bool parseDmPayload(const std::string &raw, RoomMessage &message) const { return RoomMessageCodec::parse(raw, message); }

    // Simulated queues for unit tests
    void scheduleFallback(const std::string &roomName, const std::string &payload, uint32_t toNode);
    const std::vector<LoRaOutgoingDm> &pendingFallback() const { return pendingFallback_; }
    void clearFallback() { pendingFallback_.clear(); }

    void scheduleLocalInject(const RoomMessage &message);
    const std::vector<RoomMessage> &pendingLocalInjects() const { return pendingLocalInjects_; }
    void clearLocalInjects() { pendingLocalInjects_.clear(); }

    void scheduleResyncRequest(const std::string &payload);
    const std::vector<std::string> &pendingResyncRequests() const { return pendingResync_; }
    void clearResync() { pendingResync_.clear(); }

  private:
    RoomServerConfig &config_;
    RoomStateStore &state_;
    bool started = false;
    std::vector<LoRaOutgoingDm> pendingFallback_;
    std::vector<RoomMessage> pendingLocalInjects_;
    std::vector<std::string> pendingResync_;
};

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
