# RX NVMC / 校验失败恢复

用户错误：`NVMC wait for ready failed: Memory access error at 0x4001e400`，探针 `941000024`。该错误表示烧录器没能正常访问 NVMC；报错中的 `Probably a memory protection issue` 是推测，不能仅据此确认 APPROTECT。供电/SWD 不稳定、复位和现存固件的看门狗也需考虑。

## 已修改的软件

原 `src/main.c` 调用 `wdt_setup(wdt, 0)`，本地 NCS 3.3.0 的 `wdt_nrfx.c` 因此设置 RUN_HALT，CPU 被调试器暂停时 2 s WDT 仍计时。已改为 `WDT_OPT_PAUSE_HALTED_BY_DBG`；正常运行/睡眠时的看门狗仍启用。这是针对一个明确干扰风险的修正，不代表已证明它就是本次实板错误的唯一原因。新设置需烧录后才生效，无法直接改变板内旧固件。

本地构建配置没有开启 `CONFIG_NRF_APPROTECT_LOCK`，使用默认 `CONFIG_NRF_APPROTECT_USE_UICR`；没有为了此错误添加其他安全或 TrustZone 配置。

## 由用户执行

1. 先 STOP 并断开刺激负载；结束 VS Code debug、RTT Viewer、Programmer 等占用同一探针的会话。
2. 保持 RX 的正常输入供电稳定、SWDIO/SWDCLK/GND/VTref 连接可靠；无线耦合在擦写期间变化可能使 SWD 失联。探针参考电压接线不等于探针已给目标板供电。
3. 在 NCS 3.3.0 工具链终端执行以下命令。**Recover 会清除所连接 RX 的 Flash、持久化设置及 UICR，然后重写固件；不是普通保留数据的增量烧录。** 下方指定用户当前 RX 探针序列号，避免选到 TX。

```sh
cd /Users/epiiic/Desktop/VNS_RX_nRF
west build -d build .
west flash -d build --runner nrfutil --dev-id 941000024 --recover
```

本地 NCS 3.3.0 的 `nrf_common.py` 明确按 recover → program → reset 执行，`nrfutil.py` 支持 recover 操作。无需手工编辑生成的 batch JSON，也不需要删除 build。

4. 等待擦写、Verify 和复位完整成功后再进行 App 连接测试。
5. 若 **Recover 本身**仍报 NVMC/Memory access 错误，保留完整 Recover 日志，优先检查供电、SWD 与探针连接；不要继续用普通 Flash 反复覆盖，也不能据此直接断定芯片损坏。

## 依据与未验证事项

- [Nordic nRF52832 Production Programming](https://docs.nordicsemi.com/r/bundle/nwp_027/page/wp/nwp_027/intro.html)：列出已有 APPROTECT/BPROT 和看门狗等编程边界情况。
- [Nordic nRF52 recovery](https://docs.nordicsemi.com/r/bundle/nrfutil/page/nrfutil-device/guides/programming_recovery.html/recovering-nrf52-nrf54l-and-nrf91-series-devices)：Recover 擦除 Flash 并写入防止保护再次开启的小程序。
- 本次只阅读现有文件、本地 SDK 和官方文档并修改源码，没有执行构建、目标连接、擦除、Recover、烧录或硬件测试。仅尝试读取 CLI 帮助，nrfutil 被本环境日志目录写权限阻止，nrfjprog 未能通过其 Rosetta 环境检查，因此没有用它们的帮助执行结果冒充硬件验证。
