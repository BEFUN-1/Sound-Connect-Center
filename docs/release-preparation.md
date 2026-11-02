# 发布与产物来源

更新日期：2026-11-02。当前发布目标：**v2.0.6-dev**，Windows x64 Development 预发布。

## 本次开发预览

本次按项目维护者明确要求，上传已经交付的
`SonyDeviceCenter-Qt2-2.0.6-dev-Setup.exe`，保留文件名与原始内容，
在 GitHub Release 标为 **prerelease**。该包未签名，并非从新 tag 重建的正式包。
其 SHA-256 为：

```text
3bec4c740a809c213fde22e50bce2365e35687310a10ee88e287aaece976973a
```

发布的源码快照包含 Qt 迁移及 2.0.6 修复、当前文档和模拟界面截图。
上传准备时已将 `apps/`、`libs/`、`Client/`、`compat/` 与根 CMake 文件逐项
对照交付时源码清单，应用构建输入一致。文档、截图、截图生成辅助函数及
开发 tag 的 CI 过滤属于后续发布整理，不冒充原安装包已包含的新改动。

Release 提供 `SHA256SUMS.txt` 及来源清单。交付清单原有的
`dirty_worktree: true` 和旧基础提交如实保留；最终发布来源文件另记录上传的
源码提交和对照范围，不为原包补写不存在的干净构建／tag 归属。

本次只上传指定 Windows 安装器及来源、校验文件，不发布 Linux/macOS 包。
验收范围以 [2.0.6 记录](acceptance-2.0.6-2026-11-02.md) 和
[发布说明](releases/v2.0.6-dev.md) 为准。

## 上传内容与隐私

操作范围是此源码仓库。父工作区 APK、反编译目录、本地归档、构建产物和
个人配置不加入 Git。根目录托盘图片、资源 CSV 和原始测试文本属于本地 QA
输出，已排除。

文档截图通过真实 QML 界面、模拟耳机和临时设置目录生成，仅抓取应用窗口，
不拍摄桌面、通知区或真实蓝牙设备列表。每张图均须检查；公开文本不包含
用户路径、真实设备地址或凭据。具体方法见 [截图说明](screenshots/README.md)。

## 正式发布流程

开发预览不改变正式包的以下要求：

1. 审阅并提交源码；从干净提交创建与 CMake 版本匹配的 `v<version>` tag。
2. 使用新的构建目录编译，运行完整自动化测试并补对应平台验收。
3. Windows 正式包必须配置 Authenticode 证书与时间戳，验证程序及安装器签名。
4. 检查运行时、版本、源码记录和校验和，完成实际安装／升级／卸载验收。
5. 推送正式 tag 后，由 Release 工作流生成并创建新的发布，不覆盖旧附件。

`packaging/verify-release-source.py` 拒绝脏工作区、未跟踪源码、缺失正式 tag 或
tag 与 HEAD 不一致。`v*-*` 开发 tag 不触发正式签名发布工作流，预发布通过
单独的上传流程提供既有包；CI 的正式检查和签名要求保留。

GitHub Actions 默认 `contents: read`，仅发布任务获得 `contents: write`，
依赖 action 固定到完整 commit SHA。正式构建包含 `SOURCE-<platform>.json`，
发布任务生成文件名形式的 `SHA256SUMS`。

## 本地检查

```powershell
git diff --check
python -m unittest discover -s tests -p test_release_safety.py -v
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

正式源码校验：

```powershell
python packaging/verify-release-source.py
```

仅检查干净的开发源码、允许无正式 tag 时可使用 `--allow-untagged`；
这不将其转换成正式签名构建。签名与平台依赖见 [打包说明](../packaging/README.md)。
