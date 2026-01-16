/*
# This file is based on code from https://github.com/VilemR/meshtstic_modules_mod
# Original author: VilemR
# Modifications by: (sleepingshorty)
# License: GNU GPL v3.0 (see LICENSE file)
*/

#pragma once
#include "SinglePortModule.h"


/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class SignalReplyModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    SignalReplyModule() : SinglePortModule("XXXXMod", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  //virtual ~SignalReplyModule() {}

  protected:
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */

    virtual meshtastic_MeshPacket *allocReply() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    void sendTextReply(const meshtastic_MeshPacket &request, const std::string &fullMessage) ;
    void sendTextReplySplit(const meshtastic_MeshPacket &request, const std::string &fullMessage);


};

extern SignalReplyModule *signalReplyModule;
