@echo off
rem Upload the Pi code (this pi\ folder) to the Raspberry Pi with rsync in WSL -> ~/pap on the Pi.
rem   upload.bat          upload only
rem   upload.bat build    upload, build pap-core + pap-web on the Pi, restart the services
rem Login by SSH key: the first run creates a WSL key and asks for the Pi password once (ssh-copy-id).
setlocal
set TARGET=pnp@192.168.178.66
set DEST=pap/

rem SSH key login set up? (BatchMode = never ask for a password)
wsl -e ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new %TARGET% true >nul 2>&1
if errorlevel 1 (
    echo No SSH key login to %TARGET% yet - setting it up, enter the Pi password once.
    wsl -e sh -c "test -f ~/.ssh/id_ed25519 || ssh-keygen -t ed25519 -N '' -f ~/.ssh/id_ed25519 -q"
    wsl -e ssh-copy-id -o StrictHostKeyChecking=accept-new %TARGET%
    if errorlevel 1 ( echo SSH key setup failed. & exit /b 1 )
)

echo Uploading %~dp0 to %TARGET%:~/%DEST%
wsl --cd "%~dp0" -e rsync -rltz --chmod=D755,F644 --delete --info=stats1 ^
    --exclude=/core/build*/ --exclude=/web/node_modules/ --exclude=/web/dist/ ^
    --exclude=__pycache__/ --exclude=*.o --exclude=upload.bat --exclude=ui-preview.cmd --exclude=/firmware/ ^
    --rsync-path="mkdir -p ~/%DEST% && rsync" ^
    ./ %TARGET%:%DEST%
if errorlevel 1 ( echo Upload failed. & exit /b 1 )

rem Octopus firmware (build it first: pio run in octopus-fw; this board is an F446) -> ~/pap/firmware/pap-motion.bin
rem for pap-core --flash
set FW=%~dp0..\octopus-fw\.pio\build\octopus_f446\firmware.bin
if exist "%FW%" (
    wsl --cd "%~dp0..\octopus-fw\.pio\build\octopus_f446" -e rsync -tz --chmod=F644 ^
        --rsync-path="mkdir -p ~/%DEST%firmware && rsync" ^
        firmware.bin %TARGET%:%DEST%firmware/pap-motion.bin
    if errorlevel 1 ( echo Firmware upload failed. & exit /b 1 )
) else (
    echo No Octopus firmware built yet - skipped
)

if /i "%~1"=="build" (
    echo Building on the Pi ...
    wsl -e ssh -t %TARGET% "cd ~/pap/core && make -j4 && cd ~/pap/web && (test -d node_modules || npm install) && npm run build && (systemctl list-unit-files pap-core.service >/dev/null && sudo systemctl restart pap-core pap-web || echo 'services not installed yet')"
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
)
echo Done.
