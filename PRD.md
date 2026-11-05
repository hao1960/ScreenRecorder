# Windows 录屏学习工具 PRD

状态：需求基线，开发路线待重新调整

> 本文档依据 `E:\Interview_preparation\ScreenRecorder_learning\.scratch\screen-recorder\PRD.md` 整理，保留原文中的产品需求、验收口径与历史规划。计划能力不代表已实现；版本顺序、学习里程碑及开发路线等待重新调整。
>
> 日期：2026-10-26。首版目标平台：Windows 10/11。

> 2026-11-05 补充：音频录制、帧率和编码参数选择均为项目需求。原文的版本划分属于历史规划，具体开发顺序与首版范围待重新确定。

## Problem Statement

学习者希望通过一个能亲眼看到结果的桌面项目练习 Qt、C++ 和 FFmpeg：从界面发起录制、观察状态、停止后得到可播放的视频，并能解释每个模块的职责。现有课程说明把系统音频、暂停、多编码器和跨平台等复杂能力压进七天，还展示了一段在启用音频时参数顺序不正确的 FFmpeg 命令。按该说明直接开发，学习者很难判断什么是首版必须完成的闭环，也缺少可验证的验收标准。

原文编写时学习工作区尚无录屏代码。本项目现在已有录屏原型，实际状态见下节；不能将原文的“从零开始”视为当前代码状态。

### 当前项目状态

- 已有 Qt Widgets 主界面、FFmpeg 进程控制、主屏幕区域选择、保存目录与录制模式记忆，以及部分错误处理。
- 当前 FFmpeg 使用 `gdigrab`、`libx264` 和 MP4；区域选择限于主屏幕。全屏模式使用 FFmpeg 的 `desktop` 输入，尚需对照首版主显示器要求验证实际捕获范围。
- 现有 Debug 配置已构建通过；尚未按本文的 15 秒全屏和区域场景完成录制、播放及媒体信息验收。项目当前没有自动化测试。
- `mainwindow.cpp` 和 `mainwindow.h` 存在未提交修改，包含环境能力检查、错误提示和停止超时处理。以上状态是本次整理时的快照，后续以代码和实际验证为准。

## Solution

交付一个本地运行的 Windows 录屏学习工具。用户通过“录制模式”选择捕获目标：首版（MVP）可选主显示器全屏或一块自定义屏幕区域；V0.3 增加选定应用窗口。全屏和区域模式录下所选屏幕范围内实际显示的内容，固定区域不会自动跟随某个窗口。首版还支持设置保存目录、开始录制，并通过应用内的停止按钮结束录制，得到包含 H.264 视频流的 MP4 文件。界面清楚显示“待机、准备中、录制中、结束中、失败”状态，以及实际输出位置和可理解的失败原因。

### 首版范围和验收

| 能力 | 用户可见行为与验收口径 |
| --- | --- |
| 环境检查 | 启动录制前检查 FFmpeg 可执行文件、`gdigrab` 输入和 H.264 编码器是否可用；缺失时提示原因和处理方向，不进入“录制中”。 |
| 全屏录制 | 用户选主显示器全屏后可录制并停止；输出文件包含视频流，画面尺寸与实际捕获范围一致。 |
| 区域选择 | 用户拖拽形成有效矩形，界面显示尺寸；取消选择不启动录制。选区与录制画面在 Windows 缩放设置下保持一致。 |
| 录制控制 | 重复点击开始不会启动多个 FFmpeg 进程；点击停止后等待视频文件完成写入，再提示成功。 |
| 文件输出 | 用户可设置保存目录；自动生成不重复的文件名，不覆盖现有文件；失败时说明文件是否可用。 |
| 状态与错误 | 启动失败、设备不可用、参数无效、目录不可写、磁盘空间不足或 FFmpeg 异常退出时显示明确错误，并允许重新尝试。 |
| 基础配置 | 保存最近一次有效的保存目录和录制模式；异常或过期配置可回退到安全默认值。 |
| 可复现演示 | 在已配置环境上完成一次 15 秒全屏录制和一次 15 秒区域录制；停止后两段视频均可打开，且状态与文件位置一致。 |

原首版规划仅要求单显示器、无声录制；音频和参数选择现已纳入项目需求，具体版本归属待定。UI、选区与录制命令是学习对象；视觉打磨以清晰可用为准。每个开发里程碑都应可构建并演示，但不承诺零基础七小时完成整套产品。

