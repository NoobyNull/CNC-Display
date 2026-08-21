#pragma once

// Flash /update.bin from the SD card into the inactive OTA slot and reboot
// into it. Blocking, draws its own full-screen progress, and only returns on
// failure (the reason lands in the console log). Requires the two-slot
// partition table (app0/app1) — the first flash after that change must still
// go over the wire.
bool fwupd_run();
