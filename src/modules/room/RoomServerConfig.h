#pragma once

#include "configuration.h"

#if ROOM_SERVER_ENABLED

#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/module_config.pb.h"
#include <string>
#include <vector>

namespace roomserver {

struct RoomChannelDefinition {
    ChannelIndex slotIndex = 0;
    std::string roomName;
    std::vector<uint8_t> preSharedKey;
    bool uplinkEnabled = false;
    bool downlinkEnabled = false;
    bool clientMuted = true;
};

class RoomServerConfig
{
  public:
    RoomServerConfig();

    void reload();

    bool isEnabled() const { return enabled; }
    const std::string &controlChannel() const { return controlChannelName; }
    const std::vector<RoomChannelDefinition> &rooms() const { return roomDefinitions; }
    size_t roomCount() const { return roomDefinitions.size(); }

    const RoomChannelDefinition *findRoom(const std::string &roomName) const;

    const meshtastic_ModuleConfig_MQTTConfig &mqttConfig() const { return mqttConfigCopy; }

  private:
    static constexpr const char *kControlChannelDefault = "Room_Communication";

    bool validatePrimaryChannel(const meshtastic_Channel &primary) const;
    static std::string sanitizeName(const char *rawName);

    bool enabled = false;
    std::string controlChannelName = kControlChannelDefault;
    std::vector<RoomChannelDefinition> roomDefinitions;
    meshtastic_ModuleConfig_MQTTConfig mqttConfigCopy = meshtastic_ModuleConfig_MQTTConfig_init_default;
};

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
