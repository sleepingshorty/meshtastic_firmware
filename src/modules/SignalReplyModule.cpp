#include "SignalReplyModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "NeighborInfoModule.h"

SignalReplyModule *signalReplyModule;


void sendTextReply(const meshtastic_MeshPacket &request, const std::string &fullMessage) {
    constexpr size_t MAX_MSG_LEN = 200;

    // Split-Nachrichten nach max. 200 Zeichen
    for (size_t offset = 0; offset < fullMessage.length(); offset += MAX_MSG_LEN) {
        std::string chunk = fullMessage.substr(offset, MAX_MSG_LEN);

        auto reply = allocDataPacket();
        reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        reply->decoded.payload.size = chunk.length();
        memcpy(reply->decoded.payload.bytes, chunk.c_str(), reply->decoded.payload.size);

        reply->from = nodeDB->getNodeNum();
        reply->to = (isToUs(&request) && request.from != 0) ? request.from : request.to;
        reply->channel = request.channel;

        // PKI nur bei DMs
        if (!isBroadcast(request.to)) {
            meshtastic_NodeInfoLite *targetNode = nodeDB->getMeshNode(reply->to);
            if (targetNode && targetNode->user.public_key.size == 32) {
                reply->pki_encrypted = true;
            }
        }

        // Metadaten übernehmen
        reply->decoded.want_response = request.decoded.want_response;
        reply->decoded.has_bitfield = request.decoded.has_bitfield;
        reply->decoded.bitfield = request.decoded.bitfield;
        reply->want_ack = (request.from != 0) ? request.want_ack : false;
        reply->id = generatePacketId();
        if (request.priority == meshtastic_MeshPacket_Priority_UNSET) {
            reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        }

        service->handleToRadio(*reply);
    }
}


// Custom implementation of strcasestr by "liquidraver"
const char* strcasestr_custom(const char* haystack, const char* needle) {
    if (!haystack || !needle) return nullptr;
    size_t needle_len = strlen(needle);
    if (!needle_len) return haystack;
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return nullptr;
}

