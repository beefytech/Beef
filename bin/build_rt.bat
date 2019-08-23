PUSHD %~dp0..\

@ECHO @@@@@@@@@@@@@@ Win64 @@@@@@@@@@@@@@

@ECHO ---- Building BeefRT64 (Debug) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT64 (Debug Static) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Debug Static" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT64 (Debug Static CStatic) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Debug Static CStatic" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT64 (Release) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT64 (Release Static) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Release Static" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT6 (Release Static CStatic) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Release Static CStatic" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building BeefDbg64 (Debug) ----
CALL bin\msbuild.bat BeefRT\BeefDbg\BeefDbg.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefDbg64 (Release) ----
CALL bin\msbuild.bat BeefRT\BeefDbg\BeefDbg.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building MinRT (Debug) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Debug GUI) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration="Debug GUI" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Release) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Release GUI) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration="Release GUI" /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO @@@@@@@@@@@@@ Win32 @@@@@@@@@@@@@@

@ECHO ---- Building BeefRT32 (Debug) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT32 (Debug Static) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Debug Static" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT32 (Debug Static CStatic) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Debug Static CStatic" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT32 (Release) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT32 (Release Static) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Release Static" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefRT32 (Release Static CStatic) ----
CALL bin\msbuild.bat BeefRT\BeefRT.vcxproj /p:Configuration="Release Static CStatic" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building BeefDbg32 (Debug) ----
CALL bin\msbuild.bat BeefRT\BeefDbg\BeefDbg.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefDbg32 (Release) ----
CALL bin\msbuild.bat BeefRT\BeefDbg\BeefDbg.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building MinRT (Debug) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Debug GUI) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration="Debug GUI" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Release) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building MinRT (Release GUI) ----
CALL bin\msbuild.bat BeefRT\MinRT\MinRT.vcxproj /p:Configuration="Release GUI" /p:Platform=Win32 /p:SolutionDir=%cd%\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

GOTO DONE

:FAILED
@ECHO FAILED BEEFRT!
POPD
PAUSE
EXIT /b %ERRORLEVEL%

:DONE
POPD