<p align="center"><img src="Assets/bluetooth-icon.svg" width="160" alt="Eric-BlueToolFixup"></p>

<h1 align="center">Eric-BlueToolFixup</h1>
<p align="center"><b>给 macOS 上的 Intel 蓝牙接通隔空投送和即时热点所需的 BLE 功能</b><br>
Sonoma 14、Sequoia 15、Tahoe 26 · x86_64 · Eric-AirportItlwm 的配套项目</p>

<p align="center"><a href="README.md">English</a></p>

---

这是 [Eric-AirportItlwm](https://github.com/Ersiter/Eric-AirportItlwm) 的蓝牙配套。从 macOS 自带的
`bluetoothd` 接通隔空发现用的 LE 广播和扫描。即时热点还要让控制器的过滤列表能处理 iPhone 的私有地址。

## 功能状态

| 功能 | 系统 | 状态 |
|---|---|---|
| LE 扩展广播与扫描 | macOS 26 Tahoe | 🧪 已核对两版的补丁字节，当前改动待上机。 |
| LE 扩展广播与扫描 | macOS 15 Sequoia、macOS 14 Sonoma | 🧪 已按 15.8 系列和 14.8.9 补齐特征码，待端到端验证。 |
| 即时热点私有地址连接 | macOS 26 Tahoe | 🧪 RPA 地址推导已离线核对，待验证热点加入和重连。 |
| `bluetoothd` 的 APFS 分页 SIGBUS 修复 | macOS 26 Tahoe | 🧪 x86_64 Debug 已构建核验，待实机。 |

## 安装

从 [Eric-BlueToolFixup Releases](https://github.com/Ersiter/Eric-BlueToolFixup/releases) 单独下载蓝牙 kext。
同一份 x86_64 构建供该版本列出的 macOS 使用，Wi-Fi 驱动可以另外选。
项目的 AWDL 测试包见 [Eric-AirportItlwm Releases](https://github.com/Ersiter/Eric-AirportItlwm/releases)。

1. 在实际启动的 EFI 里替换 `BlueToolFixup.kext`。使用社区配套测试包时，按包内说明替换两个 kext。
2. 沿用 Lilu 和 Intel 蓝牙固件配置。在 `Kernel → Add` 里把唯一一条启用的 `BlueToolFixup.kext` 放在 Lilu 后面。
3. 从测试用 EFI 启动，按包内说明设启动参数。

只用蓝牙时，沿用 Intel 蓝牙固件配置，把 `BlueToolFixup.kext` 放在 Lilu 后面即可。
BlueToolFixup 可独立于 AirportItlwm 运行。隔空投送传文件还需要 Wi-Fi 的 AWDL 正常工作。

### 启动参数

当前源码默认在特征码命中时应用 LE 广播、扫描和 RPA 补丁。`-itlawdl` 开启 AirportItlwm 的
AWDL，蓝牙补丁独立按自己的默认设置运行。旧版以随包说明为准。

| 参数 | 作用 |
|---|---|
| `-btlfxadvsets` | 搭配 `-btlfxnointel` 时，单独重新启用 LE 扩展广播和扫描补丁。 |
| `-btlfxrpa` | 搭配 `-btlfxnointel` 时，单独重新启用私有地址过滤列表补丁。 |
| `-btlfxnointel` | 关掉默认的 Intel 补丁，单项参数仍能打开对应功能。 |
| `-btlfxoff` | 关闭 BlueToolFixup。 |

## 反馈问题

复现前运行 `Collect-EricWirelessDiagnostics.command`，选择蓝牙设备发现。写明蓝牙控制器型号、macOS 构建号
及设备是否出现。含系统切片的 ZIP 请私下发送。蓝牙问题可以发到本仓库的 [Issue](https://github.com/Ersiter/Eric-BlueToolFixup/issues)。

## 编译

在 macOS 上使用 Xcode。解压 [Lilu 1.7.2 Debug](https://github.com/acidanthera/Lilu/releases/tag/1.7.2) 取得头文件和库，然后从本仓库根目录执行。

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

kext 在构建目录下的 `products/Debug/BlueToolFixup.kext`。

## 源流

[Acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) → Dhinak G 的 BlueToolFixup
→ 本项目，补充 Eric-AirportItlwm 在 Intel 蓝牙上所需的 BLE 功能。

## 致谢

- [Acidanthera](https://github.com/acidanthera) 与 Dhinak G：BlueToolFixup、Lilu 和 MacKernelSDK。
- [crazyhulk](https://github.com/crazyhulk) 与 [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm)：BLE 补丁参考。
- [IntelBTUnified](https://github.com/laguillo/IntelBTUnified/releases/tag/v1.0.1)：压缩页 SIGBUS 的诊断和处理思路。
- [OpenIntelWireless](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) 与贡献者：Intel 蓝牙固件加载和 HCI 补丁。

## 许可

BlueToolFixup 基础代码沿用 [BrcmPatchRAM 的 GPL-2.0 许可证](LICENSES/BrcmPatchRAM-GPL-2.0.txt)。
第三方补丁参考在相应源码位置署名。
