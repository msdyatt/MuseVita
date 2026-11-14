#ifndef MUSEVITA_NETWORK_STATE_H
#define MUSEVITA_NETWORK_STATE_H

typedef enum MuseVitaNetworkState {
    MUSEVITA_NETWORK_UNKNOWN = 0,
    MUSEVITA_NETWORK_OFFLINE,
    MUSEVITA_NETWORK_CONNECTING,
    MUSEVITA_NETWORK_ONLINE,
    MUSEVITA_NETWORK_ERROR,
} MuseVitaNetworkState;

typedef MuseVitaNetworkState (*MuseVitaNetworkProbe)(void *context);

typedef struct MuseVitaNetworkProvider {
    MuseVitaNetworkProbe probe;
    void *context;
} MuseVitaNetworkProvider;

MuseVitaNetworkProvider muse_vita_default_network_provider(void);
const char *muse_vita_network_label(MuseVitaNetworkState state);

#endif
