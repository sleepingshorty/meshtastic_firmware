#pragma once

#include "configuration.h"

#if ROOM_SERVER_ENABLED

#include "RoomMessage.h"
#include "RoomServerConfig.h"
#include "RoomStateStore.h"
#include <optional>

namespace roomserver {

struct MqttMessage {
    std::string roomName;
    std::string payload;
    bool isAck = false;
    std::string roomServerId;
};

class RoomMqttBridge
{
  public:
    RoomMqttBridge(RoomServerConfig &config, RoomStateStore &state) : config_(config), state_(state) {}

    // Expose prefix for MQTT subscription routing
    static constexpr const char *kTopicPrefix = "rooms/messages/";
    static constexpr const char *kAckSegment = "/ack/";
    static constexpr const char *kSubscribedSegment = "/subscribed/";

    void start();

    std::string composeMqttPayload(const std::string &roomName, const std::string &payload) const
    {
        return RoomMessageCodec::compose(roomName, payload);
    }

    std::string composeTopic(const std::string &roomName, bool isAck, const std::string &roomServerId) const;

    std::optional<MqttMessage> parseIncoming(const std::string &topic, const std::string &payload) const;

    bool publishMessage(const std::string &roomName, const std::string &payload, const std::string &roomServerId);
    bool publishAck(const std::string &roomName, const std::string &roomServerId);
    bool publishSubscribed(const std::string &roomName, const std::string &roomServerId);

  private:
    RoomServerConfig &config_;
    RoomStateStore &state_;
    bool started = false;
};

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
