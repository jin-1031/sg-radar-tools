@echo off
setlocal EnableDelayedExpansion
rem Run from a VS2022 x64 Developer Command Prompt (or Developer PowerShell).
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "OUT=%ROOT%\radar-studio\x64\Release\tests"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /EHsc /std:c++20 /O2 "%~dp0reconnect_policy_test.cpp" /Fe:reconnect_policy_test.exe
if errorlevel 1 goto fail
reconnect_policy_test.exe
if errorlevel 1 goto fail
cl /nologo /EHsc /std:c++20 /O2 /D_WIN32_WINNT=0x0601 /I"%ROOT%" /I"%ROOT%\common\thirdparty\asio\include" "%~dp0retina_connection_test.cpp" "%ROOT%\radar-studio\src\device\retina.cpp" "%ROOT%\common\os\network.cpp" "%ROOT%\radar-emulator\src\device\mock_protocol.cpp" /Fe:retina_connection_test.exe
if errorlevel 1 goto fail
retina_connection_test.exe
if errorlevel 1 goto fail
rem Reuse the existing Studio build's recorder and zstd objects, without modifying shared code.
set "BUILD=%ROOT%\radar-studio\x64\Release"
set "ZSTD="
for %%F in ("%BUILD%\zstd*.obj" "%BUILD%\entropy_common.obj" "%BUILD%\error_private.obj" "%BUILD%\fse_compress.obj" "%BUILD%\fse_decompress.obj" "%BUILD%\hist.obj" "%BUILD%\huf_compress.obj" "%BUILD%\huf_decompress.obj" "%BUILD%\xxhash.obj" "%BUILD%\pool.obj" "%BUILD%\threading.obj") do set "ZSTD=!ZSTD! "%%~fF""
cl /nologo /EHsc /std:c++20 /O2 /MD /I"%ROOT%" "%~dp0recorder_session_test.cpp" "%BUILD%\point_cloud_recorder.obj" !ZSTD! /Fe:recorder_session_test.exe
if errorlevel 1 goto fail
recorder_session_test.exe
if errorlevel 1 goto fail
cl /nologo /EHsc /std:c++20 /O2 /MD /I"%ROOT%" "%~dp0utf8_log_test.cpp" "%BUILD%\debug_console.obj" "%BUILD%\point_cloud_recorder.obj" !ZSTD! /Fe:utf8_log_test.exe
if errorlevel 1 goto fail
utf8_log_test.exe
if errorlevel 1 goto fail
cl /nologo /EHsc /std:c++20 /O2 /MD /I"%ROOT%" /I"%ROOT%\common\thirdparty\imgui" /I"%ROOT%\common\thirdparty\font-awesome" "%~dp0korean_font_test.cpp" "%BUILD%\imgui.obj" "%BUILD%\imgui_draw.obj" "%BUILD%\imgui_tables.obj" "%BUILD%\imgui_widgets.obj" "%BUILD%\imgui_custom.obj" "%BUILD%\colormap.obj" user32.lib /Fe:korean_font_test.exe
if errorlevel 1 goto fail
korean_font_test.exe "%ROOT%\asset\font\fa-solid-900.ttf"
if errorlevel 1 goto fail
cl /nologo /EHsc /std:c++20 /O2 /MD /I"%ROOT%" "%~dp0recording_path_test.cpp" "%BUILD%\point_cloud_recorder.obj" !ZSTD! /Fe:recording_path_test.exe
if errorlevel 1 goto fail
recording_path_test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
