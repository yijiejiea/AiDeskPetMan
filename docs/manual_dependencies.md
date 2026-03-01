# 手动依赖配置说明（不自动安装 vcpkg）

如果你不希望使用 vcpkg 自动安装依赖，请手动准备库文件，并通过
`CMAKE_PREFIX_PATH` 指向依赖根目录。

## 必需依赖库

- `fmt`
- `spdlog`
- `nlohmann_json`
- `Qt6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets)`

## 依赖目录中应包含的 CMake 包配置

你的依赖根目录至少需要包含以下配置文件：

- `share/fmt/fmt-config.cmake`
- `share/spdlog/spdlogConfig.cmake`
- `share/nlohmann_json/nlohmann_jsonConfig.cmake`
- `lib/cmake/Qt6/Qt6Config.cmake`

## 典型目录结构

```text
D:\third_party\installed\x64-windows\
  include\
  lib\
  debug\lib\
  bin\
  debug\bin\
  share\fmt\
  share\spdlog\
  share\nlohmann_json\
  lib\cmake\Qt6\
```

## 手动模式：配置与构建

```powershell
.\scripts\configure_insiders.ps1 -Config Debug
.\scripts\build_insiders.ps1 -Config Debug
```

固定路径位置：
- `scripts/local_paths.ps1` 中的 `DefaultDepsRoot`
- `scripts/local_paths.ps1` 中的 `DefaultQtRoot`

## 运行时 DLL 路径要求

启动程序前，确保以下路径可被系统找到：

- 将 `<DepsRoot>\bin` 加入 `PATH`
- Debug 运行时，如有需要，将 `<DepsRoot>\debug\bin` 加入 `PATH`
- 将 `<QtRoot>\bin` 加入 `PATH`
- 设置 `QT_PLUGIN_PATH=<QtRoot>\plugins`
