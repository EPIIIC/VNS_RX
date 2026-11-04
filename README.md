# VNS RX — Phase 2 prototype

2026-11-02：用户确认 RX 与旧 VNS_nRF 使用相同接法。已配置实际输出脚，移除独立的 BOARD_CONFIRMED / EXPERIMENT_APPROVED 开关。就绪状态来自驱动初始化、参数有效性和当前 BLE 会话，不需要额外批准操作。

## 当前配置

| 项目 | 配置/来源 |
|---|---|
| SDK / target | NCS 3.3.0 / nrf52dk/nrf52832，唯一 prj.conf |
| ISET | P0.18，高有效；用户确认沿用旧 app.overlay |
| #VNS_EN | P0.16，低有效；启动先置高并使用内部上拉 |
| ISET carrier | PWM0，约 100 kHz，TOP=160 |
| 刺激指示 LED2 | P0.12，高有效；TIMER1 产生 100 kHz 嵌套 PWM，亮度跟随 ISET，占空窗口跟随刺激脉冲 |
| 脉冲 | TIMER2 / PPI / GPIOTE；TIMER3 持续时间截止；TIMER4 通信截止 |
| 默认 UI | 1 mA / 15 Hz / 250 µs / 30 s；30 s 沿用旧固件默认值 |
| 电流软件范围 | 正值至 1939 µA，受旧 ISET 满量程模型约束 |
| 频率软件范围 | 0.1–500 Hz，沿用旧代码 |
| 脉宽软件范围 | 100–65535 µs，受当前协议 u16 约束，且必须小于实际周期 |
| 单次持续时间 | 正值至 3600 s，沿用旧代码；硬件 TIMER 上限另校验 |
| 建立时间 | ISET 6 ms，沿用旧 stimulation.c |
| 电压检测 | 用户确认沿用旧 RX，无电压检测部分；ADC 已禁用，控制不依赖电压读数 |
| 会话 | 无 PIN/bond，名为 VNS_RX_01，自动连接由 TX 发起 |

上述范围是旧固件和本次协议的实现范围，不是人体/动物使用的临床安全范围。2026-11-02 用户进一步明确 RX 就是旧 VNS_nRF 硬件，不需要电压检测。已移除新增的 rx_supply 模块、3.0 V 阈值、200 ms 电源稳定判据及 UI 电压项，恢复旧工程的 CONFIG_ADC=n。没有虚构电压或将供电检测标为通过；本版本没有主动电压/欠压检测。输出电流和顺应条件由假负载上的实际测量判断。

ISET 模型保留旧源码的 1939 µA 满量程。闭环电路不需要为这次修改重建标定表；软件仍需将请求电流换算为 PWM。1 mA 请求按现有向下量化得到约 994 µA 编程估计值，ACK 显示实际编程设定，不冒充测得的输出电流。

## LED2 嵌套 PWM

LED2 沿用旧 RX 的 P0.12 高有效接法。TIMER1 产生约 100 kHz 载波，其占空比直接使用 ISET 的同一量化值；TIMER2 的刺激 ON/OFF 事件通过 PPI 同步启停 LED 载波。因此 LED 只在每个实际刺激脉冲窗口内发光，亮度随编程电流变化，不依赖 BLE 包间隔或软件线程调度。

STOP、断线、通信超时、持续时间结束、复位和故障路径都会停止 TIMER1 并清零 LED。LED 只表示编程占空比与脉冲时序，不是实际输出电流反馈。短脉宽下肉眼平均亮度较低属于正常现象，不额外延长闪光。

## 操作

1. 用户重新构建并烧录本 RX；刷新配套 WebApp。TX 沿用已修复 CCC UUID 的版本，本次无需修改 TX 固件。
2. 手机连接 TX → Control → Wireless Power ON，RX 自动连接。
3. 初次输出测试只接假负载。Stimulation → 编辑参数 → Apply Parameters → 收到真实 RX ACK 后 START。
4. STOP、断线、通信超时、复位或参数错误时关闭输出；重新连接不自动恢复刺激。

```sh
cd /Users/epiiic/Desktop/VNS_RX_nRF
west build -d build .
west flash -d build --dev-id YOUR_RX_PROBE_ID
```

使用 NCS 3.3.0 工具链终端，或直接使用已有 VS Code RX build 配置。无需新建第二套 conf，无需删除 build。首次配置才需要 `west build --sysbuild -b nrf52dk/nrf52832 -d build .`。

## 验证状态

本次助手仅核对源码/本地 SDK 定义并编辑，没有运行构建、烧录、BLE 或负载测试。旧日志对应输出被编译排除的版本，不能当作本次 PWM/TIMER 路径已编译或实测的证据。原 VNS_nRF / VNS-Webapp 未修改。新硬件路径的构建结果及电流/时序仍由用户验证，见 [MANUAL_ACCEPTANCE.md](docs/MANUAL_ACCEPTANCE.md)。

内部上拉只在固件运行后生效；复位期间的实际使能电平由板上电路决定，负载测试应检查此阶段。

烧录出现 NVMC / Verify 错误时，按 [FLASH_RECOVERY.md](docs/FLASH_RECOVERY.md) 由用户恢复。`main.c` 已设置调试暂停时暂停看门狗，正常运行的看门狗保留；本次没有实际构建或执行恢复。

RX_STATUS 的 v2 电压字段保留为零（未测量），STIM_F_POWER 保持清零。WebApp 不再以该位阻止 Apply/START，也不显示电压或供电门槛消息。GPIO/参数/会话、STOP 和 TIMER 本地截止仍有效。TX 固件本次无需更新，手机监测协议和 CSV 不变。
