# MuseVita Windows pairing companion

This is a Windows 10 (1703+) / Windows 11 native BLE GATT-peripheral source
implementation for the one-time official Muse Gadget setup flow. It uses
Windows Runtime `GattServiceProvider`, `GattLocalCharacteristic`, and
`BluetoothLEAdvertisementPublisher`; it is not a browser login, cookie bridge,
or permanent relay. MuseVita connects directly over Wi-Fi after pairing.

The included `Properties/Package.appxmanifest` declares package identity,
full-trust, the `bluetooth` device capability, and the `musevita-pair.exe`
app-execution alias. Deploy it in an MSIX package or a Developer Mode package
registration to make that command available. BLE GATT-server capability remains
adapter-dependent. The normal build below validates the companion source and
protocol tests, but cannot establish that a particular adapter can advertise the
required record.

## Build and test

Install the .NET 9 SDK and the Windows 10 SDK, then run:

```powershell
dotnet test windows/MuseVitaPair.Tests/MuseVitaPair.Tests.csproj --configuration Release
dotnet build windows/MuseVitaPair/MuseVitaPair.csproj --configuration Release
```

## Pair a Vita

Package/deploy the companion with the supplied manifest, start MuseVita's
**SETUP** screen, then run the packaged executable with:

```powershell
musevita-pair --vita-url http://VITA-IP:8768/ --output .\musevita-pairing.json
```

The companion follows the one-time redirect to `/pair`, fetches only
`/pair/identity?s=...`, and rejects redirects off the Vita host or malformed
identities. It obtains the user-owned SDK token from Windows Credential Manager,
a local `--sdk-token-file`, or a hidden prompt. It never accepts a token on the
command line or prints it. `musevita-pairing.json` is written as the exact
`musevita-pairing-v1` one-file bundle and is restricted to the current Windows
owner where the filesystem supports Windows ACLs.

In the Muse app choose **Settings > Devices > Developer mode > Add Device**,
then select `MuseGadgetXXXXXX`. The returned `provision_v2` credential is
validated through a certificate-validated HTTPS `/fetch_vms` request before the
bundle is created. No Muse Web cookie, browser session, password, or invented
OAuth flow is used.

Physical discovery/pairing has not been tested here. A real Windows PC with a
Bluetooth adapter that supports LE peripheral/GATT-server mode is required; an
adapter that supports scanning alone is insufficient.