每个里程碑同时交付可运行版本和学习材料：本阶段目标与前置知识、关键 Qt/C++/FFmpeg 概念、核心流程讲解、可亲手完成的小练习、验收步骤，以及对应的面试复盘。材料必须与该版本实际代码和运行结果一致；不以未验证的“标准答案”代替实测。

### 新增需求：音频、帧率和编码参数

以下能力均需开发；版本归属、优先级及具体选项待重新制定路线时确定。现有源码尚未实现这些选择。

| 编号 | 功能需求 | 验收标准 |
| --- | --- | --- |
| R-A1 | 用户可选择无声录制或启用麦克风，并选择可用的麦克风设备。 | 无声成片不包含音频；启用麦克风后，成片能听到所选设备的输入。 |
| R-A2 | 用户可启用系统声音，且可与麦克风分别开关或同时录制。 | 分别验证仅系统声、仅麦克风、双路和无声四种组合；开启的声音可听，关闭的声音不进入成片；双路播放时音画同步。 |
| R-A3 | 音频设备不可用、权限不足或录制中断时给出可理解的提示，并允许用户调整设置后重试。 | 模拟设备不可用或移除，确认界面报告问题；不能静默将用户选择的有声录制当作成功的无声录制。 |
| R-V1 | 用户可选择录制帧率。 | 不同选项均可开始和停止录制；通过媒体信息检查实际输出帧率，并记录负载可能造成的偏差。 |
| R-V2 | 用户可选择视频编码器及其适用的质量或编码参数。 | 仅提供当前环境支持的编码器；所选参数在启动前校验，成片编码格式与选择一致，不支持的组合有明确提示。 |
| R-V3 | 保存有效的音频、帧率和编码设置，并在下次启动时恢复。 | 重启后设置保留；设备或编码能力变化导致设置失效时提示并允许重新选择。 |

#### 开发任务

- [ ] 音频设备发现与选择：列出可用麦克风，提供关闭音频的选项，处理设备消失和权限问题（R-A1、R-A3）。
- [ ] 系统声音采集与混音：实现系统声和麦克风独立开关及双路混音，验证采集方式在目标 Windows 环境可用（R-A2、R-A3）。
- [ ] 帧率设置：提供帧率选项，校验并传入录制命令（R-V1）。
- [ ] 编码设置：探测可用编码器，提供对应的质量或编码参数，校验并传入录制命令（R-V2）。
- [ ] 设置持久化：保存并恢复有效选择，处理设备及编码器变化（R-V3）。
- [ ] 录制验收：覆盖四种音频组合、不同帧率与编码选项，检查媒体信息、实际声音、音画同步及错误恢复（R-A1～R-A3、R-V1～R-V3）。

待确定：帧率选项及默认值；首批编码器、具体质量或编码参数及默认值；音频输出格式、双路混音细节；设备中断后的处理方式和各任务的版本归属。

### 新增需求：录制前倒计时

用户可开启或关闭录制前倒计时。开启后点击开始，界面显示剩余时间，方便切换到需要录制的内容；关闭后直接进入录制准备流程。倒计时期间可取消，倒计时结束后才启动录制。具体时长、默认设置和版本归属待定。

| 编号 | 验收标准 |
| --- | --- |
| R-C1 | 开启倒计时时显示剩余时间；倒计时结束后才启动录制，实际启动成功后显示录制中。 |
| R-C2 | 倒计时期间取消后回到待机，不启动录制进程、不生成录像；可重新开始。 |
| R-C3 | 关闭倒计时后，点击开始直接进入录制准备流程；全屏和区域模式均可使用。 |
| R-C4 | 倒计时期间重复点击开始不会创建多个倒计时或录制进程；倒计时结束后的启动失败仍显示错误并允许重试。 |

开发任务：

- [ ] 提供倒计时设置、剩余时间显示与取消操作（R-C1～R-C3）。
- [ ] 将倒计时接入录制状态流程，处理重复开始、取消和启动失败（R-C1、R-C2、R-C4）。
- [ ] 验证开启、关闭、取消、重复开始及启动失败，以及全屏和区域录制（R-C1～R-C4）。

### 后续迭代升级

以下版本划分和优先级保留自原 PRD，供后续重新调整开发路线时参考。

