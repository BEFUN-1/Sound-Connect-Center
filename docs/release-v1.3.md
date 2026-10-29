# Sony Device Center 1.3

发布日期：2026-10-29

## 本次修复

当电脑和手机同时连接耳机时，手机接听电话会把音频切到手机。电话结束后，
耳机虽然已经把音频切回电脑，但用于控制耳机的 RFCOMM/SPP 通道可能仍处于半开
状态。旧逻辑只根据底层连接标志判断状态；降噪、电量、均衡器、DSEE、多点连接
和关机命令在这个状态下会一直等待响应或 ACK，最终显示 `Timeout waiting for
response from device`。

1.3 做了以下处理：

- 命令响应或 ACK 超时后立即把控制会话标记为不健康，避免继续复用半开通道。
- 下一次操作会重新建立控制连接并重新完成协议握手。
- V2 握手失败会正确返回连接失败，不再把失败吞掉后报告为已连接。
- 重连时保留设备名称和协议选择，确保重新连接使用正确的协议版本。

## 验证

自动化回归测试覆盖了以下环节：

- 半开控制连接在响应超时后变为断开状态。
- 后续操作触发重连并重新握手。
- V2 握手超时会报告连接失败。
- 电话切换后电量、降噪、均衡器、DSEE、多点连接和关机命令不再永久复用失效会话。

本次目标测试共 13 个测试用例、55 个断言，全部通过；C++ 原生构建、
Windows 桌面构建和 `sonyctl --help` 检查也全部通过。

在本机 Windows Release 全量 CTest 中，6 个旧的 FakeTransport/时序场景仍会
失败，另有 1 个未构建的 transport 测试目标；这些场景不属于本次电话切换
控制通道修复。发布门禁采用上面的定向回归测试，并保留该全量结果供后续测试
夹具整理时追踪。

## Windows 发布文件

| 文件 | SHA-256 |
| --- | --- |
| `SonyDeviceCenter.exe` | `2D30C651576CF14642B51045EE7BF2492DA8FE4034EC78035AD31A3520D33543` |
| `SonyDeviceCenter-Setup.exe` | `ABCAD7CC438CBE73A913EAA6B89765830FAAF58A19A6ED1F2EC9D05B19B835BF` |
| `SonyDeviceCenter-Windows-x64.zip` | `9F30158C9CA0FDC3F4FF25AA0EAF73646B085E2A23D82D3A49A9FCA6835D16F1` |

安装器和便携 ZIP 均由修复后的 Windows Release payload 生成。发布页中的
`SonyDeviceCenter.exe` 是同一 payload 的直接启动器，适合不需要安装的用户。

## English

Version 1.3 fixes the stale RFCOMM/SPP control session that could remain after a
phone call took over a multipoint headset. When audio returned to the computer,
the old session could still appear connected while every control request timed
out. A response or ACK timeout now invalidates the session; the next operation
reconnects and completes the handshake. V2 handshake failures are also reported
as connection failures instead of being swallowed.

The regression suite covered the half-open link, timeout invalidation, reconnect,
and V2 handshake failure paths. The targeted run passed 13 tests and 55
assertions, and the native and Windows desktop builds passed.
