// System resource detection: CPUID for CPU info, GetMemoryMap for RAM.

#include "SystemInfo.h"
#include "Freestanding.h"

namespace SystemInfo {

static char sVendor[16]  = {};
static char sBrand[52]   = {};
static UINT32 sCoreCount = 1;
static UINT64 sTotalMem  = 0;
static EFI_MP_SERVICES_PROTOCOL* sMpServices = nullptr;
static bool sMpAvailable = false;
static UINT32 sEnabledProcessors = 1;

static UINT32 sCpuStepping     = 0;
static UINT32 sL1DataCacheKB   = 0;
static UINT32 sL1InstCacheKB   = 0;
static UINT32 sL2CacheKB       = 0;
static UINT32 sL3CacheKB       = 0;
static UINT32 sMemSpeedMHz     = 0;
static UINT32 sMemConfigSpeed  = 0;
static UINT32 sMemChannelCount = 0;

// ── CPUID helpers ────────────────────────────────────────────
static void CpuidRaw(UINT32 leaf, UINT32 subleaf,
                     UINT32& eax, UINT32& ebx, UINT32& ecx, UINT32& edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
}

static void DetectCpuVendor() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0, 0, eax, ebx, ecx, edx);
    // Vendor string: EBX + EDX + ECX
    memcpy(sVendor + 0, &ebx, 4);
    memcpy(sVendor + 4, &edx, 4);
    memcpy(sVendor + 8, &ecx, 4);
    sVendor[12] = '\0';
}

static void DetectCpuBrand() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0x80000000, 0, eax, ebx, ecx, edx);
    if (eax < 0x80000004) {
        StrCopy(sBrand, "Unknown CPU", sizeof(sBrand));
        return;
    }
    for (UINT32 i = 0; i < 3; ++i) {
        CpuidRaw(0x80000002 + i, 0, eax, ebx, ecx, edx);
        memcpy(sBrand + i * 16 +  0, &eax, 4);
        memcpy(sBrand + i * 16 +  4, &ebx, 4);
        memcpy(sBrand + i * 16 +  8, &ecx, 4);
        memcpy(sBrand + i * 16 + 12, &edx, 4);
    }
    sBrand[48] = '\0';
    // Trim leading spaces
    int start = 0;
    while (sBrand[start] == ' ') ++start;
    if (start > 0) {
        int len = static_cast<int>(StrLen(sBrand + start));
        memmove(sBrand, sBrand + start, len + 1);
    }
}

static void DetectCoreCount() {
    UINT32 eax, ebx, ecx, edx;

    // Try leaf 0xB (Extended Topology Enumeration)
    CpuidRaw(0, 0, eax, ebx, ecx, edx);
    UINT32 maxLeaf = eax;

    if (maxLeaf >= 0x1F) {
        // Try leaf 0x1F (V2 Extended Topology) first — newer Intel
        CpuidRaw(0x1F, 1, eax, ebx, ecx, edx);
        if ((ebx & 0xFFFF) > 0) {
            sCoreCount = ebx & 0xFFFF;
            return;
        }
    }

    if (maxLeaf >= 0x0B) {
        // Leaf 0xB subleaf 1 = core-level count
        CpuidRaw(0x0B, 1, eax, ebx, ecx, edx);
        if ((ebx & 0xFFFF) > 0) {
            sCoreCount = ebx & 0xFFFF;
            return;
        }
    }

    // Fallback: leaf 0x1 — logical processor count in EBX[23:16]
    if (maxLeaf >= 0x01) {
        CpuidRaw(0x01, 0, eax, ebx, ecx, edx);
        UINT32 logicalCount = (ebx >> 16) & 0xFF;
        if (logicalCount > 0) {
            sCoreCount = logicalCount;
            return;
        }
    }

    sCoreCount = 1;
}

