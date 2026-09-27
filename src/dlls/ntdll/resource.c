#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/*
 * リソースの検索．メモリ上のイメージのリソースディレクトリ (型 → 名前 → 言語 の3段) をたどる．
 * 名前は整数 (IS_INTRESOURCE) か文字列 (大文字小文字を区別しない)．
 */

static IMAGE_NT_HEADERS64* nt_headers(PVOID base) {
    IMAGE_DOS_HEADER* dos = base;
    return (IMAGE_NT_HEADERS64*)((BYTE*)base + dos->e_lfanew);
}

static IMAGE_RESOURCE_DIRECTORY* resource_root(PVOID base, ULONG* size) {
    IMAGE_DATA_DIRECTORY* dir = &nt_headers(base)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    if (!dir->VirtualAddress || !dir->Size) return NULL;
    if (size) *size = dir->Size;
    return (IMAGE_RESOURCE_DIRECTORY*)((BYTE*)base + dir->VirtualAddress);
}

static WCHAR upcase(WCHAR c) {
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static BOOL name_matches(IMAGE_RESOURCE_DIRECTORY* root, IMAGE_RESOURCE_DIRECTORY_ENTRY* entry, ULONG_PTR id) {
    if (IS_INTRESOURCE(id)) return !entry->NameIsString && entry->Id == (WORD)id;
    if (!entry->NameIsString) return FALSE;

    IMAGE_RESOURCE_DIR_STRING_U* s = (IMAGE_RESOURCE_DIR_STRING_U*)((BYTE*)root + entry->NameOffset);
    const WCHAR* name = (const WCHAR*)id;
    for (WORD i = 0; i < s->Length; i++) {
        if (!name[i] || upcase(name[i]) != upcase(s->NameString[i])) return FALSE;
    }
    return name[s->Length] == 0;
}

static IMAGE_RESOURCE_DIRECTORY_ENTRY* entries(IMAGE_RESOURCE_DIRECTORY* dir) {
    return (IMAGE_RESOURCE_DIRECTORY_ENTRY*)(dir + 1);
}

static IMAGE_RESOURCE_DIRECTORY_ENTRY* find_entry(IMAGE_RESOURCE_DIRECTORY* root, IMAGE_RESOURCE_DIRECTORY* dir, ULONG_PTR id) {
    UINT count = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (UINT i = 0; i < count; i++) {
        if (name_matches(root, &entries(dir)[i], id)) return &entries(dir)[i];
    }
    return NULL;
}

static IMAGE_RESOURCE_DIRECTORY* subdirectory(IMAGE_RESOURCE_DIRECTORY* root, IMAGE_RESOURCE_DIRECTORY_ENTRY* entry) {
    return entry->DataIsDirectory ? (IMAGE_RESOURCE_DIRECTORY*)((BYTE*)root + entry->OffsetToDirectory) : NULL;
}

/*
 * Info = {型, 名前, 言語}．Level は 0〜2 (何段目までたどるか)．Level 2 で DataEntry を返す．
 * 言語が見つからなければ，中立 (0) → 最初の言語 の順に探す．
 */
NTSTATUS NTAPI LdrFindResource_U(PVOID BaseAddress, const ULONG_PTR* ResourceInfo, ULONG Level,
                                 IMAGE_RESOURCE_DATA_ENTRY** ResourceDataEntry) {
    IMAGE_RESOURCE_DIRECTORY* root = resource_root(BaseAddress, NULL);
    if (!root || Level > 2) return NTMAC_STATUS_RESOURCE_DATA_NOT_FOUND;

    IMAGE_RESOURCE_DIRECTORY_ENTRY* e = find_entry(root, root, ResourceInfo[0]);
    if (!e) return NTMAC_STATUS_RESOURCE_TYPE_NOT_FOUND;
    IMAGE_RESOURCE_DIRECTORY* names = subdirectory(root, e);
    if (!names) return NTMAC_STATUS_RESOURCE_DATA_NOT_FOUND;

    e = find_entry(root, names, ResourceInfo[1]);
    if (!e) return NTMAC_STATUS_RESOURCE_NAME_NOT_FOUND;
    IMAGE_RESOURCE_DIRECTORY* langs = subdirectory(root, e);
    if (!langs) return NTMAC_STATUS_RESOURCE_DATA_NOT_FOUND;

    e = Level >= 2 ? find_entry(root, langs, ResourceInfo[2]) : NULL;
    if (!e) e = find_entry(root, langs, 0);
    if (!e && langs->NumberOfNamedEntries + langs->NumberOfIdEntries > 0) e = &entries(langs)[0];
    if (!e || e->DataIsDirectory) return NTMAC_STATUS_RESOURCE_LANG_NOT_FOUND;

    *ResourceDataEntry = (IMAGE_RESOURCE_DATA_ENTRY*)((BYTE*)root + e->OffsetToData);
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI LdrAccessResource(PVOID BaseAddress, const IMAGE_RESOURCE_DATA_ENTRY* ResourceDataEntry,
                                 PVOID* Resource, PULONG Size) {
    if (Resource) *Resource = (BYTE*)BaseAddress + ResourceDataEntry->OffsetToData;
    if (Size) *Size = ResourceDataEntry->Size;
    return NTMAC_STATUS_SUCCESS;
}
