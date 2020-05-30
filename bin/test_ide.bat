@ECHO --------------------------- Beef test_ide.bat Version 3 ---------------------------
@ECHO OFF
SETLOCAL EnableDelayedExpansion
@SET PATH=c:\Python27;%PATH%

@SET MSBUILD_FLAGS=
@IF "%1" NEQ "fast" goto SKIP
@SET FASTTEST=1
@ECHO Performing fast test (Win64/Debug only)
:SKIP

PUSHD %~dp0..\

@SET TESTPATH=IDE\Tests\CompileFail001
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\Test1
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\DebuggerTests
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\TestDynCrt1
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\SlotTest
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\BugW001
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\BugW002
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\BugW003
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@SET TESTPATH=IDE\Tests\IndentTest
@CALL :TEST
@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR

@GOTO :EMPTYTEST

:TEST
@FOR %%i IN (%TESTPATH%\scripts\*.txt) DO (
	@ECHO Testing %%i in BeefIDE_d - Win64
	%~dp0\RunAndWait %~dp0..\IDE\dist\BeefIDE_d.exe -proddir=%~dp0..\%TESTPATH% -test=%cd%\%%i
	@IF !ERRORLEVEL! NEQ 0 GOTO:EOF

	@IF !FASTTEST! NEQ 1 (		
		@ECHO Testing %%i in BeefIDE - Win64
		%~dp0\RunAndWait %~dp0..\IDE\dist\BeefIDE.exe -proddir=%~dp0..\%TESTPATH% -test=%cd%\%%i
		@IF !ERRORLEVEL! NEQ 0 GOTO:EOF	

		@ECHO Testing %%i - Win32
		%~dp0\RunAndWait %~dp0..\IDE\dist\BeefIDE_d.exe -proddir=%~dp0..\%TESTPATH% -test=%cd%\%%i -platform=Win32
		@IF !ERRORLEVEL! NEQ 0 GOTO:EOF
	)
)
GOTO:EOF

:EMPTYTEST
@PUSHD %cd%\IDE\Tests\EmptyTest
@FOR %%i IN (scripts\*.txt) DO (
	@ECHO Testing IDE\Tests\EmptyTest\%%i in BeefIDE_d - Win64
	%~dp0\RunAndWait %~dp0\..\IDE\dist\BeefIDE_d.exe -test=%cd%\%%i
	@IF !ERRORLEVEL! NEQ 0 GOTO HADERROR_EMPTY
)
@POPD

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR_EMPTY
@POPD

:HADERROR
@ECHO #### FAILED ####
@POPD
@EXIT /b %ERRORLEVEL%