@SET P4_CHANGELIST=%1

@PUSHD %~dp0..\

@if not exist install goto NoRMDIR
rmdir /S /Q install
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
:NoRMDIR
mkdir install
IF %ERRORLEVEL% == 0 GOTO COPY
timeout 2
mkdir install
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

:COPY
xcopy IDE\dist\BeefIDE*.exe install\IDE\dist\
copy IDE\dist\BeefPerf*.exe install\IDE\dist\
copy IDE\dist\BeefySysLib64*.* install\IDE\dist\
copy IDE\x64\Debug\*.pdb install\IDE\dist\
copy IDE\x64\Release\*.pdb install\IDE\dist\
copy IDE\dist\IDEHelper*.* install\IDE\dist\
copy IDE\dist\Beef*RT*.* install\IDE\dist\
copy IDE\dist\Beef*Dbg*.* install\IDE\dist\
copy IDE\dist\libclang.dll install\IDE\dist\
copy IDE\dist\userdict.txt install\IDE\dist\
copy IDE\dist\BeefDbgVis.toml install\IDE\dist\
copy IDE\dist\en_*.* install\IDE\dist\
copy IDE\dist\Config.bfuser install\IDE\dist\
copy IDE\dist\BeefConfig_install.toml install\IDE\dist\BeefConfig.toml
xcopy /y /s /q Beefy2D install\Beefy2D\
xcopy /y /s /q IDEHelper\Tests install\IDEHelper\Tests\ /EXCLUDE:bin\xcopy_exclude_build.txt
xcopy /y /q IDEHelper\BeefProj.toml install\IDEHelper\
mkdir install\BeefySysLib
xcopy /y /q BeefySysLib\BeefProj.toml install\BeefySysLib\
mkdir install\Debugger64
xcopy /y Debugger64\BeefProj.toml install\Debugger64\
xcopy /y IDE\dist\shaders install\IDE\dist\shaders\
xcopy /y IDE\dist\images install\IDE\dist\images\
xcopy /y IDE\dist\fonts install\IDE\dist\\\fonts\
xcopy /y /s /q IDE\corlib install\IDE\corlib\
xcopy /y /s /q IDE\Resources install\IDE\Resources\
xcopy /y /s /q IDE\Tests install\IDE\Tests\ /EXCLUDE:bin\xcopy_exclude_build.txt
xcopy /y /s /q IDE\mintest\src install\IDE\mintest\src\
xcopy /y /s /q IDE\mintest\minlib install\IDE\mintest\minlib\
xcopy /y /s /q IDE\mintest\mintest2 install\IDE\mintest\mintest2\
xcopy /y /s /q IDE\mintest\LibA install\IDE\mintest\LibA\
xcopy /y /s /q IDE\mintest\LibB install\IDE\mintest\LibB\
copy IDE\mintest\Beef* install\IDE\mintest\
xcopy /y /s /q IDE\src install\IDE\src\
copy IDE\Beef*.toml install\IDE\
xcopy /y /q bin install\bin\

attrib /S -r install

mkdir builds
cd install
del ..\builds\build_%P4_CHANGELIST%.zip
"C:\Program Files\7-Zip\7z.exe" a -r ..\builds\build_%P4_CHANGELIST%.zip
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

GOTO :DONE

:FAILED
@ECHO FAILED!
POPD
EXIT /b %ERRORLEVEL%

:DONE
POPD
EXIT /B 0