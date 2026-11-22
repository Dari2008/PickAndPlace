@echo off
rem Upload the Pi code (this pi\ folder) to the Raspberry Pi with rsync in WSL -> ~/pap on the Pi.
rem   upload.bat          upload only
rem   upload.bat build    upload, build pap-core + pap-web on the Pi (services keep running the old build)
rem   upload.bat build -auto-restart   ... and restart the pap-core + pap-web services afterwards
rem   upload.bat build -flash          ... and flash the Octopus firmware (octopus-fw, build it first:
rem                                    pio run -e octopus_f446); pap-core is stopped for it and started again
rem Login by SSH key: the first run creates a WSL key and asks for the Pi password once (ssh-copy-id).
setlocal
set TARGET=pnp@192.168.178.66
set DEST=pap/

set BUILD=
set RESTART=
set FLASH=
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="build" ( set BUILD=1 & shift & goto args )
if /i "%~1"=="-auto-restart" ( set RESTART=1 & shift & goto args )
if /i "%~1"=="-flash" ( set FLASH=1 & shift & goto args )
echo Unknown argument "%~1" - use: upload.bat [build] [-auto-restart] [-flash]
exit /b 1
:argsdone

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
    if defined FLASH ( echo -flash needs it: pio run -e octopus_f446 in octopus-fw first. & exit /b 1 )
)

if defined BUILD (
    echo Building on the Pi ...
    wsl -e ssh -t %TARGET% "cd ~/pap/core && make -j4 && cd ~/pap/web && (test -d node_modules || npm install) && npm run build"
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
)
if defined FLASH (
    echo Flashing the Octopus ^(pap-core stopped meanwhile^) ...
    wsl -e ssh -t %TARGET% "sudo systemctl stop pap-core 2>/dev/null; ~/pap/core/build/pap-core --data ~/pap-data --flash-only; r=$?; sudo systemctl start pap-core 2>/dev/null; exit $r"
    if errorlevel 1 ( echo Flashing failed - pap-core runs again with the old firmware on the board. & exit /b 1 )
)
if defined RESTART (
    echo Restarting the services ...
    wsl -e ssh -t %TARGET% "systemctl list-unit-files pap-core.service >/dev/null && sudo systemctl restart pap-core pap-web || echo 'services not installed yet'"
    if errorlevel 1 ( echo Restart failed. & exit /b 1 )
) else if defined BUILD (
    echo Services not restarted - they still run the old build. Restart: upload.bat -auto-restart
    echo   or by hand on the Pi: sudo systemctl restart pap-core pap-web
)
echo Done.
