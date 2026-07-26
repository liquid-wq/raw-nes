@echo off
chcp 65001 >nul
title RAW-NES Installation
python install.py
if errorlevel 1 (
  echo.
  echo Python nicht gefunden. Bitte Python installieren: https://www.python.org/downloads/
  pause
)
