# Silkstride · 丝行

A tiny articulated spider that wanders your Windows desktop.
一只会在 Windows 桌面自主游走、可以拎起脚尖的关节蜘蛛。

[English](#english) · [简体中文](#简体中文) · [Download / 下载](https://github.com/lisniuse/silkstride/releases/latest) · [MIT](LICENSE)

<p align="center"><img src="docs/preview.png" alt="Silkstride desktop spider / 丝行桌面蜘蛛" width="360"></p>

## English

Silkstride is a native **C++17 / Win32 / GDI+** desktop pet for Windows 10/11 x64.
Eight articulated legs, Verlet physics when picked up, natural banded coloring,
optional rainbow cycling, and a small behavior system bring it to life. No browser engine, third-party runtime, or admin
rights required. Startup with Windows is enabled on the first normal launch.
Rendering runs without an application frame cap; motion and physics use fixed
120 Hz updates so faster rendering does not speed up the creature. Pausing waits
for input instead of continuously rendering an unchanged frame.

### Download and run

Download `Silkstride-windows-x64.zip` from [Releases](https://github.com/lisniuse/silkstride/releases/latest),
extract it, and launch `Silkstride.exe`. The release executable is unsigned.
Look for its spider icon in the system tray, including the hidden-icons overflow.
Only one instance runs at a time.

When launched from a terminal job that terminates its children on cleanup,
Silkstride relaunches independently if the host explicitly permits Windows job
breakaway. This prevents a terminal cleanup from taking the pet down with it.
Ordinary diagnostics stay synchronous. If the host forbids breakaway, the app
respects that policy; launch it from Explorer instead. The runtime log records
job flags, relaunch origin, and any detachment error. A host may impose further
job limits; for development launches, prefer `pwsh -NoProfile -File ./run.ps1`,
which asks the Windows shell to start the pet instead of a terminal launcher.

**Recycle Bin feeding permanently deletes real files. It is enabled by default.**
To disable it, uncheck **Eat Recycle Bin files (permanently delete one at a time)**
in the tray menu. Use disposable files when trying this feature.

### What it does

- **Smooth body, independent steps.** Six simulated sections per leg: a combined coxa/trochanter
  root, femur, short patella, tibia, metatarsus, and tarsus. The torso follows a
  smooth path without step-induced bobbing or yaw. Planted feet stay still as
  the legs extend or retract; approaching the reach limit triggers another step.
  Turns reposition feet in sequence while planted contacts stay fixed; the torso
  turns with the steps instead of rotating the entire frozen pose.
  Walking uses variable planar projections to suggest out-of-plane flexion;
  grabbing and throwing retain fixed nominal bone lengths and free-body physics.
  Leg pairs I–IV use approximately **1.00 : 0.83 : 0.48 : 0.83** lengths, inspired
  by adult female *Nephila pilipes*. The front and rear pairs extend lengthwise;
  the short third pair takes smaller steps. This remains a planar approximation.
  Proportions follow [Robinson & Robinson (1973), Table 1](https://doi.org/10.5479/si.00810282.149)
  and [Harvey et al. (2007)](https://doi.org/10.1071/IS05016); resting angles are artistic choices.
  Angular lanes keep exposed legs apart during walking and turning. The abdomen
  has an oval, rounded triangular outline tapering toward the tail.
  Lateral roots open into a forward V, a short side pair, and rearward hind legs.
  Side constraints and segment-to-head clearance also apply while being dragged.
  Soft body and leg shadows add depth; lifted feet and a held body cast fainter,
  more distant shadows.
  Foot placement preserves extension, and distal joint limits prevent hooked feet.
  Continuous contacts replace grid snapping and per-step random jitter. Small
  corrective steps are skipped, and foot lift is visual height rather than a
  sideways force on the body; smooth swing curves ease takeoff and landing.
- **Natural regional colors by default.** A dark brown head/chest, a lighter
  patterned abdomen, and brown legs with dark bands. Enable rainbow cycling in
  the tray for synchronized changing colors; turning it off restores natural
  colors. Each launch starts with the natural palette.
- **Can be picked up.** Hold any foot tip with the left mouse button. The other
  legs flail while the body hangs from your pointer. Release gently or fling it;
  it lands and recovers its footing. Other desktop clicks pass through.
- **Has a daily rhythm.** Explores, observes, approaches cautiously, retreats from
  fast incoming mouse movement, rests in corners, and grooms its legs. When startled, it
  rapidly escapes away from the approaching cursor. Ordinary wandering turns
  toward its destination before moving forward.
  Free exploration uses short walking bouts with varied pace, smooth acceleration
  and deceleration, and brief inspection pauses; escape and feeding keep their own pace.
  Each new walking bout randomly selects a slow, medium, or fast pace
  (about 75, 195, or 450 pixels/second at the default speed). Hunger does not
  affect movement speed or tier selection.
  While stopped, any of the eight legs can probe, stretch, or tap at random
  intervals and amplitudes, without repeating the same leg consecutively.
  Attention and approach navigation use current cursor observations without an artificial delay.
  A startle immediately begins a faster stepping turn toward the escape route,
  then accelerates along the head direction without sideways sliding. It replans when the cursor continues approaching,
  and chooses a distant corner when the screen edge blocks a direct escape.
  Its escape speed is twice the original speed.
- **Remembers interactions.** Personality, energy, trust, fear, hunger, and visited
  regions persist across restarts. It gradually calms down after being handled.
- **Gets hungry.** A satiated body is glossy and nearly black, ordinary hunger
  restores its natural colors, and severe hunger fades it toward gray without
  slowing movement. A hungry spider can
  approach a visible Recycle Bin filename and chew before consuming that file.
- **Speaks English and Chinese.** The initial language follows Windows UI language
  (Chinese for Chinese locales; English otherwise). Change it in
  **Language / 语言 → English / 简体中文**; your choice is saved immediately.

### Controls

| Action | Control |
| --- | --- |
| Pick up / drag | Left-drag any foot tip |
| Throw / put down | Release the left mouse button |
| Put down without throwing | `Esc` while holding |
| Pause / resume | `Ctrl+Alt+S`, or double-click the tray icon |
| Quit | `Ctrl+Alt+Q`, or the tray menu |
| Speed, size, rainbow, mouse following, monitor | Right-click the tray icon |
| Show / hide hunger panel | Tray → **Show hunger panel** |
| Change language | Tray → **Language / 语言** |
| Start with Windows | Tray → **Start with Windows** (enabled on first launch) |

The hunger panel starts **hidden on every launch**. The tray tooltip and menu
still show hunger and energy. Hiding the panel does not pause feeding.
Speed, size, pause, and panel visibility reset on launch; language, personality,
memory, hunger, and the feeding toggle persist.

### Start with Windows

The first normal launch (including the first launch after upgrading from v0.1.0)
adds a per-user Windows sign-in startup entry. Uncheck **Start with Windows** in
the tray menu to remove it. Later launches respect your choice and do not add it
back. Diagnostic modes never configure real startup entries.

Registration uses the `Silkstride` value under
`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`; a first-run
marker is stored under `HKEY_CURRENT_USER\Software\Silkstride`. No administrator
rights are needed. Keep the executable at its registered location; after moving
it, toggle startup off and on to update the path. Before removing the portable
app, turn startup off to remove its entry. Windows may delay startup or override
it through its own startup-app controls.

### Hunger and feeding

Hunger ranges from 0 to 100 and starts at 20 for a new pet. A full meal leaves
about **1–1.5 active hours** of satiety (below 25), with a dark, glossy body.
Ordinary hunger (25–80) lasts another **3.5 active hours**, with natural body colors.
At 80 it seeks food and begins fading toward gray; 80–100 takes another hour.
Activity and rest use the same timing. Hunger does not rise while paused or
closed and never reduces movement speed. Eating continues until hunger is 5 or lower.

It only targets a visible, exposed, genuine Windows Recycle Bin folder view on
the pet's monitor. It locates an actual filename, walks close, and chews for about
4 seconds. It first steps to face the filename, then moves forward along its
head direction, realigning when the target changes. Each confirmed permanent
deletion reduces hunger by 15 points. It
rechecks file identity, visibility, position, and body distance before deleting;
scrolling, sorting, or moving the window makes it locate the target again.

Directories are excluded. It never empties the whole bin, recursively deletes
folders, reads file contents, or executes files. Ambiguous, unavailable, or failed
targets are skipped. Closing, minimizing, covering, or switching away from the
view, pausing, picking up the pet, or disabling feeding cancels pending feeding.
A deletion that has already completed cannot be undone.

### Local data

The app uses these files under `%LOCALAPPDATA%\NeonSpider\`:

| File | Contents |
| --- | --- |
| `creature.state` | Personality, needs, interaction memory, hunger, feeding toggle |
| `ui.settings` | Chosen interface language |
| `runtime.log` | Startup, exit reasons, exceptions, and a one-minute health heartbeat |
| `runtime.previous.log` | Previous log after rotation |

`NeonSpider` is the pre-release data-directory name, retained to preserve existing
pets. The app works locally and has no telemetry or network dependency. It samples
mouse movement for behavior and uses Shell/UI Automation to locate visible
Recycle Bin items. Personal state and test outputs are excluded from this repo.

If the pet disappears, check `runtime.log`. Normal exits distinguish the tray
menu, `Ctrl+Alt+Q`, window-close requests, and Windows session ending. Unhandled
exceptions record their code and address; C++ termination is logged too. If a
session lacks its final exit record, the next launch flags it as incomplete.
This can indicate a crash, forced termination, power loss, or another abrupt
stop; the marker alone cannot determine which. Some fail-fast failures can bypass
exception handlers. Timestamps use UTC (`Z`; add 8 hours for China time).
Logs do not record mouse trails, file contents, or the names of eaten files.
A log of 1 MiB or more rotates on launch, retaining one previous file.

### Build

Install **Visual Studio 2022** with **Desktop development with C++**, a Windows
SDK, and **PowerShell 7**. No external C++ libraries are needed.

```powershell
git clone https://github.com/lisniuse/silkstride.git
cd silkstride
pwsh -NoProfile -File ./build.ps1
pwsh -NoProfile -File ./run.ps1
```

The script builds x64 Release with a static C++ runtime. CMake 3.20+ is also
supported:

```powershell
cmake -S . -B out/cmake -G "Visual Studio 17 2022" -A x64
cmake --build out/cmake --config Release
./out/cmake/Release/Silkstride.exe
```

### Diagnostics and contributing

`build/` contains the runnable executable; compiler objects go to `out/obj/`.
Exit the running pet before diagnostics, then run:

```powershell
pwsh -NoProfile -File ./test.ps1
# Or select a suite:
pwsh -NoProfile -File ./test.ps1 -Suite startup
```

Reports and preview images go to `out/tests/`; the script fails if a diagnostic
fails or does not produce a report. For the manual arguments below, run
`../../build/Silkstride.exe` from `out/tests/` so outputs stay outside `build/`.

| Argument | Coverage |
| --- | --- |
| `--motion-test` | Walking, planted feet, projected leg extension, joint deflection, turn stability, smooth torso trajectory |
| `--interaction-test` | All eight legs at three sizes: drag, flail, throw, land; body response relative to pointer travel |
| `--behavior-test` | Fear, curiosity, rest, memory, two-minute physical simulation |
| `--feeding-test` | Mock feeding, interruption, filename approach, persistence; no real deletions |
| `--i18n-test` | Both catalogs, state labels, locale fallback, preference serialization, panel previews |
| `--startup-test` | First-launch registration, opt-out persistence, quoted paths, isolated registry cleanup |
| `--logging-test` | Durable events, exception formatting, incomplete-session detection, rotation and fixture cleanup |
| `--launch-test` | Owned kill-on-close job, independent diagnostic relaunch, survival after launcher cleanup |
| `--smoke-test` | Real window, tray, and hook; exits after three seconds |
| `--preview` | Export `preview.png` without starting autonomous behavior |
| `--preview-rainbow` | Export `preview-rainbow.png` showing the optional rainbow palette |
| `--input-test` | Moves the cursor and exercises real mouse input; restores cursor position |
| `--recycle-probe` | Read-only view and filename-coordinate inspection |
| `--feeding-canary-test` | Recycles and permanently deletes only its own fresh GUID-named disposable file |

The canary test requires a visible Recycle Bin window, may select/scroll to its
test file, and leaves that file in the bin if validation fails. It never enables
ordinary feeding or submits pre-existing files for deletion. Diagnostic modes do
not load or overwrite pet memory. The startup test uses its own temporary GUID
registry subtree and never modifies real startup entries. Logging tests use
temporary files under `out/tests/`, not personal runtime logs. The launch test
uses only an owned three-second diagnostic window; it skips the breakaway check
if an outer host job explicitly denies it. CI runs the eight
non-destructive suites and uploads the Windows executable and reports.

See [AGENTS.md](AGENTS.md) for contributor conventions. **Commit messages must use
Conventional Commits**, e.g. `feat(i18n): add a translation`.

### Credits and license

The walking mechanics were inspired by [Rainbow Spider by run-time](https://codepen.io/run-time/pen/abYeqZ),
particularly its contact order and Verlet distance/angle constraints. Silkstride
implements these ideas in native C++ with desktop input, autonomy, persistence,
and Shell integration. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Released under the [MIT License](LICENSE).

## 简体中文

丝行（Silkstride）是一个原生 **C++17 / Win32 / GDI+** 桌面宠物，支持 Windows
10/11 x64。八条六段关节腿，抓起时使用 Verlet 物理系统，默认自然分区配色和腿部环纹，
也可切换循环彩虹配色，具有自主行为。
无需浏览器引擎、额外运行库或管理员权限。首次正常运行默认加入开机启动。
渲染不设程序帧数上限；动作和物理按固定 120 Hz 更新，帧率提高不会让蜘蛛动作加速。
暂停时等待输入，不持续重复渲染静止画面。

### 下载与运行

从 [Releases](https://github.com/lisniuse/silkstride/releases/latest) 下载
`Silkstride-windows-x64.zip`，解压后运行 `Silkstride.exe`。发布程序未做数字签名。
右下角系统托盘中有蜘蛛图标，可能收在“隐藏的图标”中。程序同时只运行一个实例。

从“清理时结束全部子进程”的终端任务组启动时，如果宿主明确允许 Windows 的任务组脱离，
程序会重新独立启动，避免终端清理时连带退出。普通诊断模式仍同步运行。宿主不允许脱离或
重新施加限制时会尊重限制，可改从资源管理器启动。运行日志记录任务组标志、独立启动来源及
失败代码。开发时优先使用 `pwsh -NoProfile -File ./run.ps1`，由 Windows Shell 启动宠物，
避免直接依附于终端启动器。

**回收站进食会永久删除真实文件，默认开启。** 可在托盘取消勾选
**回收站进食（逐个永久删除文件）**。体验此功能时请使用一次性测试文件。

### 功能

- **身体平稳、腿独立迈步**：每条腿模拟六段：合并基节与转节的腿根、腿节、短膝节、胫节、
  后跗节和跗节。身体沿平滑路线移动，不随换脚上下起伏或左右晃动。落地脚点保持固定，
  转身时依次抬脚重新落位，身体配合换脚转向，不再把整套静止姿势一起旋转。
  腿随身体前进伸展或收拢，接近活动范围边缘时再迈步。行走时通过平面投影长度变化表现
  腿向屏幕外屈伸；抓起和甩动时仍采用固定标称骨长与自由身体物理。第一至第四对腿的
  长度约为 **1.00∶0.83∶0.48∶0.83**，参考成年雌性大木林蛛（*Nephila pilipes*）；
  前后两对沿身体纵向伸展，短第三对使用较小步幅。这仍是平面简化模型。
  比例依据 [Robinson 夫妇（1973）表 1](https://doi.org/10.5479/si.00810282.149)
  和 [Harvey 等（2007）](https://doi.org/10.1071/IS05016)，静止朝向属于造型设计。
  行走与转向时，通过各腿的角度活动范围避免外露腿段交叉；腹部为椭圆感的圆角三角轮廓，尾端收尖。
  腿根从头胸部两侧向外伸展，前腿呈 V 形，短腿居侧，后腿向后伸；拖拽时也约束左右侧归属和整段腿与头部的间距。
  身体与八条腿带柔和投影，抬脚或被抓起时阴影变淡、拉开距离，增强离地感。
  落脚点保留伸展距离，并限制末端关节折角，避免后腿折回成钩。
  使用连续落脚点，取消网格吸附和逐步随机抖动，跳过过小的修正步；抬脚只表现为视觉高度，
  不再横向拉扯身体，起落采用平滑曲线。
- **默认自然分区配色**：头胸部深褐色，腹部为较浅的暖褐色并带细纹，腿部有深色环带。
  托盘可开启彩虹循环，使全身同步变色；关闭后恢复自然色。每次启动默认使用自然配色。
- **抓脚、挣扎和甩动**：左键按住任意脚尖，身体悬挂，其他七条腿自由挣扎。轻放或甩出后，
  它会落地并恢复支撑。其他桌面点击正常穿透。
- **自主生活节奏**：探索、停下观察、好奇靠近、躲避快速逼近的鼠标、去角落休息、整理腿脚。
  自由探索采用长短、快慢不同的短段行走，平滑起步和减速，中间短暂停下观察；逃跑和进食节奏独立。
  每次起步随机选择慢、中、快三档，默认速度下约为每秒 75、195、450 像素。
  饥饿不影响移动速度或档位选择。
  停下时八条腿都可能随机试探、伸展或轻点，间隔与幅度各异，不连续选中同一只脚。
  随机游走先转向目标再向前走；受惊时立即用更快的换脚动作转向远离鼠标的路线，
  对准后沿头部方向加速，逃跑中重新规划路线也先调整朝向，避免身体侧向滑动。
  注视和靠近使用当前鼠标位置，不设置人工延迟。鼠标继续逼近时重新规划逃跑路线，
  屏幕边缘挡住直接逃跑路线时改选远处角落。逃跑速度保持为最初速度的两倍。
- **性格与互动记忆**：性格、精力、信任、警觉、饥饿和探索记录跨重启保留，被抓后会逐渐平静。
- **饥饿与进食**：饱足时身体偏黑、带光泽，普通饥饿恢复自然颜色，特别饥饿才逐渐褪色偏灰；腿部颜色不变，也不降低行动速度。仍可以爬到回收站里实际可见的文件名附近咀嚼并吃掉文件。
- **中英文界面**：首次按 Windows 界面语言选择（中文系统使用简体中文，其余使用 English），
  可在 **Language / 语言 → 简体中文 / English** 切换，立即保存偏好。

### 操作

| 操作 | 方式 |
| --- | --- |
| 拎起、拖动 | 左键按住任意脚尖 |
| 甩出、轻放 | 松开左键 |
| 不带甩动地放下 | 抓住时按 `Esc` |
| 暂停、继续 | `Ctrl+Alt+S` 或双击托盘图标 |
| 退出 | `Ctrl+Alt+Q` 或托盘菜单 |
| 速度、大小、彩虹配色、跟随鼠标、切换屏幕 | 右键托盘图标 |
| 显示、隐藏饥饿面板 | 托盘 → **显示饥饿提示面板** |
| 切换语言 | 托盘 → **Language / 语言** |
| 开机启动 | 托盘 → **开机启动**（首次运行默认开启） |

饥饿面板**每次启动默认隐藏**，托盘提示和菜单仍可查看饥饿与精力。隐藏面板不影响进食。
速度、大小、暂停和面板显示状态在启动时重置；语言、性格、记忆、饥饿和进食开关会保留。

### 开机启动

首次正常运行（包括从 v0.1.0 升级后第一次运行）会自动添加当前用户登录 Windows 时的
启动项。右键托盘取消勾选 **开机启动** 即可移除；之后再次运行会尊重选择，不会自动加回来。
所有诊断模式都不会配置真实启动项。

启动项为注册表 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`
下的 `Silkstride` 值，首次配置标记保存在 `HKEY_CURRENT_USER\Software\Silkstride`，
无需管理员权限。请保持程序所在位置不变；移动后关闭再开启此选项即可更新路径。删除便携版
程序前可先关闭此选项以移除启动项。Windows 可能延后启动，系统自己的启动应用设置也可能
覆盖此行为。

### 饥饿与回收站进食

饥饿值为 0～100，新宠物初始为 20。吃饱后保持约 **1～1.5 小时**的饱足状态（低于 25），
身体偏黑、带光泽；普通饥饿阶段（25～80）再持续约 **3.5 小时**，恢复自然颜色。
达到 80 后开始寻找食物，并逐渐褪色偏灰；80 增至 100 还需约 1 小时。
只计算宠物运行且未暂停的时间，活动与休息采用相同节奏；关闭时不增长。
饥饿不降低移动速度，进食仍持续到饥饿不高于 5。

它只会寻找所在屏幕上可见、未被遮住的真实 Windows 回收站视图。先定位文件名，踩步转到面朝文件，
再沿头部方向靠近并停稳；目标变化时也先重新对准，避免侧滑。
咀嚼约 4 秒。每成功永久删除一个文件，饥饿减少 15 点。删除前再次核对文件身份、可见性、
位置和身体距离；滚动、排序或移动窗口时会重新定位。

不吃文件夹，不清空整个回收站，不递归删除目录，不读取文件内容，不执行文件。无法明确定位、
无法访问或处理失败的条目会跳过。关闭、最小化、遮住或切走回收站视图，暂停、抓走蜘蛛或
关闭进食开关，会中止尚未执行的进食。已经完成的永久删除无法撤回。

### 本地数据

存档位于 `%LOCALAPPDATA%\NeonSpider\`：

| 文件 | 内容 |
| --- | --- |
| `creature.state` | 性格、需求、互动记忆、饥饿和进食开关 |
| `ui.settings` | 界面语言偏好 |
| `runtime.log` | 启动、退出原因、异常和每分钟运行状态 |
| `runtime.previous.log` | 轮换时保留的上一份日志 |

`NeonSpider` 是早期版本的数据目录名，为保留原有宠物继续使用。程序在本地运行，无遥测或
联网依赖；采样鼠标移动用于行为判断，通过 Shell 与 UI Automation 定位可见回收站条目。
个人存档和测试输出不进入源码仓库。

如果宠物突然消失，可以查看 `runtime.log`：正常退出会区分托盘菜单、`Ctrl+Alt+Q`、
窗口关闭请求和 Windows 会话结束。未处理异常记录异常代码和地址，C++ 异常终止也会记录。
上次运行缺少结束记录时，下次启动会标记为“上次会话未完成”；这可能来自崩溃、强制结束、
断电等，不能单凭此标记确认具体原因。部分快速失败异常可能绕过异常处理器。
时间使用 UTC（末尾 `Z`，北京时间需加 8 小时）。不记录鼠标轨迹、文件内容或吃掉的文件名。
日志达到 1 MiB 时在启动时轮换，保留一份旧日志。

### 编译

安装 **Visual Studio 2022** 的“**使用 C++ 的桌面开发**”组件、Windows SDK 和
**PowerShell 7**。无需额外 C++ 依赖。

```powershell
git clone https://github.com/lisniuse/silkstride.git
cd silkstride
pwsh -NoProfile -File ./build.ps1
pwsh -NoProfile -File ./run.ps1
```

脚本生成 x64 Release，静态链接 C++ 运行库。也支持 CMake 3.20+：

```powershell
cmake -S . -B out/cmake -G "Visual Studio 17 2022" -A x64
cmake --build out/cmake --config Release
./out/cmake/Release/Silkstride.exe
```

### 验证与贡献

`build/` 只放可运行程序，编译中间文件放在 `out/obj/`。先退出运行中的宠物，再执行：

```powershell
pwsh -NoProfile -File ./test.ps1
# 也可只选择一组：
pwsh -NoProfile -File ./test.ps1 -Suite startup
```

报告和预览图统一输出到 `out/tests/`，测试失败或没有生成报告时脚本报错。手动使用下列
参数时，也请从 `out/tests/` 运行 `../../build/Silkstride.exe`，避免污染 `build/`。

| 参数 | 验证内容 |
| --- | --- |
| `--motion-test` | 步态、脚点固定、腿投影伸缩、关节转折、转向稳定、身体轨迹平滑 |
| `--interaction-test` | 八条腿 × 三种大小：拖拽、挣扎、甩动和落地；身体相对鼠标行程的响应 |
| `--behavior-test` | 警觉、好奇、休息、记忆和两分钟真实物理模拟 |
| `--feeding-test` | 模拟进食、中断、靠近文件名和存档；不删除真实文件 |
| `--i18n-test` | 中英文文案、行为标签、语言回退、偏好序列化和面板预览 |
| `--startup-test` | 首次注册、关闭后不重建、路径引号和隔离注册表清理 |
| `--logging-test` | 事件落盘、异常格式、未完成会话识别、轮换和测试文件清理 |
| `--launch-test` | 隔离的“关闭即结束进程”任务组、独立启动、宿主清理后仍存活 |
| `--smoke-test` | 实际窗口、托盘与钩子，三秒后退出 |
| `--preview` | 导出 `preview.png`，不启动自主行为 |
| `--preview-rainbow` | 导出可选彩虹配色的 `preview-rainbow.png` |
| `--input-test` | 短暂移动鼠标，验证真实输入，结束后恢复光标 |
| `--recycle-probe` | 只读检查实际回收站窗口和文件名坐标 |
| `--feeding-canary-test` | 仅回收并永久删除自己新建的 GUID 一次性测试文件 |

Canary 测试要求真实回收站窗口可见，会选中并滚动到自己的测试条目；失败时可能将测试文件
留在回收站。它不会开启日常进食或提交原有文件进行删除。诊断模式不加载或覆盖宠物存档。
开机启动测试使用自己临时创建的 GUID 注册表子树，不修改真实启动项。日志测试在
`out/tests/` 使用临时文件，不修改个人运行日志。启动生命周期测试只操作自己的三秒诊断窗口；
外层宿主明确禁止脱离任务组时会跳过对应检查。CI 运行八组
无破坏性的检查，并上传 Windows 程序和报告。

贡献约定见 [AGENTS.md](AGENTS.md)。**提交信息必须使用 Conventional Commits（CC）风格**，
例如 `feat(i18n): add a translation`。

### 致谢与协议

行走机制参考 [run-time 的 Rainbow Spider](https://codepen.io/run-time/pen/abYeqZ)，
尤其是换脚顺序和 Verlet 距离、角度约束。丝行使用原生 C++ 实现，并加入桌面交互、自主行为、
记忆和 Shell 集成。详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

本项目采用 [MIT 协议](LICENSE)。
