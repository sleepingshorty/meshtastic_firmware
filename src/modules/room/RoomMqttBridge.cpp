#include "RoomMqttBridge.h"

#if ROOM_SERVER_ENABLED

#include "Default.h"
#include "mqtt/MQTT.h"
#include "mesh/NodeDB.h"

namespace roomserver {

void RoomMqttBridge::start()
{
    if (!config_.isEnabled()) {
        LOG_INFO("RoomMqttBridge: disabled (RoomServerConfig not enabled)");
        return;
    }
    if (!mqtt || !mqtt->isEnabled()) {
        LOG_WARN("RoomMqttBridge: MQTT not available, start deferred");
        return;
    }

    mqtt->subscribe("rooms/messages/#", 1);

    started = true;

    const std::string serverId = nodeDB->getNodeId();
    for (const auto &room : config_.rooms()) {
        publishSubscribed(room.roomName, serverId);
    }
}

std::string RoomMqttBridge::composeTopic(const std::string &roomName, bool isAck, const std::string &roomServerId) const
{
    std::string topic = kTopicPrefix;
    topic += roomName;
    if (isAck) {
        topic += kAckSegment;
        topic += roomServerId;
    } else if (!roomServerId.empty()) {
        topic += kSubscribedSegment;
        topic += roomServerId;
    }
    return topic;
}

std::optional<MqttMessage> RoomMqttBridge::parseIncoming(const std::string &topic, const std::string &payload) const
{
    if (topic.rfind(kTopicPrefix, 0) != 0)
        return std::nullopt;

    MqttMessage msg;
    const auto suffix = topic.substr(std::char_traits<char>::length(kTopicPrefix));

    const auto ackPos = suffix.find(kAckSegment);
    const auto subPos = suffix.find(kSubscribedSegment);

    if (ackPos != std::string::npos) {
        msg.isAck = true;
        msg.roomName = suffix.substr(0, ackPos);
        msg.roomServerId = suffix.substr(ackPos + std::char_traits<char>::length(kAckSegment));
    } else if (subPos != std::string::npos) {
        msg.isAck = false;
        msg.roomName = suffix.substr(0, subPos);
        msg.roomServerId = suffix.substr(subPos + std::char_traits<char>::length(kSubscribedSegment));
    } else {
        msg.isAck = false;
        msg.roomName = suffix;
    }

    const bool hasServerMarker = msg.isAck || !msg.roomServerId.empty();
    if (!hasServerMarker) {
        RoomMessage dm;
        if (!RoomMessageCodec::parse(payload, dm))
            return std::nullopt;

        if (dm.roomName != msg.roomName)
            return std::nullopt;

        msg.payload = dm.payload;
    } else {
        msg.payload = payload;
    }

    return msg;
}

bool RoomMqttBridge::publishMessage(const std::string &roomName, const std::string &payload, const std::string &roomServerId)
{
    if (!started || !mqtt || !mqtt->isEnabled())
        return false;

    auto topic = composeTopic(roomName, false, roomServerId);
    auto data = composeMqttPayload(roomName, payload);
    return mqtt->publish(topic.c_str(), data.c_str(), false);
}

bool RoomMqttBridge::publishAck(const std::string &roomName, const std::string &roomServerId)
{
    if (!started || !mqtt || !mqtt->isEnabled())
        return false;

    auto topic = composeTopic(roomName, true, roomServerId);
    return mqtt->publish(topic.c_str(), "", false);
}

bool RoomMqttBridge::publishSubscribed(const std::string &roomName, const std::string &roomServerId)
{
    if (!started || !mqtt || !mqtt->isEnabled())
        return false;

    auto topic = composeTopic(roomName, false, roomServerId);
    return mqtt->publish(topic.c_str(), "subscribed", false);
}

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
