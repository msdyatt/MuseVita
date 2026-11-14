#include "network_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define MUSEVITA_NET_MEMORY_SIZE (1024 * 1024)

static uint8_t net_memory[MUSEVITA_NET_MEMORY_SIZE] __attribute__((aligned(4)));
static bool net_module_loaded;
static bool net_initialized;
static bool netctl_initialized;

int muse_vita_network_init(void) {
    SceNetInitParam net_init_param;
    int result;

    result = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (result < 0) return result;
    net_module_loaded = true;

    net_init_param.memory = net_memory;
    net_init_param.size = sizeof(net_memory);
    net_init_param.flags = 0;
    result = sceNetInit(&net_init_param);
    if (result < 0) {
        muse_vita_network_term();
        return result;
    }
    net_initialized = true;

    result = sceNetCtlInit();
    if (result < 0) {
        muse_vita_network_term();
        return result;
    }
    netctl_initialized = true;
    return 0;
}

void muse_vita_network_term(void) {
    if (netctl_initialized) {
        sceNetCtlTerm();
        netctl_initialized = false;
    }
    if (net_initialized) {
        sceNetTerm();
        net_initialized = false;
    }
    if (net_module_loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
        net_module_loaded = false;
    }
}

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
