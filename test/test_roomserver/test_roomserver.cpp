#include "TestUtil.h"
#include "modules/room/RoomServerConfig.h"
#include "modules/room/RoomMqttBridge.h"
#include "modules/room/RoomServerService.h"
#include "modules/room/RoomStateStore.h"

#if ROOM_SERVER_ENABLED

#include "FSCommon.h"
#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include "mesh/Channels.h"
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/channel.pb.h"

#include <unity.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

using namespace roomserver;

namespace {

ChannelIndex highestChannelIndex = 0;

void resetChannelEnvironment()
{
    channels = Channels();
    std::memset(&channelFile, 0, sizeof(channelFile));
    channelFile.channels_count = 0;
    highestChannelIndex = 0;
    moduleConfig = meshtastic_LocalModuleConfig_init_default;
}

void setMqttDefaults()
{
    moduleConfig.has_mqtt = true;
    moduleConfig.mqtt = meshtastic_ModuleConfig_MQTTConfig_init_default;
    moduleConfig.mqtt.enabled = true;
    std::strncpy(moduleConfig.mqtt.address, "mqtt.example", sizeof(moduleConfig.mqtt.address) - 1);
    std::strncpy(moduleConfig.mqtt.username, "room-user", sizeof(moduleConfig.mqtt.username) - 1);
    std::strncpy(moduleConfig.mqtt.root, "rooms", sizeof(moduleConfig.mqtt.root) - 1);
}

meshtastic_Channel makeChannel(ChannelIndex index, meshtastic_Channel_Role role, const char *name, bool uplink, bool downlink,
                               bool muted, std::initializer_list<uint8_t> pskBytes)
{
    meshtastic_Channel channel = meshtastic_Channel_init_default;
    channel.index = static_cast<int8_t>(index);
    channel.role = role;
    channel.has_settings = true;
    std::strncpy(channel.settings.name, name, sizeof(channel.settings.name) - 1);
    channel.settings.name[sizeof(channel.settings.name) - 1] = '\0';
    channel.settings.uplink_enabled = uplink;
    channel.settings.downlink_enabled = downlink;
    channel.settings.has_module_settings = true;
    channel.settings.module_settings.is_muted = muted;
    channel.settings.psk.size = static_cast<pb_size_t>(pskBytes.size());
    size_t pos = 0;
    for (uint8_t byte : pskBytes) {
        channel.settings.psk.bytes[pos++] = byte;
    }
    return channel;
}

void addChannel(const meshtastic_Channel &channel)
{
    ChannelIndex index = static_cast<ChannelIndex>(channel.index);
    if (static_cast<pb_size_t>(index + 1) > channelFile.channels_count)
        channelFile.channels_count = static_cast<pb_size_t>(index + 1);
    channels.setChannel(channel);
    highestChannelIndex = std::max(highestChannelIndex, index);
}

void finalizeChannels()
{
    if (channelFile.channels_count == 0)
        return;
    channelFile.channels_count = static_cast<pb_size_t>(highestChannelIndex + 1);
    channels.onConfigChanged();
}

#ifdef FSCom
void ensureFilesystemReady()
{
    if (!spiLock)
        initSPI();
    static bool mounted = false;
    if (!mounted) {
        fsInit();
        mounted = true;
    }
    concurrency::LockGuard guard(spiLock);
    FSCom.mkdir("/prefs");
}

void removePersistedState()
{
    ensureFilesystemReady();
    concurrency::LockGuard guard(spiLock);
    if (FSCom.exists(RoomStateStore::kStateFile))
        FSCom.remove(RoomStateStore::kStateFile);
}
#else
void ensureFilesystemReady() {}
void removePersistedState() {}
#endif

} // namespace

void setUp()
{
    resetChannelEnvironment();
    setMqttDefaults();
    RoomServerService::instance().resetForTest();
}

void tearDown() {}

void test_room_server_config_disabled_without_control_channel()
{
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Home", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, false, false, {0x01}));
    finalizeChannels();

    RoomServerConfig config;
    TEST_ASSERT_FALSE(config.isEnabled());
    TEST_ASSERT_EQUAL_UINT32(0, config.roomCount());
}

