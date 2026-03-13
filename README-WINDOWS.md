### Windows

#### Install python + git
- Install python >= 3.9 and git
- If you install python from microsoft store, "python setup.py install" will fail, but that step isn't required.
- WIN+R ```cmd```

#### Install Winfsp (for fuse)
Download and install [here](https://winfsp.dev/rel/)

#### Grab files and install
```shell
git clone https://github.com/bkerler/mtkclient
cd mtkclient
pip3 install -r requirements.txt
```

#### Install USB Drivers
mtkclient uses WinUSB to communicate with MediaTek devices. Install the mtkclient driver package:

**Option A: MSI installer (recommended)**
- Download the `mtkclient_drivers.msi` from the [Releases](../../releases) page
- Run the MSI — it installs WinUSB drivers for BROM/Preloader/DA/ADB and serial (COM port) drivers automatically
- The installer removes old MTK drivers, installs the signing certificate, and registers the new drivers

**Option B: Manual install**
- Run `Setup\Windows\driver\install_drivers.bat` as Administrator
- This installs: ADB/Bootloader/MTP driver, Preloader/BROM/DA driver, and Serial (COM port) driver

**What gets installed:**
- **ADB/Bootloader/MTP** — `android_winusb.inf` (shows as "Android Device" in Device Manager)
- **BROM/Preloader/DA** — `mtk_preloader.inf` (shows as "MediaTek USB Devices" in Device Manager)
- **Serial ports (VCOM)** — `cdc-acm.inf` (shows as COM ports)

> **Note:** UsbDk is **no longer required**. The WinUSB drivers work directly with libusb
> (bundled in `mtkclient/Windows/`). If you previously installed UsbDk, you may uninstall it.

#### Building wheel issues (creds to @Oyoh-Edmond)
##### Download and Install the Build Tools:
    Go to the Visual Studio Build Tools [download](https://visualstudio.microsoft.com/visual-cpp-build-tools) page.
    Download the installer and run it.

###### Select the Necessary Workloads:
    In the installer, select the "Desktop development with C++" workload.
    Ensure that the "MSVC v142 - VS 2019 C++ x64/x86 build tools" (or later) component is selected.
    You can also check "Windows 10 SDK" if it’s not already selected.

###### Complete the Installation:
    Click on the "Install" button to begin the installation.
    Follow the prompts to complete the installation.
    Restart your computer if required.