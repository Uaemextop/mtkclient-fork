### Windows

#### Install python + git
- Install python >= 3.9 and git
- If you install python from microsoft store, "python setup.py install" will fail, but that step isn't required.
- WIN+R ```cmd```

#### Install Winfsp (for fuse)
Download and install [here](https://winfsp.dev/rel/)

#### Grab files and install
```shell
git clone https://github.com/Uaemextop/mtkclient-fork
cd mtkclient
pip3 install -r requirements.txt
```

#### Install the integrated open-source MTK driver package
- This repository now includes an open-source Windows driver integration under `Setup/Windows`.
- Install the CDC INF directly if you want a fast setup without a custom kernel binary:
  ```powershell
  pnputil /add-driver Setup\Windows\driver\CDC\mtk_preloader_opensource.inf /install
  ```
- Or use the integrated installer script for the KMDF driver package:
  ```powershell
  powershell -ExecutionPolicy Bypass -File Setup\Windows\installer\install_driver.ps1
  ```
- Driver build automation for the bundled package is available in `.github/workflows/build-windows-driver.yml`.
- Full Windows installation instructions are documented in `Setup/Windows/docs/INSTALL.md`.

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
