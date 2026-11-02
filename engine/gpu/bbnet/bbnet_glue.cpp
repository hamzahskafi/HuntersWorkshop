// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the online backend's registration. Online play is The Hunter's Dream's (thd/):
// its own sceNet, NetCtl and NP functions answer the game's network imports; the game's HTTP
// (sceHttp) and SSL (sceSsl, served by shadPS4's Ssl2) stay shadPS4's.

#include <cstdint>

#include "core/libraries/libs.h"
#include "core/libraries/network/http.h"
#include "core/libraries/network/ssl2.h"
#include "thd/thd.h"

extern "C" void bbnet_initialize(void) {
    if (BbThd::Enabled()) BbThd::Initialize();
}

// The process is ending (the window closed, or Quit to Desktop): The Hunter's Dream's server is told
// the player left the room (it hears nothing else until the heartbeat stops).
extern "C" void bbnet_exit_begin(int timeout_ms) {
    if (BbThd::Enabled()) BbThd::ExitBegin(timeout_ms);
}
extern "C" void bbnet_exit_wait(void) {
    if (BbThd::Enabled()) BbThd::ExitWait();
}

extern "C" void bbnet_set_image(unsigned char* image, uint64_t size) {
    BbThd::SetImage(image, size);
}

extern "C" void bbnet_register(void) {
    // 0.4.4 beta 28: online play is The Hunter's Dream's, and BB_ONLINE=1 is set only with it
    // (start/online.cpp). Its own sceNet, NetCtl and NP functions answer every network import the
    // game has; the game's HTTP and SSL stay shadPS4's. Offline, nothing here is registered and the
    // runtime's own offline answers serve the game (runtime_services.c).
    if (!BbThd::Enabled()) return;
    Core::Loader::SymbolsResolver resolver;
    BbThd::Register(&resolver);
    Libraries::Http::RegisterLib(&resolver);
    Libraries::Ssl2::RegisterLib(&resolver);
}
