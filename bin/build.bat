@ECHO --------------------------- Beef Build.Bat Version 5 ---------------------------

@SET P4_CHANGELIST=%1

PUSHD %~dp0..\

@SET MSBUILD_FLAGS=
@IF "%1" NEQ "clean" goto BUILD
@SET MSBUILD_FLAGS=/t:Clean,Build
@ECHO Performing clean build
:BUILD

@IF EXIST stats GOTO STATS_HAS
mkdir stats
:STATS_HAS

@IF EXIST extern\llvm_win64_13_0_1\_Done.txt GOTO LLVM_HAS
call extern\llvm_build.bat
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
:LLVM_HAS

@IF EXIST IDE\dist\llvm\bin\lld-link.exe GOTO LLD_HAS
@ECHO ========== MISSING LLVM TOOLS ==========
@ECHO IDE\dist\llvm\bin\lld-link.exe not found. Copy in from a Beef install or an LLVM/Clang install.
@GOTO HADERROR
:LLD_HAS

copy BeefLibs\SDL2\dist\SDL2.dll IDE\dist
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefySysLib\BeefySysLib.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat IDEHelper\IDEHelper.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat IDEHelper\IDEHelper.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/build_rt.bat %1
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefBoot\BeefBoot.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

CALL bin/msbuild.bat BeefBoot\BeefBoot.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=%cd%\ /v:m %MSBUILD_FLAGS%
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefBuild_bootd
IDE\dist\BeefBoot_d.exe --out="IDE\dist\BeefBuild_bootd.exe" --src=IDE\src --src=BeefBuild\src --src=BeefLibs\corlib\src --src=BeefLibs\Beefy2D\src --src=BeefLibs\libgit2\src --define=CLI --define=DEBUG --startup=BeefBuild.Program --linkparams="Comdlg32.lib kernel32.lib user32.lib advapi32.lib shell32.lib IDE\dist\Beef042RT64_d.lib IDE\dist\IDEHelper64_d.lib IDE\dist\BeefySysLib64_d.lib"
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefBuild_boot
IDE\dist\BeefBoot.exe --out="IDE\dist\BeefBuild_boot.exe" --src=IDE\src --src=BeefBuild\src --src=BeefLibs\corlib\src --src=BeefLibs\Beefy2D\src --src=BeefLibs\libgit2\src --define=CLI --define=RELEASE --startup=BeefBuild.Program --linkparams="Comdlg32.lib kernel32.lib user32.lib advapi32.lib shell32.lib IDE\dist\Beef042RT64.lib IDE\dist\IDEHelper64.lib IDE\dist\BeefySysLib64.lib"
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

@ECHO Building RandoCode
IDE\dist\BeefBuild_d -proddir=BeefTools\RandoCode -config=Release
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@ECHO Building BeefPerf
IDE\dist\BeefBuild_d -proddir=BeefTools\BeefPerf -config=Release
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
