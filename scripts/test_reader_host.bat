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

echo Running frontlight_peek host tests...
docker run --rm -v "%REPO_ROOT%:/src" -w /src/apps/common gcc:14-bookworm bash -c "gcc -DFRONTLIGHT_PEEK_HOST_TEST -I. test_frontlight_peek.c frontlight_peek.c -o /tmp/test_frontlight_peek && /tmp/test_frontlight_peek"
set ERR=%ERRORLEVEL%
if %ERR% NEQ 0 (
  echo frontlight_peek host tests FAILED
  exit /b %ERR%
)

echo Running Reader C host tests...
docker run --rm -v "%REPO_ROOT%:/src" -w /src/apps/Reader/tests gcc:14-bookworm bash -c "gcc -DHOST_TEST -I. -I.. test_host.c ../bar.c -o /tmp/test_host && /tmp/test_host"
set ERR=%ERRORLEVEL%
if %ERR% NEQ 0 (
  echo Reader C host tests FAILED
  exit /b %ERR%
)
exit /b 0
