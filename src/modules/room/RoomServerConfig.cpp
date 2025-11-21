#include "RoomServerConfig.h"

#if ROOM_SERVER_ENABLED

#include "Default.h"
#include "mesh/NodeDB.h"
#include <algorithm>
#include <cctype>

namespace roomserver {

namespace {

bool equalsIgnoreCase(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(lhs[i]) != std::tolower(rhs[i]))
            return false;
    }
    return true;
}

} // namespace

RoomServerConfig::RoomServerConfig()
{
    reload();
}

std::string RoomServerConfig::sanitizeName(const char *rawName)
{
    if (!rawName)
        return {};

    std::string name(rawName);
    auto terminator = std::find(name.begin(), name.end(), '\0');
    name.assign(name.begin(), terminator);

    // Trim surrounding whitespace to reduce surprises from the phone app
    auto notSpaceFront = std::find_if_not(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
    auto notSpaceBack = std::find_if_not(name.rbegin(), name.rend(),
                                         [](char c) { return std::isspace(static_cast<unsigned char>(c)); })
                            .base();
    if (notSpaceFront >= notSpaceBack)
        return {};

    return std::string(notSpaceFront, notSpaceBack);
}

bool RoomServerConfig::validatePrimaryChannel(const meshtastic_Channel &primary) const
{
    if (!primary.has_settings) {
        LOG_WARN("RoomServerConfig: primary channel missing settings");
        return false;
    }

    if (primary.role != meshtastic_Channel_Role_PRIMARY) {
        LOG_WARN("RoomServerConfig: channel %d is not PRIMARY", primary.index);
        return false;
    }

    std::string candidate = sanitizeName(primary.settings.name);
    if (candidate.empty() || !equalsIgnoreCase(candidate, kControlChannelDefault)) {
        LOG_INFO("RoomServerConfig: primary channel '%s' != '%s', disabling",
                 candidate.empty() ? "<unnamed>" : candidate.c_str(), kControlChannelDefault);
        return false;
    }

    return true;
}

void RoomServerConfig::reload()
{
    roomDefinitions.clear();
    controlChannelName = kControlChannelDefault;
    mqttConfigCopy = moduleConfig.mqtt;
    enabled = false;

    const ChannelIndex primaryIndex = channels.getPrimaryIndex();
    meshtastic_Channel &primary = channels.getByIndex(primaryIndex);

    if (!validatePrimaryChannel(primary)) {
        LOG_DEBUG("RoomServerConfig: primary channel criteria not met");
        return;
    }

    controlChannelName = sanitizeName(primary.settings.name);
    if (controlChannelName.empty())
        controlChannelName = kControlChannelDefault;

    const ChannelIndex total = channels.getNumChannels();
    for (ChannelIndex idx = 0; idx < total; ++idx) {
        if (idx == primaryIndex)
            continue;

        meshtastic_Channel &channel = channels.getByIndex(idx);
        if (!channel.has_settings)
            continue;
        if (channel.role == meshtastic_Channel_Role_DISABLED)
            continue;

        std::string roomName = sanitizeName(channel.settings.name);
        if (roomName.empty())
            continue;

        RoomChannelDefinition definition;
        definition.slotIndex = idx;
        definition.roomName = roomName;
        definition.uplinkEnabled = channel.settings.uplink_enabled;
        definition.downlinkEnabled = channel.settings.downlink_enabled;
        definition.clientMuted = !(channel.settings.has_module_settings) || channel.settings.module_settings.is_muted;

        const auto keyLength = static_cast<size_t>(channel.settings.psk.size);
        definition.preSharedKey.assign(channel.settings.psk.bytes, channel.settings.psk.bytes + keyLength);

        roomDefinitions.emplace_back(std::move(definition));
    }

    if (roomDefinitions.empty()) {
        LOG_WARN("RoomServerConfig: no configured secondary channels, Room-Server disabled");
        enabled = false;
        return;
    }

    enabled = true;
    LOG_INFO("RoomServerConfig: detected %u room slot(s), control channel '%s'", static_cast<unsigned>(roomDefinitions.size()),
             controlChannelName.c_str());
}

const RoomChannelDefinition *RoomServerConfig::findRoom(const std::string &roomName) const
{
    auto it =
        std::find_if(roomDefinitions.begin(), roomDefinitions.end(),
                     [&roomName](const RoomChannelDefinition &candidate) { return equalsIgnoreCase(candidate.roomName, roomName); });

    return it == roomDefinitions.end() ? nullptr : &(*it);
}

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
