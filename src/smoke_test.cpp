#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "nvEncodeAPI.h"

using GetMaxFn = NVENCSTATUS(NVENCAPI*)(uint32_t*);
using CreateFn = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);

int main() {
    HMODULE module = LoadLibraryW(L"build\\nvEncCompat64.dll");
    if (!module) {
        std::printf("load failed: %lu\n", GetLastError());
        return 1;
    }

    auto get_max = reinterpret_cast<GetMaxFn>(
        GetProcAddress(module, "NvEncodeAPIGetMaxSupportedVersion"));
    auto create = reinterpret_cast<CreateFn>(
        GetProcAddress(module, "NvEncodeAPICreateInstance"));
    if (!get_max || !create) {
        std::printf("exports missing: %lu\n", GetLastError());
        return 2;
    }

    uint32_t max_version = 0;
    const NVENCSTATUS max_status = get_max(&max_version);

    NV_ENCODE_API_FUNCTION_LIST functions = {};
    functions.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    const NVENCSTATUS create_status = create(&functions);

    std::printf("maxStatus=%d maxVersion=0x%08X createStatus=%d oldPreset=%p exPreset=%p init=%p\n",
                static_cast<int>(max_status), max_version,
                static_cast<int>(create_status),
                reinterpret_cast<void*>(functions.nvEncGetEncodePresetConfig),
                reinterpret_cast<void*>(functions.nvEncGetEncodePresetConfigEx),
                reinterpret_cast<void*>(functions.nvEncInitializeEncoder));

    return (max_status == NV_ENC_SUCCESS &&
            create_status == NV_ENC_SUCCESS &&
            functions.nvEncGetEncodePresetConfig &&
            functions.nvEncGetEncodePresetConfigEx &&
            functions.nvEncInitializeEncoder) ? 0 : 3;
}