// ── Memory map ───────────────────────────────────────────────
static void DetectMemory() {
    if (!gBS) return;

    UINTN mapSize = 0;
    UINTN mapKey, descSize;
    UINT32 descVersion;

    // First call to get required buffer size
    EFI_STATUS status = gBS->GetMemoryMap(
        &mapSize, nullptr, &mapKey, &descSize, &descVersion);

    if (status != EFI_BUFFER_TOO_SMALL) return;

    // Add headroom (allocating changes the map)
    mapSize += 2 * descSize;
    UINT8* buf = nullptr;
    status = gBS->AllocatePool(EfiLoaderData, mapSize, reinterpret_cast<VOID**>(&buf));
    if (EFI_ERROR(status) || !buf) return;

    status = gBS->GetMemoryMap(
        &mapSize, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(buf),
        &mapKey, &descSize, &descVersion);

    if (!EFI_ERROR(status)) {
        UINT64 total = 0;
        for (UINTN off = 0; off < mapSize; off += descSize) {
            auto desc = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(buf + off);
            if (desc->Type == EfiConventionalMemory) {
                total += desc->NumberOfPages * EFI_PAGE_SIZE;
            }
        }
        sTotalMem = total;
    }

    gBS->FreePool(buf);
}

// ── MP Services detection ────────────────────────────────────
static void DetectMpServices() {
    if (!gBS) return;

    EFI_GUID mpGuid = EFI_MP_SERVICES_PROTOCOL_GUID;
    EFI_STATUS status = gBS->LocateProtocol(
        &mpGuid, nullptr, reinterpret_cast<VOID**>(&sMpServices));

    if (!EFI_ERROR(status) && sMpServices) {
        sMpAvailable = true;
        UINTN total = 0, enabled = 0;
        status = sMpServices->GetNumberOfProcessors(
            sMpServices, &total, &enabled);
        if (!EFI_ERROR(status) && enabled > 0) {
            sEnabledProcessors = static_cast<UINT32>(enabled);
            // Override CPUID core count with MP Services (more accurate)
            sCoreCount = sEnabledProcessors;
        }
    }
}

static void DetectCpuStepping() {
    UINT32 eax, ebx, ecx, edx;
    CpuidRaw(0x1, 0, eax, ebx, ecx, edx);
    sCpuStepping = eax & 0xF;
}

static UINT32 CalcCacheKBFromLeaf4(UINT32 /*eax*/, UINT32 ebx, UINT32 ecx) {
    UINT32 lineSize   = (ebx & 0xFFF) + 1;
    UINT32 partitions = ((ebx >> 12) & 0x3FF) + 1;
    UINT32 ways       = ((ebx >> 22) & 0x3FF) + 1;
    UINT32 sets       = ecx + 1;
    return (lineSize * partitions * ways * sets) / 1024;
}

static void DetectCpuCacheIntel() {
    UINT32 eax, ebx, ecx, edx;
    for (UINT32 sub = 0; sub < 16; ++sub) {
        CpuidRaw(0x4, sub, eax, ebx, ecx, edx);
        UINT32 type  = eax & 0x1F;
        UINT32 level = (eax >> 5) & 0x7;
        if (type == 0) break;

        UINT32 kb = CalcCacheKBFromLeaf4(eax, ebx, ecx);
        if (level == 1 && type == 1 && sL1DataCacheKB == 0) sL1DataCacheKB = kb;
        if (level == 1 && type == 2 && sL1InstCacheKB == 0) sL1InstCacheKB = kb;
        if (level == 2 && sL2CacheKB == 0)                  sL2CacheKB = kb;
        if (level == 3 && sL3CacheKB == 0)                  sL3CacheKB = kb;
    }
}

