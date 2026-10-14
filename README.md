# QEMU 3dfx WinHelper

WinUI 3 / C++ 图形管理器，当前版本 0.3.0，随包提供本目录的 QEMU 3dfx 11.1.2 x64 构建。宿主机是 Windows 10 2004+ / Windows 11 x64；Windows XP 是虚拟机内的客体系统。新版使用 `qemu-system-x86_64w.exe`，现有 32 位 XP 可继续使用。

## 使用

运行便携目录中的 `qemu-3dfx-winhelper.exe`。整个文件夹一起搬迁，不要只复制 EXE。无需 Python、MSYS2、.NET、管理员权限或预装 Windows App SDK；便携发布目录自带 WinUI 和 Visual C++ 运行库。

1. 完整解压 `dist.zip`，双击根目录唯一的 `qemu-3dfx-winhelper.exe`。运行文件和固件已经在发布时配置并检查，无需环境设置。
2. “新建”创建按需增长的 qcow2 磁盘，默认 32 GiB、1 GiB 内存、2 核和 TCG。选择自己的 XP 安装 ISO。安装光盘不放进发布包。
3. 勾选“仅本次优先从光盘启动”，随后启动虚拟机。硬件表单会在启动时保存。声卡只支持 AC97，可通过“AC97 声卡”开关关闭。
4. “鼠标”开关选择 USB 或 PS/2，运行中切换立即生效并保存；关机时随硬件配置保存。选择“工具盘”，再点击“挂载 / 保存光盘”，可以使用根目录 `guest-tools.iso` 中的 3dfx / OpenGL 客体工具和测试程序。AC97 驱动需按客体系统单独准备。
5. 在“关机快照”创建、恢复、删除快照。运行或暂停的虚拟机不允许做快照操作。正常关机按钮发送 ACPI 请求，须等待客体实际退出；无响应时可在客体内关机。

“导入磁盘”将 qcow2、raw/img、vmdk、vdi、vhd、vhdx 的当前磁盘内容转换成新的独立 qcow2，保留原文件，不导入旧快照历史。“导出当前磁盘”创建独立备份，不包含内部快照历史；拒绝覆盖已有目标。移除列表项只停用 `machine.json`，保留磁盘与快照；把 `machine.removed-*.json` 改回 `machine.json` 可恢复列表项。

QEMU 以独立窗口运行；关闭管理器不会关闭客体，再次打开可以继续管理。支持同时运行多台虚拟机、运行中挂载或弹出 ISO。QMP 仅监听 127.0.0.1，端口自动分配，命令执行前核对实例名称。

根目录启动程序将自身所在目录传给 WinUI 主体作为工作区，路径不依赖当前工作目录。开发/测试时也可以指定完整工作区：

```powershell
.\qemu-3dfx-winhelper.exe --workspace D:\MyVMs
```

## 文件与迁移

```text
qemu-3dfx-winhelper.exe        带图标的原生入口，无需旁置 DLL
guest-tools.iso               客体工具盘，与主程序放在一起
使用说明.txt
bin/                         QEMU 最小运行集合
  share/                     3 个 PC BIOS 文件
  winui/                     WinUI 主体、WinUI/VC++ 运行库及资源
  qemu-helper-cli.exe         发布检查与开发用命令行工具
  docs/、licenses/            技术说明、许可证
machines/<id>/
  machine.json               硬件、ISO、唯一 MAC
  disk.qcow2                 硬盘与内部快照
  snapshot-labels.json        中文快照显示名
  runtime.json               可核对创建时间的进程身份
  firmware/                  自动生成的 3 个固件缓存
  media/                     自动生成的 ISO 别名缓存
  OpenGLid.ini                首次启动复制的 OpenGlide 配置，可按虚拟机调整
  logs/                      每次启动单独的 QEMU 日志
```

关机后复制整个工作区即可；目录外的 ISO 或程序仍是绝对路径，需要重新选择。复制整个 `machines/<id>` 目录可保留磁盘、快照及名称。跨平台 GUI 尚未实现。

升级时先关闭虚拟机，将原来的 `machines` 文件夹复制到新版目录；如有自定义配置，也可复制 `workspace.json`。已知旧版默认 QEMU 和工具盘路径自动适配新版，自定义路径保持原样。机器型号仍固定为 `pc-i440fx-9.2`，避免升级程序时改变已有 XP 的虚拟硬件。旧版创建的 qcow2 磁盘和关机快照已做恢复兼容测试。

为兼容 MinGW 构建的路径处理，管理器自动在虚拟机目录下准备英文相对路径，ISO 同一文件系统优先用硬链接，不增加数据副本；跨盘或不支持硬链接时复制 ISO。换盘成功后清理旧的自有缓存。不要在客体运行时修改源 ISO。缓存可在关机后删除；下次启动会重新生成。