| 版本 | 目标 | 新增能力 | 升级完成的判据 |
| --- | --- | --- | --- |
| V0.2 日常可用（首版后优先） | 减少重复设置和录制操作成本 | 麦克风单路录音、帧率与 H.264 质量预设、托盘、应用内快捷键、录制时长与文件大小显示、设置页、录制区域记忆和界面细节优化 | 麦克风可选可关；有声和无声录制均可正确生成文件；重启后有效设置得以恢复。 |
| V0.3 Windows 深化 | 处理真实桌面环境 | 录制模式中增加应用窗口选择与窗口捕获、窗口失效提示、多显示器和不同缩放比例、系统声音回环采集、系统声与麦克风混音、系统级快捷键、悬浮控制栏 | 用户可明确选择全屏、区域或窗口模式；目标窗口移动或被其他窗口遮挡时，成片仍只包含目标内容；目标最小化或关闭时提示并结束录制、保存已录片段；跨屏选区坐标正确；双路音频可听且可分别开关；全局快捷键冲突可提示。 |
| V0.4 录制工作流 | 支持长时间和中断场景 | 暂停／继续（分段录制与合并）、录制恢复策略、异常退出后的残留文件识别、历史录制列表 | 暂停期间不计入成片时长；多次继续后画面和声音衔接且无明显音画错位；异常终止后用户能看见残留文件与恢复建议。 |
| V1.0 分发与性能 | 形成可交付的 Windows 桌面软件 | H.265、VP9、AV1 按实际环境逐步加入；编码能力探测、硬件编码可选、性能基准、安装包、日志诊断与依赖许可说明 | 各编码器仅在当前 FFmpeg 构建支持时显示；安装包在干净测试机上可录制、停止并播放。 |
| 探索阶段 | 深入音视频与平台能力 | 直接使用 FFmpeg 库处理帧；直播推流；macOS/Linux 捕获适配 | 每项单独立需求并验证技术可行性，不作为 V1.0 承诺。 |

迭代顺序以首版稳定性为前提。后续版本不以固定七天工期承诺；发现某项能力的设备或平台兼容性成本超出预期时，保留版本号并调整交付范围。

## User Stories

### 首版

1. As a learner, I want to build and run the application from documented steps, so that I can reproduce each milestone on my Windows machine.
2. As a recorder user, I want to see whether FFmpeg is available before recording, so that I can fix my environment without losing time.
3. As a recorder user, I want to choose the main display, so that I can record a full-screen demonstration.
4. As a recorder user, I want to drag a recording region, so that I can keep unrelated content out of the video.
5. As a recorder user, I want to see the selected region's dimensions, so that I can judge whether it fits my purpose.
6. As a recorder user, I want to cancel region selection, so that an accidental drag does not start a recording.
7. As a recorder user, I want to choose a save directory, so that I know where my recordings will go.
8. As a recorder user, I want each recording to receive a unique file name, so that an earlier recording is preserved.
9. As a recorder user, I want to start recording with one action, optionally after a countdown, so that I can prepare the selected capture area.
10. As a recorder user, I want to see the current recording state, so that I know whether capture has actually started or finished.
11. As a recorder user, I want to stop recording from the application, so that my video is finalized and playable.
12. As a recorder user, I want an obvious result and output location after stopping, so that I can open the video quickly.
13. As a recorder user, I want a clear error when capture or encoding fails, so that I can correct the problem and retry.
14. As a recorder user, I want invalid or old settings to recover safely, so that the application remains usable after an upgrade.
15. As a learner, I want documented acceptance scenarios for full-screen and region capture, so that I can tell whether each milestone really works.

### 后续迭代

16. As a recorder user, I want to choose a microphone or record silently, so that the video has the right narration.
17. As a recorder user, I want simple frame-rate and quality presets, so that I can balance clarity, CPU use, and file size.
18. As a recorder user, I want tray controls and in-app shortcuts, so that I can operate recording without keeping the main window visible.
19. As a recorder user, I want to capture a chosen application window even when another window covers it, so that the video keeps showing the target's live content as it moves or is obscured.
20. As a recorder user, I want capture to work across displays and scale factors, so that the selected area matches the output.
21. As a recorder user, I want to record system sound and microphone together or separately, so that meetings and explanations are audible.
22. As a recorder user, I want a system-wide hotkey with conflict feedback, so that I can control recording while another app has focus.
23. As a recorder user, I want a small floating controller, so that I can see and control recording without returning to the main window.
24. As a recorder user, I want to pause and resume, so that inactive intervals are omitted from the final video.
25. As a recorder user, I want to recover useful output after an interrupted recording when possible, so that one crash does not hide the entire session.
26. As a recorder user, I want only supported codecs shown as options, so that selecting an unavailable encoder never creates a false promise.
27. As a recorder user, I want an installer with the required dependencies, so that the application works on a clean Windows machine.
28. As a learner, I want the product's measured behavior and implementation tradeoffs documented, so that I can discuss the project accurately in an interview.
29. As a learner, I want a guide for each runnable milestone, so that I can understand the goal and prerequisites before changing code.
30. As a learner, I want a small exercise and a reproducible check for each milestone, so that I can verify my understanding through practice.
31. As a learner, I want interview questions tied to implemented decisions and observed results, so that I can explain the project without memorizing unsupported claims.
32. As a recorder user, I want recording to finish with a clear explanation when my target window is minimized or closed, so that I can still use the footage already captured.

