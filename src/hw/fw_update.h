#pragma once

// Flash /update.bin from the SD card into the inactive OTA slot and reboot
// into it. Blocking, draws its own full-screen progress, and only returns on
// failure (the reason lands in the console log). Requires the two-slot
// partition table (app0/app1) — the first flash after that change must still
// go over the wire.
bool fwupd_run();

// Same, but fetched over WiFi from the update URL (default: the repo's fw
// branch on Gitea). Credentials come from NVS, bootstrapped once from a
// wifi.txt on the SD card. The radio lives only inside this call — off again
// on every exit path — so the no-WiFi rule for the running display stands.
bool fwupd_net();
