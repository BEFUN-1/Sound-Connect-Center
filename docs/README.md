# 文档导航

当前版本：**2.0.6 正式版**，2026-11-02，Windows x64。维护者已验证并批准本次安装文件，安装包尚未签名。

## 开始使用

- [中文介绍、功能与界面预览](../README.md)
- [English introduction](../README.en.md)
- [安装、连接、托盘与常见问题](getting-started.md)
- [2.0.6 发布说明与下载](releases/v2.0.6.md)
- [设备与功能验证矩阵](device-matrix.md)
- [截图来源、隐私与生成方法](screenshots/README.md)

## 开发与发布

- [贡献指南与本地检查](../CONTRIBUTING.md)
- [构建、平台打包与签名](../packaging/README.md)
- [发布方式、产物来源与正式版要求](release-preparation.md)
- [当前架构](architecture-current.md)
- [连接生命周期与 IPC](ipc-and-lifecycle.md)
- [尚未解决的问题](technical-debt.md)
- [Qt 迁移边界](qt-2.0-migration.md)
- [界面术语与 Sony 官方依据](ui-wording.md)
- [协议分析记录](reverse-engineering/)

## 当前版本验证

- [2.0.6 验收记录](acceptance-2.0.6-2026-11-02.md)
- [Windows 连接跟随规则、回归与真机边界](reconnect-follow-system-2.0.6-2026-11-02.md)

本次正式版使用维护者已验证的既有安装文件，内容与此前开发预览一致，仅下载文件名去掉 `-dev`。原始构建来源和未签名状态如实保留；维护者确认发布不改写自动化测试记录。截图使用模拟数据，不能代替硬件验证。

## 历史验证

历史文档描述对应版本，不自动适用于当前版本；其中“尚未上传”等文字记录当时状态。
2.0.5 的永久暂停策略已由 2.0.6 的跟随系统规则替代。

- [2.0.5 连接策略、资源优化与验收](acceptance-2.0.5-2026-11-01.md)
- [2.0.5 真机步骤](hardware-acceptance-2.0.5.md)
- [2.0.4 安全修复与验证](security-fixes-2026-11-01.md)
- [通话切回电脑后控制恢复](multipoint-call-recovery-2026-11-01.md)
- [Qt 2.0 验收](qt-2.0-acceptance-2026-10-31.md)
- [2.0.1 验证](qt-2.0.1-validation-2026-11-01.md)
- [2.0.2 电源验证](qt-2.0.2-power-validation-2026-11-01.md)
- [2.0.3 重连验证](qt-2.0.3-reconnect-validation-2026-11-01.md)
- [Windows Forms 1.3 发布说明](history/v1.3/release-v1.3.md)
- [Windows Forms 1.3 XM6 验证](history/v1.3/windows-xm6-validation-2026-10-19.md)
- [旧客户端架构](history/legacy/architecture-current-legacy.md)

二进制通过 GitHub Release 提供；构建目录、个人配置、原始 QA 日志与父工作区的 APK／反编译目录不属于源码发布内容。
