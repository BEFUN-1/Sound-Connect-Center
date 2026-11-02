# 发布与产物来源

更新日期：2026-11-02。当前正式版本：**v2.0.6**，Windows x64。

## 2.0.6 正式发布决定

维护者已验证指定的 `SonyDeviceCenter-Qt2-2.0.6-dev-Setup.exe`，并明确批准
将同一文件作为正式版本重新上传到 GitHub Release。

正式发布使用 `v2.0.6` tag，`prerelease: false`，并设为 Latest。
下载文件名为 `SonyDeviceCenter-Qt2-2.0.6-Setup.exe`，仅去掉名称中的 `-dev`，
文件字节内容不变，原始文件保留。SHA-256：

```text
3bec4c740a809c213fde22e50bce2365e35687310a10ee88e287aaece976973a
```

安装包尚未签名，本次没有重新构建。发布来源文件如实记录维护者确认、
原始文件名、正式下载文件名、未签名状态、原构建的 `dirty_worktree: true`
以及原基础提交。源码 tag 表示发布时的源码与说明，不补写原包来自干净 tag
构建的证明。此前 `v2.0.6-dev` 保留为历史预发布。

上传前将 `apps/`、`libs/`、`Client/`、`compat/` 和根 CMake 文件与原交付源码
清单逐项核对；文档、截图辅助函数和发布入口更新不改变已验证安装文件。
本次只上传 Windows 安装器及校验、来源和原验证摘要，不发布 Linux/macOS 包。

`v2.0.6` 从自动 Release 构建工作流中排除，由维护者批准的既有文件发布流程
上传，确保不会被自动重建的文件替换。其他版本的自动构建和签名要求继续保留。

详情见 [2.0.6 正式发布说明](releases/v2.0.6.md) 和
[原交付验收记录](acceptance-2.0.6-2026-11-02.md)。

## 上传内容与隐私

操作范围是此源码仓库。父工作区 APK、反编译目录、本地归档、构建产物和
个人配置不加入 Git。根目录托盘图片、资源 CSV 和原始测试文本属于本地 QA
输出，已排除。

文档截图通过真实 QML 界面、模拟耳机和临时设置目录生成，仅抓取应用窗口，
不拍摄桌面、通知区或真实蓝牙设备列表。每张图均须检查；公开文本不包含
用户路径、真实设备地址或凭据。具体方法见 [截图说明](screenshots/README.md)。

## 后续自动构建与签名发布流程

2.0.6 采用上述维护者确认流程；后续自动签名发布仍使用以下流程：

1. 审阅并提交源码；从干净提交创建与 CMake 版本匹配的 `v<version>` tag。
2. 使用新的构建目录编译，运行完整自动化测试并补对应平台验收。
3. Windows 正式包必须配置 Authenticode 证书与时间戳，验证程序及安装器签名。
4. 检查运行时、版本、源码记录和校验和，完成实际安装／升级／卸载验收。
5. 推送正式 tag 后，由 Release 工作流生成并创建新的发布，不覆盖旧附件。

`packaging/verify-release-source.py` 拒绝脏工作区、未跟踪源码、缺失正式 tag 或
tag 与 HEAD 不一致。`v*-*` 开发 tag 不触发正式签名发布工作流，2.0.6 通过单独上传流程提供维护者已验证的既有包；后续自动构建的源码检查和签名要求保留。

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
