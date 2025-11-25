@echo off
set PORT=9000
set R=4
set X=10
set Z=30
set W=200
set LABEL=200
set B=0.05

start "" build\robots.exe %PORT% %R% %X% %Z% %W% %LABEL% %B%
timeout /t 1 >nul
build\vision.exe 127.0.0.1 %PORT% 15 %Z% 1234
