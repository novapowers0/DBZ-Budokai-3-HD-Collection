@echo off
rem Informe de rendimiento de la ultima sesion de dbz3 (lineas "dbz3: perf").
rem Doble clic para ejecutarlo. Pega el texto que imprime.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0perf_report.ps1" -Count 2
echo.
pause
