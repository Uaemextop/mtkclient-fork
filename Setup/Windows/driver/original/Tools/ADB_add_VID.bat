@echo off

adb kill-server
pushd c:
cd %USERPROFILE%

if exist .android (goto DIR_EXIST) else goto MK_DIR

:DIR_EXIST
echo .android directory is already exist!
goto EDIT

:MK_DIR
echo .android directory is not exist, so we need to make a new directory!
mkdir .android
goto EDIT

:EDIT
cd .android
echo 0x0E8D >> adb_usb.ini

notepad %USERPROFILE%\.android\adb_usb.ini
popd
pause

