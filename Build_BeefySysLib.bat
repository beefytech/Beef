@REM Builds native BeefySysLib (DLL) within this tree; post-build copies into BeefLibs\Beefy2D\dist.
@REM SolutionDir must be passed explicitly since we build the vcxproj directly.
PUSHD %~dp0

CALL bin\msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%~dp0 /v:q
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin\msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%~dp0 /v:q
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
