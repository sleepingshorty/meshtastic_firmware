#include "RoomServerService.h"

#if ROOM_SERVER_ENABLED

#include "Default.h"
#include "RoomLoRaDispatcher.h"
#include "RoomMqttBridge.h"
#include "mesh/NodeDB.h"
#include <cstdlib>
#include <sstream>
#include <vector>

namespace roomserver {

namespace {

bool parseNodeId(const std::string &id, uint32_t &out)
{
    if (id.empty())
        return false;

    const char *cursor = id.c_str();
    if (*cursor == '!')
        ++cursor;

    char *end = nullptr;
    unsigned long value = std::strtoul(cursor, &end, 16);
    if (cursor == end || (end && *end != '\0'))
        return false;

    out = static_cast<uint32_t>(value);
    return true;
}

} // namespace

RoomServerService &RoomServerService::instance()
{
    static RoomServerService service;
    return service;
}

void RoomServerService::begin()
{
    if (initialized)
        return;

    config_.reload();
    if (!config_.isEnabled()) {
        LOG_INFO("RoomServerService: disabled (primary channel criteria not met or no rooms configured)");
        return;
    }

    stateStore_.load();
    mqttBridge_ = std::make_unique<RoomMqttBridge>(config_, stateStore_);
    loRaDispatcher_ = std::make_unique<RoomLoRaDispatcher>(config_, stateStore_);

    mqttBridge_->start();
    loRaDispatcher_->start();
    stateStore_.setMqttOnline(mqtt && mqtt->isEnabled());
    ensureResyncIfNeeded();

    initialized = true;
    LOG_INFO("RoomServerService: ready with %u configured room(s)", static_cast<unsigned>(config_.roomCount()));
}

void RoomServerService::handleMqtt(const std::string &topic, const std::string &payload)
{
    if (!initialized || !mqttBridge_)
        return;

    auto parsed = mqttBridge_->parseIncoming(topic, payload);
    if (!parsed)
        return;

    const auto *room = config_.findRoom(parsed->roomName);
    if (!room) {
        LOG_DEBUG("RoomServerService: ignore MQTT message for unknown room '%s'", parsed->roomName.c_str());
        return;
    }

    stateStore_.setMqttOnline(true);

    if (!parsed->roomServerId.empty()) {
        uint32_t nodeId = 0;
        if (parseNodeId(parsed->roomServerId, nodeId)) {
            stateStore_.addOrUpdateRoomPeer(room->roomName, {nodeId, 0});
            if (parsed->isAck)
                stateStore_.recordAck(room->roomName, nodeId);
        } else {
            LOG_WARN("RoomServerService: could not parse room-server id '%s'", parsed->roomServerId.c_str());
        }
    }

    if (parsed->isAck) {
        LOG_INFO("RoomServerService: MQTT ack room=%s from=%s", parsed->roomName.c_str(),
                 parsed->roomServerId.empty() ? "<unknown>" : parsed->roomServerId.c_str());
        return;
    }

    RoomMessage msg{parsed->roomName, parsed->payload};
    if (loRaDispatcher_)
        loRaDispatcher_->scheduleLocalInject(msg);

    const std::string localId = nodeDB ? nodeDB->getNodeId() : "";
    if (mqttBridge_ && !localId.empty())
        mqttBridge_->publishAck(parsed->roomName, localId);

    LOG_INFO("RoomServerService: MQTT message room=%s payload_len=%u server=%s",
             parsed->roomName.c_str(), static_cast<unsigned>(parsed->payload.size()),
             parsed->roomServerId.empty() ? "<none>" : parsed->roomServerId.c_str());
}

void RoomServerService::handleLoRaDm(const std::string &rawPayload, uint32_t fromNode)
{
    if (!initialized || !loRaDispatcher_)
        return;

    auto parsed = loRaDispatcher_->parseIncomingDm(rawPayload, fromNode);
    if (!parsed)
        return;

    const auto *room = config_.findRoom(parsed->message.roomName);
    if (!room)
        return;

    stateStore_.addOrUpdateRoomPeer(room->roomName, {fromNode, 0});
    loRaDispatcher_->scheduleLocalInject(parsed->message);

    const std::string localId = nodeDB ? nodeDB->getNodeId() : "";
    if (mqttBridge_ && !localId.empty())
        mqttBridge_->publishAck(parsed->message.roomName, localId);
}

void RoomServerService::handleLocalRoomMessage(const std::string &roomName, const std::string &payload)
{
    if (!initialized || !mqttBridge_ || !loRaDispatcher_)
        return;

    const auto *room = config_.findRoom(roomName);
    if (!room)
        return;

    bool published = mqttBridge_->publishMessage(roomName, payload, "");
    const auto expected = expectedPeersForRoom(roomName);

    if (!published || !stateStore_.isMqttOnline()) {
        for (uint32_t peer : expected) {
            loRaDispatcher_->scheduleFallback(roomName, payload, peer);
        }
    }
}

std::vector<uint32_t> RoomServerService::expectedPeersForRoom(const std::string &roomName) const
{
    std::vector<uint32_t> peers;
    const auto &stored = stateStore_.getRoomPeers(roomName);
    peers.reserve(stored.size());
    for (const auto &p : stored)
        peers.push_back(p.nodeId);
    return peers;
}

void RoomServerService::ensureResyncIfNeeded()
{
    if (!loRaDispatcher_ || stateStore_.isMqttOnline())
        return;

    bool anyMissing = false;
    for (const auto &room : config_.rooms()) {
        if (stateStore_.getRoomPeers(room.roomName).empty()) {
            anyMissing = true;
            break;
        }
    }

    if (!anyMissing)
        return;

    std::ostringstream oss;
    oss << "[ROOM_CTRL] RESYNC_REQUEST rooms=";
    bool first = true;
    for (const auto &room : config_.rooms()) {
        if (!first)
            oss << ",";
        first = false;
        oss << room.roomName;
    }

    loRaDispatcher_->scheduleResyncRequest(oss.str());
}

#ifdef PIO_UNIT_TESTING
void RoomServerService::resetForTest()
{
    stateStore_.clear();
    mqttBridge_.reset();
    loRaDispatcher_.reset();
    initialized = false;
}
#endif

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
