# MuseVita Mac pairing companion

This is a macOS CoreBluetooth peripheral for the one-time Muse Gadget setup
flow. It is not a browser login, cookie bridge, or permanent runtime bridge.
The Muse mobile app still performs account authorization and sends the device
credentials only after the user approves the community gadget.

## Build

```sh
swift build --package-path mac/MuseVitaPair
swift test --package-path mac/MuseVitaPair
```

## Pair and create the file

Start MuseVita on the Vita, open SETUP, and note its local URL. Then run:

```sh
swift run --package-path mac/MuseVitaPair musevita-pair \
  --vita-url http://VITA-IP:8768/ \
  --output ./musevita-pairing.json
```

The tool reads the Vita's non-secret identity from the local setup server. It
asks for the SDK token from `MUSEGADGET_SDK_TOKEN`, `--sdk-token-file`, or a
hidden prompt. It never prints the token or writes it to logs. The output file
is mode `0600` and contains live credentials, so keep it private.

In the Muse phone app, use **Settings > Devices > Developer mode > Add Device**
and choose the advertised `MuseGadgetXXXXXX` device. Approve the community
gadget. When the phone finishes, the companion validates the returned device
credentials and writes `musevita-pairing.json`.

This implementation is source-compatible with Meta's published Linux Gadget
SDK protocol and is validated against the public v5 framing/crypto vectors.
It has not yet been paired with a physical Muse phone in this checkout. The
Mac must have a working Bluetooth LE controller and the user must supply their
own SDK token.

If the command reports that Bluetooth is powered off or no controller is
available, check System Settings > Bluetooth and System Settings > Privacy &
Security > Bluetooth. `system_profiler SPBluetoothDataType` should show a
controller; an iPhone connected by USB does not provide a Bluetooth controller
to the Mac.
