#include "network_state.h"

#include <stddef.h>

#include <psp2/net/netctl.h>

static MuseVitaNetworkState vita_network_probe(void *context) {
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    (void)context;
    if (sceNetCtlInetGetState(&state) < 0) return MUSEVITA_NETWORK_UNKNOWN;
    switch (state) {
        case SCE_NETCTL_STATE_CONNECTED: return MUSEVITA_NETWORK_ONLINE;
        case SCE_NETCTL_STATE_CONNECTING:
        case SCE_NETCTL_STATE_FINALIZING: return MUSEVITA_NETWORK_CONNECTING;
        case SCE_NETCTL_STATE_DISCONNECTED: return MUSEVITA_NETWORK_OFFLINE;
        default: return MUSEVITA_NETWORK_UNKNOWN;
    }
}

MuseVitaNetworkProvider muse_vita_default_network_provider(void) {
    MuseVitaNetworkProvider provider = {vita_network_probe, NULL};
    return provider;
}

const char *muse_vita_network_label(MuseVitaNetworkState state) {
    switch (state) {
        case MUSEVITA_NETWORK_ONLINE: return "Wi-Fi online";
        case MUSEVITA_NETWORK_CONNECTING: return "Wi-Fi connecting";
        case MUSEVITA_NETWORK_OFFLINE: return "Wi-Fi offline";
        case MUSEVITA_NETWORK_ERROR: return "Network error";
        default: return "Network status unknown";
    }
}
