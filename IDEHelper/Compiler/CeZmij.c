// Compiles the vendored zmij float formatter (BeefRT/rt/zmij.c) into IDEHelper
// under renamed symbols for the comptime engine (CeMachine). The rename avoids
// duplicate symbol errors on platforms where libBeefRT.a and libIDEHelper.a are
// both statically linked into the same binary.
#define zmij_detail_write_float ce_zmij_detail_write_float
#define zmij_detail_write_double ce_zmij_detail_write_double
#include "../../BeefRT/rt/zmij.c"
