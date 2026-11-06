@echo off
set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..
pushd "%REPO_ROOT%\apps\Reader\tests"

python test_host.py
set ERR=%ERRORLEVEL%
if %ERR% NEQ 0 (
  echo Host tests FAILED
  popd
  exit /b %ERR%
)
echo Host tests OK
popd
exit /b 0
