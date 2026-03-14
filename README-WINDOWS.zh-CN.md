### Windows

#### 安装 python + git
- 安装 [python](https://www.python.org/downloads/) >= 3.9 和 [git](https://git-scm.com/downloads/win)
- 如果你是从 Microsoft Store 安装的 python，执行 "python setup.py install" 可能会失败，但这一步并非必须。
- 通过按下 WIN+R 键, 输入 ``cmd`` 并回车来打开终端

#### 安装 Winfsp (用于 fuse)
下载并安装 [此处](https://winfsp.dev/rel/)

#### 安装 OpenSSL 1.1.1（用于 Python scrypt 依赖项）

下载并安装 [此处](https://sourceforge.net/projects/openssl-for-windows/files/)

#### 获取文件并安装
```
git clone https://github.com/Uaemextop/mtkclient-fork
cd mtkclient
pip3 install -r requirements.txt
```

#### 安装仓库中集成的开源 MTK 驱动包
- 本仓库已经在 `Setup/Windows` 中集成了开源 Windows 驱动与安装资源。
- 如果你想快速安装且不使用自定义内核驱动，可直接安装 CDC INF：
  ```powershell
  pnputil /add-driver Setup\Windows\driver\CDC\mtk_preloader_opensource.inf /install
  ```
- 如果你要使用完整的 KMDF 驱动包，可运行集成安装脚本：
  ```powershell
  powershell -ExecutionPolicy Bypass -File Setup\Windows\installer\install_driver.ps1
  ```
- 用于编译驱动和安装包的 CI 工作流位于 `.github/workflows/build-windows-driver.yml`。
- 详细安装说明请查看 `Setup/Windows/docs/INSTALL.md`。

#### 解决编译 wheel 报错的问题 (感谢 @Oyoh-Edmond)
##### 下载并安装构建工具:
- 前往 Visual Studio 生成工具[下载](https://visualstudio.microsoft.com/visual-cpp-build-tools)页面。
- 下载安装程序并运行它。
    
###### 选择必要的构建组件包:
- 在安装程序中，选择 "使用 C++ 进行桌面开发" 组件。
- 确保已选中 "MSVC v142 - VS 2019 C++ x64/x86 生成工具"（或更高版本）组件。
- 如果尚未选中“Windows 10 SDK”，你也可以选中它。

###### 完成安装:
- 点击 "安装" 按钮开始安装。
- 按照提示完成安装。
- 如有需要，请重启电脑。
