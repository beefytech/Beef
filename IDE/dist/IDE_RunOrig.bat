copy IDE.exe host\IDE.exe
copy BeefySysLib64.* host
copy IDEHelper64.* host
copy libclang.dll host
copy userdict.txt host
copy lib*.dll host
copy Beefy2D.dll host
copy en_*.* host
xcopy /y shaders host\shaders\
xcopy /y images host\images\
xcopy /y fonts host\fonts\
cd host
set _NO_DEBUG_HEAP=1
START IDE.exe -proddir=c:\beef\ide 2>ErrorLog_bf_copy.txt