mkdir host

rmdir /S /Q host\bk5
move host\bk4 host\bk5
move host\bk3 host\bk4
move host\bk2 host\bk3
move host\bk1 host\bk2
mkdir host\bk1
copy host\IDEHelper64*.* host\bk1
copy host\BeefRT64*.* host\bk1
copy host\BeefySysLib64*.* host\bk1
copy host\IDE_bf*.exe host\bk1

copy IDE_bf*.* host
IF ERRORLEVEL 1 GOTO FAILED
copy BeefySysLib64*.* host
copy ..\x64\Debug\*.pdb host
copy ..\x64\Release\*.pdb host
copy IDEHelper64*.* host
copy BeefRT*.* host
copy libclang.dll host
copy userdict.txt host
@REM copy lib*.dll host
copy Beefy2D.dll host
copy Standard.dbgvis host
copy en_*.* host
copy Config.bfuser host
xcopy /y shaders host\shaders\
xcopy /y images host\images\
xcopy /y fonts host\fonts\
cd host
set _NO_DEBUG_HEAP=1
START IDE_bfd.exe -proddir=c:\beef\ide 2>ErrorLog_bf_copy.txt
IF ERRORLEVEL 1 GOTO FAILED
GOTO DONE

:FAILED
@ECHO FAILED!
PAUSE
EXIT

:DONE