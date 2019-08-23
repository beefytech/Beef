#pragma once

#include "BeefySysLib/Common.h"
//#include "config.h"
//#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <map>

typedef uint64_t addr_t;
typedef uint16_t tag_t;

namespace llvm {
class BasicBlock;
class ExecutionEngine;
class Function;
class Module;
class PointerType;
class StructType;
class Value;
}

enum RegForm : int8
{
	RegForm_Invalid = -1,
	RegForm_Unknown,
	RegForm_SByte,
	RegForm_SByte16,
	RegForm_Byte,
	RegForm_Byte16,
	RegForm_Short,
	RegForm_Short8,
	RegForm_UShort,
	RegForm_UShort8,
	RegForm_Int,
	RegForm_Int4,
	RegForm_UInt,
	RegForm_UInt4,
	RegForm_Long,
	RegForm_Long2,
	RegForm_ULong,
	RegForm_ULong2,
	RegForm_Float,
	RegForm_Float4,
	RegForm_Float8,
	RegForm_Double,
	RegForm_Double2,
	RegForm_Double4,
};

