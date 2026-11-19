//
//  BlueToolFixup.cpp
//  BrcmPatchRAM
//
//  Created by Dhinak G on 6/11/21.
//


#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <Headers/kern_devinfo.hpp>
#include <IOKit/IOLocks.h>
#include <sys/ubc.h>
#include "BtdRpaDerive.hpp"


#define MODULE_SHORT "btlfx"


class EXPORT BlueToolFixup : public IOService {
    OSDeclareDefaultStructors(BlueToolFixup)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(BlueToolFixup, IOService)


IOService *BlueToolFixup::probe(IOService *provider, SInt32 *score) {
    return ADDPR(startSuccess) ? IOService::probe(provider, score) : nullptr;
}

bool BlueToolFixup::start(IOService *provider) {
    if (!IOService::start(provider)) {
        SYSLOG("init", "failed to start the parent");
        return false;
    }
    setProperty("VersionInfo", kextVersion);
    registerService();
    
    return true;
}


#pragma mark - Patches

static const uint8_t kSkipUpdateFilePathOriginal[] = "/etc/bluetool/SkipBluetoothAutomaticFirmwareUpdate";
static const uint8_t kSkipUpdateFilePathPatched[]  = "/System/Library/CoreServices/boot.efi";


// Workaround 12.4 Beta 3+ bug where macOS may detect the Bluetooth chipset twice
// Once as internal, and second as an external dongle:
// 'ERROR -- Third Party Dongle has the same address as the internal module'
// Mainly applicable for BCM2046 and BCM2070 chipsets (BT2.1)
static const uint8_t kSkipAddressCheckOriginal[] =
{
    0x48, 0x89, 0xF3,             // mov    rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call   <somewhere>
    0x85, 0xC0,                   // test   eax, eax
    0x74, 0x1D,                   // je
};

static const uint8_t kSkipAddressCheckPatched[] =
{
    0x48, 0x89, 0xF3,             // mov        rbx, rsi
    0xE8, 0x00, 0x00, 0x00, 0x00, // call       <somewhere>
    0x85, 0xC0,                   // test       eax, eax
    0x72, 0x1D,                   // jb short
};

static const uint8_t kSkipAddressCheckMask[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0xFF,
};

static const uint8_t kVendorCheckOriginal[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0x74                     // jnz short
};

static const uint8_t kVendorCheckPatched[] =
{
    0x81, 0xFA,              // cmp edx
    0x5C, 0x0A, 0x00, 0x00,  // Vendor BRCM,
    0xEB                     // jmp short
};

// Workaround for bugged chipset range check that
// doesn't consider 0 "THIRD_PARTY_DONGLE" as valid.
// This patch allows bluetooth to turn back on after the first power cycle.
// See https://github.com/acidanthera/BrcmPatchRAM/pull/18 for more details.
static const uint8_t kBadChipsetCheckOriginal[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0x72                     // jb short
};

static const uint8_t kBadChipsetCheckPatched[] =
{
    0x81, 0xF9,              // cmp ecx
    0xCF, 0x07, 0x00, 0x00,  // int 1999
    0xEB                     // jmp short
};

static const uint8_t kBadChipsetCheckOriginal13_3[] =
{
    0x81, 0xF9,              // cmp ecx
    0x9E, 0x0F, 0x00, 0x00,  // int 3998
    0x77, 0x1A               // ja short
};

static const uint8_t kBadChipsetCheckPatched13_3[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kSkipInternalControllerNVRAMCheck13_3[] =
{
    0x41, 0x80, 0x00, 0x01, // xor     r15b, 1
    0x75, 0x00,             // jnz     short
    0x84, 0xDB,             // test    bl, bl
    0x75, 0x00              // jnz     short
};

static const uint8_t kSkipInternalControllerNVRAMCheckMask13_3[] =
{
    0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00
};

static const uint8_t kSkipInternalControllerNVRAMCheckPatched13_3[] =
{
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90,
    0x90, 0x90
};

static const uint8_t kBadChipsetCheckOriginal15_4[] =
{
    0x81, 0xF9,              // cmp ecx
    0x6F, 0x17, 0x00, 0x00,  // int 5999
    0x77, 0x30               // ja short
};

static const uint8_t kBadChipsetCheckPatched15_4[] =
{
    0x90, 0x90,
    0x90, 0x90, 0x90, 0x90,
    0x90, 0x90
};

// Optional universal workaround for bluetoothd Read Clock failures.
// Many third-party Bluetooth controllers (like Intel) fail Apple's HCI_Read_Clock check.
// When connecting to modern Apple headphones with W1/H1/H2 chips (e.g., AirPods Pro 2),
// this causes bluetoothd to abort A2DP audio bring-up in the Fast-Connect reconnect path.
// This results in the infamous "connected but no sound" issue for these devices.
// See: https://github.com/OpenIntelWireless/IntelBluetoothFirmware/issues/462
// We patch the Fast-Connect A2DP gate to unconditionally force the success path.
// Activated by boot-arg -btlfxa2dpcheck

// Apple FastConnect A2DP reconnect gate.
// Replaces "test r14d, r14d; je" with "xor r14d, r14d; jmp" to
// unconditionally take the audio-publish path.

// macOS 14.x/15.x/16.x (Sonoma/Sequoia/Tahoe)
static const uint8_t kFastConnectA2DPGateOriginal14_0[] =
{
    0x45, 0x85, 0xF6,
    0x74, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGatePatched14_0[] =
{
    0x45, 0x31, 0xF6,
    0xEB, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGateMask14_0[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};

// macOS 12.x/13.x (Monterey/Ventura)
static const uint8_t kFastConnectA2DPGateOriginal12_0[] =
{
    0x85, 0xDB,
    0x74, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGatePatched12_0[] =
{
    0x31, 0xDB,
    0xEB, 0x25,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x18
};

static const uint8_t kFastConnectA2DPGateMask12_0[] =
{
    0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};

// LE event mask (0x10017015c on 26.4.1): bluetoothd writes 0x1f (legacy LE events only) when
// the chipset is 0, so Extended Advertising Report, Advertising Set Terminated, etc. are
// masked and an extended scan never reports anything. Drop the cmove so the full mask
// (same as every Apple chipset) is used.
// test eax, eax; lea rax, [mask1f]; lea rdx, [maskFull]; cmove rdx, rax
static const uint8_t kLeEventMaskOriginal26[] =
{
    0x85, 0xC0,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x0F, 0x44, 0xD0
};

static const uint8_t kLeEventMaskPatched26[] =
{
    0x85, 0xC0,
    0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x1F, 0x40, 0x00
};

// The two lea operands are rip-relative and move with every build, so they are not compared.
static const uint8_t kLeEventMaskMask26[] =
{
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF
};

// Advertising set random address (0x10050ef2e on 26.4.1): for chipset <= 3 bluetoothd turns
// off LE Set Advertising Set Random Address, then enables sets whose own address type is
// random, which the controller rejects with Invalid HCI Command Parameters (0x12).
// Only skip that one call (jmp +7); the chipset == 0 block after it is left as is.
// cmp dword [rax+0x334], 3; ja +0x2a -> jmp +7
static const uint8_t kAdvSetAddrOriginal26[] =
{
    0x83, 0xB8, 0x34, 0x03, 0x00, 0x00, 0x03,
    0x77, 0x2A,
    0x31, 0xFF, 0xE8, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t kAdvSetAddrPatched26[] =
{
    0x83, 0xB8, 0x34, 0x03, 0x00, 0x00, 0x03,
    0xEB, 0x07,
    0x31, 0xFF, 0xE8, 0x00, 0x00, 0x00, 0x00
};

// The call displacement changes between builds and is not compared.
static const uint8_t kAdvSetAddrMask26[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

// LE Extended Create Connection (0x10050eb5c on 26.4.1). With extended scanning on, the
// controller rejects legacy LE Create Connection (0x200d) with Command Disallowed, and after
// enough of those bluetoothd restarts itself ("Exceed max allowed consecutive command
// disallowed events"). Feed 1 into the setter of gEnableExtendedCreateConnectionCommand.
// movzx edi, byte [rbp-0xa2]; call ... -> mov edi, 1; nop2; call ...
static const uint8_t kExtCreateConnOriginal26[] =
{
    0x0F, 0xB6, 0xBD, 0x5E, 0xFF, 0xFF, 0xFF,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x0F, 0xB6, 0xBD, 0x5B, 0xFF, 0xFF, 0xFF,
    0x44, 0x89, 0xFF,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x8A, 0xB5, 0x63, 0xFF, 0xFF, 0xFF
};

static const uint8_t kExtCreateConnPatched26[] =
{
    0xBF, 0x01, 0x00, 0x00, 0x00, 0x66, 0x90,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x0F, 0xB6, 0xBD, 0x5B, 0xFF, 0xFF, 0xFF,
    0x44, 0x89, 0xFF,
    0xE8, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x8A, 0xB5, 0x63, 0xFF, 0xFF, 0xFF
};

// The two call displacements are not compared; the following instructions make the site unique.
static const uint8_t kExtCreateConnMask26[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Advertising set random address before every enable (0x1001f951b on 26.4.1). bluetoothd
// caches each set's random address and only sends LE Set Advertising Set Random Address
// when it changed. The Intel controller forgets the address when the set's parameters are
// rewritten (LE Set Extended Advertising Parameters with a random own address type), so
// the following enable fails with 0x12 over and over until the address rotates (in an
// HCI trace: 64693 failures without a fresh 0x2035, 0 with one). Always send it.
// mov edx, [rdx+rdi+3]; xor edx, [rax+rcx+0xd11]; or edx, r8d; je +0x80 -> nop6
static const uint8_t kAdvSetAddrResendOriginal26[] =
{
    0x8B, 0x54, 0x3A, 0x03,
    0x33, 0x94, 0x08, 0x11, 0x0D, 0x00, 0x00,
    0x44, 0x09, 0xC2,
    0x0F, 0x84, 0x80, 0x00, 0x00, 0x00
};

static const uint8_t kAdvSetAddrResendPatched26[] =
{
    0x8B, 0x54, 0x3A, 0x03,
    0x33, 0x94, 0x08, 0x11, 0x0D, 0x00, 0x00,
    0x44, 0x09, 0xC2,
    0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00
};

// macOS 15 (Sequoia, Darwin 24) sites, for -btlfxadvsets and the Intel default.
//
// Taken from ForbiddenItlwm's BTLEAdvFixup table (GPL-3.0, WarnerATCN / retropineaple), which read them from
// bluetoothd 24H241 (15.8.3) and found the same bytes in 24H23 (15.8) and 24H232 (15.8.2). The same four edits
// as on Tahoe, found there by the same log strings ("BT chip support for extended advertisingsets" / "extended
// scans"): the capability query becomes "mov eax, 1" and the failed check before it stops skipping it, the
// "turn off Set Advertising Set Random Address" call goes away for chipset <= 3, and
// gEnableExtendedCreateConnectionCommand is fed 1. Differences from Tahoe: the queries are vtable slots 0x7c0 and
// 0x848, the chipset field is at 0x2f8 and its "ja" jumps 0x23. The call displacements are not compared.
//
// On this build the LE event mask and extended create connection patterns above already match once (ABI report
// of 24H232), so those two are shared. Not known for Sequoia: the Filter Accept List RPA tables (-btlfxrpa), so
// Instant Hotspot's private-address connect stays off there. Not checked against a bluetoothd of our own: the
// first Sequoia machine to run this reports each applied site in the log ("[advsets] ... (Sequoia)").
static const uint8_t kSeqAdvCapFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x38, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0xC0, 0x07, 0x00, 0x00, 0x41, 0x89, 0xC6
};
static const uint8_t kExtendedCapReplace[] =
{
    0x84, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x90, 0x41, 0x89, 0xC6
};
static const uint8_t kSeqScanCapFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x38, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0x48, 0x08, 0x00, 0x00, 0x41, 0x89, 0xC6
};
// find mask: everything but the call displacement; replace mask: the bytes that change
static const uint8_t kSeqCapFindMask[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t kSeqCapReplaceMask[] =
{
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00
};
// Tahoe initCoreStack enables BT Spec sets when extended advertising is active.
// Keep the +0x9c0 query native. For +0x918 and +0x9a8, bypass the chipset branch
// and return supported, then let bluetoothd apply its DisableExtAdvertising and DisableExtScans preferences.
static const uint8_t kTahoeExtAdvFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x38, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0x18, 0x09, 0x00, 0x00, 0x41, 0x89, 0xC6
};
static const uint8_t kTahoeExtScanFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x38, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0xA8, 0x09, 0x00, 0x00, 0x41, 0x89, 0xC6
};
// cmp dword [rax+0x2f8], 3; ja +0x23; xor edi, edi; call ... -> the xor and the call become nops
static const uint8_t kSeqAddrFind[] =
{
    0x83, 0xB8, 0xF8, 0x02, 0x00, 0x00, 0x03, 0x77, 0x23, 0x31, 0xFF, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kSeqAddrReplace[] =
{
    0x83, 0xB8, 0xF8, 0x02, 0x00, 0x00, 0x03, 0x77, 0x23, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};
static const uint8_t kSeqAddrFindMask[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kSeqAddrReplaceMask[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
// The set-address resend (kAdvSetAddrResend above) with its struct offset and jump displacement left open: the
// shape is the compiler's, the offset is the bluetoothd build's. Not seen in a Sequoia bluetoothd yet.
static const uint8_t kSeqResendFind[] =
{
    0x8B, 0x54, 0x3A, 0x03, 0x33, 0x94, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x09, 0xC2, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kSeqResendReplace[] =
{
    0x8B, 0x54, 0x3A, 0x03, 0x33, 0x94, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x09, 0xC2, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00
};
static const uint8_t kSeqResendFindMask[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kSeqResendReplaceMask[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// macOS 14 Sonoma (14.8.9, 23J631): the same four sites as Sequoia and Tahoe, from the BTLEAdvFixup table of
// Kaixi2004/AirportIntel (GPL-3.0; its provenance is to be settled before release), which read them from Apple's
// installer and matched them by the same log strings. The capability queries are vtable slots 0x700 and 0x770, the
// chipset field is at 0x2c4, the failed check's je is 0x134 and the "ja" jumps 0x23. The call displacements are not compared,
// except in the extended create connection site, which has too little around it to be specific without them: it
// matches 14.8.9 only. Not checked against a bluetoothd of our own: each applied site logs "(Sonoma)".
static const uint8_t kSonAdvCapFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x34, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0x00, 0x07, 0x00, 0x00, 0x41, 0x89, 0xC6
};
static const uint8_t kSonAdvCapReplace[] =
{
    0x84, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x90, 0x41, 0x89, 0xC6
};
static const uint8_t kSonScanCapFind[] =
{
    0x84, 0xC0, 0x0F, 0x84, 0x34, 0x01, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xFF, 0x91, 0x70, 0x07, 0x00, 0x00, 0x41, 0x89, 0xC6
};
static const uint8_t kSonScanCapReplace[] =
{
    0x84, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x08, 0x48, 0x89, 0xC7, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x90, 0x41, 0x89, 0xC6
};
static const uint8_t kSonAddrFind[] =
{
    0x83, 0xB8, 0xC4, 0x02, 0x00, 0x00, 0x03, 0x77, 0x23, 0x31, 0xFF, 0xE8, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kSonAddrReplace[] =
{
    0x83, 0xB8, 0xC4, 0x02, 0x00, 0x00, 0x03, 0x77, 0x23, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};
// movzx edi, byte [rbp-0x92]; call setter (23J631 only) -> mov edi, 1; nop; nop; call setter
static const uint8_t kSonExtConnFind[] =
{
    0x0F, 0xB6, 0xBD, 0x6E, 0xFF, 0xFF, 0xFF, 0xE8, 0xC8, 0xBF, 0xD7, 0xFF
};
static const uint8_t kSonExtConnReplace[] =
{
    0xBF, 0x01, 0x00, 0x00, 0x00, 0x90, 0x90, 0xE8, 0xC8, 0xBF, 0xD7, 0xFF
};
static const uint8_t kSonExtConnReplaceMask[] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Tahoe 25.4 / 25E246 command descriptor: require an unavailable capability
// for Read Stored Link Key, using bluetoothd's existing nonfatal local fallback.
// Activated by boot-arg -btlfxbe200key.
static const uint8_t kBE200StoredKeyOriginal[] = {0x02, 0x00, 0x0D, 0x0C, 0x20, 0x06, 0x07, 0x00};
static const uint8_t kBE200StoredKeyPatched[]  = {0x02, 0x00, 0x0D, 0x0C, 0xFF, 0xFF, 0x07, 0x00};
static bool shouldPatchBE200StoredKey = false;

// Filter Accept List RPA substitution for third-party controllers (-btlfxrpa).
//
// bluetoothd does not program the controller's resolving list on a third-party controller. It
// resolves a peer's Resolvable Private Address (RPA) in software, replaces the address of the
// advertising report with the peer's identity address, and later puts that identity address
// into the controller's Filter Accept List (LE Add Device To Filter Accept List, 0x2011) before
// LE Extended Create Connection (0x2043, Initiator_Filter_Policy 1). The peer advertises with
// its RPA, so the controller never matches it and the connection times out (Instant Hotspot:
// "BLE client connect timeout"; also rapportd/sharingd connections to an iPhone).
//
// The resolver (RpaResolve*) records every (identity, RPA) pair in a 16 slot cache of 24 bytes
// (identity 7, RPA 7 at +8, snapshot of the RPA at the last add 7 at +16), and the add and remove
// functions send the cached RPA to the controller (the helper) while bluetoothd's own list keeps the identity.
// Two functions are the hand-written part: a dead logging block of the resolver (cache writer) and one
// of the add function (lookup helper); both are reached from the patched call sites.
// The cache sits in the zero filled tail of bluetoothd's __DATA segment, behind its last section.
//
// Derived from crazyhulk/BlueToolFixup-FixA2DP-BLE PR 1 (macOS 26.7.2), which carries the bytes of that one build.
// Here they are computed for whatever bluetoothd runs (BtdRpaDerive.hpp): the five pieces are found by signature, and
// the displacements follow from the address of the page (the Mach-O header page, seen first, gives the base) and from
// where the zero filled tail of __DATA starts. The result is byte for byte what the per-build tables held for 25E246
// and 25G229, whose patched code was checked in an emulator. A build whose pieces are not all there, or whose
// bytes are not the shape the patch was written for, is left alone and the log says why.
static bool shouldPatchRpa = false;

static volatile int rpaLayoutState = 0;    // 0 none, 1 being written, 2 published
static btd::Layout rpaLayout;

static bool patchRpaFilterAcceptList(void *page, memory_object_offset_t pageOffset, const char *path) {
    uint32_t magic;
    memcpy(&magic, page, sizeof(magic));
    if (magic == btd::kMachO64 && __atomic_load_n(&rpaLayoutState, __ATOMIC_ACQUIRE) == 0) {
        btd::Layout fresh;
        if (btd::parseHeader(static_cast<const uint8_t *>(page), PAGE_SIZE, pageOffset, &fresh) &&
            __sync_bool_compare_and_swap(&rpaLayoutState, 0, 1)) {
            rpaLayout = fresh;
            __atomic_store_n(&rpaLayoutState, 2, __ATOMIC_RELEASE);
            SYSLOG(MODULE_SHORT, "[rpa] bluetoothd layout: __TEXT %llx, cache at %llx%s", fresh.textVA, fresh.cacheVA,
                   fresh.cacheFits ? "" : " (no room behind __DATA)");
        }
    }
    btd::Layout none;
    const btd::Layout &layout = __atomic_load_n(&rpaLayoutState, __ATOMIC_ACQUIRE) == 2 ? rpaLayout : none;
    btd::Report report;
    switch (btd::derivePage(static_cast<uint8_t *>(page), PAGE_SIZE, pageOffset, layout, &report)) {
    case btd::Outcome::NothingHere:
        break;
    case btd::Outcome::Patched:
        if (report.resolver)
            SYSLOG(MODULE_SHORT, "[rpa] patched the RPA resolver of bluetoothd in %s (hook %llx, cache writer %llx)", path,
                   report.hook, report.save);
        if (report.filterList)
            SYSLOG(MODULE_SHORT, "[rpa] patched Filter Accept List add/remove of bluetoothd in %s (add %llx, remove %llx, helper %llx)",
                   path, report.add, report.remove, report.helper);
        break;
    case btd::Outcome::NoLayout:
        SYSLOG(MODULE_SHORT, "[rpa] RPA code found in %s before its Mach-O header page was seen; not patched", path);
        break;
    case btd::Outcome::NoCacheRoom:
        SYSLOG(MODULE_SHORT, "[rpa] RPA code found in %s, but __DATA has no room for the cache; not patched", path);
        break;
    case btd::Outcome::Incomplete:
        SYSLOG(MODULE_SHORT, "[rpa] only part of the RPA code is on one page of %s; not patched", path);
        break;
    case btd::Outcome::Unexpected:
        SYSLOG(MODULE_SHORT, "[rpa] RPA code in %s is not the shape the patch was written for; not patched", path);
        break;
    }
    return report.resolver || report.filterList;
}

static bool shouldPatchBoardId = false;
static bool shouldPatchAddress = false;
static bool shouldPatchNvramCheck = false;
static bool shouldPatchA2DPCheck = false;
static bool loggedA2DPCheckMiss = false;
static bool shouldPatchSpecAdvSets = false;

static constexpr size_t kBoardIdSize = sizeof("Mac-F60DEB81FF30ACF6");
static constexpr size_t kBoardIdSizeLegacy = sizeof("Mac-F22586C8");

static const char boardIdsWithUSBBluetooth[][kBoardIdSize] = {
    "Mac-F60DEB81FF30ACF6",
    "Mac-9F18E312C5C2BF0B",
    "Mac-937CB26E2E02BB01",
    "Mac-E43C1C25D4880AD6",
    "Mac-06F11FD93F0323C5",
    "Mac-06F11F11946D27C5",
    "Mac-A369DDC4E67F1C45",
    "Mac-FFE5EF870D7BA81A",
    "Mac-DB15BD556843C820",
    "Mac-B809C3757DA9BB8D",
    "Mac-65CE76090165799A",
    "Mac-4B682C642B45593E",
    "Mac-77F17D7DA9285301",
    "Mac-BE088AF8C5EB4FA2"
};

static mach_vm_address_t orig_cs_validate {};

// IntelBTUnified v1.0.1 identified the compressed-block SIGBUS and the page-in workaround.
// Drop patched neighbours so APFS can validate the original 64 KiB block on Tahoe.
static constexpr off_t kApfsCompressionBlockSize = 64 * 1024;
static constexpr size_t kMaxPatchedPages = 32;

struct PatchedExecutable {
    vnode_t vnode;
    uint32_t vid;
    size_t count;
    off_t offsets[kMaxPatchedPages];
};

static PatchedExecutable patchedBlueTool {};
static PatchedExecutable patchedBluetoothd {};
static IOSimpleLock *patchedExecutablesLock = nullptr;
static mach_vm_address_t orig_vnode_pagein {};
static errno_t (*invalidateFilePages)(vnode_t, off_t, off_t, off_t *, int) = nullptr;
static uint32_t (*getVnodeId)(vnode_t) = nullptr;
static bool loggedEviction = false;

static void rememberPatchedPage(PatchedExecutable &file, vnode_t vp, off_t offset) {
    if (!patchedExecutablesLock)
        return;

    const uint32_t vid = getVnodeId(vp);
    IOSimpleLockLock(patchedExecutablesLock);
    if (__atomic_load_n(&file.vnode, __ATOMIC_RELAXED) != vp || file.vid != vid) {
        file.vid = vid;
        file.count = 0;
        __atomic_store_n(&file.vnode, vp, __ATOMIC_RELEASE);
    }
    size_t i = 0;
    while (i < file.count && file.offsets[i] != offset)
        ++i;
    if (i == file.count && file.count < kMaxPatchedPages)
        file.offsets[file.count++] = offset;
    IOSimpleLockUnlock(patchedExecutablesLock);
}

static void invalidatePatchedNeighbours(PatchedExecutable &file, vnode_t vp, off_t start, off_t end) {
    const off_t firstBlock = start / kApfsCompressionBlockSize;
    const off_t lastBlock = (end - 1) / kApfsCompressionBlockSize;
    const uint32_t vid = getVnodeId(vp);
    off_t neighbours[kMaxPatchedPages];
    size_t count = 0;

    IOSimpleLockLock(patchedExecutablesLock);
    if (__atomic_load_n(&file.vnode, __ATOMIC_RELAXED) == vp && file.vid == vid) {
        for (size_t i = 0; i < file.count; ++i) {
            const off_t offset = file.offsets[i];
            const off_t block = offset / kApfsCompressionBlockSize;
            // APFS already owns the requested range for this page-in.
            if (block >= firstBlock && block <= lastBlock && (offset < start || offset >= end))
                neighbours[count++] = offset;
        }
    }
    IOSimpleLockUnlock(patchedExecutablesLock);

    for (size_t i = 0; i < count; ++i) {
        const off_t offset = neighbours[i];
        const errno_t error = invalidateFilePages(vp, offset, offset + PAGE_SIZE, nullptr, UBC_INVALIDATE);
        if (!__atomic_exchange_n(&loggedEviction, true, __ATOMIC_RELAXED))
            SYSLOG(MODULE_SHORT, "[pagein] invalidated patched page at 0x%llx (error %d)",
                   static_cast<unsigned long long>(offset), error);
        DBGLOG(MODULE_SHORT, "[pagein] invalidate 0x%llx (error %d)", static_cast<unsigned long long>(offset), error);
    }
}

static int patched_vnode_pagein(vnode_t vp, upl_t upl, upl_offset_t uplOffset, vm_object_offset_t fileOffset,
                                upl_size_t size, int flags, int *error) {
    if (size) {
        if (__atomic_load_n(&patchedBluetoothd.vnode, __ATOMIC_ACQUIRE) == vp)
            invalidatePatchedNeighbours(patchedBluetoothd, vp, fileOffset, fileOffset + size);
        else if (__atomic_load_n(&patchedBlueTool.vnode, __ATOMIC_ACQUIRE) == vp)
            invalidatePatchedNeighbours(patchedBlueTool, vp, fileOffset, fileOffset + size);
    }
    return FunctionCast(patched_vnode_pagein, orig_vnode_pagein)(vp, upl, uplOffset, fileOffset, size, flags, error);
}

static void routePageinInvalidation(KernelPatcher &patcher) {
    invalidateFilePages = reinterpret_cast<decltype(invalidateFilePages)>(patcher.solveSymbol(KernelPatcher::KernelID, "_ubc_msync"));
    if (!invalidateFilePages)
        patcher.clearError();
    getVnodeId = reinterpret_cast<decltype(getVnodeId)>(patcher.solveSymbol(KernelPatcher::KernelID, "_vnode_vid"));
    if (!getVnodeId)
        patcher.clearError();
    if (!invalidateFilePages || !getVnodeId) {
        SYSLOG(MODULE_SHORT, "[pagein] missing ubc_msync or vnode_vid");
        return;
    }

    patchedExecutablesLock = IOSimpleLockAlloc();
    if (!patchedExecutablesLock) {
        SYSLOG(MODULE_SHORT, "[pagein] could not allocate tracking lock");
        return;
    }
    KernelPatcher::RouteRequest route("_vnode_pagein", patched_vnode_pagein, orig_vnode_pagein);
    if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &route, 1)) {
        patcher.clearError();
        IOSimpleLockFree(patchedExecutablesLock);
        patchedExecutablesLock = nullptr;
        SYSLOG(MODULE_SHORT, "[pagein] failed to route vnode_pagein");
        return;
    }
    SYSLOG(MODULE_SHORT, "[pagein] Tahoe patched-page invalidation enabled");
}

#pragma mark - Kernel patching code

static inline bool searchAndPatch(const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *patch, size_t replaceSize) {
    if (!KernelPatcher::findAndReplace(const_cast<void *>(haystack), haystackSize, needle, findSize, patch, replaceSize))
        return false;
    DBGLOG(MODULE_SHORT, "found string to patch at %s!", path);
    return true;
}

template <size_t findSize, size_t replaceSize, typename T>
static inline bool searchAndPatch(const void *haystack, size_t haystackSize, const char *path, const T (&needle)[findSize], const T (&patch)[replaceSize]) {
    return searchAndPatch(haystack, haystackSize, path, needle, findSize * sizeof(T), patch, replaceSize * sizeof(T));
}

static inline bool searchAndPatchWithMask(const void *haystack, size_t haystackSize, const char *path, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize, const void *patchMask, size_t replaceMaskSize) {
    if (!KernelPatcher::findAndReplaceWithMask(const_cast<void *>(haystack), haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, patchMask, replaceMaskSize))
        return false;
    DBGLOG(MODULE_SHORT, "found string to patch at %s!", path);
    return true;
}

template <size_t findSize, size_t findMaskSize, size_t replaceSize, size_t replaceMaskSize, typename T>
static inline bool searchAndPatchWithMask(const void *haystack, size_t haystackSize, const char *path, const T (&needle)[findSize], const T (&findMask)[findMaskSize], const T (&patch)[replaceSize], const T (&patchMask)[replaceMaskSize]) {
    return searchAndPatchWithMask(haystack, haystackSize, path, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceMaskSize * sizeof(T));
}




static bool searchAndPatchNamedWithMask(const void *haystack, size_t haystackSize, const char *path, const char *name, const void *needle, size_t findSize, const void *findMask, size_t findMaskSize, const void *patch, size_t replaceSize, const void *patchMask, size_t replaceMaskSize) {
    if (!KernelPatcher::findAndReplaceWithMask(const_cast<void *>(haystack), haystackSize, needle, findSize, findMask, findMaskSize, patch, replaceSize, patchMask, replaceMaskSize))
        return false;

    SYSLOG(MODULE_SHORT, "[a2dpcheck] patched %s in %s", name, path);
    return true;
}

template <size_t findSize, size_t findMaskSize, size_t replaceSize, size_t replaceMaskSize, typename T>
static bool searchAndPatchNamedWithMask(const void *haystack, size_t haystackSize, const char *path, const char *name, const T (&needle)[findSize], const T (&findMask)[findMaskSize], const T (&patch)[replaceSize], const T (&patchMask)[replaceMaskSize]) {
    return searchAndPatchNamedWithMask(haystack, haystackSize, path, name, needle, findSize * sizeof(T), findMask, findMaskSize * sizeof(T), patch, replaceSize * sizeof(T), patchMask, replaceMaskSize * sizeof(T));
}


#pragma mark - Patched functions

static void patched_cs_validate_page(vnode_t vp, memory_object_t pager, memory_object_offset_t page_offset, const void *data, int *validated_p, int *tainted_p, int *nx_p) {
    char path[PATH_MAX];
    int pathlen = PATH_MAX;
    FunctionCast(patched_cs_validate_page, orig_cs_validate)(vp, pager, page_offset, data, validated_p, tainted_p, nx_p);
    static constexpr size_t dirLength = sizeof("/usr/sbin/")-1;
    if (vn_getpath(vp, path, &pathlen) == 0 && UNLIKELY(strncmp(path, "/usr/sbin/", dirLength) == 0)) {
        if (strcmp(path + dirLength, "BlueTool") == 0) {
            bool patched = searchAndPatch(data, PAGE_SIZE, path, kSkipUpdateFilePathOriginal, kSkipUpdateFilePathPatched);
            if (shouldPatchBoardId)
                patched |= searchAndPatch(data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
            if (patched)
                rememberPatchedPage(patchedBlueTool, vp, page_offset);
        }
        else if (strcmp(path + dirLength, "bluetoothd") == 0) {
            bool patched = false;
            if (shouldPatchBE200StoredKey && KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE,
                kBE200StoredKeyOriginal, sizeof(kBE200StoredKeyOriginal), kBE200StoredKeyPatched, sizeof(kBE200StoredKeyPatched))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "BE200 stored-key descriptor patched for native unsupported-command fallback");
            }
            patched |= searchAndPatch(data, PAGE_SIZE, path, kVendorCheckOriginal, kVendorCheckPatched);
            patched |= searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal, kBadChipsetCheckPatched);
            if (getKernelVersion() >= KernelVersion::Ventura && getKernelVersion() < KernelVersion::Sequoia)
                patched |= searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal13_3, kBadChipsetCheckPatched13_3);
            if (getKernelVersion() >= KernelVersion::Sequoia)
                patched |= searchAndPatch(data, PAGE_SIZE, path, kBadChipsetCheckOriginal15_4, kBadChipsetCheckPatched15_4);
            if (shouldPatchNvramCheck)
                patched |= searchAndPatchWithMask(data, PAGE_SIZE, path, kSkipInternalControllerNVRAMCheck13_3, sizeof(kSkipInternalControllerNVRAMCheck13_3), kSkipInternalControllerNVRAMCheckMask13_3, sizeof(kSkipInternalControllerNVRAMCheckMask13_3), kSkipInternalControllerNVRAMCheckPatched13_3, sizeof(kSkipInternalControllerNVRAMCheckPatched13_3), nullptr, 0);
            if (shouldPatchBoardId)
                patched |= searchAndPatch(data, PAGE_SIZE, path, boardIdsWithUSBBluetooth[0], kBoardIdSize, BaseDeviceInfo::get().boardIdentifier, kBoardIdSize);
            if (shouldPatchAddress)
                patched |= searchAndPatchWithMask(data, PAGE_SIZE, path, kSkipAddressCheckOriginal, kSkipAddressCheckMask, kSkipAddressCheckPatched, kSkipAddressCheckMask);
            if (shouldPatchA2DPCheck) {
                bool a2dpPatched = false;
                // macOS 14.x/15.x/26.x (Sonoma/Sequoia/Tahoe) pattern
                if (getKernelVersion() >= KernelVersion::Sonoma) {
                    a2dpPatched = searchAndPatchNamedWithMask(data, PAGE_SIZE, path, "fast-connect A2DP BT-clock gate (macOS 14+)", kFastConnectA2DPGateOriginal14_0, kFastConnectA2DPGateMask14_0, kFastConnectA2DPGatePatched14_0, kFastConnectA2DPGateMask14_0);
                }
                // macOS 12.x/13.x (Monterey/Ventura) pattern
                else if (getKernelVersion() >= KernelVersion::Monterey) {
                    a2dpPatched = searchAndPatchNamedWithMask(data, PAGE_SIZE, path, "fast-connect A2DP BT-clock gate (macOS 12/13)", kFastConnectA2DPGateOriginal12_0, kFastConnectA2DPGateMask12_0, kFastConnectA2DPGatePatched12_0, kFastConnectA2DPGateMask12_0);
                }

                patched |= a2dpPatched;
                if (!a2dpPatched && !loggedA2DPCheckMiss) {
                    loggedA2DPCheckMiss = true;
                    SYSLOG(MODULE_SHORT, "[a2dpcheck] enabled for bluetoothd, but no known FastConnect Gate pattern was seen yet");
                }
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kLeEventMaskOriginal26, sizeof(kLeEventMaskOriginal26), kLeEventMaskMask26, sizeof(kLeEventMaskMask26), kLeEventMaskPatched26, sizeof(kLeEventMaskPatched26), kLeEventMaskMask26, sizeof(kLeEventMaskMask26))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched LE event mask in %s", path);
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kAdvSetAddrOriginal26, sizeof(kAdvSetAddrOriginal26), kAdvSetAddrMask26, sizeof(kAdvSetAddrMask26), kAdvSetAddrPatched26, sizeof(kAdvSetAddrPatched26), kAdvSetAddrMask26, sizeof(kAdvSetAddrMask26))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched advertising set random address in %s", path);
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kExtCreateConnOriginal26, sizeof(kExtCreateConnOriginal26), kExtCreateConnMask26, sizeof(kExtCreateConnMask26), kExtCreateConnPatched26, sizeof(kExtCreateConnPatched26), kExtCreateConnMask26, sizeof(kExtCreateConnMask26))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched extended create connection in %s", path);
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplace(const_cast<void *>(data), PAGE_SIZE, kAdvSetAddrResendOriginal26, sizeof(kAdvSetAddrResendOriginal26), kAdvSetAddrResendPatched26, sizeof(kAdvSetAddrResendPatched26))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched advertising set address resend in %s", path);
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kTahoeExtAdvFind, sizeof(kTahoeExtAdvFind), kSeqCapFindMask, sizeof(kSeqCapFindMask), kExtendedCapReplace, sizeof(kExtendedCapReplace), kSeqCapReplaceMask, sizeof(kSeqCapReplaceMask))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched extended advertising sets capability in %s", path);
            }
            if (shouldPatchSpecAdvSets &&
                KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, kTahoeExtScanFind, sizeof(kTahoeExtScanFind), kSeqCapFindMask, sizeof(kSeqCapFindMask), kExtendedCapReplace, sizeof(kExtendedCapReplace), kSeqCapReplaceMask, sizeof(kSeqCapReplaceMask))) {
                patched = true;
                SYSLOG(MODULE_SHORT, "[advsets] patched extended scanning capability in %s", path);
            }
            if (shouldPatchSpecAdvSets && getKernelVersion() == KernelVersion::Sequoia) {
                // The needle of a masked search is the pattern with its wildcards zeroed (Lilu compares data & mask).
                auto seq = [&](const char *name, const uint8_t *find, size_t size, const uint8_t *findMask,
                               const uint8_t *replace, const uint8_t *replaceMask) {
                    uint8_t needle[32];
                    for (size_t i = 0; i < size; i++)
                        needle[i] = find[i] & findMask[i];
                    if (KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, needle, size, findMask, size,
                                                              replace, size, replaceMask, size)) {
                        patched = true;
                        SYSLOG(MODULE_SHORT, "[advsets] patched %s in %s (Sequoia)", name, path);
                    }
                };
                seq("extended advertising sets capability", kSeqAdvCapFind, sizeof(kSeqAdvCapFind), kSeqCapFindMask,
                    kExtendedCapReplace, kSeqCapReplaceMask);
                seq("extended scanning capability", kSeqScanCapFind, sizeof(kSeqScanCapFind), kSeqCapFindMask,
                    kExtendedCapReplace, kSeqCapReplaceMask);
                seq("advertising set random address", kSeqAddrFind, sizeof(kSeqAddrFind), kSeqAddrFindMask,
                    kSeqAddrReplace, kSeqAddrReplaceMask);
                seq("advertising set address resend", kSeqResendFind, sizeof(kSeqResendFind), kSeqResendFindMask,
                    kSeqResendReplace, kSeqResendReplaceMask);
            }
            if (shouldPatchSpecAdvSets && getKernelVersion() == KernelVersion::Sonoma) {
                auto son = [&](const char *name, const uint8_t *find, size_t size, const uint8_t *findMask,
                               const uint8_t *replace, const uint8_t *replaceMask) {
                    uint8_t needle[32];
                    for (size_t i = 0; i < size; i++)
                        needle[i] = find[i] & findMask[i];
                    if (KernelPatcher::findAndReplaceWithMask(const_cast<void *>(data), PAGE_SIZE, needle, size, findMask, size,
                                                              replace, size, replaceMask, size)) {
                        patched = true;
                        SYSLOG(MODULE_SHORT, "[advsets] patched %s in %s (Sonoma)", name, path);
                    }
                };
                son("extended advertising sets capability", kSonAdvCapFind, sizeof(kSonAdvCapFind), kSeqCapFindMask,
                    kSonAdvCapReplace, kSeqCapReplaceMask);
                son("extended scanning capability", kSonScanCapFind, sizeof(kSonScanCapFind), kSeqCapFindMask,
                    kSonScanCapReplace, kSeqCapReplaceMask);
                son("advertising set random address", kSonAddrFind, sizeof(kSonAddrFind), kSeqAddrFindMask,
                    kSonAddrReplace, kSeqAddrReplaceMask);
                // fixed displacement: an exact find, a replace mask that leaves the displacement alone
                static const uint8_t extConnFindMask[sizeof(kSonExtConnFind)] =
                    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
                son("extended create connection", kSonExtConnFind, sizeof(kSonExtConnFind), extConnFindMask,
                    kSonExtConnReplace, kSonExtConnReplaceMask);
            }
            if (shouldPatchRpa)
                patched |= patchRpaFilterAcceptList(const_cast<void *>(data), page_offset, path);
            if (patched)
                rememberPatchedPage(patchedBluetoothd, vp, page_offset);
        }
    }
}


