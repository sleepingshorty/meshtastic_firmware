#pragma once

#include "configuration.h"

#if ROOM_SERVER_ENABLED

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace roomserver {

struct RoomPeerState {
    uint32_t nodeId = 0;
    uint32_t lastSeenSeconds = 0;
};

class RoomStateStore
{
  public:
    bool load();
    bool save() const;

    void clear();

    void setRoomPeers(const std::string &roomName, std::vector<RoomPeerState> peers);
    const std::vector<RoomPeerState> &getRoomPeers(const std::string &roomName) const;
    void recordAck(const std::string &roomName, uint32_t nodeId);

    void setMqttOnline(bool online) { mqttOnline = online; }
    bool isMqttOnline() const { return mqttOnline; }

    static constexpr const char *kStateFile = "/prefs/roomserver_state.bin";
    static constexpr uint32_t kMagic = 0x52535256;         // 'RSRV'
    static constexpr uint16_t kStateVersion = 1;
    static constexpr size_t kRoomNameStorage = 16;         // channel names < 12 bytes, add headroom

    void addOrUpdateRoomPeer(const std::string &roomName, RoomPeerState peer);

  private:
    bool loadFromDisk();
    bool persistToDisk() const;

    std::map<std::string, std::vector<RoomPeerState>> roomPeers;
    mutable std::vector<RoomPeerState> emptyPeers;
    bool mqttOnline = false;
};

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
