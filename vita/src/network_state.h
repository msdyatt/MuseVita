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

/* Initialize the Vita network modules before probing Wi-Fi state. */
int muse_vita_network_init(void);
void muse_vita_network_term(void);

MuseVitaNetworkProvider muse_vita_default_network_provider(void);
const char *muse_vita_network_label(MuseVitaNetworkState state);

#endif
