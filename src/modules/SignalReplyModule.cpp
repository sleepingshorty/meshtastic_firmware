#include "SignalReplyModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"
#include "NeighborInfoModule.h"
#include <Throttle.h>
#include "Default.h"
#include "RadioLibInterface.h"
#include "Router.h"

SignalReplyModule *signalReplyModule;


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


void SignalReplyModule::sendTextReplySplit(const meshtastic_MeshPacket &request, const std::string &fullMessage) {
    constexpr size_t MAX_MSG_LEN = 200;

    for (size_t offset = 0; offset < fullMessage.length(); offset += MAX_MSG_LEN) {
        std::string chunk = fullMessage.substr(offset, MAX_MSG_LEN);

        auto reply = allocDataPacket();
        reply->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        reply->decoded.payload.size = chunk.length();
        memcpy(reply->decoded.payload.bytes, chunk.c_str(), reply->decoded.payload.size);

        // EXAKT wie Ping
        reply->from = nodeDB->getNodeNum();
        reply->to = (isToUs(&request) && request.from != 0) ? request.from : request.to;

        if (!isBroadcast(request.to)) {
            meshtastic_NodeInfoLite *targetNode = nodeDB->getMeshNode(reply->to);
            if (!isBroadcast(request.to) && targetNode && targetNode->user.public_key.size == 32) {
                reply->pki_encrypted = true;
            }
        }

        reply->channel = request.channel;
        reply->decoded.want_response = request.decoded.want_response;
        reply->decoded.has_bitfield = request.decoded.has_bitfield;
        reply->decoded.bitfield = request.decoded.bitfield;
        reply->want_ack = (request.from != 0) ? request.want_ack : false;

        if (request.priority == meshtastic_MeshPacket_Priority_UNSET) {
            reply->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        } else {
            reply->priority = request.priority;
        }

        reply->id = generatePacketId();

        // DEBUG zur Prüfung
        LOG_INFO("Sending neighbor chunk: '%s' to=%d on channel=%d", chunk.c_str(), reply->to, reply->channel);

        service->handleToRadio(*reply);
    }
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

   
//RX Stats:
 // LOG_DEBUG("Send queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
 if (strcasestr_custom(messageRequest, "status_info") != nullptr) {
    static uint32_t lastStatus = millis(); 
    if (!Throttle::isWithinTimespanMs(lastStatus, FIVE_SECONDS_MS)) {
        lastStatus = millis();

        char messageReply[300];
        if (RadioLibInterface::instance && router) {
            snprintf(messageReply, sizeof(messageReply),
                     "Radio stats:\n"
                     "TX Good: %d\n"
                     "RX Good: %d\n"
                     "RX Bad: %d\n"
                     "TX Relayed: %d\n"
                     "RX Duplicate: %d\n"
                     "TX Relay Canceled: %d",
                     RadioLibInterface::instance->txGood,
                     RadioLibInterface::instance->rxGood,
                     RadioLibInterface::instance->rxBad,
                     RadioLibInterface::instance->txRelay,
                     router->rxDupe,
                     router->txRelayCanceled);
        } else {
            snprintf(messageReply, sizeof(messageReply), "Status info unavailable (no RadioLib or Router).");
        }

        sendTextReplySplit(currentRequest, messageReply);
    }
}



 //0-Hop Neighbors:
 if (strcasestr_custom(messageRequest, "neighbor_info") != nullptr) {
    std::string fullMessage;

    static uint32_t lastNeighBorInfo = millis(); 
        if (!Throttle::isWithinTimespanMs(lastNeighBorInfo, FIVE_SECONDS_MS)) {
            lastNeighBorInfo = millis();
            auto neighborList = neighborInfoModule->getNeighbors();
            for (const auto &n : neighborList) {
                char line[64];
                snprintf(line, sizeof(line), "- 0x%x: SNR %.1f\n", n.node_id, n.snr);
                fullMessage += line;
            }

            if (fullMessage.empty()) {
                fullMessage = "No neighbors found.";
            }

            sendTextReplySplit(currentRequest, fullMessage);
    }
}


    //This condition is meant to reply to message containing request "ping" or
    //range module message sending mesage in "seq"uence - e.g. seq 1, seq 2, seq 3.... etc
    //in such case this module sends back information about sgnal quality as well.
    //If not interested in replies to RangeModule semove "seq" condition

    if ((strcasestr_custom(messageRequest, "ping") != nullptr) &&
    currentRequest.from != 0x0 &&
    currentRequest.from != nodeDB->getNodeNum())
    {
        static uint32_t lastPing = millis(); 
        if (!Throttle::isWithinTimespanMs(lastPing, FIVE_SECONDS_MS)) {
            lastPing = millis();
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

            // Logging
            //LOG_ERROR("SignalReplyModule::handleReceived(): '%s' from %s.", messageRequest, username);

            snprintf(messageReply, sizeof(messageReply), "%s: Pong! %d Hops, RSSI %d dBm, SNR %.1f dB",
                    username, (hopStart - hopLimit), currentRequest.rx_rssi, currentRequest.rx_snr);

            // Versende über die getestete zentrale Methode
            sendTextReplySplit(currentRequest, messageReply);
        }
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