ProcessMessage SignalReplyModule::handleReceived(const meshtastic_MeshPacket &currentRequest)
{
    auto &p = currentRequest.decoded;
    char messageRequest[250];
    for (size_t i = 0; i < p.payload.size; ++i)
    {
        messageRequest[i] = static_cast<char>(p.payload.bytes[i]);
    }
    messageRequest[p.payload.size] = '\0';

    //0-Hop Neighbors:
    if (strcasestr_custom(messageRequest, "neighbor") != nullptr) {
        constexpr size_t MAX_MSG_SIZE = 200;
        char message[512] = "";
        size_t msgLen = 0;
    

        auto neighborList = neighborInfoModule->getNeighbors();
        
        for (size_t i = 0; i < neighborList.size(); ++i) {
            const auto &n = neighborList[i];

       
            char line[64];
            snprintf(line, sizeof(line), "- 0x%x: SNR %.1f\n", n.node_id, n.snr);
    
            if (msgLen + strlen(line) >= MAX_MSG_SIZE) {
                // Sende Nachricht ab
                auto reply = allocDataPacket();
                reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                reply->decoded.payload.size = msgLen;
                reply->from = nodeDB->getNodeNum();
                reply->to = currentRequest.from;
                reply->channel = currentRequest.channel;
                memcpy(reply->decoded.payload.bytes, message, msgLen);
                reply->pki_encrypted = !isBroadcast(currentRequest.to);
                reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
                reply->id = generatePacketId();
                service->handleToRadio(*reply);
    
                // Nachricht zurücksetzen
                msgLen = 0;
                message[0] = '\0';
            }
    
            strncat(message, line, sizeof(message) - strlen(message) - 1);
            msgLen = strlen(message);
        }
    
        // Letzte Nachricht abschicken (Rest)
        if (msgLen > 0) {
            auto reply = allocDataPacket();
            reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            reply->decoded.payload.size = msgLen;
            reply->from = nodeDB->getNodeNum();
            reply->to = currentRequest.from;
            reply->channel = currentRequest.channel;
            memcpy(reply->decoded.payload.bytes, message, msgLen);
            reply->pki_encrypted = !isBroadcast(currentRequest.to);
            reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
            reply->id = generatePacketId();
            service->handleToRadio(*reply);
        }
    }
    




    //This condition is meant to reply to message containing request "ping" or
    //range module message sending mesage in "seq"uence - e.g. seq 1, seq 2, seq 3.... etc
    //in such case this module sends back information about sgnal quality as well.
    //If not interested in replies to RangeModule semove "seq" condition

    if ( ( (strcasestr_custom(messageRequest, "ping")) != nullptr) &&   //fix 2025-03-06 (liquidraver & Brabrouk)
         currentRequest.from != 0x0 &&  //fix 2025-05-08
         currentRequest.from != nodeDB->getNodeNum())
    {
        int hopLimit = currentRequest.hop_limit;
        int hopStart = currentRequest.hop_start;

        char idSender[10];
        char idReceipient[10];
        snprintf(idSender, sizeof(idSender), "%d", currentRequest.from);
        snprintf(idReceipient, sizeof(idReceipient), "%d", nodeDB->getNodeNum());

        char messageReply[250];
        meshtastic_NodeInfoLite *nodeSender = nodeDB->getMeshNode(currentRequest.from);
        const char *username = nodeSender->has_user ? nodeSender->user.short_name : idSender;
        meshtastic_NodeInfoLite *nodeReceiver = nodeDB->getMeshNode(nodeDB->getNodeNum());
        const char *usernameja = nodeReceiver->has_user ? nodeReceiver->user.short_name : idReceipient;

        LOG_ERROR("SignalReplyModule::handleReceived(): '%s' from %s.", messageRequest, username);

        snprintf(messageReply, sizeof(messageReply), "%s: Pong! %d Hops, RSSI %d dBm, SNR %.1f dB", username, (hopStart-hopLimit),currentRequest.rx_rssi, currentRequest.rx_snr);

        auto reply = allocDataPacket();
        reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        reply->decoded.payload.size = strlen(messageReply);
        //reply->from = getFrom(&currentRequest);
        reply->from = nodeDB->getNodeNum();
        reply->to = (isToUs(&currentRequest) && currentRequest.from != 0) ? currentRequest.from : currentRequest.to;
        if (!isBroadcast(currentRequest.to)) {
            meshtastic_NodeInfoLite *targetNode = nodeDB->getMeshNode(reply->to);
            if (!isBroadcast(currentRequest.to) && targetNode && targetNode->user.public_key.size == 32) {
                reply->pki_encrypted = true;
            }
        }
        reply->channel = currentRequest.channel;
        reply->decoded.want_response = currentRequest.decoded.want_response;
        reply->decoded.has_bitfield = currentRequest.decoded.has_bitfield;
        reply->decoded.bitfield = currentRequest.decoded.bitfield;
        reply->want_ack = (currentRequest.from != 0) ? currentRequest.want_ack : false;
        
        LOG_INFO("Replying on channel: %d, from: %d, to: %d", reply->channel, reply->from, reply->to);

        if (currentRequest.priority == meshtastic_MeshPacket_Priority_UNSET)
        {
            reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        }
        reply->id = generatePacketId();
        memcpy(reply->decoded.payload.bytes, messageReply, reply->decoded.payload.size);
        service->handleToRadio(*reply);
    }
    notifyObservers(&currentRequest);
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *SignalReplyModule::allocReply()
{
    assert(currentRequest); // should always be !NULL
#ifdef DEBUG_PORT
    auto req = *currentRequest;
    auto &p = req.decoded;
    // The incoming message is in p.payload
    LOG_INFO("Received message from=0x%0x, id=%d, msg=%.*s", req.from, req.id, p.payload.size, p.payload.bytes);
#endif
    screen->print("Send reply\n");
    const char *replyStr = "Message Received";
    auto reply = allocDataPacket();                 // Allocate a packet for sending
    reply->decoded.payload.size = strlen(replyStr); // You must specify how many bytes are in the reply
    reply->which_payload_variant = meshtastic_MeshPacket_decoded_tag;

    memcpy(reply->decoded.payload.bytes, replyStr, reply->decoded.payload.size);
    return reply;
}

bool SignalReplyModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
