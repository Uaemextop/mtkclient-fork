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

#### Windows USB Driver (Recommended - No UsbDk Required)

This project includes an open-source KMDF USB CDC ACM serial driver that replaces
the proprietary MediaTek usb2ser.sys and eliminates the need for libusb and UsbDk.

##### Option A: CDC-Only Driver (Easiest - No Signing Required)
Uses Windows built-in usbser.sys. Works immediately:
```cmd
pnputil /add-driver Setup\Windows\driver\CDC\mtk_preloader_opensource.inf /install
```

##### Option B: Custom KMDF Driver (Full-Featured)
Requires test signing for community builds:
```cmd
bcdedit /set testsigning on
powershell -ExecutionPolicy Bypass -File Setup\Windows\installer\install_driver.ps1
```

##### Verify Installation
After installation, Device Manager should show under Ports (COM and LPT):
- MediaTek USB Port (BROM) - when device is in Boot ROM mode
- MediaTek PreLoader USB VCOM Port - when device is in preloader mode
- MediaTek DA USB VCOM Port - when download agent is active

When using mtkclient with the serial driver, specify the COM port:
```shell
python mtk.py --serialport COMx
```

#### Legacy: UsbDk (No longer required)
- Install normal MTK Serial Port driver (or use default Windows COM Port one, make sure no exclamation is seen)
- Get usbdk installer (.msi) from [here](https://github.com/daynix/UsbDk/releases/) and install it
- Test on device connect using "UsbDkController -n" if you see a device with 0x0E8D 0x0003

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