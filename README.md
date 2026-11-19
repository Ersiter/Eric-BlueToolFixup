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
| APFS page-in fix for `bluetoothd` SIGBUS | macOS 26 Tahoe | 🧪 x86_64 Debug build verified. Hardware testing is pending. |

## Install

Download the Bluetooth kext from [Eric-BlueToolFixup Releases](https://github.com/Ersiter/Eric-BlueToolFixup/releases).
The x86_64 build is shared across the macOS versions listed for that release. Choose your Wi-Fi driver separately.
For the project's AWDL test builds, see [Eric-AirportItlwm Releases](https://github.com/Ersiter/Eric-AirportItlwm/releases).

1. Replace `BlueToolFixup.kext` in the EFI you boot. For a paired community test, use both kexts in that test ZIP.
2. Keep Lilu and your Intel Bluetooth firmware configuration. Enable one `BlueToolFixup.kext` entry after Lilu in `Kernel → Add`.
3. Boot from a test EFI and follow the package note for its boot arguments.

For Bluetooth-only use, install `BlueToolFixup.kext` after Lilu with your Intel Bluetooth firmware setup.
BlueToolFixup runs independently of AirportItlwm. AirDrop transfers also need working AWDL over Wi-Fi.

### Boot arguments

On the current source, BlueToolFixup tries the LE advertising, scanning and RPA patches by default when
their signatures match. `-itlawdl` enables AWDL in AirportItlwm. The Bluetooth defaults apply independently.
Follow the package note when using an older build.

| Argument | Effect |
|---|---|
| `-btlfxadvsets` | Re-enable the LE advertising and scanning patches with `-btlfxnointel`. |
| `-btlfxrpa` | Re-enable the private-address filter-list patches with `-btlfxnointel`. |
| `-btlfxnointel` | Turn off the default Intel patches. Individual switches can still enable their own paths. |
| `-btlfxoff` | Turn off BlueToolFixup. |

## Reporting a problem

Start `Collect-EricWirelessDiagnostics.command` before reproducing and select Bluetooth device discovery.
Include your Bluetooth controller, macOS build and what you saw. Send the ZIP privately if it includes a system binary.
For AirDrop, say which device was sending and whether the other device appeared. Report Bluetooth problems in
[Issues](https://github.com/Ersiter/Eric-BlueToolFixup/issues).

## Build

Use Xcode on macOS. Extract [Lilu 1.7.2 Debug](https://github.com/acidanthera/Lilu/releases/tag/1.7.2) for its build headers and libraries. Run this from the repository root.

```sh
mkdir -p build
git clone https://github.com/acidanthera/BrcmPatchRAM.git build/BrcmPatchRAM
git -C build/BrcmPatchRAM checkout --detach 65389020769837e92e9fb939bac34bd9a3e25bb2
git clone https://github.com/acidanthera/MacKernelSDK.git build/BrcmPatchRAM/MacKernelSDK
git -C build/BrcmPatchRAM/MacKernelSDK checkout --detach 3f750085caa17ec3a7880f11c11bf4f48cd6a164
cp -R /path/to/Lilu.kext build/BrcmPatchRAM/
cp src/BlueToolFixup.cpp src/BtdRpaDerive.hpp build/BrcmPatchRAM/BrcmPatchRAM/
cd build/BrcmPatchRAM
xcodebuild -project BrcmPatchRAM.xcodeproj -target BlueToolFixup -configuration Debug \
  ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO SYMROOT="$PWD/products" OBJROOT="$PWD/obj" build
```

The kext is in `products/Debug/BlueToolFixup.kext` in the build tree.

## Lineage

[Acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) → BlueToolFixup by Dhinak G → this project,
with the BLE changes needed by Eric-AirportItlwm on Intel Bluetooth.

## Credits

- [Acidanthera](https://github.com/acidanthera) and Dhinak G for BlueToolFixup, Lilu and MacKernelSDK.
- [crazyhulk](https://github.com/crazyhulk) and [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm) for the BLE patch references.
- [IntelBTUnified](https://github.com/laguillo/IntelBTUnified/releases/tag/v1.0.1) for the compressed-page SIGBUS diagnosis and workaround.
- [OpenIntelWireless](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) and contributors for Intel Bluetooth firmware loading and HCI patches.

## License

The BlueToolFixup base retains [BrcmPatchRAM's GPL-2.0 license](LICENSES/BrcmPatchRAM-GPL-2.0.txt).
Third-party patch references are credited at the relevant source sites.
