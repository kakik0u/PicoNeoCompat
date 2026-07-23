#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "nvEncodeAPI.h"

namespace {

HMODULE g_self = nullptr;
HMODULE g_real = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
wchar_t g_log_path[MAX_PATH] = {};
wchar_t g_config_path[MAX_PATH] = {};
INIT_ONCE g_config_once = INIT_ONCE_STATIC_INIT;

using CreateInstanceFn = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
using GetMaxVersionFn = NVENCSTATUS(NVENCAPI*)(uint32_t*);

CreateInstanceFn g_create_instance = nullptr;
GetMaxVersionFn g_get_max_version = nullptr;
PNVENCGETENCODEPRESETCONFIG g_get_preset_config = nullptr;
PNVENCGETENCODEPRESETCONFIGEX g_get_preset_config_ex = nullptr;
PNVENCINITIALIZEENCODER g_initialize_encoder = nullptr;
PNVENCRECONFIGUREENCODER g_reconfigure_encoder = nullptr;

void Log(const char* format, ...) {
    char message[2048] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);

    char line[2300] = {};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
               "%llu pid=%lu tid=%lu %s\r\n",
               static_cast<unsigned long long>(GetTickCount64()),
               GetCurrentProcessId(), GetCurrentThreadId(), message);

    AcquireSRWLockExclusive(&g_lock);
    HANDLE file = CreateFileW(g_log_path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool GuidEqual(const GUID& a, const GUID& b) {
    return InlineIsEqualGUID(a, b) != FALSE;
}

void GuidText(const GUID& guid, char* output, size_t output_size) {
    _snprintf_s(output, output_size, _TRUNCATE,
               "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
               guid.Data1, guid.Data2, guid.Data3,
               guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
               guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
}

struct PresetTranslation {
    GUID preset;
    NV_ENC_TUNING_INFO tuning;
    const char* source_name;
    const char* target_name;
    bool changed;
};

struct OverrideConfig {
    bool preset_enabled = false;
    GUID preset = {};
    const char* preset_name = "Auto";
    bool tuning_enabled = false;
    NV_ENC_TUNING_INFO tuning = NV_ENC_TUNING_INFO_UNDEFINED;
    const char* tuning_name = "Auto";
    bool multipass_enabled = false;
    NV_ENC_MULTI_PASS multipass = NV_ENC_MULTI_PASS_DISABLED;
    const char* multipass_name = "Auto";
};

OverrideConfig g_config;

bool ParsePreset(const wchar_t* value, GUID* preset, const char** name) {
    if (_wcsicmp(value, L"P1") == 0) {
        *preset = NV_ENC_PRESET_P1_GUID; *name = "P1"; return true;
    }
    if (_wcsicmp(value, L"P2") == 0) {
        *preset = NV_ENC_PRESET_P2_GUID; *name = "P2"; return true;
    }
    if (_wcsicmp(value, L"P3") == 0) {
        *preset = NV_ENC_PRESET_P3_GUID; *name = "P3"; return true;
    }
    if (_wcsicmp(value, L"P4") == 0) {
        *preset = NV_ENC_PRESET_P4_GUID; *name = "P4"; return true;
    }
    if (_wcsicmp(value, L"P5") == 0) {
        *preset = NV_ENC_PRESET_P5_GUID; *name = "P5"; return true;
    }
    if (_wcsicmp(value, L"P6") == 0) {
        *preset = NV_ENC_PRESET_P6_GUID; *name = "P6"; return true;
    }
    if (_wcsicmp(value, L"P7") == 0) {
        *preset = NV_ENC_PRESET_P7_GUID; *name = "P7"; return true;
    }
    return false;
}

bool ParseTuning(const wchar_t* value, NV_ENC_TUNING_INFO* tuning,
                 const char** name) {
    if (_wcsicmp(value, L"HighQuality") == 0) {
        *tuning = NV_ENC_TUNING_INFO_HIGH_QUALITY;
        *name = "HighQuality";
        return true;
    }
    if (_wcsicmp(value, L"LowLatency") == 0) {
        *tuning = NV_ENC_TUNING_INFO_LOW_LATENCY;
        *name = "LowLatency";
        return true;
    }
    if (_wcsicmp(value, L"UltraLowLatency") == 0) {
        *tuning = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
        *name = "UltraLowLatency";
        return true;
    }
    if (_wcsicmp(value, L"Lossless") == 0) {
        *tuning = NV_ENC_TUNING_INFO_LOSSLESS;
        *name = "Lossless";
        return true;
    }
    return false;
}

bool ParseMultiPass(const wchar_t* value, NV_ENC_MULTI_PASS* multipass,
                    const char** name) {
    if (_wcsicmp(value, L"Disabled") == 0) {
        *multipass = NV_ENC_MULTI_PASS_DISABLED;
        *name = "Disabled";
        return true;
    }
    if (_wcsicmp(value, L"Quarter") == 0) {
        *multipass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
        *name = "Quarter";
        return true;
    }
    if (_wcsicmp(value, L"Full") == 0) {
        *multipass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
        *name = "Full";
        return true;
    }
    return false;
}

BOOL CALLBACK LoadConfig(PINIT_ONCE, PVOID, PVOID*) {
    if (GetFileAttributesW(g_config_path) == INVALID_FILE_ATTRIBUTES) {
        Log("config not found; using automatic compatibility mapping");
        return TRUE;
    }

    wchar_t value[64] = {};
    GetPrivateProfileStringW(L"NVENC", L"Preset", L"Auto",
                             value, _countof(value), g_config_path);
    if (_wcsicmp(value, L"Auto") != 0 &&
        !ParsePreset(value, &g_config.preset, &g_config.preset_name)) {
        Log("invalid config Preset=%ls; using Auto", value);
    } else if (_wcsicmp(value, L"Auto") != 0) {
        g_config.preset_enabled = true;
    }

    GetPrivateProfileStringW(L"NVENC", L"Tuning", L"Auto",
                             value, _countof(value), g_config_path);
    if (_wcsicmp(value, L"Auto") != 0 &&
        !ParseTuning(value, &g_config.tuning, &g_config.tuning_name)) {
        Log("invalid config Tuning=%ls; using Auto", value);
    } else if (_wcsicmp(value, L"Auto") != 0) {
        g_config.tuning_enabled = true;
    }

    GetPrivateProfileStringW(L"NVENC", L"MultiPass", L"Auto",
                             value, _countof(value), g_config_path);
    if (_wcsicmp(value, L"Auto") != 0 &&
        !ParseMultiPass(value, &g_config.multipass,
                        &g_config.multipass_name)) {
        Log("invalid config MultiPass=%ls; using Auto", value);
    } else if (_wcsicmp(value, L"Auto") != 0) {
        g_config.multipass_enabled = true;
    }

    Log("config loaded preset=%s tuning=%s multipass=%s",
        g_config.preset_name, g_config.tuning_name,
        g_config.multipass_name);
    return TRUE;
}

void EnsureConfig() {
    InitOnceExecuteOnce(&g_config_once, LoadConfig, nullptr, nullptr);
}

PresetTranslation TranslatePresetDefault(const GUID& preset) {
    if (GuidEqual(preset, NV_ENC_PRESET_LOW_LATENCY_HP_GUID))
        return {NV_ENC_PRESET_P2_GUID, NV_ENC_TUNING_INFO_LOW_LATENCY,
                "LowLatencyHP", "P2/LowLatency", true};
    if (GuidEqual(preset, NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID))
        return {NV_ENC_PRESET_P3_GUID, NV_ENC_TUNING_INFO_LOW_LATENCY,
                "LowLatencyDefault", "P3/LowLatency", true};
    if (GuidEqual(preset, NV_ENC_PRESET_LOW_LATENCY_HQ_GUID))
        return {NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_LOW_LATENCY,
                "LowLatencyHQ", "P4/LowLatency", true};
    if (GuidEqual(preset, NV_ENC_PRESET_HP_GUID))
        return {NV_ENC_PRESET_P2_GUID, NV_ENC_TUNING_INFO_HIGH_QUALITY,
                "HP", "P2/HighQuality", true};
    if (GuidEqual(preset, NV_ENC_PRESET_DEFAULT_GUID))
        return {NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_HIGH_QUALITY,
                "Default", "P4/HighQuality", true};
    if (GuidEqual(preset, NV_ENC_PRESET_HQ_GUID) ||
        GuidEqual(preset, NV_ENC_PRESET_BD_GUID))
        return {NV_ENC_PRESET_P5_GUID, NV_ENC_TUNING_INFO_HIGH_QUALITY,
                "HQ/BD", "P5/HighQuality", true};
    if (GuidEqual(preset, NV_ENC_PRESET_LOSSLESS_DEFAULT_GUID) ||
        GuidEqual(preset, NV_ENC_PRESET_LOSSLESS_HP_GUID))
        return {NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_LOSSLESS,
                "Lossless", "P4/Lossless", true};

    return {preset, NV_ENC_TUNING_INFO_LOW_LATENCY,
            "modern/unknown", "unchanged/LowLatency", false};
}

PresetTranslation TranslatePreset(const GUID& preset) {
    PresetTranslation translation = TranslatePresetDefault(preset);
    EnsureConfig();
    if (g_config.preset_enabled) {
        translation.preset = g_config.preset;
        translation.changed = true;
    }
    if (g_config.tuning_enabled) {
        translation.tuning = g_config.tuning;
        translation.changed = true;
    }
    if (g_config.preset_enabled || g_config.tuning_enabled)
        translation.target_name = "configured override";
    return translation;
}

void TranslateRateControl(NV_ENC_CONFIG* config) {
    if (!config) return;

    const auto old_mode = static_cast<unsigned int>(config->rcParams.rateControlMode);
    switch (old_mode) {
        case 0x8: // NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ
            config->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
            config->rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
            Log("translated RC CBR_LOWDELAY_HQ(0x8) -> CBR, multipass=quarter");
            break;
        case 0x10: // NV_ENC_PARAMS_RC_CBR_HQ
            config->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
            config->rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
            Log("translated RC CBR_HQ(0x10) -> CBR, multipass=full");
            break;
        case 0x20: // NV_ENC_PARAMS_RC_VBR_HQ
            config->rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
            config->rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
            Log("translated RC VBR_HQ(0x20) -> VBR, multipass=full");
            break;
        default:
            break;
    }
    EnsureConfig();
    if (g_config.multipass_enabled) {
        config->rcParams.multiPass = g_config.multipass;
        Log("configured multipass override=%s",
            g_config.multipass_name);
    }
}

void TranslateInitializeParams(NV_ENC_INITIALIZE_PARAMS* params) {
    if (!params) return;

    const PresetTranslation translation = TranslatePreset(params->presetGUID);
    char before[64] = {};
    char after[64] = {};
    GuidText(params->presetGUID, before, sizeof(before));
    GuidText(translation.preset, after, sizeof(after));

    if (translation.changed) {
        params->presetGUID = translation.preset;
        params->tuningInfo = translation.tuning;
    } else if (params->tuningInfo == NV_ENC_TUNING_INFO_UNDEFINED) {
        params->tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
    }

    TranslateRateControl(params->encodeConfig);
    Log("initialize %ux%u preset=%s -> %s (%s -> %s), tuning=%u rc=%u multipass=%u initVer=0x%08X cfgVer=0x%08X",
        params->encodeWidth, params->encodeHeight, before, after,
        translation.source_name, translation.target_name,
        static_cast<unsigned int>(params->tuningInfo),
        params->encodeConfig ? static_cast<unsigned int>(params->encodeConfig->rcParams.rateControlMode) : 0xFFFFFFFFu,
        params->encodeConfig ? static_cast<unsigned int>(params->encodeConfig->rcParams.multiPass) : 0xFFFFFFFFu,
        params->version,
        params->encodeConfig ? params->encodeConfig->version : 0u);
}

bool EnsureReal() {
    if (g_real && g_create_instance && g_get_max_version) return true;

    wchar_t system_path[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_path, MAX_PATH);
    if (!length || length + 20 >= MAX_PATH) return false;
    wcscat_s(system_path, L"\\nvEncodeAPI64.dll");

    HMODULE module = LoadLibraryW(system_path);
    if (!module) {
        Log("LoadLibraryW(real nvEncodeAPI64.dll) failed error=%lu", GetLastError());
        return false;
    }

    auto create = reinterpret_cast<CreateInstanceFn>(
        GetProcAddress(module, "NvEncodeAPICreateInstance"));
    auto max_version = reinterpret_cast<GetMaxVersionFn>(
        GetProcAddress(module, "NvEncodeAPIGetMaxSupportedVersion"));
    if (!create || !max_version) {
        Log("GetProcAddress(real entrypoints) failed error=%lu", GetLastError());
        FreeLibrary(module);
        return false;
    }

    g_real = module;
    g_create_instance = create;
    g_get_max_version = max_version;
    Log("loaded real NVENC library from %ls", system_path);
    return true;
}

NVENCSTATUS NVENCAPI HookGetEncodePresetConfig(
    void* encoder, GUID encode_guid, GUID preset_guid,
    NV_ENC_PRESET_CONFIG* preset_config) {
    const PresetTranslation translation = TranslatePreset(preset_guid);
    char codec[64] = {};
    char before[64] = {};
    char after[64] = {};
    GuidText(encode_guid, codec, sizeof(codec));
    GuidText(preset_guid, before, sizeof(before));
    GuidText(translation.preset, after, sizeof(after));

    NVENCSTATUS status = NV_ENC_ERR_GENERIC;
    if (g_get_preset_config_ex) {
        status = g_get_preset_config_ex(encoder, encode_guid,
                                        translation.preset,
                                        translation.tuning,
                                        preset_config);
    } else if (g_get_preset_config) {
        status = g_get_preset_config(encoder, encode_guid,
                                     translation.preset, preset_config);
    }

    if (status == NV_ENC_SUCCESS && preset_config)
        TranslateRateControl(&preset_config->presetCfg);

    Log("GetPresetConfig codec=%s preset=%s -> %s (%s -> %s) tuning=%u presetCfgVer=0x%08X status=%d",
        codec, before, after, translation.source_name, translation.target_name,
        static_cast<unsigned int>(translation.tuning),
        preset_config ? preset_config->version : 0u,
        static_cast<int>(status));
    return status;
}

NVENCSTATUS NVENCAPI HookInitializeEncoder(
    void* encoder, NV_ENC_INITIALIZE_PARAMS* params) {
    TranslateInitializeParams(params);
    const NVENCSTATUS status = g_initialize_encoder
        ? g_initialize_encoder(encoder, params)
        : NV_ENC_ERR_GENERIC;
    Log("InitializeEncoder status=%d", static_cast<int>(status));
    return status;
}

NVENCSTATUS NVENCAPI HookReconfigureEncoder(
    void* encoder, NV_ENC_RECONFIGURE_PARAMS* params) {
    if (params) TranslateInitializeParams(&params->reInitEncodeParams);
    const NVENCSTATUS status = g_reconfigure_encoder
        ? g_reconfigure_encoder(encoder, params)
        : NV_ENC_ERR_GENERIC;
    Log("ReconfigureEncoder status=%d", static_cast<int>(status));
    return status;
}

} // namespace

NVENCSTATUS NVENCAPI
NvEncodeAPIGetMaxSupportedVersion(uint32_t* version) {
    if (!EnsureReal()) return NV_ENC_ERR_NO_ENCODE_DEVICE;
    const NVENCSTATUS status = g_get_max_version(version);
    Log("GetMaxSupportedVersion status=%d version=0x%08X",
        static_cast<int>(status), version ? *version : 0u);
    return status;
}

NVENCSTATUS NVENCAPI
NvEncodeAPICreateInstance(NV_ENCODE_API_FUNCTION_LIST* function_list) {
    if (!function_list) return NV_ENC_ERR_INVALID_PTR;
    if (!EnsureReal()) return NV_ENC_ERR_NO_ENCODE_DEVICE;

    const uint32_t requested_version = function_list->version;
    const NVENCSTATUS status = g_create_instance(function_list);
    if (status != NV_ENC_SUCCESS) {
        Log("CreateInstance real call failed requestedVer=0x%08X status=%d",
            requested_version, static_cast<int>(status));
        return status;
    }

    g_get_preset_config = function_list->nvEncGetEncodePresetConfig;
    g_get_preset_config_ex = function_list->nvEncGetEncodePresetConfigEx;
    g_initialize_encoder = function_list->nvEncInitializeEncoder;
    g_reconfigure_encoder = function_list->nvEncReconfigureEncoder;

    function_list->nvEncGetEncodePresetConfig = HookGetEncodePresetConfig;
    function_list->nvEncInitializeEncoder = HookInitializeEncoder;
    if (function_list->nvEncReconfigureEncoder)
        function_list->nvEncReconfigureEncoder = HookReconfigureEncoder;

    Log("CreateInstance hooked requestedVer=0x%08X ex=%p init=%p reconfig=%p",
        requested_version, reinterpret_cast<void*>(g_get_preset_config_ex),
        reinterpret_cast<void*>(g_initialize_encoder),
        reinterpret_cast<void*>(g_reconfigure_encoder));
    return status;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        wchar_t module_path[MAX_PATH] = {};
        GetModuleFileNameW(instance, module_path, MAX_PATH);
        wchar_t* slash = wcsrchr(module_path, L'\\');
        if (slash) *(slash + 1) = L'\0';
        wcscpy_s(g_log_path, module_path);
        wcscat_s(g_log_path, L"nvenc_compat.log");
        wcscpy_s(g_config_path, module_path);
        wcscat_s(g_config_path, L"nvenc_compat.ini");
    }
    return TRUE;
}
