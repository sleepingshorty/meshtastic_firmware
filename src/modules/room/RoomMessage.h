#pragma once

#include <string>

namespace roomserver {

struct RoomMessage {
    std::string roomName;
    std::string payload;
};

/** Helper to parse and compose `<room_name>:<payload>` without global IDs. */
class RoomMessageCodec
{
  public:
    static bool parse(const std::string &raw, RoomMessage &out);
    static std::string compose(const std::string &roomName, const std::string &payload);
};

} // namespace roomserver
