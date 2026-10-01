# Diva Physics Timing

为 **Hatsune Miku: Project DIVA Mega Mix+** 提供可调的次级物理更新与显示插值，保留游戏高刷新率。当前版本 **0.1.18**，已用 **DMM PR56** 测试。

头发与裙子/服装可分别设置 **20–60 Hz**，分别开关修正及显示插值。可选 ReShade 中文面板，修改后直接生效。

**头发的横向移动颤动仍未完全解决。** 部分镜头和服装的修正观感可能不理想，可以关闭头发组，保留裙子/服装修正。首次安装默认关闭总修正，先由使用者选择启用。

[下载安装包](https://github.com/xureimu-bit/diva-physics-timing/releases) · [DLAA 模组](https://github.com/xureimu-bit/diva-native-dlss)

## 安装

1. 完全退出游戏，下载 Release 中的 `DivaPhysicsTiming-0.1.18.zip`，解压到游戏根目录。最终应有 `mods/Diva Physics Timing/DivaPhysicsTiming.dll`。
2. 在 DMM 中勾选 **Diva Physics Timing**，通过 DMM 启动。
3. 使用面板需要安装支持 addon 的 ReShade；已验证 **ReShade 6.8.0 / addon API 20**。将安装包中的 `DivaPhysicsControls.addon64` 放在游戏 EXE 所在目录，并确保该目录包含在 ReShade 的插件搜索路径中。
4. 打开 ReShade 覆盖层，进入“插件”，展开 **Diva Physics Controls**。覆盖层按键由自己的 ReShade 设置决定。

面板会查找固定的 `mods/Diva Physics Timing/DivaPhysicsTiming.dll`，请保留此文件夹名称。不装 ReShade 也可直接编辑模组目录内的 `physics.ini`，约 500 ms 重新读取，无需重启。

更新前备份 `physics.ini`。卸载时先在 DMM 取消勾选，退出游戏后删除模组文件夹及可选面板 addon。

## 调节

| 选项 | 初始值 | 作用 |
| --- | --- | --- |
| 总修正 Mode | 0，关闭 | 关闭时使用游戏原生物理 |
| 头发 HairHz | 30 Hz | 20–60 Hz 整数可调 |
| 裙子/服装 ClothingHz | 30 Hz | 同上；骨骼裙子与布料都归该组 |
| HairEnabled / ClothingEnabled | 1 / 1 | 独立关闭该组修正，恢复原生物理 |
| HairInterpolation / ClothingInterpolation | 1 / 1 | 在渲染帧之间插值显示物理姿态 |
| HairSmoothing | 0 | 额外三姿态发束平滑，需开启头发插值；增加约半个物理周期的显示延迟 |

点击“应用并保存”才提交面板修改。“恢复 30 Hz 默认”会**启用**总修正与两组修正、设置 30/30 Hz、打开两组插值，并关闭额外头发平滑；它与首次安装的关闭状态不同。

可以从 30/30 Hz 开始对比。频率升高可能减小原生求解器的摆动幅度，不能把频率当作软硬程度滑块。头发观感不满意时可单独取消“修复头发物理”。其他身体次级物理与未分类骨骼沿用 `PhysicsHz`。本模组不设置游戏整体 FPS，需另用高刷模组设置渲染上限。

INI 的基本开关示例：

```ini
[Physics]
Mode=1
PhysicsHz=30
HairHz=30
ClothingHz=30
HairEnabled=0
ClothingEnabled=1
HairInterpolation=1
HairSmoothing=0
ClothingInterpolation=1
```

这是保留服装修正、头发使用原生物理的示例。完整默认配置在 [physics.ini](dist/mods/Diva%20Physics%20Timing/physics.ini)。缺少分组频率、设置 0 或越界值时，继承 `PhysicsHz`。面板只保存其对应的用户参数；历史诊断与实验输入修正选项不要随意修改。

## 实现与局限

按各物理对象自己的频率调度原生回调，隔离局部物理时间，缓存次级姿态，再附着到当前帧身体/父骨骼。主动画和渲染继续使用原始时间；发束平滑是显示处理。

标准头发和服装骨骼通过名称前缀分类，物理子分支继承父级类别，避免同一依赖链使用不同频率。自定义骨骼命名可能无法正确归类。头发/服装频率不会改变身体物理的分组。

这是实验版本，并非完整的高刷新率物理解耦。快速平移时仍可能颤动；修正效果取决于服装、骨骼结构和 PV。采用游戏版本相关的偏移与挂钩，启动时验证预期指令；未覆盖其他游戏版本及全部模组组合。测试曾覆盖的加载问题已有回归用例，但不能保证所有 PV 组合无问题。

排查时先在面板关闭总修正，再确认 DMM 禁用本模组后的表现。报告请附游戏版本、PV/时间点、服装、渲染 FPS、分组设置和日志片段；上传前删掉个人路径。

## 从源码构建与测试

需要 Visual Studio 2022 的 C++ 桌面开发工具、MASM、Windows SDK、CMake 3.20+。依赖头文件及 MinHook 源码已包含。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

输出为 `build/Release/DivaPhysicsTiming.dll` 与 `build/Release/DivaPhysicsControls.addon64`。发布包使用已做游戏内验证的构建；重新构建不保证二进制逐字节一致。

14 项测试覆盖多帧率与分组频率、原生回退、依赖分类、姿态显示、加载、暂停、线程/嵌套时间隔离、寄存器保存、配置/API 热切换及保存错误反馈。测试通过不能代替游戏内的视觉检查。可用 `-DBUILD_BENCHMARKS=ON` 编译额外基准，默认不运行。

## 许可

项目自己的代码采用 [MIT](LICENSE)。MinHook、ReShade SDK、ImGui 和参考项目保留各自许可证，见 [第三方声明](THIRD-PARTY-NOTICES.md)。本项目不包含游戏文件，为非官方模组。

## English

Experimental secondary-physics timing controls for Project DIVA Mega Mix+, tested with DMM PR56. Independent 20–60 Hz hair and clothing rates, per-group native fallback, presentation interpolation, and optional hair smoothing. The ReShade addon provides an in-game Chinese control panel. Hair translation jitter remains an acknowledged limitation; disabling hair correction while retaining clothing correction is supported. The initial configuration leaves the global correction disabled. This mod does not set render FPS.