static void DetectCpuCacheAmd() {
    UINT32 eax, ebx, ecx, edx;

    // Try CPUID 0x8000001D (Zen+ extended cache topology, same format as Intel leaf 4)
    CpuidRaw(0x80000000, 0, eax, ebx, ecx, edx);
    UINT32 maxExt = eax;

    if (maxExt >= 0x8000001D) {
        for (UINT32 sub = 0; sub < 16; ++sub) {
            CpuidRaw(0x8000001D, sub, eax, ebx, ecx, edx);
            UINT32 type  = eax & 0x1F;
            UINT32 level = (eax >> 5) & 0x7;
            if (type == 0) break;

            UINT32 kb = CalcCacheKBFromLeaf4(eax, ebx, ecx);
            if (level == 1 && type == 1 && sL1DataCacheKB == 0) sL1DataCacheKB = kb;
            if (level == 1 && type == 2 && sL1InstCacheKB == 0) sL1InstCacheKB = kb;
            if (level == 2 && sL2CacheKB == 0)                  sL2CacheKB = kb;
            if (level == 3 && sL3CacheKB == 0)                  sL3CacheKB = kb;
        }
        return;
    }

    // Fallback: legacy AMD leaves
    if (maxExt >= 0x80000005) {
        CpuidRaw(0x80000005, 0, eax, ebx, ecx, edx);
        sL1DataCacheKB = (ecx >> 24) & 0xFF;
        sL1InstCacheKB = (edx >> 24) & 0xFF;
    }
    if (maxExt >= 0x80000006) {
        CpuidRaw(0x80000006, 0, eax, ebx, ecx, edx);
        sL2CacheKB = (ecx >> 16) & 0xFFFF;
        sL3CacheKB = ((edx >> 18) & 0x3FFF) * 512; // units of 512 KB
    }
}

static void DetectCpuCache() {
    // Use Intel path for GenuineIntel, AMD path for AuthenticAMD
    if (sVendor[0] == 'G') { // GenuineIntel
        DetectCpuCacheIntel();
    } else if (sVendor[0] == 'A') { // AuthenticAMD
        DetectCpuCacheAmd();
    } else {
        // Unknown vendor: try Intel leaf 4 as a best-effort
        DetectCpuCacheIntel();
    }
}

// ── SMBIOS detection ─────────────────────────────────────────

static bool GuidEqual(const EFI_GUID& a, const EFI_GUID& b) {
    if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
    for (int i = 0; i < 8; ++i)
        if (a.Data4[i] != b.Data4[i]) return false;
    return true;
}

// Returns pointer to string at 1-based index, or "" if not found.
static const char* SmbiosGetString(const UINT8* structBase, UINT8 index) {
    if (index == 0) return "";
    const char* s = reinterpret_cast<const char*>(structBase + structBase[1]);
    for (UINT8 i = 1; i < index; ++i) {
        while (*s) ++s;
        ++s;
        if (*s == '\0') return ""; // ran off end of strings
    }
    return s;
}

// Returns pointer to byte after the structure's string section.
static const UINT8* SmbiosNextStruct(const UINT8* p) {
    const char* s = reinterpret_cast<const char*>(p + p[1]);
    while (s[0] || s[1]) ++s;
    return reinterpret_cast<const UINT8*>(s + 2);
}

static void ParseSmbiosTable(const UINT8* start, UINT32 len) {
    const UINT8* p   = start;
    const UINT8* end = start + len;

    char  banks[8][64];
    UINT32 bankCount = 0;

    while (p < end && p + 4 <= end) {
        UINT8 type   = p[0];
        UINT8 length = p[1];

        if (type == 127) break; // end-of-table marker
        if (length < 4)  break; // malformed

        if (type == 17 && length >= 0x18) { // Memory Device
            UINT16 size = *reinterpret_cast<const UINT16*>(p + 0x0C);
            bool populated = (size & 0x7FFF) != 0 && size != 0xFFFF;

            if (populated) {
                UINT16 speed = *reinterpret_cast<const UINT16*>(p + 0x15);
                if (speed > 0 && sMemSpeedMHz == 0) sMemSpeedMHz = speed;

                if (length >= 0x22) {
                    UINT16 cs = *reinterpret_cast<const UINT16*>(p + 0x20);
                    if (cs > 0 && sMemConfigSpeed == 0) sMemConfigSpeed = cs;
                }

                // Collect unique bank locator strings for channel count
                const char* bankStr = SmbiosGetString(p, p[0x11]);
                if (bankStr[0] != '\0' && bankCount < 8) {
                    bool found = false;
                    for (UINT32 i = 0; i < bankCount; ++i)
                        if (StrCmp(banks[i], bankStr) == 0) { found = true; break; }
                    if (!found)
                        StrCopy(banks[bankCount++], bankStr, 64);
                }
            }
        }

        p = SmbiosNextStruct(p);
    }

    if (bankCount > 0) sMemChannelCount = bankCount;
}

