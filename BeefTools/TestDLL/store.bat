call ..\..\bin\p4index.cmd -source=. -symbols=. -debug
..\..\bin\symstore add /f x64\Debug\*.dll /s c:\BeefSyms /t TestDLL /compress 
..\..\bin\symstore add /f x64\Debug\*.pdb /s c:\BeefSyms /t TestDLL /compress 
copy x64\Debug\TestDLL.dll ..\..\IDE\dist\
