C++ rules:
Use NULL instead of nullptr
Instead of the stdint types such as "int32_t" use our int type aliases such as "int32" (the same without the _t).
BeefySysLib contains many system abstractions and "helper classes". The lowest level interfaces are declared in PlatformInterface.h, including file IO, threads, process and spawning.

When spawning a process, we can interact with it's stdio/stdout/stderr by using BfpSpawn_Create with BfpSpawnFlag_RedirectStdOutput | BfpSpawnFlag_RedirectStdError. It should probably also be made invisible with BfpSpawnFlag_NoWindow. Since you can't use async reading on those handles (use BfpSpawn_GetStdHandles), you must uses threads (BfpThread_Create) to read/write from those. In the read thread use BfpFile_Read, where acceptable results are BfpFileResult_Ok or BfpFileResult_PartialData, otherwise the process has died and you can exit. When BfpSpawn_WaitFor indicates thes process is dead, close those handles (which will cause the reads/writes in the threads to return errors and stop blocking), then BfpThread_WaitFor, BfpThread_Release, and BfpSpawn_Release.

Use Beefy::String (BeefySysLib/util/String.h) for strings. When used as arguments we generally pass those as "const StringImpl&".

Use Beefy::Array (BeefySysLib/util/Array.h) for an auto-resizing contiguous collection, similar to std::vector.

Use Beefy::Dictionary (BeefySysLib/util/Dictionary.h) for a hashed key/value dictionary.

For thread synchronization, we generally use critical sections rather than mutexes, and that functionality is wrapped up in Beefy::CritSect (BeefySysLib\util\CritSect). We usually use AutoCrit to get RAII critical scoped sections instead of manually calling Lock/Unlock. For thread "wait events", we wrap that in Beefy::SyncEvent.

BeefLang and C++ rules:
When you have a statement such as "if (a == b || c == d)", use extra parentheses such as "if ((a == b) || (c == d))"

LLDB Context:
The IDE uses line numbers starting from 0, whereas LLDB starts at 1. Adjust line numbers when necessary.
When we are debugging files that end with ".bf", that is Beef code.
Function names may come back from LLDB as "bf::namespace::name" but we should display that as "namespace.name" for Beef code only.
