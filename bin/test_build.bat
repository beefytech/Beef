
@ECHO --------------------------- Beef Test_Build.Bat Version 1 ---------------------------

@SET P4_CHANGELIST=%1

PUSHD %~dp0..\

md stats

@REM GOTO RANDO


@ECHO Testing IDEHelper\Tests
CALL bin/msbuild.bat IDEHelper\Tests\CLib\CLib.vcxproj /p:Configuration=Debug /p:Platform=x64
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
IDE\dist\BeefBuild_d -proddir=IDEHelper\Tests -test
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
IDE\dist\BeefBuild_d -proddir=IDEHelper\Tests\BeefLinq -test
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Testing IDEHelper\Tests (Win32)
CALL bin/msbuild.bat IDEHelper\Tests\CLib\CLib.vcxproj /p:Configuration=Debug /p:Platform=x86
IDE\dist\BeefBuild_d -proddir=IDEHelper\Tests -test -platform=Win32
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Testing SysMSVCRT
del IDE\Tests\SysMSVCRT\build\Debug_Win64\SysMSVCRT\SysMSVCRT.exe
IDE\dist\BeefBuild_d -proddir=IDE\Tests\SysMSVCRT
IDE\Tests\SysMSVCRT\build\Debug_Win64\SysMSVCRT\SysMSVCRT.exe 1000 234
@IF %ERRORLEVEL% NEQ 1234 GOTO HADERROR

@ECHO Testing IDE\corlib
IDE\dist\BeefBuild_d -proddir=BeefLibs\corlib -test
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefIDE_d with BeefBuild_d
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\IDE_Debug_d.csv
bin\RunWithStats IDE\dist\BeefBuild_d -proddir=IDE -clean -config=Debug_NoDeps
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\IDE_Debug_d_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\IDE_Debug_d_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE

@ECHO Building BeefIDE_d with BeefBuild
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\IDE_Debug.csv
bin\RunWithStats IDE\dist\BeefBuild -proddir=IDE -clean -config=Debug_NoDeps -noir
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\IDE_Debug_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\IDE_Debug_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE

@ECHO Building BeefIDE with BeefBuild
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\IDE_Release.csv
bin\RunWithStats IDE\dist\BeefBuild -proddir=IDE -clean -config=Release -noir
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\IDE_Release_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\IDE_Release_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE

:RANDO

@PUSHD IDE\Tests\Rando
%~dp0..\BeefTools\RandoCode\RandoCode Scripts\Test002.toml
@POPD
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@SET STATS_NAME=Rando001
@SET STATS_PRODDIR=IDE\Tests\Rando
@REM CALL :PROFILE_BUILD
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@SET STATS_NAME=Rando001
@SET STATS_PRODDIR=IDE\Tests\Rando
CALL :PROFILE_IDE
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:PROFILE_BUILD
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\%STATS_NAME%_Debug_d.csv
bin\RunWithStats IDE\dist\BeefBuild_d -proddir=%STATS_PRODDIR% -clean -config=Debug
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\%STATS_NAME%_Debug_d_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\%STATS_NAME%_Debug_d_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE
@REM --------
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\%STATS_NAME%_Debug.csv
bin\RunWithStats IDE\dist\BeefBuild -proddir=%STATS_PRODDIR% -clean -config=Debug -noir
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\%STATS_NAME%_Debug_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\%STATS_NAME%_Debug_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE
@REM --------
GOTO:EOF

:PROFILE_IDE
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="Nop()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_START
@SET STATS_FILE=stats\%STATS_NAME%_Debug_IDE.csv
bin\RunWithStats IDE\dist\BeefIDE -proddir=%STATS_PRODDIR% -test=%~dp0..\IDE\Tests\scripts\DebugAndExit.txt
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_RUN
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="SelectLastSession(); SaveSession(@""%~dp0..\stats\%STATS_NAME%_Debug_IDE_%P4_CHANGELIST%.bfps""); SaveEntrySummary(""BfCompiler_Compile"", @""%~dp0..\stats\%STATS_NAME%_Debug_IDE_%P4_CHANGELIST%.txt""); CloseIfAutoOpened()"
@IF %ERRORLEVEL% NEQ 0 GOTO FAILPERF_DONE
@REM --------
@SET STATS_FILE=stats\%STATS_NAME%_Debug_IDE_NoPerf.csv
bin\RunWithStats IDE\dist\BeefIDE -proddir=%STATS_PRODDIR% -test=%~dp0..\IDE\Tests\scripts\DebugAndExit.txt
GOTO:EOF

:FAILPERF_START
@ECHO !!!!!PROFILING FAILED!!!!!
@POPD
@EXIT /b 1

:FAILPERF_RUN
@ECHO !!!!!FAILED (WHILE PROFILING)!!!!!
bin\RunAndWait IDE\dist\BeefPerf.exe -cmd="CloseIfAutoOpened()"
@POPD
@EXIT /b 1

:FAILPERF_DONE
@ECHO !!!!!PROFILING FAILED (AFTER DONE)!!!!!
@POPD
@EXIT /b 1

:HADERROR
@ECHO !!!!!FAILED!!!!!
@POPD
@EXIT /b %ERRORLEVEL%