#pragma mark - Patches on start/stop

static void pluginStart() {
    SYSLOG(MODULE_SHORT, "start");
    // There is no point in routing cs_validate_range, because this kext should only be running on Monterey+
    if (getKernelVersion() < KernelVersion::Monterey)
        return;
    lilu.onPatcherLoadForce([](void *user, KernelPatcher &patcher) {
        // The Intel AirDrop patches (advertising sets, RPA substitution, BE200 stored key) are on by default: every
        // one of them only fires on a bluetoothd page whose bytes match, so a build that doesn't fit is left alone.
        // -btlfxnointel turns them all off; the single -btlfx flags still work on their own with it.
        const bool intelAirDrop = !checkKernelArgument("-btlfxnointel");
        shouldPatchBE200StoredKey = getKernelVersion() == KernelVersion::Tahoe &&
            getKernelMinorVersion() == 4 && (intelAirDrop || checkKernelArgument("-btlfxbe200key"));
        if (shouldPatchBE200StoredKey)
            SYSLOG(MODULE_SHORT, "BE200 stored-key compatibility experiment enabled for Darwin25.4");
        if (getKernelVersion() >= KernelVersion::Sonoma) {
            shouldPatchBoardId = checkKernelArgument("-btlfxboardid");
        } else {
            auto boardId = BaseDeviceInfo::get().boardIdentifier;
            auto boardIdSize = strlen(boardId) + 1;
            shouldPatchBoardId = boardIdSize == kBoardIdSize || boardIdSize == kBoardIdSizeLegacy;
            if (shouldPatchBoardId)
                for (size_t i = 0; i < arrsize(boardIdsWithUSBBluetooth); i++)
                    if (strcmp(boardIdsWithUSBBluetooth[i], boardId) == 0) {
                        shouldPatchBoardId = false;
                        break;
                    }
        }
        if ((getKernelVersion() == KernelVersion::Monterey && getKernelMinorVersion() >= 5) || getKernelVersion() > KernelVersion::Monterey)
            // 12.4 Beta 3+, XNU 21.5
            shouldPatchAddress = checkKernelArgument("-btlfxallowanyaddr");
        if (getKernelVersion() <= KernelVersion::Sonoma)
            shouldPatchNvramCheck = checkKernelArgument("-btlfxnvramcheck");
        shouldPatchA2DPCheck = checkKernelArgument("-btlfxa2dpcheck");
        shouldPatchSpecAdvSets = intelAirDrop || checkKernelArgument("-btlfxadvsets");
        if (shouldPatchSpecAdvSets)
            SYSLOG(MODULE_SHORT, "[advsets] extended LE advertising and scanning patches enabled");
        if (shouldPatchA2DPCheck)
            SYSLOG(MODULE_SHORT, "[a2dpcheck] bluetoothd Read Clock workaround enabled (fast-connect A2DP bypass)");
        shouldPatchRpa = intelAirDrop || checkKernelArgument("-btlfxrpa");
        if (shouldPatchRpa)
            SYSLOG(MODULE_SHORT, "[rpa] Filter Accept List RPA substitution for third-party controllers enabled");
        if (getKernelVersion() == KernelVersion::Tahoe)
            routePageinInvalidation(patcher);
        KernelPatcher::RouteRequest csRoute = KernelPatcher::RouteRequest("_cs_validate_page", patched_cs_validate_page, orig_cs_validate);
        if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &csRoute, 1))
            SYSLOG(MODULE_SHORT, "failed to route cs validation pages");
    });
}

// Boot args.
static const char *bootargOff[] {
    "-btlfxoff"
};
static const char *bootargDebug[] {
    "-btlfxdbg"
};
static const char *bootargBeta[] {
    "-btlfxbeta"
};

// Plugin configuration.
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    static_cast<KernelVersion>(27),   // Lilu's enum ends at Tahoe (25); macOS 27 is Darwin 27
    pluginStart
};
