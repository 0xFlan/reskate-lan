@echo off
rem Starts the Steam-free LAN server. --lan-auto stands down when another LAN server answers.
rem Everything typed after this file's name is passed to the server.
cd /d "%~dp0"
ReSkateServer.exe --lan-auto --no-update %*
rem Keep the window open, so a failure leaves its message readable.
pause
