PUSHD %~dp0

mkdir host

@ECHO ---- Building BeefySysLib (Debug) ----
CALL ../../bin/msbuild.bat ..\..\BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\..\..\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefySysLib (Release) ----
CALL ../../bin/msbuild.bat ..\..\BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\..\..\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

REM @ECHO ---- Building libhunspell (Debug) ----
REM CALL ../../bin/msbuild.bat ..\..\libhunspell\libhunspell.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\..\ /v:m
REM IF %ERRORLEVEL% NEQ 0 GOTO FAILED
REM @ECHO ---- Building libhunspell (Release) ----
REM CALL ../../bin/msbuild.bat ..\..\libhunspell\libhunspell.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\..\ /v:m
REM IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building IDEHelper (Debug) ----
CALL ../../bin/msbuild.bat ..\..\IDEHelper\IDEHelper.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\..\..\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building IDEHelper (Release) ----
CALL ../../bin/msbuild.bat ..\..\IDEHelper\IDEHelper.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\..\..\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building BeefBoot (Debug) ----
CALL ../../bin/msbuild.bat ..\..\BeefBoot\BeefBoot.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\..\..\ /v:m
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

CALL ../../bin/build_rt.bat
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building BeefBuild (bootstrapped) ----
BeefBoot_d.exe --out="BeefBuild_boot.exe" --src=..\src --src=..\..\BeefBuild\src --src=..\..\BeefLibs\corlib\src --src=..\..\BeefLibs\Beefy2D\src --define=CLI --define=DEBUG --startup=BeefBuild.Program --linkparams="Comdlg32.lib kernel32.lib user32.lib advapi32.lib shell32.lib Beef042RT64_d.lib IDEHelper64_d.lib BeefySysLib64_d.lib"
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building BeefBuild (Debug) ----
BeefBuild_boot.exe -proddir=..\..\BeefBuild -config=Debug -platform=Win64
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building BeefBuild (Release) ----
REM BeefBuild_boot.exe -proddir=..\..\BeefBuild -config=Release -platform=Win64
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Testing IDEHelper (Debug) ----
BeefBuild_d.exe -proddir=..\..\IDEHelper\Tests -test
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

@ECHO ---- Building IDE (Debug) ----
BeefBuild_boot.exe -proddir=..\ -config=Debug -platform=Win64
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
@ECHO ---- Building IDE (Release) ----
BeefBuild_boot.exe -proddir=..\ -config=Release -platform=Win64
IF %ERRORLEVEL% NEQ 0 GOTO FAILED

echo on

@ECHO ---- Copying files ----
rmdir /S /Q host\bk5
move host\bk4 host\bk5
move host\bk3 host\bk4
move host\bk2 host\bk3
move host\bk1 host\bk2
mkdir host\bk1
copy host\IDEHelper*.* host\bk1
copy host\Beef*RT*.* host\bk1
copy host\Beef*Dbg*.* host\bk1
copy host\BeefySysLib64*.* host\bk1
copy host\BeefIDE*.exe host\bk1

copy BeefIDE*.* host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy BeefySysLib64*.dll host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy BeefySysLib64*.pdb host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy IDEHelper*.dll host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy IDEHelper*.pdb host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy Beef*RT*.* host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy Beef*Dbg*.* host
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
copy userdict.txt host
@REM copy lib*.dll host
copy Beefy2D.dll host
copy BeefDbgVis.toml host
copy en_*.* host
copy BeefUser.toml host
copy BeefConfig_host.toml host\BeefConfig.toml
xcopy /y shaders host\shaders\
xcopy /y images host\images\
xcopy /y fonts host\fonts\
cd host
set _NO_DEBUG_HEAP=1
START BeefIDE_d.exe -proddir=c:\beef\ide 2>ErrorLog_bf_copy.txt
cd ..
IF %ERRORLEVEL% NEQ 0 GOTO FAILED
GOTO DONE

:FAILED
@ECHO FAILED!
POPD
PAUSE
EXIT /b %ERRORLEVEL%

:DONE
POPD
EXIT /B 0