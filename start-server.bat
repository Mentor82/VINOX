@echo off
setlocal
cd /d "%~dp0"
set PATH=%~dp0out\windows-msvc-debug\build;%PATH%
echo ==============================================================================
echo   Starting VINOX OpenAI-compatible HTTP Server on http://127.0.0.1:8080
echo ==============================================================================
"%~dp0out\windows-msvc-debug\build\vinox-server.exe" --host 127.0.0.1 --port 8080 --model "C:\ai\models\OpenVINO\Qwen2.5-1.5B-Instruct-int4-ov" --cors
pause
