# MaaEnd TrayFix

一个用于修复 **MaaEnd 退出后 Windows 通知区域残留“幽灵”托盘图标** 的外置补丁。

当 MaaEnd 使用“结束自身”等方式退出时，托盘图标有时不会立即从 Explorer 通知区域移除，需要鼠标划过后才消失。本项目通过一个放置在 `MaaEnd.exe` 同级目录的 `powrprof.dll` 兼容代理捕获托盘图标注册信息，并由独立的 `rundll32.exe` 监视器在 MaaEnd 进程结束后主动执行 `Shell_NotifyIconW(NIM_DELETE)`。

## 使用

1. 完全退出 MaaEnd。
2. 从 [Releases](../../releases/latest) 下载 `powrprof.dll`。
3. 将 `powrprof.dll` 放到 `MaaEnd.exe` 同级目录。
4. 正常启动 MaaEnd，无需额外配置或启动参数。

卸载时，完全退出 MaaEnd 后删除 `powrprof.dll` 即可。

## 兼容性

- Windows x86_64
- 构建时自动读取当前 Windows `System32\PowrProf.dll` 的全部命名导出并生成代理跳板。
- 所有 PowrProf 调用继续转发到真正的系统 DLL，避免破坏 MaaEnd、WebView2 以及 NVIDIA 用户态驱动对 PowrProf API 的依赖。
- CI 会验证完整 PowrProf 导出兼容、应用目录 DLL 加载、`GetPwrCapabilities` / `PowerDeterminePlatformRole` 等 NVIDIA 相关接口，以及退出后的托盘清理链路。

## 实现概览

`MaaEnd.exe` 会从应用目录加载 `powrprof.dll`。本项目的代理 DLL：

1. 镜像系统 `PowrProf.dll` 的命名导出；
2. 延迟解析并转发到 `C:\Windows\System32\powrprof.dll`；
3. 对主进程的 `Shell_NotifyIconW` IAT 进行最小替换，记录 `NIM_ADD` 对应的 `HWND/uID`；
4. 启动系统 `rundll32.exe` 作为独立监视器；
5. MaaEnd 退出后由监视器发送 `NIM_DELETE`，清理残留托盘图标。

## 构建

需要 Windows x64 + MSVC。GitHub Actions 会自动生成 PowrProf 代理导出面、编译 DLL，并运行兼容性测试。

> 本项目是针对 MaaEnd 的第三方外置补丁，不属于 MaaEnd 官方项目。
