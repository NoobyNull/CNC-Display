#pragma once

// Flash new firmware, fetched over WiFi from the update URL (default: the
// repo's fw branch on Gitea), into the inactive OTA slot and reboot into it.
// Blocking, draws its own full-screen progress, and only returns on failure
// (the reason lands in the console log). Credentials come from NVS (or the
// built-in defaults), bootstrapped from a wifi.txt in a HAS_SD_CARD build. The radio lives only
// inside this call — off again on every exit path — so the no-WiFi rule for
// the running display stands. Requires the two-slot partition table
// (app0/app1); recovery from a bad image is the ROM bootloader over the CH340.
bool fwupd_net();
