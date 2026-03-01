# VS2026 Insiders 构建说明

## 前置条件

- 已安装并可用：`D:\visualstudioInsiders\VC\Auxiliary\Build\vcvars64.bat`
- 已安装 CMake 3.28+ 与 Ninja
- 已安装 Qt：`D:\qt\6.10.1\msvc2022_64`
- 阶段 2（Live2D）需要手动放置 Cubism SDK 到：
  `D:\CPPcode\AiDeskPetMan\third_party\CubismSdkForNative`
  也可以通过脚本参数覆盖该路径
- 依赖库支持两种方式：
  - 手动依赖模式：自己安装依赖并通过 `CMAKE_PREFIX_PATH` 指向
  - 可选 vcpkg 模式：设置 `VCPKG_ROOT` 并使用 `-UseVcpkg`

手动依赖目录结构见：
- `docs/manual_dependencies.md`

## 固定路径位置

统一在 `scripts/local_paths.ps1` 中配置：

- `DefaultDepsRoot`
- `DefaultQtRoot`
- `DefaultCubismSdkRoot`

如需调整，只改这一处即可。

## Live2D 控制方式（重点）

现在脚本默认是 `Auto` 模式，会自动读取：
- `config/config.json` -> `skin.enable_live2d`

对应关系：

- `true`：传递 `-DMIKUDESK_ENABLE_LIVE2D=ON`
- `false`：传递 `-DMIKUDESK_ENABLE_LIVE2D=OFF`

你仍然可以手动强制：

- `-Live2D On`：强制开启
- `-Live2D Off`：强制关闭
- `-EnableLive2D`：等同于强制开启（兼容旧命令）

说明：
- `config.json` 只负责“运行配置偏好”，脚本会把它转换成 CMake 编译开关。
- Cubism SDK 链接是编译期行为，因此底层仍由 CMake 选项控制。

## 配置（Debug，手动依赖）

默认自动读取 `config/config.json`：

```powershell
.\scripts\configure_insiders.ps1 -Config Debug
```

强制开启 Live2D：

```powershell
.\scripts\configure_insiders.ps1 -Config Debug -Live2D On
```

## 构建（Debug，手动依赖）

默认自动读取 `config/config.json`：

```powershell
.\scripts\build_insiders.ps1 -Config Debug
```

强制关闭 Live2D：

```powershell
.\scripts\build_insiders.ps1 -Config Debug -Live2D Off
```

## 一键运行（配置 + 构建 + 启动）

默认自动读取 `config/config.json`：

```powershell
.\scripts\run_insiders.ps1 -Config Debug
```

运行并传递程序参数示例：

```powershell
.\scripts\run_insiders.ps1 -Config Debug -- --debug
```

## 覆盖 Cubism SDK 路径

如果 SDK 不在默认目录，手动指定：

```powershell
.\scripts\build_insiders.ps1 -Config Debug -Live2D On -CubismSdkPath "D:\你的路径\CubismSdkForNative"
```

## Live2D 严格校验

当 Live2D 为 ON 时，CMake 会严格检查：

- `Core/include/Live2DCubismCore.h`
- `Framework/src/CubismFramework.cpp`
- `Core/lib/windows/x86_64/` 下的 x64 Core `.lib`

任意一项缺失会直接 `FATAL_ERROR` 失败。

## 可选：使用 vcpkg 模式

```powershell
.\scripts\configure_insiders.ps1 -Config Debug -UseVcpkg
.\scripts\build_insiders.ps1 -Config Debug -UseVcpkg
```

## 运行测试

```powershell
ctest --preset insiders-debug
```

默认会排除带 `integration` 标签的测试。

## 不用脚本，直接启动 EXE

直接启动 `mikudesk.exe` 前，请先设置环境变量：

```powershell
$qt_root = "D:\qt\6.10.1\msvc2022_64"
$deps_root = "D:\CPPcode\AiDeskPetMan\build\insiders-debug\vcpkg_installed\x64-windows"
$env:PATH = "$qt_root\bin;$deps_root\bin;$deps_root\debug\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt_root\plugins"
```
