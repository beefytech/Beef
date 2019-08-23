@echo off

for /f "usebackq tokens=*" %%i in (`%~dp0\vswhere -prerelease -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
  set VcInstallDir=%%i
)

SET VsBuildDir=%VcInstallDir%\MSBuild\15.0
@IF EXIST "%VcInstallDir%\MSBuild\Current" SET VsBuildDir=%VcInstallDir%\MSBuild\Current

"%VsBuildDir%\Bin\MSBuild.exe" %*