void test_room_server_config_collects_secondary_definitions()
{
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xFF}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, false, false, {0x01, 0x02, 0x03}));
    addChannel(makeChannel(2, meshtastic_Channel_Role_SECONDARY, "  Room Beta  ", false, true, true, {0x0A, 0x0B}));
    finalizeChannels();

    RoomServerConfig config;
    TEST_ASSERT_TRUE(config.isEnabled());
    TEST_ASSERT_EQUAL_STRING("Room_Communication", config.controlChannel().c_str());
    TEST_ASSERT_EQUAL_UINT32(2, config.roomCount());

    const auto *alpha = config.findRoom("room alpha");
    TEST_ASSERT_NOT_NULL(alpha);
    TEST_ASSERT_EQUAL_UINT8(1, alpha->slotIndex);
    TEST_ASSERT_TRUE(alpha->uplinkEnabled);
    TEST_ASSERT_FALSE(alpha->downlinkEnabled);
    TEST_ASSERT_FALSE(alpha->clientMuted);
    TEST_ASSERT_EQUAL_UINT8(3, alpha->preSharedKey.size());
    TEST_ASSERT_EQUAL_UINT8(0x03, alpha->preSharedKey.back());

    const auto *beta = config.findRoom("ROOM BETA");
    TEST_ASSERT_NOT_NULL(beta);
    TEST_ASSERT_EQUAL_UINT8(2, beta->slotIndex);
    TEST_ASSERT_FALSE(beta->uplinkEnabled);
    TEST_ASSERT_TRUE(beta->downlinkEnabled);
    TEST_ASSERT_TRUE(beta->clientMuted);
    TEST_ASSERT_EQUAL_STRING("rooms", config.mqttConfig().root);
}

void test_room_mqtt_bridge_parses_ack_without_payload()
{
    RoomServerConfig config;
    RoomStateStore store;
    RoomMqttBridge bridge(config, store);

    auto parsed = bridge.parseIncoming("rooms/messages/chat/ack/!00000042", "");
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(parsed->isAck);
    TEST_ASSERT_EQUAL_STRING("chat", parsed->roomName.c_str());
    TEST_ASSERT_EQUAL_STRING("!00000042", parsed->roomServerId.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, parsed->payload.size());

    auto subscribed = bridge.parseIncoming("rooms/messages/chat/subscribed/!00000099", "subscribed");
    TEST_ASSERT_TRUE(subscribed.has_value());
    TEST_ASSERT_FALSE(subscribed->isAck);
    TEST_ASSERT_EQUAL_STRING("chat", subscribed->roomName.c_str());
    TEST_ASSERT_EQUAL_STRING("!00000099", subscribed->roomServerId.c_str());
    TEST_ASSERT_EQUAL_STRING("subscribed", subscribed->payload.c_str());
}

void test_room_state_store_merges_peers()
{
    RoomStateStore store;
    store.clear();

    store.addOrUpdateRoomPeer("Room Alpha", {1u, 10u});
    store.addOrUpdateRoomPeer("Room Alpha", {1u, 20u});
    const auto &alpha = store.getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(1, alpha.size());
    TEST_ASSERT_EQUAL_UINT32(20u, alpha[0].lastSeenSeconds);

    store.addOrUpdateRoomPeer("Room Alpha", {2u, 30u});
    const auto &after = store.getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(2, after.size());
    TEST_ASSERT_EQUAL_UINT32(2u, after[1].nodeId);
    TEST_ASSERT_EQUAL_UINT32(30u, after[1].lastSeenSeconds);
}

void test_room_server_service_tracks_subscriptions()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x01}));
    finalizeChannels();

    service.begin();
    service.handleMqtt("rooms/messages/Room Alpha/subscribed/!00000023", "subscribed");
    const auto &peers = service.state().getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(1, peers.size());
    TEST_ASSERT_EQUAL_UINT32(0x23u, peers[0].nodeId);
}

void test_room_server_service_records_ack_peers()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x02}));
    finalizeChannels();

    service.begin();
    service.handleMqtt("rooms/messages/Room Alpha/ack/!00000042", "");
    const auto &peers = service.state().getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(1, peers.size());
    TEST_ASSERT_EQUAL_UINT32(0x42u, peers[0].nodeId);
}

void test_room_server_service_injects_mqtt_to_local()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x03}));
    finalizeChannels();

    service.begin();
    service.handleMqtt("rooms/messages/Room Alpha", "Room Alpha:hello");
    auto dispatcher = service.loRaDispatcher();
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_EQUAL_UINT32(1, dispatcher->pendingLocalInjects().size());
    TEST_ASSERT_EQUAL_STRING("Room Alpha", dispatcher->pendingLocalInjects()[0].roomName.c_str());
}

void test_room_server_service_fallback_without_mqtt()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x04}));
    finalizeChannels();

    service.begin();
    service.state().setRoomPeers("Room Alpha", {RoomPeerState{1u, 0u}, RoomPeerState{2u, 0u}});
    service.state().setMqttOnline(false);

    service.handleLocalRoomMessage("Room Alpha", "payload");
    auto dispatcher = service.loRaDispatcher();
    TEST_ASSERT_EQUAL_UINT32(2, dispatcher->pendingFallback().size());
}

