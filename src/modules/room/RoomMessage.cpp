#include "RoomMessage.h"

namespace roomserver {

bool RoomMessageCodec::parse(const std::string &raw, RoomMessage &out)
{
    auto pos = raw.find(':');
    if (pos == std::string::npos || pos == 0 || pos == raw.size() - 1)
        return false;

    out.roomName = raw.substr(0, pos);
    out.payload = raw.substr(pos + 1);
    return true;
}

std::string RoomMessageCodec::compose(const std::string &roomName, const std::string &payload)
{
    return roomName + ":" + payload;
}

} // namespace roomserver
