MuseVita Linux one-click pairing
================================

Use a physical Linux machine, Raspberry Pi, or Ubuntu live USB with a working
Bluetooth LE adapter. WSL and most virtual machines cannot provide the BLE
peripheral role reliably.

1. Put the Vita and Linux machine on the same Wi-Fi network.
2. Launch MuseVita on the Vita and open SETUP.
3. Make the script executable:

   chmod +x musevita-linux-pair.sh

4. Double-click it and choose Run, or run it from a terminal:

   ./musevita-linux-pair.sh

The script uses Meta's published Linux Gadget SDK at the pinned revision,
seeds it with the Vita identity, opens the official Muse phone pairing window,
creates one Desktop/musevita-pairing.json file, and stops the Linux service.

The SDK token is entered in a hidden prompt. Never put it in the script,
command line, QR code, or chat.
