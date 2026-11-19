#ifndef BtdRpaDerive_hpp
#define BtdRpaDerive_hpp

// Derives the -btlfxrpa patch bytes of any bluetoothd x86_64 build from the build itself, one 4 KB page at a time (that
// is all cs_validate_page shows). The patch has five pieces that differ per build: the call between the resolver hook
// and its cache writer, the cache address in the writer and in the lookup helper, and the jump out of the helper. Each
// is a displacement computed from the virtual address of the piece, so a build needs no table of its own: the pieces
// are found by signature (the same ones for every build seen so far) and the displacements follow from where the page
// sits and where the zero-filled tail of __DATA starts, which the Mach-O header page gives.
//
// Header-only and free of kernel types, so the host test (BtdRpaTest.cpp) runs the same code on whole bluetoothd files.

#include <stdint.h>
#include <string.h>

namespace btd {

static constexpr uint32_t kMachO64 = 0xfeedfacf;
static constexpr uint32_t kCpuX86_64 = 0x01000007;
static constexpr uint32_t kExecute = 2;
static constexpr uint32_t kSegment64 = 0x19;
static constexpr size_t kCacheSlots = 16;
static constexpr size_t kCacheSlotSize = 24;

// What the header page tells about the x86_64 slice. headerOffset is the offset cs_validate_page reports for the
// header page, so a later page's address in the slice is textVA + (its offset - headerOffset).
struct Layout {
    bool valid = false;
    uint64_t headerOffset = 0;
    uint64_t textVA = 0;
    uint64_t textSize = 0;
    uint64_t cacheVA = 0;
    bool cacheFits = false;
};

inline bool parseHeader(const uint8_t *page, size_t pageSize, uint64_t pageOffset, Layout *out) {
    if (pageSize < 32)
        return false;
    uint32_t magic, cpu, type, ncmds, sizeofcmds;
    memcpy(&magic, page, 4);
    memcpy(&cpu, page + 4, 4);
    memcpy(&type, page + 12, 4);
    memcpy(&ncmds, page + 16, 4);
    memcpy(&sizeofcmds, page + 20, 4);
    if (magic != kMachO64 || cpu != kCpuX86_64 || type != kExecute)
        return false;

    bool haveText = false, haveData = false;
    uint64_t textVA = 0, textSize = 0, sectionEnd = 0, dataVA = 0, dataSize = 0;
    size_t o = 32;
    for (uint32_t i = 0; i < ncmds; i++) {
        if (o + 8 > pageSize)
            break;
        uint32_t cmd, len;
        memcpy(&cmd, page + o, 4);
        memcpy(&len, page + o + 4, 4);
        if (len < 8 || o + len > pageSize)
            break;
        if (cmd == kSegment64 && len >= 72) {
            const uint8_t *seg = page + o;
            uint64_t vmaddr, vmsize, fileoff, filesize;
            uint32_t nsects;
            memcpy(&vmaddr, seg + 24, 8);
            memcpy(&vmsize, seg + 32, 8);
            memcpy(&fileoff, seg + 40, 8);
            memcpy(&filesize, seg + 48, 8);
            memcpy(&nsects, seg + 64, 4);
            if (memcmp(seg + 8, "__TEXT\0", 7) == 0 && fileoff == 0) {
                textVA = vmaddr;
                textSize = filesize;
                haveText = true;
            } else if (memcmp(seg + 8, "__DATA\0", 7) == 0) {
                dataVA = vmaddr;
                dataSize = vmsize;
                haveData = true;
                if (72 + (size_t)nsects * 80 > len)
                    return false;
                for (uint32_t s = 0; s < nsects; s++) {
                    uint64_t addr, size;
                    memcpy(&addr, seg + 72 + s * 80 + 32, 8);
                    memcpy(&size, seg + 72 + s * 80 + 40, 8);
                    if (addr + size > sectionEnd)
                        sectionEnd = addr + size;
                }
            }
        }
        o += len;
    }
    if (!haveText || !haveData)
        return false;

    out->headerOffset = pageOffset;
    out->textVA = textVA;
    out->textSize = textSize;
    out->cacheVA = (sectionEnd + 15) & ~(uint64_t)15;
    out->cacheFits = out->cacheVA >= dataVA && out->cacheVA + kCacheSlots * kCacheSlotSize <= dataVA + dataSize;
    out->valid = true;
    return true;
}

// The pieces. "??" is any byte. Hook, add, remove and the helper's first 28 bytes are the same machine code in every
// build seen; the displacements inside them are what the wildcards stand for.
static const char *const kHook = "42 8A 44 20 FF 41 88 46 06 66 41 89 56 04 41 89 0E C6 03 01";
static const char *const kSave =
    "BF 01 00 00 00 BE 43 00 00 00 E8 ?? ?? ?? ?? 84 C0 74 ?? 48 8D 3D ?? ?? ?? ?? 31 DB 31 C0 E8 ?? ?? ?? ?? BF 43 00 "
    "00 00 E8 ?? ?? ?? ?? 49 89 C6 48 89 C7 BE";
static const char *const kAdd = "49 89 C6 41 0F B6 17 49 8D 4F 01 48 8D 35 ?? ?? ?? ??";
static const char *const kRemove = "48 89 C3 41 0F B6 17 49 8D 4F 01 48 8D 35 ?? ?? ?? ??";
static const char *const kHelper =
    "BF 02 00 00 00 BE 42 00 00 00 E8 ?? ?? ?? ?? BB 68 00 00 00 84 C0 0F 84 ?? ?? ?? ?? 0F B6 35 ?? ?? ?? ?? 0F B7 15 "
    "?? ?? ?? ?? 48 8D 3D ?? ?? ?? ?? 31 C0 E8 ?? ?? ?? ?? BF 42 00 00 00 E8 ?? ?? ?? ?? 49 89 C6 48 89 C7 BE 10 00 00 "
    "00 E8 ?? ?? ?? ?? 84 C0 74 ?? 4C 89 F7 E8 ??";

static constexpr size_t kHookSize = 20;
static constexpr size_t kSaveSize = 52;
static constexpr size_t kAddSize = 18;
static constexpr size_t kRemoveSize = 18;
static constexpr size_t kHelperSize = 91;
static constexpr size_t kMaxSig = 96;

struct Sig {
    uint8_t byte[kMaxSig];
    uint8_t mask[kMaxSig];
    size_t size;
};

inline int hexValue(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

inline Sig parseSig(const char *spec) {
    Sig s;
    s.size = 0;
    for (const char *c = spec; *c;) {
        if (*c == ' ') {
            c++;
            continue;
        }
        if (c[0] == '?') {
            s.byte[s.size] = 0;
            s.mask[s.size++] = 0;
        } else {
            s.byte[s.size] = (uint8_t)(hexValue(c[0]) << 4 | hexValue(c[1]));
            s.mask[s.size++] = 0xff;
        }
        c += 2;
    }
    return s;
}

// How many times the signature matches inside the page, and where the first one is. A match must lie wholly in the page.
inline size_t countSig(const uint8_t *page, size_t pageSize, const Sig &sig, size_t *first) {
    size_t n = 0;
    for (size_t i = 0; i + sig.size <= pageSize; i++) {
        size_t k = 0;
        while (k < sig.size && (page[i + k] & sig.mask[k]) == sig.byte[k])
            k++;
        if (k == sig.size) {
            if (!n)
                *first = i;
            n++;
        }
    }
    return n;
}

inline void put32(uint8_t *p, int64_t value) {
    int32_t v = (int32_t)value;
    memcpy(p, &v, 4);
}

enum class Outcome {
    NothingHere,
    Patched,
    NoLayout,        // a piece is on the page but the header page was not seen first
    NoCacheRoom,     // __DATA has no zero-filled room behind its last section
    Incomplete,      // some of the pieces of a group are not on this page (or are not unique)
    Unexpected,      // a piece matched but its bytes are not the shape the patch was written for
};

struct Report {
    bool resolver = false;       // hook and save were patched
    bool filterList = false;     // add, remove and helper were patched
    uint64_t hook = 0, save = 0, add = 0, remove = 0, helper = 0;
};

// Patches one page in place. Each group (hook with save, add with remove and helper) is patched whole or not at all.
// pageOffset is the offset cs_validate_page reported for the page; layout comes from parseHeader.
inline Outcome derivePage(uint8_t *page, size_t pageSize, uint64_t pageOffset, const Layout &layout, Report *report) {
    // parsed on every call: a function-local static would need the compiler's guard, which a kext does not have
    const Sig hook = parseSig(kHook), save = parseSig(kSave), add = parseSig(kAdd), remove = parseSig(kRemove),
              helper = parseSig(kHelper);
    size_t hookAt = 0, saveAt = 0, addAt = 0, removeAt = 0, helperAt = 0;
    size_t hooks = countSig(page, pageSize, hook, &hookAt);
    size_t helpers = countSig(page, pageSize, helper, &helperAt);
    if (!hooks && !helpers)
        return Outcome::NothingHere;

    Outcome result = Outcome::NothingHere;
    uint64_t pageVA = 0;
    bool placed = false;
    if (layout.valid && pageOffset >= layout.headerOffset && pageOffset - layout.headerOffset + pageSize <= layout.textSize) {
        pageVA = layout.textVA + (pageOffset - layout.headerOffset);
        placed = true;
    }

    if (hooks) {
        size_t saves = countSig(page, pageSize, save, &saveAt);
        if (hooks != 1 || saves != 1)
            result = Outcome::Incomplete;
        else if (!placed)
            result = Outcome::NoLayout;
        else if (!layout.cacheFits)
            result = Outcome::NoCacheRoom;
        else if (saveAt + 0x7a > pageSize || memcmp(page + saveAt + 0x78, "\x31\xdb", 2) != 0 ||
                 memcmp(page + hookAt + 14, "\x41\x89\x0E\xC6\x03\x01", 6) != 0)
            result = Outcome::Unexpected;
        else {
            uint64_t hookVA = pageVA + hookAt, saveVA = pageVA + saveAt;
            // the resolver's hook calls into the cache writer at the writer's second instruction
            uint8_t *h = page + hookAt + 14;
            h[0] = 0xE8;
            put32(h + 1, (int64_t)(saveVA + 2) - (int64_t)(hookVA + 19));
            h[5] = 0x90;
            // the cache writer: skips to the original "xor ebx, ebx" return, then stores the identity and RPA in a slot
            uint8_t *s = page + saveAt;
            static const uint8_t head[] = {0xEB, 0x76, 0x41, 0x89, 0x0E, 0xC6, 0x03, 0x01, 0x44, 0x89, 0xE8, 0x83, 0xE0, 0x0F,
                                           0x48, 0x8D, 0x04, 0x40, 0x48, 0x8D, 0x15};
            static const uint8_t tail[] = {0x48, 0x8D, 0x04, 0xC2, 0x89, 0x08, 0x41, 0x8B, 0x4E, 0x03, 0x89, 0x48, 0x03,
                                           0x41, 0x8B, 0x0F, 0x89, 0x48, 0x08, 0x41, 0x8B, 0x4F, 0x03, 0x89, 0x48, 0x0B, 0xC3};
            memcpy(s, head, sizeof(head));
            put32(s + 21, (int64_t)layout.cacheVA - (int64_t)(saveVA + 25));
            memcpy(s + 25, tail, sizeof(tail));
            if (report) {
                report->resolver = true;
                report->hook = hookVA;
                report->save = saveVA;
            }
            result = Outcome::Patched;
        }
    }

    if (helpers) {
        size_t adds = countSig(page, pageSize, add, &addAt);
        size_t removes = countSig(page, pageSize, remove, &removeAt);
        if (helpers != 1 || adds != 1 || removes != 1)
            return result == Outcome::Patched ? result : Outcome::Incomplete;
        if (!placed)
            return result == Outcome::Patched ? result : Outcome::NoLayout;
        if (!layout.cacheFits)
            return result == Outcome::Patched ? result : Outcome::NoCacheRoom;
        static const uint8_t kAddBody[] = {0x41, 0x0F, 0xB6, 0x17, 0x49, 0x8D, 0x4F, 0x01};
        uint8_t *a = page + addAt, *r = page + removeAt, *p = page + helperAt;
        uint64_t helperVA = pageVA + helperAt, addVA = pageVA + addAt, removeVA = pageVA + removeAt;
        // the helper's "je" leaves the function; its target stays where it was
        int32_t je;
        memcpy(&je, p + 24, 4);
        uint64_t exitVA = helperVA + 22 + 6 + (int64_t)je;
        if (memcmp(a + 3, kAddBody, sizeof(kAddBody)) != 0 || memcmp(r + 3, kAddBody, sizeof(kAddBody)) != 0 ||
            (exitVA >= helperVA && exitVA < helperVA + kHelperSize))
            return result == Outcome::Patched ? result : Outcome::Unexpected;

        a[3] = 0x31; a[4] = 0xD2; a[5] = 0xE8;
        put32(a + 6, (int64_t)(helperVA + 10) - (int64_t)(addVA + 10));
        a[10] = 0x90;
        r[3] = 0x6A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0xE8;
        put32(r + 7, (int64_t)(helperVA + 10) - (int64_t)(removeVA + 11));

        static const uint8_t first[] = {0xBB, 0x68, 0x00, 0x00, 0x00, 0xE9};
        static const uint8_t middle[] = {0x48, 0x8D, 0x05};
        static const uint8_t body[] = {0xB9, 0x10, 0x00, 0x00, 0x00, 0x41, 0x8B, 0x37, 0x41, 0x8B, 0x7F, 0x03, 0x80, 0x7C, 0xD0,
                                       0x08, 0x01, 0x75, 0x24, 0x44, 0x8B, 0x00, 0x41, 0x31, 0xF0, 0x44, 0x8B, 0x48, 0x03,
                                       0x41, 0x31, 0xF9, 0x45, 0x09, 0xC1, 0x75, 0x12, 0x84, 0xD2, 0x75, 0x08, 0x48, 0x8B,
                                       0x70, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x8D, 0x40, 0x10, 0xEB, 0x0B, 0x48, 0x83,
                                       0xC0, 0x18, 0xFF, 0xC9, 0x75, 0xCD, 0x4C, 0x89, 0xF8, 0x0F, 0xB6, 0x10, 0x48, 0x8D,
                                       0x48, 0x01, 0xC3};
        memcpy(p, first, sizeof(first));
        put32(p + 6, (int64_t)exitVA - (int64_t)(helperVA + 10));
        memcpy(p + 10, middle, sizeof(middle));
        put32(p + 13, (int64_t)layout.cacheVA - (int64_t)(helperVA + 17));
        memcpy(p + 17, body, sizeof(body));
        if (report) {
            report->filterList = true;
            report->add = addVA;
            report->remove = removeVA;
            report->helper = helperVA;
        }
        result = Outcome::Patched;
    }
    return result;
}

}  // namespace btd

#endif