## Implementation Decisions

- 客户端使用 Qt 6 Widgets、C++17 和 CMake；录屏通过受控的 FFmpeg 命令行子进程完成。开发环境以 FFmpeg 8.0 为基线，同时在运行时探测所需能力。调用 FFmpeg 命令行不等于直接使用 libavcodec/libavdevice API。
- 首版平台限定 Windows 10/11。屏幕输入先使用 `gdigrab`。后续跨平台工作需要各平台独立的捕获实现，条件编译本身不构成跨平台能力。
- 录制模式是用户显式选择的选项，不根据鼠标位置自动改变正在录制的目标。全屏和区域模式捕获屏幕上的可见画面；V0.3 的窗口模式在用户选定目标窗口后锁定该目标。其他窗口遮挡目标时不得把遮挡窗口录入；目标最小化或关闭时给出原因、结束录制并保存已录片段。
- `gdigrab` 的窗口句柄输入可作为 V0.3 的技术候选，但必须在目标应用上通过遮挡场景验收。如果候选无法满足已确认的业务行为，应评估适合的 Windows 窗口捕获实现，不以“已选中窗口”代替“正确录到窗口内容”。
- 将“用户选择的捕获目标”“录制参数”“子进程生命周期”“配置存储”和“界面状态”分为独立职责。界面通过明确的录制状态更新按钮、提示和错误；状态至少包括待机、准备中、录制中、结束中、失败。
- 构建 FFmpeg 命令时先列出所有输入，再列出流映射与输出编码参数，最后指定输出文件。仅使用 `QStringList` 传参，避免把整条命令交给 shell 解析。
- 首版使用 H.264、MP4、固定的安全默认帧率和质量参数；参数校验发生在启动前。后续每种编码器拥有独立的可用性检查与参数配置，不能把 H.264 的 CRF 和 preset 直接套用于所有编码器。
- 自定义区域先在 Qt 界面中选择，再转换为 FFmpeg 所需的 Windows 捕获坐标。录制前关闭选择遮罩，并验证矩形在可捕获区域内；尺寸不满足编码要求时给予提示或执行明确的尺寸调整。
- 开始录制前确定唯一输出路径，默认不覆盖用户已有文件。停止时先请求 FFmpeg 正常完成封装，等待退出并检查结果；超时或异常退出时保留可诊断信息并明确提示。
- 首版保存最近一次有效保存目录和录制模式。配置读取失败不阻止程序启动；无效配置回退到默认值。
- 麦克风和系统音频作为独立输入接入；混音在明确的音频流关系下完成。设备缺失时提示用户，可由用户选择无声录制，不静默改变录制选择。原 V0.2、V0.3 的版本归属待调整。
- 暂停／继续不作为对 FFmpeg 进程的简单挂起操作。暂停期间从最终视频时间线上删除；V0.4 评估分段录制、合并及时间戳处理后实现，并以输出视频连续性验收。
- 系统级快捷键在 Windows 上使用明确的注册与释放流程；若快捷键已被其他程序占用，界面显示冲突并保持其他操作可用。
- 应用说明、演示和简历表述必须对应实际测试通过的功能；不把代码行数、类数量或未验证的性能数值作为质量指标。
- 学习材料按可运行里程碑组织，围绕真实实现解释概念、关键决策、练习和验收。面试题以已实现和已验证的内容为依据，不承诺固定题数。

## Testing Decisions

