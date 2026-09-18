@echo off
rem Informe del upscale de texturas HD (pipeline, texturas aceptadas, upx, fps).
rem Doble clic para ejecutarlo. Pega el texto que imprime.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0upscale_report.ps1" -Count 4
echo.
pause
