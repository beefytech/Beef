copy IDE_bfd.exe host\IDE_bfd.exe
copy BeefySysLib64_d.* host
copy IDEHelper64_d.* host
copy libclang.dll host
copy userdict.txt host
@REM copy lib*.dll host
copy Beefy2D.dll host
copy Standard.dbgvis host
copy en_*.* host
xcopy /y shaders host\shaders\
xcopy /y images host\images\
xcopy /y fonts host\fonts\
cd host
set _NO_DEBUG_HEAP=1
START IDE_bfd.exe -proddir=c:\beef\ide 2>ErrorLog_bf_copy.txt