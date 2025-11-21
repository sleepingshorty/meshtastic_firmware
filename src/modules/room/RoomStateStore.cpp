#include "RoomStateStore.h"

#if ROOM_SERVER_ENABLED

#include "Default.h"
#include "FSCommon.h"
#include "SafeFile.h"
#include "SPILock.h"
#include <algorithm>
#include <cstring>

namespace roomserver {

namespace {
struct RoomStateHeader {
    uint32_t magic = RoomStateStore::kMagic;
    uint16_t version = RoomStateStore::kStateVersion;
    uint16_t reserved = 0;
    uint32_t recordCount = 0;
};

struct RoomStateEntry {
    char roomName[RoomStateStore::kRoomNameStorage] = {};
    uint32_t nodeId = 0;
    uint32_t lastSeenSeconds = 0;
};

} // namespace

bool RoomStateStore::load()
{
    roomPeers.clear();
    emptyPeers.clear();
    mqttOnline = false;
    return loadFromDisk();
}

bool RoomStateStore::save() const
{
    return persistToDisk();
}

void RoomStateStore::clear()
{
    roomPeers.clear();
    emptyPeers.clear();
    mqttOnline = false;
}

void RoomStateStore::setRoomPeers(const std::string &roomName, std::vector<RoomPeerState> peers)
{
    roomPeers[roomName] = std::move(peers);
}

void RoomStateStore::recordAck(const std::string &roomName, uint32_t nodeId)
{
    addOrUpdateRoomPeer(roomName, {nodeId, 0});
}

void RoomStateStore::addOrUpdateRoomPeer(const std::string &roomName, RoomPeerState peer)
{
    auto &peers = roomPeers[roomName];
    auto existing =
        std::find_if(peers.begin(), peers.end(), [&peer](const RoomPeerState &p) { return p.nodeId == peer.nodeId; });
    if (existing != peers.end()) {
        existing->lastSeenSeconds = peer.lastSeenSeconds;
        return;
    }
    peers.push_back(peer);
}

const std::vector<RoomPeerState> &RoomStateStore::getRoomPeers(const std::string &roomName) const
{
    auto it = roomPeers.find(roomName);
    if (it != roomPeers.end())
        return it->second;

    emptyPeers.clear();
    return emptyPeers;
}

bool RoomStateStore::loadFromDisk()
{
#ifdef FSCom
    concurrency::LockGuard guard(spiLock);

    if (!FSCom.exists(kStateFile)) {
        LOG_INFO("RoomStateStore: no persisted state at %s", kStateFile);
        return false;
    }

    File handle = FSCom.open(kStateFile, FILE_O_READ);
    if (!handle) {
        LOG_ERROR("RoomStateStore: failed opening %s", kStateFile);
        return false;
    }

    RoomStateHeader header;
    if (handle.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
        LOG_ERROR("RoomStateStore: failed reading header");
        handle.close();
        return false;
    }

    if (header.magic != kMagic || header.version != kStateVersion) {
        LOG_WARN("RoomStateStore: incompatible state file, ignoring");
        handle.close();
        return false;
    }

    for (uint32_t idx = 0; idx < header.recordCount; ++idx) {
        RoomStateEntry entry;
        if (handle.read(reinterpret_cast<uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
            LOG_ERROR("RoomStateStore: truncated state file");
            break;
        }

        entry.roomName[kRoomNameStorage - 1] = '\0';
        std::string roomName(entry.roomName);
        RoomPeerState peer{};
        peer.nodeId = entry.nodeId;
        peer.lastSeenSeconds = entry.lastSeenSeconds;
        roomPeers[roomName].push_back(peer);
    }

    handle.close();
    LOG_INFO("RoomStateStore: loaded %u persisted room peer record(s)", header.recordCount);
    return true;
#else
    return false;
#endif
}

bool RoomStateStore::persistToDisk() const
{
#ifdef FSCom
    RoomStateHeader header;
    header.recordCount = 0;
    for (const auto &pair : roomPeers)
        header.recordCount += pair.second.size();

    SafeFile file(kStateFile);
    if (!file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header))) {
        LOG_ERROR("RoomStateStore: failed writing header");
        return false;
    }

    for (const auto &pair : roomPeers) {
        RoomStateEntry entry;
        std::strncpy(entry.roomName, pair.first.c_str(), kRoomNameStorage - 1);
        for (const auto &peer : pair.second) {
            entry.nodeId = peer.nodeId;
            entry.lastSeenSeconds = peer.lastSeenSeconds;
            if (!file.write(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry))) {
                LOG_ERROR("RoomStateStore: failed writing entry for %s", pair.first.c_str());
                return false;
            }
        }
    }

    if (!file.close()) {
        LOG_ERROR("RoomStateStore: unable to finalize %s", kStateFile);
        return false;
    }
    return true;
#else
    return false;
#endif
}

} // namespace roomserver

#endif // ROOM_SERVER_ENABLED
