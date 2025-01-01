@ECHO OFF

@ECHO Downloading Emscripten...
..\bin\curl.exe -O https://www.beeflang.org/EmsdkDep1.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@ECHO Extracting Emscripten...
cd ..
bin\tar.exe -xf wasm\EmsdkDep1.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
del wasm\EmsdkDep1.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO Emscripten Installed!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
