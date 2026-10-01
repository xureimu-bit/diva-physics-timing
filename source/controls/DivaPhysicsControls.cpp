#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <imgui.h>
#include <reshade.hpp>
#include "../physics_settings.hpp"
extern "C" __declspec(dllexport) const char* NAME="Diva Physics Controls";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Independent hair and skirt/clothing physics controls for Diva Physics Timing 0.1.18.";
static bool registered=false;
static physics_settings::GetControls getControls=nullptr;
static physics_settings::SetControls setControls=nullptr;
static bool draftLoaded=false,dirty=false,submitFailed=false;
static physics_settings::Controls draft;
static void resolve() {
    if(getControls)return;
    HMODULE core=GetModuleHandleW(L"DivaPhysicsTiming.dll");if(!core)return;
    wchar_t corePath[32768]{},exePath[32768]{};
    if(!GetModuleFileNameW(core,corePath,32768)||!GetModuleFileNameW(nullptr,exePath,32768))return;
    std::wstring expected(exePath);expected.resize(expected.find_last_of(L"\\/")+1);expected+=L"mods\\Diva Physics Timing\\DivaPhysicsTiming.dll";
    if(_wcsicmp(expected.c_str(),corePath))return;
    auto getter=reinterpret_cast<physics_settings::GetControls>(GetProcAddress(core,"DivaPhysicsGetControls"));
    auto setter=reinterpret_cast<physics_settings::SetControls>(GetProcAddress(core,"DivaPhysicsSetControls"));
    if(getter&&setter){getControls=getter;setControls=setter;}
}
static bool checkbox(const char* label,int32_t& value) {bool b=value!=0;if(ImGui::Checkbox(label,&b)){value=b?1:0;return true;}return false;}
static void draw(reshade::api::effect_runtime*) {
    resolve();physics_settings::Controls active;
    if(!getControls||!getControls(&active)||!active.armed) {
        ImGui::TextWrapped("尚未连接物理模组。请在 DMM 启用 Diva Physics Timing 0.1.18，然后重启游戏。");return;
    }
    if(!draftLoaded){draft=active;draftLoaded=true;}
    ImGui::Text("当前：%s | 头发 %d Hz | 裙子/服装 %d Hz",active.mode?"开启":"关闭",active.hairHz,active.clothingHz);
    if(active.pending)ImGui::TextUnformatted("正在应用……");
    if(active.lastSaveError)ImGui::Text("保存失败（Windows 错误 %d）；请重试。",active.lastSaveError);
    if(submitFailed)ImGui::TextUnformatted("本次修改未被接受，请重新读取当前设置。");
    ImGui::Separator();
    dirty|=checkbox("启用物理修复",draft.mode);
    ImGui::TextUnformatted("头发");
    dirty|=checkbox("修复头发物理",draft.hairEnabled);
    dirty|=ImGui::SliderInt("头发频率 (Hz)",&draft.hairHz,physics_settings::minHz,physics_settings::maxHz,"%d",ImGuiSliderFlags_AlwaysClamp);
    dirty|=checkbox("头发显示插值",draft.hairInterpolation);
    dirty|=checkbox("额外头发平滑",draft.hairSmoothing);
    ImGui::TextWrapped("额外平滑需开启显示插值；会增加约半个物理周期的发束显示延迟。");
    ImGui::Separator();ImGui::TextUnformatted("裙子 / 服装");
    dirty|=checkbox("修复裙子/服装物理",draft.clothingEnabled);
    dirty|=ImGui::SliderInt("裙子/服装频率 (Hz)",&draft.clothingHz,physics_settings::minHz,physics_settings::maxHz,"%d",ImGuiSliderFlags_AlwaysClamp);
    dirty|=checkbox("裙子/服装显示插值",draft.clothingInterpolation);
    ImGui::TextWrapped("频率调高可能减小摆动幅度。可先试头发 40 Hz、裙子 30 Hz；其他身体物理沿用原设置。");
    ImGui::Separator();
    if(ImGui::Button("应用并保存")){submitFailed=!setControls(&draft);if(!submitFailed)dirty=false;}
    ImGui::SameLine();
    if(ImGui::Button("恢复 30 Hz 默认")){draft={};submitFailed=!setControls(&draft);dirty=submitFailed;}
    ImGui::SameLine();if(ImGui::Button("读取当前设置")){draft=active;dirty=false;submitFailed=false;}
    if(dirty)ImGui::TextUnformatted("有修改尚未应用。");
    ImGui::TextWrapped("点击应用后立即生效，无需重启。关闭某组修复会恢复该组的游戏原生物理；不会冻结头发或裙子。");
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);registered=reshade::register_addon(instance);if(!registered){reshade::unregister_addon(instance);return FALSE;}reshade::register_overlay(nullptr,draw);}
    if(reason==DLL_PROCESS_DETACH&&registered){reshade::unregister_overlay(nullptr,draw);reshade::unregister_addon(instance);}
    return TRUE;
}
