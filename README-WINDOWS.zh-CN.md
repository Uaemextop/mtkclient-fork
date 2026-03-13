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
git clone https://github.com/bkerler/mtkclient
cd mtkclient
pip3 install -r requirements.txt
```

#### 安装 USB 驱动
mtkclient 使用 WinUSB 与 MediaTek 设备通信。安装 mtkclient 驱动程序包：

**方式 A：MSI 安装程序（推荐）**
- 从 [Releases](../../releases) 页面下载 `mtkclient_drivers.msi`
- 运行 MSI — 自动安装 BROM/Preloader/DA/ADB 的 WinUSB 驱动和串口 (COM) 驱动
- 安装程序会自动删除旧的 MTK 驱动，安装签名证书，并注册新驱动

**方式 B：手动安装**
- 以管理员身份运行 `Setup\Windows\driver\install_drivers.bat`

> **注意：** 不再需要 UsbDk。WinUSB 驱动直接通过 libusb（包含在 `mtkclient/Windows/` 中）工作。
> 如果你之前安装了 UsbDk，可以将其卸载。

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