from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def replace(path: str, old: str, new: str):
    file = ROOT / path
    text = file.read_text()
    if old not in text:
        print(f"skip/already patched: {path}")
        return
    count = text.count(old)
    file.write_text(text.replace(old, new))
    print(f"patched {path}: {count}")


replace("dsp/DfxDspPrivate.cpp",
        'ptutil\\dfxp\\u_dfxp.h',
        'ptutil/dfxp/u_dfxp.h')

for path in (
    "dsp/ptutil/include/slout.h",
    "audiopassthru/include/slout.h",
    "audiopassthru/src/SLOUT/Slout.cpp",
):
    replace(path,
            "#if defined( WIN32 ) // Wide char functions only supported in WIN32 builds.",
            "#if defined( WIN32 ) || defined(FXSOUND_LINUX)")

file = ROOT / "dsp/ptutil/PWAV/PwavConvert.cpp"
text = file.read_text()
text = text.replace(
    '#ifndef __ANDROID__\n#include <windows.h>\n#else\n#ifndef DWORD\n#define DWORD unsigned int\n#endif\n#endif //WIN32',
    '#if !defined(__ANDROID__) && !defined(FXSOUND_LINUX)\n#include <windows.h>\n#elif defined(__ANDROID__)\n#ifndef DWORD\n#define DWORD unsigned int\n#endif\n#endif')
text = text.replace(
    '#ifndef __ANDROID__\n#include "u_pwav.h"\n#endif //WIN32',
    '#if !defined(__ANDROID__) && !defined(FXSOUND_LINUX)\n#include "u_pwav.h"\n#endif')
file.write_text(text)
print("patched dsp/ptutil/PWAV/PwavConvert.cpp")

file = ROOT / "dsp/ptutil/dfxp/dfxpEq.cpp"
text = file.read_text()
text = text.replace(
    'swprintf(wcp_boost_cut, L"%.2f", *rp_boost_cut);',
    'swprintf(wcp_boost_cut, DFXP_REGISTRY_BUFFER_LENGTH, L"%.2f", *rp_boost_cut);')
text = text.replace(
    'swprintf(wcp_boost_cut, L"%s", wcp_key_value);',
    'swprintf(wcp_boost_cut, DFXP_REGISTRY_BUFFER_LENGTH, L"%s", wcp_key_value);')
file.write_text(text)
print("patched dsp/ptutil/dfxp/dfxpEq.cpp")

file = ROOT / "dsp/ptutil/dfxp/dfxpRegistryStandard.cpp"
text = file.read_text()
text = text.replace('swprintf(wcp_top_shared_folder_path, L"");',
                    "wcp_top_shared_folder_path[0] = L'\\0';")
text = text.replace('swprintf(wcp_top_vendor_specific_folder_path, L"");',
                    "wcp_top_vendor_specific_folder_path[0] = L'\\0';")
text = text.replace('swprintf(wcp_dfx_ui_path, L"");',
                    "wcp_dfx_ui_path[0] = L'\\0';")
file.write_text(text)
print("patched dsp/ptutil/dfxp/dfxpRegistryStandard.cpp")

replace("dsp/ptutil/dfxSharedUtil/dfxSharedUtil.cpp",
        'swprintf(wcp_top_shared_folder_path, L"");',
        "wcp_top_shared_folder_path[0] = L'\\0';")

file = ROOT / "dsp/ptutil/PRELST/Prelst.cpp"
text = file.read_text()
text = text.replace('swprintf(cast_handle->wcp_factory_dir, L"%s", wcp_factory_dir);',
                    'wcscpy(cast_handle->wcp_factory_dir, wcp_factory_dir);')
text = text.replace('swprintf(cast_handle->wcp_user_dir, L"%s", wcp_user_dir);',
                    'wcscpy(cast_handle->wcp_user_dir, wcp_user_dir);')
text = text.replace('swprintf(wcp_fullpath, L"%s\\\\%s", cast_handle->wcp_factory_dir, wcp_filename);',
                    'swprintf(wcp_fullpath, PT_MAX_PATH_STRLEN, L"%s\\\\%s", cast_handle->wcp_factory_dir, wcp_filename);')
text = text.replace('swprintf(wcp_fullpath, L"%s\\\\%s", cast_handle->wcp_user_dir, wcp_filename);',
                    'swprintf(wcp_fullpath, PT_MAX_PATH_STRLEN, L"%s\\\\%s", cast_handle->wcp_user_dir, wcp_filename);')
text = text.replace('swprintf(wcp_filename, L"%d.%s", i_index + 1, PRELST_FACTORY_EXTENSION_WIDE);',
                    'swprintf(wcp_filename, PT_MAX_PATH_STRLEN, L"%d.%s", i_index + 1, PRELST_FACTORY_EXTENSION_WIDE);')
text = text.replace('swprintf(wcp_filename, L"%d.%s", i_index + 1, PRELST_USER_EXTENSION_WIDE);',
                    'swprintf(wcp_filename, PT_MAX_PATH_STRLEN, L"%d.%s", i_index + 1, PRELST_USER_EXTENSION_WIDE);')
file.write_text(text)
print("patched dsp/ptutil/PRELST/Prelst.cpp")

for path, old, new in (
    ("dsp/ptutil/VALS/Valscfg.cpp",
     'swprintf(wcp_title, L"%s", cast_handle->wcp_title);',
     'wcscpy(wcp_title, cast_handle->wcp_title);'),
    ("dsp/ptutil/VALS/Vals.cpp",
     'swprintf(cast_to_hdl->wcp_comment, L"%s", cast_from_hdl->wcp_comment);',
     'wcscpy(cast_to_hdl->wcp_comment, cast_from_hdl->wcp_comment);'),
    ("dsp/ptutil/VALS/Valsfile.cpp",
     'swprintf(wcp_filename, L"%s%s", wcp_date_str, wcp_sec_str);',
     'wcscpy(wcp_filename, wcp_date_str); wcscat(wcp_filename, wcp_sec_str);'),
    ("dsp/ptutil/VALS/Valsfile.cpp",
     'swprintf(cast_handle->wcp_comment, L"%s", wcp_str);',
     'wcscpy(cast_handle->wcp_comment, wcp_str);'),
    ("dsp/ptutil/VALS/Valsset.cpp",
     'swprintf(cast_handle->wcp_comment, L"%s", wcp_comment);',
     'wcscpy(cast_handle->wcp_comment, wcp_comment);'),
    ("dsp/ptutil/VALS/Valsset.cpp",
     'swprintf(cast_handle->app_depend.wcpp_strings[i_index], L"%s", wcp_string);',
     'wcscpy(cast_handle->app_depend.wcpp_strings[i_index], wcp_string);'),
):
    replace(path, old, new)

for path, target in (
    ("dsp/ptechDsp/Aural/Aural0/aur_num.h", "Aur_num.h"),
    ("dsp/ptechDsp/Play/Play16/Play_num.h", "play_num.h"),
    ("dsp/ptechDsp/Play/Play32/Play_num.h", "play_num.h"),
):
    (ROOT / path).write_text(f'#pragma once\n#include "{target}"\n')
    print(f"wrote case alias: {path}")