## 源码目录与 Git

源码目录与发布目录的布局不同：

| 路径 | 内容及提交方式 |
| --- | --- |
| `src/`、`tools/`、`tests/` | 界面、核心逻辑、构建/发布脚本和测试 |
| `runtime-binary/` | 上游 QEMU EXE、DLL、固件及配置，按原始字节提交普通 Git |
| `guest-tools.iso` | 客体工具盘，按二进制提交，发布时放在主程序旁边 |
| `support/` | 支持的安装 ISO 与辅助 EXE/DLL，按二进制提交；本机 CA 私钥、证书及安装笔记忽略 |
| `reference/` | 原始探索脚本，留作参考；当前构建与发布使用 `tools/` |
| `docs/`、`licenses/`、`third_party/` | 文档、上游运行集合的审计清单、许可证和第三方源码 |

`tools/audit-runtime.py` 默认审计 `runtime-binary/`，打包脚本从此处裁剪运行集合并写入产物的 `bin/`。产物中的 `bin/winui/` 和其他目录不变；源码目录不直接作为最终用户工作区使用。

`.gitignore` 排除构建输出、NuGet 缓存、`.tools/`、`dist/`、根目录发布 ZIP、虚拟机、日志、个人工作区配置和自动生成的测试截图，文件仍保留在本机。`docs/runtime-audit.json` 保留以记录上游文件哈希。`.gitattributes` 将上游运行文件和 ISO 标为二进制，避免换行转换，不使用 Git LFS。

## 构建与测试

开发机需要 Visual Studio 2022 / 2026 的 C++ 桌面工具、Windows 10/11 SDK、CMake；首次构建联网恢复固定版本的 NuGet 包。打包和集成测试额外使用 Python 3，最终用户无需 Python。

```powershell
.\tools\build.ps1
python tools\audit-runtime.py --stage .tools\runtime-11.1.2 --output docs\runtime-audit.json
python tools\smoke-runtime.py --runtime .tools\runtime-11.1.2
python tests\integration.py
.\tools\package.ps1 -Destination .\dist\qemu-3dfx-winhelper-0.3.0 -IncludeToolsIso -Archive .\dist-0.3.0.zip
```

`audit-runtime.py --stage` 和打包脚本只接受尚不存在的输出目录/压缩包，不删除旧目录。已有验证目录可直接复用；QEMU 文件变化后请为审计选择新的输出目录。打包时自动验证 QEMU、qemu-img、固件和 XP 预设。集成测试从 `.tools/runtime-11.1.2` 创建临时副本，覆盖中文/空格/逗号路径、多实例、USB/PS2 热切换、换盘、快照保护、真实扇区恢复、导出和迁移，不安装 XP。可用 `--runtime` 和 `--cli` 指定待测发布文件；追加 `--legacy-runtime .tools/minimal-runtime` 可验证旧版 9.2.2 创建的磁盘、快照及配置迁移（需保留旧版运行集合）。

核心还可单独构建并通过 `build/core/Release/qemu-helper-cli.exe` 调用。GUI 和 CLI 共享同一套 C++ 逻辑。GUI 的原生桌面 XAML 编译已显式配置，不需要 UWP 工作负载。

图标源文件位于 `src/assets/winhelper.svg`，ICO 包含 16–256 像素九种尺寸，并嵌入根目录入口和 WinUI EXE。需要调整图标时运行 `tools/make-icon.ps1`；普通构建直接使用已有 ICO。

详细说明：[最小运行集合](docs/minimal-runtime.md) · [快照取舍](docs/snapshots.md) · [架构与移植](docs/architecture.md) · [0.3.0 兼容性验证](docs/compatibility-0.3.md)。

## 当前范围

- 固定 i440FX 9.2、Cirrus、IDE、AC97、PCnet 的 XP 预设，优先兼容已有脚本。WHPX 可选择，但需要宿主机提供相应功能；默认 TCG 已验证。
- 本机已验证 XP 安装程序启动、已有 XP 副本进入桌面、SDL/AC97/PCnet/USB 初始化、QMP 换盘和旧版关机快照恢复；客体声音输出、新工具盘驱动安装和 3dfx 游戏效果尚未验证。
- 不实现内存快照、外部差分链、多硬盘一致性快照、热插拔硬盘或虚拟机强制进程终止回退。QMP 故障时可使用 QEMU 窗口关闭客体；状态不明时磁盘操作一律拒绝。
- 管理器不会自动下载或替换你定制的 QEMU，不会修改系统 PATH 或 Windows 可选功能。使用中的受管磁盘请由这个管理器统一启动和维护。
