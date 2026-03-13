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

#### Option A: Install the mtkclient WinUSB driver (recommended)

The mtkclient project now includes an open-source **WinUSB driver** that works
natively on Windows 10/11 x64, eliminating the need for the third-party UsbDk
driver.

**Quick install (command line):**
1. Open a **Command Prompt as Administrator**
2. Navigate to the mtkclient directory
3. Run:
   ```shell
   cd Setup\Windows
   install_driver.bat
   ```

**MSI installer:**

If an MSI package has been built (see `Setup/Windows/README.md` for build
instructions), install it with:
```shell
msiexec /i Setup\Windows\output\mtkclient_driver.msi
```

The driver supports these MediaTek USB devices:
| VID      | PID      | Description        |
|----------|----------|--------------------|
| `0x0E8D` | `0x0003` | MTK Bootrom (BROM) |
| `0x0E8D` | `0x2000` | MTK Preloader      |
| `0x0E8D` | `0x2001` | MTK Preloader      |
| `0x0E8D` | `0x20FF` | MTK Preloader      |
| `0x0E8D` | `0x3000` | MTK Preloader      |
| `0x0E8D` | `0x6000` | MTK Preloader      |
| `0x1004` | `0x6000` | LG Preloader       |
| `0x22D9` | `0x0006` | OPPO Preloader     |

#### Option B: Get latest UsbDk 64-Bit (legacy)
- Install normal MTK Serial Port driver (or use default Windows COM Port one, make sure no exclamation is seen)
- Get usbdk installer (.msi) from [here](https://github.com/daynix/UsbDk/releases/) and install it
- Test on device connect using "UsbDkController -n" if you see a device with 0x0E8D 0x0003
- Works fine under Windows 10 and 11 :D

#### Building wheel issues (creds to @Oyoh-Edmond)
##### Download and Install the Build Tools:
    Go to the Visual Studio Build Tools [download](https://visualstudio.microsoft.com/visual-cpp-build-tools) page.
    Download the installer and run it.

###### Select the Necessary Workloads:
    In the installer, select the "Desktop development with C++" workload.
    Ensure that the "MSVC v142 - VS 2019 C++ x64/x86 build tools" (or later) component is selected.
    You can also check "Windows 10 SDK" if it's not already selected.

###### Complete the Installation:
    Click on the "Install" button to begin the installation.
    Follow the prompts to complete the installation.
    Restart your computer if required.
