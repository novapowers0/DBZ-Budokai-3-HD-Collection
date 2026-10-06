@echo off
rem DBZ3 HD Mod Kit - doble clic para abrir la ventana del kit de modding (sin consola).
rem Busca Python como el launcher: DBZ3_PYTHON, el lanzador "py -3" de python.org y luego
rem python / python3 del PATH (el alias de Microsoft Store no sirve: se descarta solo).
setlocal EnableExtensions
chcp 65001 >nul
title DBZ3 HD Mod Kit

set "GUI=%~dp0mod center hd\modkit_gui.py"
if not exist "%GUI%" if exist "%~dp0..\..\mod center hd\modkit_gui.py" set "GUI=%~dp0..\..\mod center hd\modkit_gui.py"
if not exist "%GUI%" goto :nogui

set "PYEXE="
if defined DBZ3_PYTHON if exist "%DBZ3_PYTHON%" set "PYEXE=%DBZ3_PYTHON%"
rem Python portatil que trae el juego (carpeta python\): no hace falta instalar nada
if not defined PYEXE if exist "%~dp0python\python.exe" set "PYEXE=%~dp0python\python.exe"
if not defined PYEXE call :probe py -3
if not defined PYEXE call :probe python
if not defined PYEXE call :probe python3
if not defined PYEXE goto :nopython

"%PYEXE%" -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)" >nul 2>nul
if errorlevel 1 goto :oldpython
"%PYEXE%" -c "import tkinter" >nul 2>nul
if errorlevel 1 goto :notk

rem pythonw.exe abre la ventana sin dejar esta consola abierta
set "PYW=%PYEXE:python.exe=pythonw.exe%"
if /i not "%PYW%"=="%PYEXE%" if exist "%PYW%" (
  start "" "%PYW%" "%GUI%" %*
  exit /b 0
)
"%PYEXE%" "%GUI%" %*
if errorlevel 1 pause
exit /b

:probe
%* -c "import sys" >nul 2>nul
if errorlevel 1 exit /b 0
for /f "usebackq delims=" %%P in (`%* -c "import sys; print(sys.executable)"`) do set "PYEXE=%%P"
exit /b 0

:nogui
echo No se encuentra "mod center hd\modkit_gui.py".
echo Copia TODO el contenido del Kit de modding junto a dbz3.exe y vuelve a intentarlo.
echo.
echo Could not find "mod center hd\modkit_gui.py". Copy the whole Modding Kit next to dbz3.exe.
pause
exit /b 1

:nopython
echo ============================================================
echo  No se encontro Python 3 en este equipo.
echo  1) Instala Python 3.11 o superior desde https://www.python.org/downloads/
echo     y marca la casilla "Add python.exe to PATH".
echo  2) Haz doble clic en instalar_requisitos.bat (una sola vez).
echo  3) Vuelve a abrir DBZ3_ModKit.bat.
echo.
echo  Python 3 was not found. Install Python 3.11+ from python.org
echo  (tick "Add python.exe to PATH"), run instalar_requisitos.bat once,
echo  then open DBZ3_ModKit.bat again.
echo ============================================================
pause
exit /b 1

:oldpython
echo Tu Python es demasiado antiguo: el kit necesita Python 3.11 o superior.
echo Your Python is too old: the kit needs Python 3.11 or newer.
echo https://www.python.org/downloads/
pause
exit /b 1

:notk
echo A tu Python le falta tkinter (la parte que dibuja ventanas).
echo Reinstala Python desde python.org dejando marcada la opcion "tcl/tk and IDLE".
echo.
echo Your Python has no tkinter. Reinstall it from python.org with "tcl/tk and IDLE" ticked.
pause
exit /b 1
