PUSHD %~dp0..\

@SET MSBUILD_FLAGS=
@IF "%1" NEQ "clean" goto BUILD
@SET MSBUILD_FLAGS=/t:Clean,Build
@ECHO Performing clean build
:BUILD

CALL bin/msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration="Release Static" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat IDEHelper\IDEHelper.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat IDEHelper\IDEHelper.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefBuild_d
IDE\dist\BeefBuild_boot -proddir=BeefBuild -config=Debug
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefBuild
IDE\dist\BeefBuild_d -proddir=BeefBuild -config=Release
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building IDE_bfd
@SET STATS_FILE=stats\IDE_Debug_build.csv
bin\RunWithStats IDE\dist\BeefBuild -proddir=IDE -clean -config=Debug_NoDeps
IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building IDE_bf
@SET STATS_FILE=stats\IDE_Release_build.csv
bin\RunWithStats IDE\dist\BeefBuild -proddir=IDE -clean -config=Release
IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
