#pragma once

#include "configuration.h"

#if ROOM_SERVER_ENABLED

#include "RoomServerConfig.h"
#include "RoomStateStore.h"
#include <vector>

namespace roomserver {

class RoomMqttBridge;
class RoomLoRaDispatcher;

class RoomServerService
{
  public:
    static RoomServerService &instance();

    void begin();

    bool isEnabled() const { return initialized && config_.isEnabled(); }

    RoomServerConfig &config() { return config_; }
    const RoomServerConfig &config() const { return config_; }

    RoomStateStore &state() { return stateStore_; }
    const RoomStateStore &state() const { return stateStore_; }

    RoomLoRaDispatcher *loRaDispatcher() { return loRaDispatcher_.get(); }
    RoomMqttBridge *mqttBridge() { return mqttBridge_.get(); }

    // Entry point for incoming MQTT messages (rooms/messages/#) from MQTT.cpp
    void handleMqtt(const std::string &topic, const std::string &payload);
    // Entry point for incoming LoRa DMs with room payloads
    void handleLoRaDm(const std::string &rawPayload, uint32_t fromNode);
    // Entry point for locally received room chat messages
    void handleLocalRoomMessage(const std::string &roomName, const std::string &payload);

#ifdef PIO_UNIT_TESTING
    void resetForTest();
#endif

  private:
    RoomServerService() = default;

    std::vector<uint32_t> expectedPeersForRoom(const std::string &roomName) const;
    void ensureResyncIfNeeded();

    RoomServerConfig config_;
    RoomStateStore stateStore_;
    std::unique_ptr<class RoomMqttBridge> mqttBridge_;
    std::unique_ptr<class RoomLoRaDispatcher> loRaDispatcher_;
    bool initialized = false;
};

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
