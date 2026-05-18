C++ style rules:
Use NULL instead of nullptr
Instead of the stdint types such as "int32_t" use our int type aliases such as "int32" (the same without the _t).

BeefLang and C++ rules:
When you have a statement such as "if (a == b || c == d)", use extra parentheses such as "if ((a == b) || (c == d))"

LLDB Context:
The IDE uses line numbers starting from 0, whereas LLDB starts at 1. Adjust line numbers when necessary.
When we are debugging files that end with ".bf", that is Beef code.
Function names may come back from LLDB as "bf::namespace::name" but we should display that as "namespace.name" for Beef code only.
