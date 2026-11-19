<p align="center"><img src="Assets/bluetooth-icon.svg" width="160" alt="Eric-BlueToolFixup"></p>

<h1 align="center">Eric-BlueToolFixup</h1>
<p align="center"><b>Intel Bluetooth LE for AirDrop and Instant Hotspot on macOS</b><br>
Sonoma 14, Sequoia 15 and Tahoe 26 · x86_64 · a companion to Eric-AirportItlwm</p>

<p align="center">
<img src="https://img.shields.io/badge/status-in%20development-orange" alt="In development">
<img src="https://img.shields.io/badge/macOS-26%20Tahoe%20%7C%2015%20Sequoia%20%7C%2014%20Sonoma-blue" alt="Target macOS versions">
<img src="https://img.shields.io/badge/arch-x86__64-lightgrey" alt="x86_64">
</p>

<p align="center"><a href="README.zh-CN.md">简体中文</a></p>

---

BlueToolFixup for the Bluetooth LE side of [Eric-AirportItlwm](https://github.com/Ersiter/Eric-AirportItlwm).
It works on macOS's `bluetoothd` to bring up the advertising and scanning that AirDrop discovery uses.
Instant Hotspot also needs the phone's private address to work through the controller's filter list.

## Status

| Feature | System | Status |
|---|---|---|
| Extended LE advertising and scanning | macOS 26 Tahoe | 🧪 Patch bytes checked against two builds. Hardware validation of the current revision is pending. |
| Extended LE advertising and scanning | macOS 15 Sequoia, macOS 14 Sonoma | 🧪 Patterns adapted from the 15.8 series and 14.8.9. End-to-end testing is pending. |
| Private-address connection for Instant Hotspot | macOS 26 Tahoe | 🧪 RPA address derivation checked offline. Hotspot join and reconnect are pending. |
| APFS page-in fix for `bluetoothd` SIGBUS | macOS 26 Tahoe | 🧪 Implemented in the working source. Build and hardware verification are pending. |

## Install

Paired test builds will be listed in the [Eric-AirportItlwm Releases](https://github.com/Ersiter/Eric-AirportItlwm/releases).
Use the Wi-Fi and Bluetooth kexts from the same package for the system it names.

1. Replace `AirportItlwm.kext` and `BlueToolFixup.kext` in the EFI you boot.
2. Keep Lilu and your Intel Bluetooth firmware configuration. Enable one `BlueToolFixup.kext` entry after Lilu in `Kernel → Add`.
3. Boot from a test EFI and follow the package note for its boot arguments.

### Boot arguments

| Argument | Effect |
|---|---|
| `-btlfxadvsets` | Enable the extended LE advertising and scanning patches when their signatures match. |
| `-btlfxrpa` | Enable the private-address filter-list patches when their signatures match. |
| `-btlfxnointel` | Turn off the default Intel patches. Individual switches can still enable their own paths. |
| `-btlfxoff` | Turn off BlueToolFixup. |

## Reporting a problem

Use the collector named in the package. Include your Bluetooth controller, macOS build, kext versions and what you did.
For AirDrop, say which device was sending and whether the other device appeared. Report Bluetooth problems in
[Issues](https://github.com/Ersiter/Eric-BlueToolFixup/issues).

## Lineage

[Acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) → BlueToolFixup by Dhinak G → this project,
with the BLE changes needed by Eric-AirportItlwm on Intel Bluetooth.

## Credits

- [Acidanthera](https://github.com/acidanthera) and Dhinak G for BlueToolFixup, Lilu and MacKernelSDK.
- [crazyhulk](https://github.com/crazyhulk) and [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm) for the BLE patch references.
- [IntelBTUnified](https://github.com/laguillo/IntelBTUnified/releases/tag/v1.0.1) for the compressed-page SIGBUS diagnosis and workaround.
- [OpenIntelWireless](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) and contributors for Intel Bluetooth firmware loading and HCI patches.