void test_room_server_resync_requests_when_peers_missing()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x05}));
    finalizeChannels();

    service.begin();
    service.state().setMqttOnline(false);
    service.handleLocalRoomMessage("Room Alpha", "hi"); // trigger no-op but ensure initialized
    auto dispatcher = service.loRaDispatcher();
    TEST_ASSERT_TRUE(dispatcher->pendingResyncRequests().size() >= 1);
}

void test_room_server_handles_lora_dm_and_acks()
{
    RoomServerService &service = RoomServerService::instance();
    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xAA}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, true, false, {0x06}));
    finalizeChannels();

    service.begin();
    service.handleLoRaDm("Room Alpha:hello", 0x77u);
    const auto &peers = service.state().getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(1, peers.size());
    TEST_ASSERT_EQUAL_UINT32(0x77u, peers[0].nodeId);
}

#ifdef FSCom
void test_room_state_store_empty_and_clear()
{
    removePersistedState();
    RoomStateStore store;
    store.clear();
    const auto &missing = store.getRoomPeers("Unknown");
    TEST_ASSERT_EQUAL_UINT32(0, missing.size());

    store.setRoomPeers("Room Alpha", {RoomPeerState{1u, 10u}});
    store.clear();
    const auto &afterClear = store.getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(0, afterClear.size());
}

void test_room_state_store_persists_to_disk()
{
    removePersistedState();
    RoomStateStore store;
    store.clear();
    store.setRoomPeers("Room Alpha", {RoomPeerState{111u, 5u}, RoomPeerState{222u, 7u}});
    store.setRoomPeers("Room Beta", {RoomPeerState{333u, 9u}});
    TEST_ASSERT_TRUE(store.save());

    RoomStateStore reloaded;
    TEST_ASSERT_TRUE(reloaded.load());
    const auto &alpha = reloaded.getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(2, alpha.size());
    TEST_ASSERT_EQUAL_UINT32(111u, alpha[0].nodeId);
    const auto &beta = reloaded.getRoomPeers("Room Beta");
    TEST_ASSERT_EQUAL_UINT32(1, beta.size());
    TEST_ASSERT_EQUAL_UINT32(333u, beta[0].nodeId);
}
#endif

void test_room_server_service_follows_config_state()
{
    RoomServerService &service = RoomServerService::instance();

    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Other", false, false, false, {0x01}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, false, false, {0x02}));
    finalizeChannels();
    service.begin();
    TEST_ASSERT_FALSE(service.isEnabled());

#ifdef FSCom
    removePersistedState();
    {
        RoomStateStore persist;
        persist.clear();
        persist.setRoomPeers("Room Alpha", {RoomPeerState{42u, 11u}});
        TEST_ASSERT_TRUE(persist.save());
    }
#endif

    resetChannelEnvironment();
    setMqttDefaults();
    addChannel(makeChannel(0, meshtastic_Channel_Role_PRIMARY, "Room_Communication", false, false, false, {0xFF}));
    addChannel(makeChannel(1, meshtastic_Channel_Role_SECONDARY, "Room Alpha", true, false, false, {0x01}));
    finalizeChannels();

    service.begin();
    TEST_ASSERT_TRUE(service.isEnabled());
#ifdef FSCom
    const auto &peers = service.state().getRoomPeers("Room Alpha");
    TEST_ASSERT_EQUAL_UINT32(1, peers.size());
    TEST_ASSERT_EQUAL_UINT32(42u, peers[0].nodeId);
#endif
}

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_room_server_config_disabled_without_control_channel);
    RUN_TEST(test_room_server_config_collects_secondary_definitions);
    RUN_TEST(test_room_mqtt_bridge_parses_ack_without_payload);
    RUN_TEST(test_room_state_store_merges_peers);
    RUN_TEST(test_room_server_service_tracks_subscriptions);
    RUN_TEST(test_room_server_service_records_ack_peers);
#ifdef FSCom
    RUN_TEST(test_room_state_store_empty_and_clear);
    RUN_TEST(test_room_state_store_persists_to_disk);
#endif
    RUN_TEST(test_room_server_service_follows_config_state);
    exit(UNITY_END());
}

void loop() {}

#else // ROOM_SERVER_ENABLED

#include <unity.h>

void setup()
{
    delay(10);
    delay(2000);
    UNITY_BEGIN();
    UNITY_END();
}

void loop() {}

#endif