- **最高层验收切入点：真实桌面录制闭环。** 在 Windows 测试环境从界面选择全屏或区域，启动、等待、停止；以实际输出文件的可播放性、视频流、时长和尺寸判断结果。用媒体探测工具读取文件信息，不只检查“文件存在”。
- **失败行为切入点：应用到外部录制进程的边界。** 使用可控的测试进程模拟“程序缺失、启动失败、立即退出、返回错误、停止超时”；从界面观察状态、提示、重试和是否遗留占用。测试不依赖内部类数量或参数拼接函数的实现方式。
- **配置切入点：用户重新打开应用。** 修改保存目录与模式，关闭并重启后验证恢复；注入无效配置后验证安全回退。
- **选区切入点：用户拖拽与最终输出。** 覆盖正常拖拽、反向拖拽、零面积、取消选择和缩放比例；以界面显示尺寸与实际录制画面是否匹配为判断标准。
- 首版新增项目内没有可直接复用的测试接口。相邻 ToDo 学习项目已有 Qt Test 对存储往返、畸形输入和网络行为进行验证，可借鉴其测试组织和临时目录做法，但录屏项目仍以端到端输出验收为主。
- 多显示器、双路音频、暂停合并、编码器兼容性和干净机器安装分别在对应迭代建立新的行为验收；首版测试不提前假装覆盖这些能力。
- **V0.3 窗口录制切入点：成片内容。** 选择带持续变化内容的目标窗口 A，录制中移动 A 并用窗口 B 遮挡 A，回看视频确认 A 的内容仍持续更新且未录入 B；最小化或关闭 A 后，界面说明原因且已录片段可播放。
- **学习材料切入点：按文档复现。** 使用一份干净的项目副本，逐步执行阶段说明，确认能构建、运行、完成练习并复现对应验收场景；材料中的预期行为与程序输出保持一致。

## Out of Scope

- 原首版规划不包含窗口捕获、麦克风、系统声音、音频混合、暂停／继续、系统级快捷键、悬浮控制栏和多编码器选择；这些能力的版本归属待重新确定。
- 首版不提供直播推流、会议协作、云同步、视频编辑、摄像头录制、游戏专用捕获、DRM 内容绕过或商业监控功能。
- 首版不承诺 macOS/Linux 运行、安装包分发、硬件编码、长时间录制可靠性指标或所有设备组合兼容。
- 文档不声称现有课程源码为原创、功能完备或已通过测试；需要取得源码与运行结果后才能评估这些主张。

## Further Notes

### 学习里程碑

以下为原 PRD 的学习顺序，是否沿用及如何对应当前代码，待重新调整开发路线时确定。

1. 建立可构建的 Qt Widgets 应用、主窗口与录制状态展示。
2. 验证 FFmpeg 环境，从主显示器录出第一段无声视频。
3. 完成启动、正常停止、异常退出与输出文件检查。
4. 完成区域选择、坐标换算与取消流程。
5. 完成保存目录、唯一命名和基础配置恢复。
6. 完成核心异常路径与用户提示。
7. 完成端到端演示、使用说明和学习复盘。

每一步都能编译和演示；“第一次可录出视频”出现在第二步。七个里程碑可以按每次约 45～60 分钟学习，调试较复杂时允许跨多天完成；它们不是七个日历日的工期保证。建议每一步记录实际碰到的问题、原因、解决办法和验收证据，再决定是否推进下一项功能。

### 风险与约束

- V0.3 窗口录制的遮挡场景需要在实际目标应用上做技术验证；FFmpeg 文档支持按窗口句柄选择目标，但不能据此推断所有应用在遮挡时都能正确输出画面。若需更换捕获方式，以已确认的成片验收标准为准。
- Windows 显示缩放、多显示器负坐标以及 Qt 逻辑像素与原生像素的差异，可能使用户所见选区与视频范围不一致；首版至少覆盖主显示器常见缩放配置。
- 输出 MP4 依赖正常结束时写入封装信息。异常中断可能产生无法播放的文件，所以不能仅凭扩展名判断成功。
- 录制进程的工作目录、FFmpeg 路径、所含编码器和设备权限依赖用户机器；环境诊断是交付的一部分。
- 未来若随安装包分发 FFmpeg，需要单独核对所使用二进制的许可、声明和构建选项。

### 技术依据

- [FFmpeg 命令行参数顺序与流映射](https://ffmpeg.org/ffmpeg.html)：输入、输出及编码参数的归属规则。
- [FFmpeg `gdigrab` 输入](https://ffmpeg.org/ffmpeg-devices.html)：Windows 桌面区域和窗口句柄捕获能力。
- [Qt 高 DPI 坐标模型](https://doc.qt.io/qt-6/highdpi.html)：逻辑像素与原生像素的转换风险。
- [Windows WASAPI 回环采集](https://learn.microsoft.com/windows/win32/coreaudio/loopback-recording)：后续系统声音采集的技术依据。
