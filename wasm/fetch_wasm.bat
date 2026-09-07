@ECHO OFF

@ECHO Downloading Emscripten...
..\bin\curl.exe -O https://www.beeflang.org/EmsdkDep2.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@ECHO Extracting Emscripten...
cd ..
bin\tar.exe -xf wasm\EmsdkDep2.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
del wasm\EmsdkDep2.zip
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO Emscripten Installed!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