static void DetectSmbios() {
    if (!gST) return;

    EFI_GUID guid3 = SMBIOS3_TABLE_GUID;
    EFI_GUID guid2 = SMBIOS_TABLE_GUID;

    auto confTable = reinterpret_cast<EFI_CONFIGURATION_TABLE*>(gST->ConfigurationTable);

    const UINT8* tableStart = nullptr;
    UINT32 tableLen = 0;

    // Prefer SMBIOS 3.x (64-bit address)
    for (UINTN i = 0; i < gST->NumberOfTableEntries && !tableStart; ++i) {
        if (!GuidEqual(confTable[i].VendorGuid, guid3)) continue;
        const UINT8* ep = reinterpret_cast<const UINT8*>(confTable[i].VendorTable);
        if (memcmp(ep, "_SM3_", 5) != 0) continue;
        UINT64 addr = *reinterpret_cast<const UINT64*>(ep + 16);
        UINT32 sz   = *reinterpret_cast<const UINT32*>(ep + 12);
        tableStart  = reinterpret_cast<const UINT8*>(static_cast<UINTN>(addr));
        tableLen    = sz;
    }

    // Fall back to SMBIOS 2.x (32-bit address)
    for (UINTN i = 0; i < gST->NumberOfTableEntries && !tableStart; ++i) {
        if (!GuidEqual(confTable[i].VendorGuid, guid2)) continue;
        const UINT8* ep = reinterpret_cast<const UINT8*>(confTable[i].VendorTable);
        if (memcmp(ep, "_SM_", 4) != 0) continue;
        UINT32 addr = *reinterpret_cast<const UINT32*>(ep + 24);
        UINT16 sz   = *reinterpret_cast<const UINT16*>(ep + 22);
        tableStart  = reinterpret_cast<const UINT8*>(static_cast<UINTN>(addr));
        tableLen    = sz;
    }

    if (tableStart && tableLen > 0)
        ParseSmbiosTable(tableStart, tableLen);
}

// ── Public API ───────────────────────────────────────────────
void Detect() {
    DetectCpuVendor();
    DetectCpuBrand();
    DetectCoreCount();
    DetectCpuStepping();
    DetectCpuCache();
    DetectMemory();
    DetectMpServices();
    DetectSmbios();
}

const char* GetCpuVendor()     { return sVendor; }
const char* GetCpuBrand()      { return sBrand; }
UINT32      GetCpuCoreCount()  { return sCoreCount; }
UINT32      GetCpuStepping()   { return sCpuStepping; }
UINT32      GetL1DataCacheKB() { return sL1DataCacheKB; }
UINT32      GetL1InstCacheKB() { return sL1InstCacheKB; }
UINT32      GetL2CacheKB()     { return sL2CacheKB; }
UINT32      GetL3CacheKB()     { return sL3CacheKB; }
UINT64      GetTotalMemoryBytes() { return sTotalMem; }
UINT64      GetTotalMemoryMB()    { return sTotalMem / (1024 * 1024); }
UINT32      GetMemorySpeedMHz()            { return sMemSpeedMHz; }
UINT32      GetMemoryConfiguredSpeedMHz()  { return sMemConfigSpeed; }
UINT32      GetMemoryChannelCount()        { return sMemChannelCount; }

bool HasMpServices() { return sMpAvailable; }
EFI_MP_SERVICES_PROTOCOL* GetMpServices() { return sMpServices; }
UINT32 GetEnabledProcessorCount() { return sEnabledProcessors; }

} // namespace SystemInfo
