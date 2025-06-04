/*
# This file is based on code from https://github.com/VilemR/meshtstic_modules_mod
# Original author: VilemR
# Modifications by: (sleepingshorty)
# License: GNU GPL v3.0 (see LICENSE file)
*/

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

int backoff_time_ms = 0;
extern int rtm_repeat_count;

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


bool isAdminNode(const meshtastic_MeshPacket &packet) {
    if (!packet.pki_encrypted || packet.public_key.size != 32) return false;

    for (int i = 0; i < 3; i++) {
        if (config.security.admin_key[i].size == 32 &&
            memcmp(packet.public_key.bytes, config.security.admin_key[i].bytes, 32) == 0) {
            return true;
        }
    }
    return false;
}


void SignalReplyModule::sendTextReplySplit(const meshtastic_MeshPacket &request, const std::string &fullMessage) {
    
    //Random Waiting time to prevent that all nodes respond at the same time

    delay(random(0, 333));  // 500–5000 ms
    
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


    //Simple LongFast Info-Logik:
    // Zusätzliche Schlagwort-Erkennung
    std::string messageLower = messageRequest;
    std::transform(messageLower.begin(), messageLower.end(), messageLower.begin(), ::tolower);

   
//RX Stats:
 // LOG_DEBUG("Send queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
 if (strcasestr_custom(messageRequest, "/status_info") != nullptr) {
    static uint32_t lastStatus = 0; 
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
 if (strcasestr_custom(messageRequest, "/neighbor_info") != nullptr) {
    std::string fullMessage;

    static uint32_t lastNeighBorInfo = 0; 
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
        static uint32_t lastPing = 0; 
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

//Client Logik überschreiben
if (strcasestr_custom(messageRequest, "/set_priority") != nullptr) {
 if (isAdminNode(currentRequest)){
    static uint32_t lastSet = 0;
        if (!Throttle::isWithinTimespanMs(lastSet, FIVE_SECONDS_MS)) {
            lastSet = millis();

            int value = atoi(messageRequest + strlen("/set_priority"));
            if (value >= 3 && value <= 8) {
                RadioLibInterface::instance->setManualPriority(value);
                char reply[64];
                snprintf(reply, sizeof(reply), "Priority set to %d.", value);
                sendTextReplySplit(currentRequest, reply);
            } else {
                if (value ==-1) 
                    {
                    sendTextReplySplit(currentRequest, "Normal client logic active");
                    }
                else{
                    sendTextReplySplit(currentRequest, "Only values between 3 (high priority) and 8 (low priority)");
                }
            }
        }
    }
    else{
        sendTextReplySplit(currentRequest, "This is a admin-command.");
    }
    
}

if (strcasestr_custom(messageRequest, "/get_priority") != nullptr) {
    static uint32_t lastGet = 0;
    if (!Throttle::isWithinTimespanMs(lastGet, FIVE_SECONDS_MS)) {
        lastGet = millis();
        int32_t manualPriority = RadioLibInterface::instance->getManualPriority();
        char reply[64];
        if (manualPriority >= 0) {
            snprintf(reply, sizeof(reply), "Current priority is %d", manualPriority);
        } else {
            snprintf(reply, sizeof(reply), "Normal client logic is active");
        }

        sendTextReplySplit(currentRequest, reply);
    }
}


//Repeat-to-Mute
if (strcasestr_custom(messageRequest, "/set_rtm_count") != nullptr) {
    if (isAdminNode(currentRequest)){
        static uint32_t lastSet = 0;
        if (!Throttle::isWithinTimespanMs(lastSet, FIVE_SECONDS_MS)) {
            lastSet = millis();

            int value = atoi(messageRequest + strlen("/set_rtm_count"));
            if (router){
                if (value > 0 && value < 1000) {
                    router->setRepeatThreshold(static_cast<uint8_t>(value));
                    char reply[100];
                    snprintf(reply, sizeof(reply), "Duplicate threshold is set to %d.", value);
                    sendTextReplySplit(currentRequest, reply);
                } else {
                    sendTextReplySplit(currentRequest, "Invalid value for /rtm_count.");
                }
            }
            else{
                sendTextReplySplit(currentRequest, "Router-instance not found. Programming error");
            }
        }
    }    
    else{
        sendTextReplySplit(currentRequest, "This is a admin-command.");
    }
}

if (strcasestr_custom(messageRequest, "/get_rtm_count") != nullptr) {
    char reply[100];
    snprintf(reply, sizeof(reply), "Aktueller RTM-Zähler: %d", static_cast<int>(router->getRepeatThreshold()));
    sendTextReplySplit(currentRequest, reply);
}


if (strcasestr_custom(messageRequest, "/enable_tx") != nullptr) {
    if (isAdminNode(currentRequest)){
        static uint32_t lastSet = 0;
            if (!Throttle::isWithinTimespanMs(lastSet, FIVE_SECONDS_MS)) {
                lastSet = millis();

                const char* arg = messageRequest + strlen("/enable_tx");
                
                config.lora.tx_enabled = true;

                // Änderungen speichern (config reload erzwingen)
                service->reloadConfig(SEGMENT_CONFIG);

                char reply[64];
                snprintf(reply, sizeof(reply), "TX is enabled");
                sendTextReplySplit(currentRequest, reply);
            }
        }    
        else{
            sendTextReplySplit(currentRequest, "This is a admin-command.");
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

