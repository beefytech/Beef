#pragma once

#include "BeefySysLib/Common.h"

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4996)
#pragma warning(disable:4267)
#pragma warning(disable:4291)
#pragma warning(disable:4267)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#pragma warning(pop)

NS_BF_BEGIN

/// raw_null_ostream - A raw_ostream that discards all output.
class debug_ostream : public llvm::raw_ostream
{
	/// write_impl - See raw_ostream::write_impl.
	void write_impl(const char *Ptr, size_t size) override
	{
		StringT<1024> str;
		str.Append(Ptr, size);
		OutputDebugStr(str);
	}

	/// current_pos - Return the current position within the stream, not
	/// counting the bytes currently in the buffer.
	uint64_t current_pos() const override
	{
		return 0;
	}
};

NS_BF_END