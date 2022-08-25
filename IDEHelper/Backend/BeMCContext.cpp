#if defined _DEBUG || true
#define BEMC_ASSERT(_Expression) (void)( (!!(_Expression)) || (AssertFail(#_Expression, __LINE__), 0) )
#else
#define BEMC_ASSERT(_Expression) (void)(0)
#endif

#include <deque>
#include "BeMCContext.h"
#include "BeCOFFObject.h"
#include "BeIRCodeGen.h"
#include "../Compiler/BfIRCodeGen.h"
#include "BeefySysLib/util/BeefPerf.h"

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(disable:4146)

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

#define IS_BIT_SET_32(bits, idx) (((bits)[(idx) / 32] & (1 << ((idx) % 32))) != 0)

// Only use "rep stosb"/"rep movsb" if total size is at least this value.  The drawback is requiring saving/restore of RAX, RDI, and RCX (and RSI for movsb)
const int BF_REP_MOV_LIMIT = 128;

static const X64CPURegister gVolatileRegs[] = { X64Reg_RAX, X64Reg_RCX, X64Reg_RDX, X64Reg_R8, X64Reg_R9, X64Reg_R10, X64Reg_R11,
	X64Reg_XMM0_f64, X64Reg_XMM1_f64, X64Reg_XMM2_f64, X64Reg_XMM3_f64, X64Reg_XMM4_f64, X64Reg_XMM5_f64 };

static const char* gOpName[] =
{
	"None",
	"@Def",
	"@DefLoad",
	"@DefPhi",
	"@DbgDecl",
	"@LifetimeExtend",
	"@LifetimeStart",
	"@LifetimeEnd",
	"@LifetimeSoftEnd",
	"@ValueScopeSoftEnd",
	"@ValueScopeHardEnd",
	"@Label",
	//"PhiValue",
	"CmpToBool",
	"MemSet",
	"MemCpy",
	"FastCheckStack",
	"TLSSetup",
	"PreserveVolatiles",
	"RestoreVolatiles",
	"Unwind_PushReg",
	"Unwind_SaveXMM",
	"Unwind_Alloc",
	"Unwind_SetBP",
	"Rem",
	"IRem",

	"Nop",
	"Unreachable",
	"EnsureCodeAt",
	"DbgBreak",
	"MFence",
	"Mov",
	"MovRaw",
	"MovSX",
	"XChg",
	"XAdd",
	"CmpXChg",
	"Load",
	"Store",
	"Push",
	"Pop",
	"Neg",
	"Not",
	"Add",
	"Sub",
	"Mul",
	"IMul",
	"Div",
	"IDiv",
	"Cmp",
	"And",
	"Or",
	"Xor",
	"Shl",
	"Shr",
	"Sar",
	"Test",
	"CondBr",
	"Br",
	"Ret",
	"Call",
};

static_assert(BF_ARRAY_COUNT(gOpName) == (int)BeMCInstKind_COUNT, "gOpName incorrect size");

void PrintBoolVec(Array<bool>& boolVec)
{
	String str;
	for (int i = 0; i < (int)boolVec.size(); i++)
	{
		str += StrFormat("%d: %s\n", i, boolVec[i] ? "true" : "false");
	}
	OutputDebugStr(str);
}

static bool IsPowerOfTwo(int64 val)
{
	return (val != 0) && ((val & (val - 1)) == 0);
}

bool BeVTrackingBits::IsSet(int idx)
{
	return IS_BIT_SET_32((uint32*)this, idx);
}

void BeVTrackingBits::Set(int idx)
{
	uint32* bits = (uint32*)this;
	bits[(idx) / 32] |= (1 << (idx % 32));
}

void BeVTrackingBits::Clear(int idx)
{
	uint32* bits = (uint32*)this;
	bits[(idx) / 32] &= ~(1 << (idx % 32));
}

BeVTrackingContext::BeVTrackingContext(BeMCContext* mcContext)
{
	mMCContext = mcContext;
	mNumEntries = 0;
	mNumItems = 0;
	mNumBlocks = -1;
	mTrackKindCount = (int)BeTrackKind_COUNT;
}

void BeVTrackingContext::Init(int numItems)
{
	mNumItems = numItems;
	mNumEntries = mNumItems * mTrackKindCount;
	mNumBlocks = (mNumEntries + 31) / 32;
}

void BeVTrackingContext::Clear()
{
	mAlloc.Clear();
	mNumBlocks = -1;
}

int BeVTrackingContext::GetBitsBytes()
{
	return mNumBlocks * 4;
}

int BeVTrackingContext::GetIdx(int baseIdx, BeTrackKind liveKind)
{
	return baseIdx + mNumItems * (int)liveKind;
}

void BeVTrackingContext::Print(BeVTrackingList* list)
{
	String str;
	for (int i : *list)
		str += StrFormat("%d ", i);
	if (list->mNumChanges > 0)
	{
		str += " |";
		for (int changeIdx = 0; changeIdx < list->mNumChanges; changeIdx++)
		{
			int change = list->GetChange(changeIdx);
			if (change >= 0)
				str += StrFormat(" +%d", change);
			else
				str += StrFormat(" -%d", -change - 1);
		}
	}
	str += "\n";
	OutputDebugStr(str);
}

BeVTrackingList* BeVTrackingContext::AllocEmptyList()
{
	int allocBytes = sizeof(int) * (2);
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;
	newList->mSize = 0;
	newList->mNumChanges = 0;
	return newList;
}

BeVTrackingList* BeVTrackingContext::AddFiltered(BeVTrackingList* list, SizedArrayImpl<int>& filteredAdds, bool perserveChangeList)
{
	int newSize = list->mSize + filteredAdds.size();
	int allocBytes = sizeof(int) * (2 + newSize);
	if (perserveChangeList)
		allocBytes += sizeof(int) * (int)(list->mNumChanges + filteredAdds.size());
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;

	{
		if (filteredAdds.size() > 1)
			std::sort(filteredAdds.begin(), filteredAdds.end());

		int addIdx = 0;
		int nextAdd;
		if (addIdx < (int)filteredAdds.size())
			nextAdd = filteredAdds[addIdx++];
		else
			nextAdd = 0x7FFFFFFF;

		int* outPtr = &newList->mEntries[0];
		for (auto idx : *list)
		{
			while (idx > nextAdd)
			{
				*(outPtr++) = nextAdd;
				if (addIdx < (int)filteredAdds.size())
					nextAdd = filteredAdds[addIdx++];
				else
					nextAdd = 0x7FFFFFFF;
			}
			*(outPtr++) = idx;
		}
		while (nextAdd != 0x7FFFFFFF)
		{
			*(outPtr++) = nextAdd;
			if (addIdx >= (int)filteredAdds.size())
				break;
			nextAdd = filteredAdds[addIdx++];
		}
	}

	newList->mSize = newSize;

	if (perserveChangeList)
	{
		for (int changeIdx = 0; changeIdx < list->mNumChanges; changeIdx++)
		{
			newList->mEntries[newSize + changeIdx] = list->GetChange(changeIdx);
		}

		for (int changeIdx = 0; changeIdx < (int)filteredAdds.size(); changeIdx++)
		{
			newList->mEntries[newSize + list->mNumChanges + changeIdx] = filteredAdds[changeIdx];
		}

		newList->mNumChanges = list->mNumChanges + (int)filteredAdds.size();
	}
	else
		newList->mNumChanges = 0;

	return newList;
}

BeVTrackingList* BeVTrackingContext::AddFiltered(BeVTrackingList* list, int idx, bool perserveChangeList)
{
	int newSize = list->mSize + 1;
	int allocBytes = sizeof(int) * (2 + newSize);
	if (perserveChangeList)
		allocBytes += sizeof(int) * (int)(list->mNumChanges + 1);
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;

	{
		int addIdx = 0;
		int nextAdd;
		nextAdd = idx;

		int* outPtr = &newList->mEntries[0];
		for (auto idx : *list)
		{
			while (idx > nextAdd)
			{
				*(outPtr++) = nextAdd;
				nextAdd = 0x7FFFFFFF;
			}
			*(outPtr++) = idx;
		}
		while (nextAdd != 0x7FFFFFFF)
		{
			*(outPtr++) = nextAdd;
			break;
		}
	}

	newList->mSize = newSize;

	if (perserveChangeList)
	{
		for (int changeIdx = 0; changeIdx < list->mNumChanges; changeIdx++)
		{
			newList->mEntries[newSize + changeIdx] = list->GetChange(changeIdx);
		}

		newList->mEntries[newSize + list->mNumChanges] = idx;
		newList->mNumChanges = list->mNumChanges + (int)1;
	}
	else
		newList->mNumChanges = 0;

	return newList;
}

BeVTrackingList* BeVTrackingContext::Add(BeVTrackingList* list, const SizedArrayImpl<int>& indices, bool perserveChangeList)
{
	SizedArray<int, 16> newIndices;
	for (int idx : indices)
	{
		if (!IsSet(list, idx))
		{
			newIndices.push_back(idx);
		}
	}
	if (newIndices.empty())
		return list;
	return AddFiltered(list, newIndices, perserveChangeList);
}

BeVTrackingList* BeVTrackingContext::Add(BeVTrackingList* list, int idx, bool perserveChangeList)
{
	if (IsSet(list, idx))
		return list;
	return AddFiltered(list, idx, perserveChangeList);
}

// Performs an 'add' for items that were in prevDest
BeVTrackingList* BeVTrackingContext::SetChanges(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom)
{
	int removeCount = 0;
	int addCount = 0;

	int newSize = prevDestEntry->mSize;
	auto prevItr = prevDestEntry->begin();
	auto prevEnd = prevDestEntry->end();
	auto mergeFromItr = mergeFrom->begin();
	auto mergeFromEnd = mergeFrom->end();
	while ((prevItr != prevEnd) && (mergeFromItr != mergeFromEnd))
	{
		int prevIdx = *prevItr;
		int mergeIdx = *mergeFromItr;
		bool done = false;

		while (mergeIdx < prevIdx)
		{
			removeCount++;
			++mergeFromItr;
			if (mergeFromItr == mergeFromEnd)
			{
				done = true;
				break;
			}
			mergeIdx = *mergeFromItr;
		}
		if (done)
			break;

		while (prevIdx < mergeIdx)
		{
			addCount++;
			++prevItr;
			if (prevItr == prevEnd)
			{
				done = true;
				break;
			}
			prevIdx = *prevItr;
		}
		if (done)
			break;
		if (prevIdx == mergeIdx)
		{
			++prevItr;
			++mergeFromItr;
		}
	}
	while (prevItr != prevEnd)
	{
		addCount++;
		++prevItr;
	}
	while (mergeFromItr != mergeFromEnd)
	{
		removeCount++;
		++mergeFromItr;
	}

	int allocBytes = sizeof(int) * (2 + newSize + addCount + removeCount);
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;
	int* outPtr = &newList->mEntries[0];

	int* changePtr = &newList->mEntries[newSize];

	prevItr = prevDestEntry->begin();
	mergeFromItr = mergeFrom->begin();
	while ((prevItr != prevEnd) && (mergeFromItr != mergeFromEnd))
	{
		int prevIdx = *prevItr;
		int mergeIdx = *mergeFromItr;
		bool done = false;

		while (mergeIdx < prevIdx)
		{
			*(changePtr++) = ~mergeIdx;
			++mergeFromItr;
			if (mergeFromItr == mergeFromEnd)
			{
				done = true;
				break;
			}
			mergeIdx = *mergeFromItr;
		}
		if (done)
			break;

		while (prevIdx < mergeIdx)
		{
			*(outPtr++) = prevIdx;
			*(changePtr++) = prevIdx;
			++prevItr;
			if (prevItr == prevEnd)
			{
				done = true;
				break;
			}
			prevIdx = *prevItr;
		}
		if (done)
			break;

		if (prevIdx == mergeIdx)
		{
			*(outPtr++) = *prevItr;
			++prevItr;
			++mergeFromItr;
		}
	}
	while (prevItr != prevEnd)
	{
		int prevIdx = *prevItr;
		*(outPtr++) = *prevItr;
		*(changePtr++) = prevIdx;
		++prevItr;
	}
	while (mergeFromItr != mergeFromEnd)
	{
		int mergeIdx = *mergeFromItr;
		*(changePtr++) = ~mergeIdx;
		++mergeFromItr;
	}

	BF_ASSERT((outPtr - &newList->mEntries[0]) == newSize);
	BF_ASSERT((changePtr - &newList->mEntries[newSize]) == addCount + removeCount);
	newList->mSize = newSize;
	newList->mNumChanges = addCount + removeCount;
	return newList;
}

BeVTrackingList* BeVTrackingContext::ClearFiltered(BeVTrackingList* list, const SizedArrayImpl<int>& indices)
{
	/*int newSize = list->mSize - indices.size();
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(sizeof(int) * (1 + newSize));
	if (indices.size() == 1)
	{
		int findIdx0 = indices[0];
		int* outPtr = 0;
		for (auto idx : indices)
		{
			if (idx != findIdx0)
				*(outPtr++) = idx;
		}
		BF_ASSERT(outPtr == &newList->mEntries[0] + indices.size());
	}
	else if (indices.size() == 2)
	{
		int findIdx0 = indices[0];
		int findIdx1 = indices[1];
		int* outPtr = 0;
		for (auto idx : indices)
		{
			if ((idx != findIdx0) && (idx != findIdx1))
				*(outPtr++) = idx;
		}
		BF_ASSERT(outPtr == &newList->mEntries[0] + indices.size());
	}
	else
	{
		int* outPtr = 0;
		for (auto idx : indices)
		{
			if (std::find(indices.begin(), indices.end(), idx) == indices.end())
				*(outPtr++) = idx;
		}
		BF_ASSERT(outPtr == &newList->mEntries[0] + indices.size());
	}
	newList->mSize = newSize;
	return newList;*/
	BF_FATAL("Broken");
	return NULL;
}

BeVTrackingList* BeVTrackingContext::Modify(BeVTrackingList* list, const SizedArrayImpl<int>& inAdds, const SizedArrayImpl<int>& inRemoves, SizedArrayImpl<int>& filteredAdds, SizedArrayImpl<int>& filteredRemoves, bool preserveChanges)
{
	for (int idx : inAdds)
	{
		if (!IsSet(list, idx))
		{
			filteredAdds.push_back(idx);
		}
	}

	for (int idx : inRemoves)
	{
		if (IsSet(list, idx))
		{
			filteredRemoves.push_back(idx);
		}
	}

	if ((filteredAdds.empty()) && (filteredRemoves.empty()))
		return list;

	int newSize = list->mSize - filteredRemoves.size() + filteredAdds.size();
	int changeSize;
	if (preserveChanges)
		changeSize = list->mNumChanges;
	else
		changeSize = filteredRemoves.size() + filteredAdds.size();

	int allocBytes = sizeof(int) * (2 + newSize + changeSize);
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;

	if (filteredAdds.size() > 1)
		std::sort(filteredAdds.begin(), filteredAdds.end());
	if (filteredRemoves.size() > 1)
		std::sort(filteredRemoves.begin(), filteredRemoves.end());

	int addIdx = 0;
	int nextAdd;
	if (addIdx < (int)filteredAdds.size())
		nextAdd = filteredAdds[addIdx++];
	else
		nextAdd = 0x7FFFFFFF;

	int removeIdx = 0;
	int nextRemove;
	if (removeIdx < (int)filteredRemoves.size())
		nextRemove = filteredRemoves[removeIdx++];
	else
		nextRemove = 0x7FFFFFFF;

	int* outPtr = &newList->mEntries[0];
	for (auto idx : *list)
	{
		if (idx == nextRemove)
		{
			if (removeIdx < (int)filteredRemoves.size())
				nextRemove = filteredRemoves[removeIdx++];
			else
				nextRemove = 0x7FFFFFFF;
			continue;
		}
		while (idx > nextAdd)
		{
			*(outPtr++) = nextAdd;
			if (addIdx < (int)filteredAdds.size())
				nextAdd = filteredAdds[addIdx++];
			else
				nextAdd = 0x7FFFFFFF;
		}
		*(outPtr++) = idx;
	}
	while (nextAdd != 0x7FFFFFFF)
	{
		*(outPtr++) = nextAdd;
		if (addIdx >= (int)filteredAdds.size())
			break;
		nextAdd = filteredAdds[addIdx++];
	}
	BF_ASSERT(outPtr == &newList->mEntries[0] + newSize);
	BF_ASSERT((nextAdd = 0x7FFFFFFF) && (nextRemove == 0x7FFFFFFF));

	if (preserveChanges)
	{
		for (int i = 0; i < list->mNumChanges; i++)
			newList->mEntries[newSize + i] = list->mEntries[list->mSize + i];
	}
	else
	{
		for (int i = 0; i < (int)filteredRemoves.size(); i++)
		{
			newList->mEntries[newSize + i] = -filteredRemoves[i] - 1;
		}
		for (int i = 0; i < (int)filteredAdds.size(); i++)
		{
			newList->mEntries[newSize + filteredRemoves.size() + i] = filteredAdds[i];
		}
	}
	newList->mSize = newSize;
	newList->mNumChanges = changeSize;

	///
	/*for (int i = 0; i < mNumEntries; i++)
		BF_ASSERT(IsSet(newList, i) == unit.mBits->IsSet(i));
	int prevIdx = -1;
	for (int idx : *newList)
	{
		BF_ASSERT(idx > prevIdx);
		prevIdx = idx;
	}
	OutputDebugStrF("Modify %d %@\n", modifyItrIdx, newList);*/
	///

	return newList;
}

int BeVTrackingContext::FindIndex(BeVTrackingList* entry, int val)
{
	int lo = 0;
	int hi = entry->mSize - 1;

	while (lo <= hi)
	{
		int i = (lo + hi) / 2;
		int midVal = entry->mEntries[i];
		int c = midVal - val;
		if (c == 0) return i;
		if (c < 0)
			lo = i + 1;
		else
			hi = i - 1;
	}
	return ~lo;
}

bool BeVTrackingContext::IsSet(BeVTrackingList* entry, int idx)
{
	return FindIndex(entry, idx) >= 0;
}

bool BeVTrackingContext::IsSet(BeVTrackingList* entry, int idx, BeTrackKind trackKind)
{
	return IsSet(entry, GetIdx(idx, trackKind));
}

BeVTrackingList* BeVTrackingContext::Clear(BeVTrackingList* list, const SizedArrayImpl<int>& indices)
{
	SizedArray<int, 16> newIndices;
	for (int idx : indices)
	{
		if (IsSet(list, idx))
		{
			newIndices.push_back(idx);
		}
	}
	if (newIndices.empty())
		return list;
	return ClearFiltered(list, newIndices);
}

bool BeVTrackingContext::IsEmpty(BeVTrackingList* list)
{
	return list->mSize == 0;
}

BeVTrackingList* BeVTrackingContext::Merge(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom)
{
	if (prevDestEntry == NULL)
		return mergeFrom;
	if (mergeFrom->mSize == 0)
		return prevDestEntry;
	if (prevDestEntry->mSize == 0)
		return mergeFrom;

	int newSize = prevDestEntry->mSize;
	auto prevItr = prevDestEntry->begin();
	auto prevEnd = prevDestEntry->end();
	auto mergeFromItr = mergeFrom->begin();
	auto mergeFromEnd = mergeFrom->end();
	while ((prevItr != prevEnd) && (mergeFromItr != mergeFromEnd))
	{
		int prevIdx = *prevItr;
		int mergeIdx = *mergeFromItr;
		bool done = false;

		while (mergeIdx < prevIdx)
		{
			newSize++;
			++mergeFromItr;
			if (mergeFromItr == mergeFromEnd)
			{
				done = true;
				break;
			}
			mergeIdx = *mergeFromItr;
		}
		if (done)
			break;

		while (prevIdx < mergeIdx)
		{
			++prevItr;
			if (prevItr == prevEnd)
			{
				done = true;
				break;
			}
			prevIdx = *prevItr;
		}
		if (done)
			break;
		if (prevIdx == mergeIdx)
		{
			++prevItr;
			++mergeFromItr;
		}
	}
	while (mergeFromItr != mergeFromEnd)
	{
		newSize++;
		++mergeFromItr;
	}

	if (newSize == prevDestEntry->mSize)
		return prevDestEntry;

	int allocBytes = sizeof(int) * (2 + newSize);
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;
	int* outPtr = &newList->mEntries[0];

	prevItr = prevDestEntry->begin();
	mergeFromItr = mergeFrom->begin();
	while ((prevItr != prevEnd) && (mergeFromItr != mergeFromEnd))
	{
		int prevIdx = *prevItr;
		int mergeIdx = *mergeFromItr;
		bool done = false;

		while (mergeIdx < prevIdx)
		{
			*(outPtr++) = mergeIdx;
			++mergeFromItr;
			if (mergeFromItr == mergeFromEnd)
			{
				done = true;
				break;
			}
			mergeIdx = *mergeFromItr;
		}
		if (done)
			break;

		while (prevIdx < mergeIdx)
		{
			*(outPtr++) = prevIdx;
			++prevItr;
			if (prevItr == prevEnd)
			{
				done = true;
				break;
			}
			prevIdx = *prevItr;
		}
		if (done)
			break;

		if (prevIdx == mergeIdx)
		{
			*(outPtr++) = *prevItr;
			++prevItr;
			++mergeFromItr;
		}
	}
	while (prevItr != prevEnd)
	{
		*(outPtr++) = *prevItr;
		++prevItr;
	}
	while (mergeFromItr != mergeFromEnd)
	{
		*(outPtr++) = *mergeFromItr;
		++mergeFromItr;
	}

	BF_ASSERT((outPtr - &newList->mEntries[0]) == newSize);
	newList->mSize = newSize;
	newList->mNumChanges = 0;
	return newList;
}

BeVTrackingList* BeVTrackingContext::MergeChanges(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom)
{
	if (prevDestEntry == mergeFrom)
		return prevDestEntry;
	if (mergeFrom->mNumChanges == 0)
		return prevDestEntry;

	SizedArray<int, 2> changes;
	for (int changeIdx = 0; changeIdx < (int)prevDestEntry->mNumChanges; changeIdx++)
		changes.push_back(prevDestEntry->GetChange(changeIdx));
	for (int changeIdx = 0; changeIdx < (int)mergeFrom->mNumChanges; changeIdx++)
	{
		// If there isn't already a change (whether and add or a remove) that refers to this same vreg,
		//  then we add this change as well
		int change = mergeFrom->GetChange(changeIdx);
		int negChange = -change - 1;

		/*if (((std::find(changes.begin(), changes.end(), change) == changes.end())) &&
			((std::find(changes.begin(), changes.end(), negChange) == changes.end())))*/
		if ((!changes.Contains(change)) && (!changes.Contains(negChange)))
		{
			changes.push_back(change);
		}
	}

	int newSize = prevDestEntry->mSize;
	int allocBytes = (int)(sizeof(int) * (2 + newSize + changes.size()));
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;
	memcpy(&newList->mEntries[0], &prevDestEntry->mEntries[0], newSize * sizeof(int));
	for (int i = 0; i < (int)changes.size(); i++)
	{
		newList->mEntries[newSize + i] = changes[i];
	}
	newList->mSize = newSize;
	newList->mNumChanges = (int)changes.size();

	return newList;
}

BeVTrackingList * BeVTrackingContext::RemoveChange(BeVTrackingList* prevDestEntry, int idx)
{
	int newSize = prevDestEntry->mSize;
	int allocBytes = (int)(sizeof(int) * (2 + newSize + prevDestEntry->mNumChanges - 1));
	auto newList = (BeVTrackingList*)mAlloc.AllocBytes(allocBytes);
	mStats.mListBytes += allocBytes;
	memcpy(&newList->mEntries[0], &prevDestEntry->mEntries[0], newSize * sizeof(int));
	bool found = false;
	int outIdx = newSize;
	for (int i = 0; i < (int)prevDestEntry->mNumChanges; i++)
	{
		int change = prevDestEntry->GetChange(i);
		if (change == idx)
		{
			found = true;
			continue;
		}
		newList->mEntries[outIdx++] = change;
	}
	if (!found)
		return prevDestEntry;

	//BF_ASSERT(found);
	newList->mSize = newSize;
	newList->mNumChanges = prevDestEntry->mNumChanges - 1;

	return newList;
}

//////////////////////////////////////////////////////////////////////////

BeMCColorizer::BeMCColorizer(BeMCContext* mcContext)
{
	mContext = mcContext;
	mReserveParamRegs = false;
}

void BeMCColorizer::Prepare()
{
	mReserveParamRegs = false;
	mNodes.Resize(mContext->mVRegInfo.size());

	for (int vregIdx = 0; vregIdx < (int)mNodes.size(); vregIdx++)
	{
		auto node = &mNodes[vregIdx];
		node->Prepare();
		//node->mActualVRegIdx = vregIdx;
		auto vregInfo = mContext->mVRegInfo[vregIdx];
		if ((vregInfo->mIsRetVal) && (mContext->mCompositeRetVRegIdx != -1) && (vregIdx != mContext->mCompositeRetVRegIdx))
			continue;
		if (vregInfo->mRelTo)
		{
			BF_ASSERT(vregInfo->mIsExpr);

			/*if ((vregInfo->IsDirectRelTo()) && (vregInfo->mRelTo.mKind == BeMCOperandKind_VReg))
				node->mActualVRegIdx = vregInfo->mRelTo.mVRegIdx;			*/

				//node->mWantsReg = false;
				//vregInfo->mReg = X64Reg_None;
				//continue;
		}

		if ((vregInfo->mRefCount > 0) || (vregInfo->mIsRetVal))
		{
			node->mWantsReg = ((vregInfo->mType->mSize > 0) && (!vregInfo->mRegNumPinned) && (!vregInfo->mSpilled) &&
				(!vregInfo->mIsExpr) && (!vregInfo->mForceMem) && (vregInfo->mFrameOffset == INT_MIN));
		}

		// 		if (vregInfo->mIsRetVal)
		// 			node->mWantsReg = true;
		if (!node->mWantsReg)
			vregInfo->mReg = X64Reg_None;
		vregInfo->mVRegAffinity = -1;
		/*if (vregInfo->mForceReg)
		{
		// We can't have reg restrictions when we have forceReg set
		BF_ASSERT(!vregInfo->mDisableR12);
		BF_ASSERT(!vregInfo->mDisableRDX);
		}*/
		if (vregInfo->mDisableR11)
			node->AdjustRegCost(X64Reg_R11, 0x0FFFFFFF);
		if (vregInfo->mDisableR12)
			node->AdjustRegCost(X64Reg_R12, 0x0FFFFFFF);
		if (vregInfo->mDisableR13)
			node->AdjustRegCost(X64Reg_R13, 0x0FFFFFFF);
		if (vregInfo->mDisableRAX)
			node->AdjustRegCost(X64Reg_RAX, 0x0FFFFFFF);
		if (vregInfo->mDisableRDX)
			node->AdjustRegCost(X64Reg_RDX, 0x0FFFFFFF);
		if (vregInfo->mDisableEx)
		{
			for (int i = X64Reg_RSI; i <= X64Reg_R15; i++)
				node->AdjustRegCost((X64CPURegister)i, 0x0FFFFFFF);
		}
	}
}

void BeMCColorizer::AddEdge(int vreg0, int vreg1)
{
	int checkVRegIdx0 = mContext->GetUnderlyingVReg(vreg0);
	int checkVRegIdx1 = mContext->GetUnderlyingVReg(vreg1);

	if (checkVRegIdx0 == checkVRegIdx1)
		return;

	auto node0 = &mNodes[checkVRegIdx0];
	auto node1 = &mNodes[checkVRegIdx1];

	if ((node0->mWantsReg) && (node1->mWantsReg))
	{
		node0->mEdges.Add(checkVRegIdx1);
		node1->mEdges.Add(checkVRegIdx0);
	}
}

void BeMCColorizer::PropogateMemCost(const BeMCOperand & operand, int memCost)
{
	if (operand.IsVRegAny())
	{
		auto vregInfo = mContext->mVRegInfo[operand.mVRegIdx];
		mNodes[operand.mVRegIdx].mMemCost += memCost;
		PropogateMemCost(vregInfo->mRelTo, memCost);
		PropogateMemCost(vregInfo->mRelOffset, memCost);
	}
}

void BeMCColorizer::GenerateRegCosts()
{
	bool doRegCost = true;

	// Disallow param reg cross-refs
	{
		Array<X64CPURegister> paramRegsLeft = mContext->mParamsUsedRegs;
		Array<int> prevRegMovs;

		auto mcBlock = mContext->mBlocks[0];

		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];

			if ((inst->IsMov()) && (inst->mArg1.IsNativeReg()) && (inst->mArg0.IsVReg()))
			{
				int vregIdx = mContext->GetUnderlyingVReg(inst->mArg0.mVRegIdx);
				auto reg = mContext->GetFullRegister(inst->mArg1.mReg);

				// Don't allow any previous "direct reg movs" assign to a vreg that gets bound
				//  to this reg.  This is to avoid "cross-referencing" param vregs.  IE:
				//    Mov %vreg0<RDX>, RCX
				//    Mov %vreg1<RCX>, RDX
				//  Because the first MOV would destroy the incoming RDX
				for (auto prevVRegIdx : prevRegMovs)
				{
					auto node = &mNodes[prevVRegIdx];
					node->AdjustRegCost(reg, 0x0FFFFFFF);
				}

				/*auto itr = std::find(paramRegsLeft.begin(), paramRegsLeft.end(), reg);
				if (itr != paramRegsLeft.end())
				{
					paramRegsLeft.erase(itr);
					if (paramRegsLeft.size() == 0)
						break;

					prevRegMovs.push_back(vregIdx);
				}*/

				if (paramRegsLeft.Remove(reg))
				{
					if (paramRegsLeft.size() == 0)
						break;
					prevRegMovs.push_back(vregIdx);
				}
			}
		}

		BF_ASSERT(paramRegsLeft.size() == 0);
	}

	int preserveDepth = 0;

	for (auto mcBlock : mContext->mBlocks)
	{
		int costMult = mcBlock->mIsLooped ? 4 : 1;

		for (int instIdx = (int)mcBlock->mInstructions.size() - 1; instIdx >= 0; instIdx--)
		{
			auto inst = mcBlock->mInstructions[instIdx];

			// We could adjust register cost based on a possibility of cross-dependencies that won't allow us
			//  to reorder the movs for param setup
			if (inst->mKind == BeMCInstKind_PreserveVolatiles)
			{
				preserveDepth++;

				int preserveCost = 8 * costMult;

				if (inst->mArg0.mKind == BeMCOperandKind_PreserveFlag)
				{
					// Just use a small cost for NoReturns, those are implied to be "unlikely" paths (ie: errors)
					//  But at the least, the code is smaller if we don't need to save the regs so use a small cost
					if ((inst->mArg0.mPreserveFlag & BeMCPreserveFlag_NoRestore) != 0)
						preserveCost = 1;
				}

				auto restoreIdx = mContext->FindRestoreVolatiles(mcBlock, instIdx);
				if (restoreIdx == -1)
					restoreIdx = mcBlock->mInstructions.size() - 1; // 'Ret'
				auto restoreInst = mcBlock->mInstructions[restoreIdx];

				// Any vregs alive during this call will incur a cost of saving/restoring if we allocate onto a volatile register.
				//  If the vreg is either input-only or output-only (short lived temporary) then we can map it directly to a volatile register
				for (int vregIdx : *inst->mLiveness)
				{
					if (vregIdx >= mContext->mLivenessContext.mNumItems)
						continue;
					auto checkVRegIdx = mContext->GetUnderlyingVReg(vregIdx);
					// Already handled the underlying vreg?
					if ((checkVRegIdx != vregIdx) && (mContext->mLivenessContext.IsSet(inst->mLiveness, checkVRegIdx)))
						continue;

					if (mContext->mLivenessContext.IsSet(restoreInst->mLiveness, vregIdx))
					{
						auto node = &mNodes[checkVRegIdx];
						if (inst->mArg0.IsNativeReg())
						{
							// Only one specific register used being preserved
							node->AdjustRegCost(inst->mArg0.mReg, preserveCost);
						}
						else
						{
							// All volatile registers preserved
							for (auto reg : gVolatileRegs)
							{
								node->AdjustRegCost(reg, preserveCost);
							}
						}
					}
				}
			}
			else if (inst->mKind == BeMCInstKind_RestoreVolatiles)
			{
				preserveDepth--;
			}

			if (inst->IsPsuedo())
				continue;

			// If both args are non-registers then we presume we may have to create a scratch register
			//  so inflate the cost.  Note that the first time through, this will presumably occur for all
			//  operations but we can change our minds as instructions are legalized
			if ((inst->mArg0.IsVReg()) && (inst->mArg1.IsVReg()))
			{
				auto vregInfo0 = mContext->mVRegInfo[inst->mArg0.mVRegIdx];
				auto vregInfo1 = mContext->mVRegInfo[inst->mArg1.mVRegIdx];
				if ((vregInfo0->mReg == X64Reg_None) && (vregInfo1->mReg == X64Reg_None))
				{
					Node* node0 = &mNodes[inst->mArg1.mVRegIdx];
					Node* node1 = &mNodes[inst->mArg1.mVRegIdx];
					node0->mMemCost += 4 * costMult;
					node1->mMemCost += 4 * costMult;
				}
			}

			if ((inst->mKind == BeMCInstKind_Div) || (inst->mKind == BeMCInstKind_IDiv))
			{
				if (inst->mArg0.IsVReg())
				{
					Node* node = &mNodes[inst->mArg0.mVRegIdx];

					int adjustCost = -4;
					if (preserveDepth >= 2)
					{
						// This has to be large enough to counteract the 'PreserveVolatile RAX'
						adjustCost -= 8;
					}

					node->AdjustRegCost(X64Reg_RAX, costMult * adjustCost);
					continue;
				}
			}

			if (inst->mKind == BeMCInstKind_MovSX)
			{
				// We can only perform MovSX on reg targets
				if (inst->mArg0.IsVReg())
					mNodes[inst->mArg0.mVRegIdx].mMemCost += 4 * costMult;
			}

			if (inst->mKind == BeMCInstKind_Mov)
			{
				if ((inst->mArg0.mKind == BeMCOperandKind_VReg) &&
					(inst->mArg1.mKind == BeMCOperandKind_VReg))
				{
					int arg0Idx = mContext->GetUnderlyingVReg(inst->mArg0.mVRegIdx);
					int arg1Idx = mContext->GetUnderlyingVReg(inst->mArg1.mVRegIdx);

					auto vregInfo0 = mContext->mVRegInfo[arg0Idx];
					auto vregInfo1 = mContext->mVRegInfo[arg1Idx];
					if (vregInfo0->mVRegAffinity == -1)
						vregInfo0->mVRegAffinity = arg1Idx;
					if (vregInfo1->mVRegAffinity == -1)
						vregInfo1->mVRegAffinity = arg0Idx;
				}
			}

			if (inst->mKind == BeMCInstKind_Call)
			{
				if (inst->mArg0.IsVRegAny())
				{
					for (int checkInstIdx = instIdx - 1; checkInstIdx >= 0; checkInstIdx--)
					{
						auto checkInst = mcBlock->mInstructions[checkInstIdx];
						if ((checkInst->mKind == BeMCInstKind_Mov) && (checkInst->mArg0.IsNativeReg()))
						{
							// This can save us from needing to mov the call addr to RAX before the call
							mNodes[inst->mArg0.mVRegIdx].AdjustRegCost(checkInst->mArg0.mReg, 2 * costMult);
						}
						else
							break;
					}
				}
			}

			if (inst->IsMov())
			{
				auto argTo = inst->mArg0;
				auto vregInfo = mContext->GetVRegInfo(argTo);
				if ((vregInfo != NULL) && (vregInfo->IsDirectRelTo()))
					argTo = vregInfo->mRelTo;

				auto argFrom = inst->mArg1;
				vregInfo = mContext->GetVRegInfo(argFrom);
				if ((vregInfo != NULL) && (vregInfo->IsDirectRelTo()))
					argFrom = vregInfo->mRelTo;

				if (argTo == argFrom)
					continue;
			}

			auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
			for (auto operand : operands)
			{
				if (operand->IsVRegAny())
				{
					Node* node = &mNodes[operand->mVRegIdx];
					auto vregInfo = mContext->mVRegInfo[operand->mVRegIdx];

					if ((vregInfo->mIsRetVal) && (mContext->mCompositeRetVRegIdx != -1))
						vregInfo = mContext->mVRegInfo[mContext->mCompositeRetVRegIdx];

					if (operand->mKind == BeMCOperandKind_VRegLoad)
					{
						// We propagate this mem cost to RelTo/RelOffset layer
						Node* node = &mNodes[operand->mVRegIdx];
						node->mMemCost += 6 * costMult;
					}

					while (vregInfo->IsDirectRelTo())
					{
						node = &mNodes[vregInfo->mRelTo.mVRegIdx];
						vregInfo = mContext->mVRegInfo[vregInfo->mRelTo.mVRegIdx];
					}

					node->mMemCost += 4 * costMult;
					if (vregInfo->mRelTo.IsVRegAny())
						mNodes[vregInfo->mRelTo.mVRegIdx].mMemCost += 4 * costMult;
					if (vregInfo->mRelOffset.IsVRegAny()) // Higher cost, will definitely require a reg
						mNodes[vregInfo->mRelOffset.mVRegIdx].mMemCost += 8 * costMult;

					if (inst->IsMov())
					{
						if (operand != &inst->mResult)
						{
							auto otherReg = X64Reg_None;
							if (operand == &inst->mArg0)
							{
								if (inst->mArg1.mKind == BeMCOperandKind_NativeReg)
									otherReg = inst->mArg1.mReg;
								if (inst->mArg1.IsImmediateFloat()) // Moving a float immediate into memory takes two instructions
									node->mMemCost += 4 * costMult;
							}
							else
							{
								if (inst->mArg0.mKind == BeMCOperandKind_NativeReg)
									otherReg = inst->mArg0.mReg;
							}

							// If we assigned ourselves to this reg then it would be a "mov reg, reg" which could
							//  be eliminated so the cost is negative.  We could be tempted to more strongly favor this,
							//  but that could cause RAX to be used for __return when it would be less costly to use
							//  memory so we don't have to preserve/restore it across call boundaries
							if (otherReg != X64Reg_None)
								node->AdjustRegCost(mContext->GetFullRegister(otherReg), -2 * costMult);
						}
					}
				}
			}
		}
	}

	for (int nodeIdx = 0; nodeIdx < (int)mNodes.size(); nodeIdx++)
	{
		auto node = &mNodes[nodeIdx];
		if (node->mMemCost > 0)
		{
			auto vregInfo = mContext->mVRegInfo[nodeIdx];
			if (vregInfo->mIsExpr)
			{
				PropogateMemCost(vregInfo->mRelTo, node->mMemCost);
				PropogateMemCost(vregInfo->mRelOffset, node->mMemCost);
			}
		}
	}

	BF_ASSERT(preserveDepth == 0);
}

void BeMCColorizer::AssignRegs(RegKind regKind)
{
	X64CPURegister highestReg;

	int totalRegs32 = 0;
	int totalRegs16 = 0;

	SizedArray<X64CPURegister, 32> validRegs;
	if (regKind == BeMCColorizer::RegKind_Ints)
	{
		highestReg = X64Reg_R15;
		validRegs = { X64Reg_RAX, X64Reg_RBX, X64Reg_RCX, X64Reg_RDX, X64Reg_RSI, X64Reg_RDI, X64Reg_R8,
			X64Reg_R9, X64Reg_R10, X64Reg_R11, X64Reg_R12, X64Reg_R13, X64Reg_R14, X64Reg_R15 };
	}
	else
	{
		highestReg = X64Reg_M128_XMM14; // Leave X64Reg_M128_XMM15 as a scratch reg
		validRegs = {
			X64Reg_M128_XMM0, X64Reg_M128_XMM1, X64Reg_M128_XMM2, X64Reg_M128_XMM3,
			X64Reg_M128_XMM4, X64Reg_M128_XMM5, X64Reg_M128_XMM6, X64Reg_M128_XMM7,
			X64Reg_M128_XMM8, X64Reg_M128_XMM9, X64Reg_M128_XMM10, X64Reg_M128_XMM11,
			X64Reg_M128_XMM12, X64Reg_M128_XMM13, X64Reg_M128_XMM14 /*, X64Reg_M128_XMM15*/};
	}

	int totalRegs = (int)validRegs.size();

#define BF_DEQUE
#ifdef BF_DEQUE
	std::deque<int> vregStack;
#else
	std::vector<int> vregStack;
#endif

	//vregStack.reserve(mNodes.size());
	SizedArray<int, 32> vregHiPriStack;
	vregHiPriStack.reserve(mNodes.size());
	SizedArray<int, 32> vregGraph;
	vregGraph.reserve(mNodes.size());

	SizedArray<int, 32> orderedSpillList;

	//
	{
		BP_ZONE("AssignRegs:vregGraph build");
		for (int vregIdx = (int)mNodes.size() - 1; vregIdx >= 0; vregIdx--)
		{
			auto node = &mNodes[vregIdx];
			if (node->mWantsReg)
			{
				auto vregInfo = mContext->mVRegInfo[vregIdx];
				bool canBeReg = false;
				if (regKind == RegKind_Ints)
				{
					if ((vregInfo->mType->IsInt()) || (vregInfo->mType->mTypeCode == BeTypeCode_Boolean) || (vregInfo->mType->mTypeCode == BeTypeCode_Pointer))
						canBeReg = true;
				}
				else if (regKind == RegKind_Floats)
				{
					if ((vregInfo->mType->IsFloat()) || (vregInfo->mType->IsVector()))
						canBeReg = true;
				}

				if (canBeReg)
				{
					node->mInGraph = true;
					node->mGraphEdgeCount = 0;
					vregGraph.push_back(vregIdx);
				}
				else
				{
					node->mInGraph = false;
				}
			}
		}
	}

	for (int vregIdx = (int)mNodes.size() - 1; vregIdx >= 0; vregIdx--)
	{
		auto node = &mNodes[vregIdx];

		if (node->mInGraph)
		{
			for (auto connNodeIdx : node->mEdges)
			{
				Node* connNode = &mNodes[connNodeIdx];
				if (connNode->mInGraph)
					node->mGraphEdgeCount++;
			}
		}
	}

	//
	{
		int graphSize = (int)vregGraph.size();

		BP_ZONE("AssignRegs:buildStack");
		while (graphSize > 0)
		{
			bool hadNewStackItem = false;
#ifdef _DEBUG
			// 			for (int graphIdx = 0; graphIdx < (int)vregGraph.size(); graphIdx++)
			// 			{
			// 				int validatedCount = 0;
			// 				int vregIdx = vregGraph[graphIdx];
			// 				if (vregIdx == -1)
			// 					continue;
			// 				Node* node = &mNodes[vregIdx];
			// 				for (auto connNodeIdx : node->mEdges)
			// 				{
			// 					Node* connNode = &mNodes[connNodeIdx];
			// 					if (connNode->mInGraph)
			// 						validatedCount++;
			// 				}
			// 				BF_ASSERT(validatedCount == node->mGraphEdgeCount);
			// 			}
#endif

			for (int graphIdx = 0; graphIdx < (int)vregGraph.size(); graphIdx++)
			{
				int vregIdx = vregGraph[graphIdx];
				if (vregIdx == -1)
					continue;
				Node* node = &mNodes[vregIdx];
				auto vregInfo = mContext->mVRegInfo[vregIdx];
				int validRegCount = (int)validRegs.size();

				BF_ASSERT(node->mGraphEdgeCount <= vregGraph.size() - 1);

				if ((node->mSpilled) || (node->mGraphEdgeCount < validRegCount))
				{
					node->mInGraph = false;
					for (auto connNodeIdx : node->mEdges)
					{
						Node* connNode = &mNodes[connNodeIdx];
						connNode->mGraphEdgeCount--;
					}

					vregGraph[graphIdx] = -1;
					graphSize--;

					if (!node->mSpilled)
					{
						vregStack.push_back(vregIdx);
						hadNewStackItem = true;
					}
					else
					{
						// We insert spills at the front so we can try to "optimistically" unspill them
						//  after all the definite coloring is done
#ifdef BF_DEQUE
						vregStack.push_front(vregIdx);
#else
						vregStack.insert(vregStack.begin(), vregIdx);
#endif
					}
				}
			}

			if (!hadNewStackItem)
			{
				BP_ZONE("Spill");

				// We need to spill!
				int bestSpillVReg = -1;
				for (int regPassIdx = 0; regPassIdx < 2; regPassIdx++)
				{
					while (!orderedSpillList.empty())
					{
						int vregIdx = orderedSpillList.back();
						orderedSpillList.pop_back();
						if (mNodes[vregIdx].mInGraph)
						{
							bestSpillVReg = vregIdx;
							break;
						}
					}
					if (bestSpillVReg != -1)
						break;

					// Order by mem cost
					orderedSpillList.reserve(vregGraph.size());
					for (int graphIdx = 0; graphIdx < (int)vregGraph.size(); graphIdx++)
					{
						int vregIdx = vregGraph[graphIdx];
						if (vregIdx == -1)
							continue;
						auto vregInfo = mContext->mVRegInfo[vregIdx];
						if (vregInfo->mForceReg)
							continue;
						orderedSpillList.push_back(vregIdx);
					}

					std::sort(orderedSpillList.begin(), orderedSpillList.end(), [&](int lhs, int rhs)
					{
						return mNodes[lhs].mMemCost > mNodes[rhs].mMemCost;
					});
				}

				/*int bestSpillVReg = -1;
				int bestSpillCost = INT_MAX;

				// We need to spill now!  Try to spill something with the fewest references
				for (int graphIdx = 0; graphIdx < (int)vregGraph.size(); graphIdx++)
				{
					int vregIdx = vregGraph[graphIdx];
					auto vregInfo = mContext->mVRegInfo[vregIdx];
					if (vregInfo->mForceReg)
						continue;
					if (vregInfo->mRefCount < bestSpillCost)
					{
						bestSpillCost = vregInfo->mRefCount;
						bestSpillVReg = vregIdx;
					}
				}*/

				if (bestSpillVReg != -1)
				{
					auto node = &mNodes[bestSpillVReg];
					node->mSpilled = true;
					auto spillVRegInfo = mContext->mVRegInfo[bestSpillVReg];
					spillVRegInfo->mSpilled = true;
				}
				else
				{
					mContext->Fail("Unable to spill vreg");
				}
			}
		}
	}

	SizedArray<bool, 32> globalRegUsedVec;
	globalRegUsedVec.resize(highestReg + 1);
	SizedArray<bool, 32> regUsedVec;
	regUsedVec.resize(highestReg + 1);

	/*String dbgStr;

	if (mContext->mDebugging)
		dbgStr += "AssignRegs ";*/

	BP_ZONE("AssignRegs:assign");
	while (vregStack.size() > 0)
	{
		int vregIdx = vregStack.back();
		vregStack.pop_back();

		/*if (mContext->mDebugging)
		{
			dbgStr += StrFormat("VReg %d ", vregIdx);
		}*/

		BeMCVRegInfo* vregInfo = mContext->mVRegInfo[vregIdx];
		Node* node = &mNodes[vregIdx];

		if (vregInfo->mVRegAffinity != -1)
		{
			auto affinityVRegInfo = mContext->mVRegInfo[vregInfo->mVRegAffinity];
			if (affinityVRegInfo->mReg != X64Reg_None)
				node->AdjustRegCost(affinityVRegInfo->mReg, -2);
		}

		for (int i = 0; i <= highestReg; i++)
			regUsedVec[i] = false;

		if (mReserveParamRegs)
		{
			// This is a fallback case for when the "streams get crossed" during initialization-
			//  IE: when arg0 gets assigned to RDX and arg1 gets assigned to RCX and we end up with:
			//   MOV arg0<RDX>, RCX
			//   MOV arg1<RCX>, RDX
			// Which is bad.
			for (auto reg : mContext->mParamsUsedRegs)
			{
				if (((int)reg < regUsedVec.size()) && (reg != vregInfo->mNaturalReg))
					regUsedVec[(int)reg] = true;
			}
		}

		for (auto connNodeIdx : node->mEdges)
		{
			Node* connNode = &mNodes[connNodeIdx];
			if (connNode->mInGraph)
			{
				auto connVRegInfo = mContext->mVRegInfo[connNodeIdx];
				auto usedReg = mContext->GetFullRegister(connVRegInfo->mReg);
				BF_ASSERT(usedReg != X64Reg_None);
				regUsedVec[(int)usedReg] = true;
			}
		}
		auto bestReg = X64Reg_None;
		int bestRegCost = 0x07FFFFFF; // 0x0FFFFFFF is considered illegal for a reg, so set the mem cost to lower than that...;

		// This is the cost of just leaving the vreg as a memory access.  In cases where we bind to a volatile
		//  register, we need to consider the cost of preserving and restoring that register across calls, so
		//  it cases where we have just a few accesses to this vreg but it spans a lot of calls then we just
		//  leave it as memory
		if (!vregInfo->mForceReg)
			bestRegCost = node->mMemCost;

		//for (auto validReg : validRegs)
		int validRegCount = (int)validRegs.size();
		for (int regIdx = 0; regIdx < validRegCount; regIdx++)
		{
			auto validReg = validRegs[regIdx];
			if (!regUsedVec[(int)validReg])
			{
				int checkCost = node->mRegCost[(int)validReg];
				// If this register is non-volatile then we'd have to save and restore it, which costs... unless
				//  some other vreg has already used this, then there's no additional cost
				if ((!globalRegUsedVec[(int)validReg]) && (!mContext->IsVolatileReg(validReg)))
				{
					int costMult = 1;
					if (regKind == BeMCColorizer::RegKind_Floats)
						costMult = 2;
					checkCost += 7 * costMult;
				}
				if (checkCost < bestRegCost)
				{
					// Try not to use registers that other params may want
					for (auto argReg : mContext->mParamsUsedRegs)
					{
						if (validReg == argReg)
							checkCost += 1;
					}
				}

				/*if (mContext->mDebugging)
				{
					dbgStr += StrFormat("Cost %d:%d ", validReg, checkCost);
				}*/

				if (checkCost < bestRegCost)
				{
					bestReg = validReg;
					bestRegCost = checkCost;
				}
			}
		}

		if (mContext->mDebugging)
		{
			//auto itr = mContext->mDbgPreferredRegs.find(vregIdx);
			//if (itr != mContext->mDbgPreferredRegs.end())

			X64CPURegister* regPtr = NULL;
			if (mContext->mDbgPreferredRegs.TryGetValue(vregIdx, &regPtr))
			{
				auto reg = *regPtr;
				if (reg == X64Reg_None)
				{
					if (!vregInfo->mForceReg)
						bestReg = reg;
				}
				else
				{
					if (!regUsedVec[(int)reg])
						bestReg = reg;
				}
			}
		}

		if (vregInfo->mSpilled)
		{
			if (bestReg != X64Reg_None)
			{
				// We managed to optimistically unspill
				vregInfo->mSpilled = false;
			}
		}
		else
		{
			if (vregInfo->mForceReg)
			{
				// We didn't end up with a usable reg -- steal a reg from one of our edges
				if (bestReg == X64Reg_None)
				{
					int bestSpillVReg = -1;
					int bestSpillCost = INT_MAX;

					for (auto connNodeIdx : node->mEdges)
					{
						Node* connNode = &mNodes[connNodeIdx];
						if (connNode->mInGraph)
						{
							auto connVRegInfo = mContext->mVRegInfo[connNodeIdx];
							auto usedReg = mContext->GetFullRegister(connVRegInfo->mReg);

							if (connVRegInfo->mForceReg)
								continue;

							if (node->mRegCost[usedReg] < 0x07FFFFFFF)
							{
								if (connVRegInfo->mRefCount < bestSpillCost)
								{
									bestSpillCost = connVRegInfo->mRefCount;
									bestSpillVReg = connNodeIdx;
								}
							}
						}
					}

					if (bestSpillVReg != -1)
					{
						/*if (mContext->mDebugging)
						{
							dbgStr += StrFormat("StealingVReg %d ", bestSpillVReg);
						}*/

						auto connNode = &mNodes[bestSpillVReg];
						auto connVRegInfo = mContext->mVRegInfo[bestSpillVReg];
						bestReg = mContext->GetFullRegister(connVRegInfo->mReg);
						connVRegInfo->mReg = X64Reg_None;

						connNode->mSpilled = true;
						connVRegInfo->mSpilled = true;

						connNode->mInGraph = false;
						for (auto connNodeEdgeIdx : connNode->mEdges)
						{
							Node* connNodeEdge = &mNodes[connNodeEdgeIdx];
							connNodeEdge->mGraphEdgeCount--;
						}

						// We insert spills at the front so we can try to "optimistically" unspill them
						//  after all the definite coloring is done
						vregStack.insert(vregStack.begin(), bestSpillVReg);
					}
				}

				// The only way we should have failed to allocate a register is if we spilled...
				BF_ASSERT((bestReg != X64Reg_None) /*|| (mReserveParamRegs)*/);
			}
		}

		vregInfo->mReg = mContext->ResizeRegister(bestReg, vregInfo->mType);
		if (bestReg != X64Reg_None)
		{
			node->mInGraph = true;
			globalRegUsedVec[(int)bestReg] = true;
			for (auto connNodeIdx : node->mEdges)
			{
				Node* connNode = &mNodes[connNodeIdx];
				connNode->mGraphEdgeCount++;
			}
		}

		/*if (mContext->mDebugging)
		{
			dbgStr += StrFormat("Reg %d ", vregInfo->mReg);
		}*/
	}

	/*if (!dbgStr.empty())
	{
		dbgStr += "\n\n";
		OutputDebugStr(dbgStr);
	}*/

	/*bool dumpStats = true;
	if (dumpStats)
	{
		String str;

		str += "Register costs:\n";
		str += "                         Mem ";
		for (int regNum = 0; regNum <= X64Reg_EDI; regNum++)
		{
			str += StrFormat("%4s", X64CPURegisters::GetRegisterName((X64CPURegister)regNum));
		}
		str += "\n";

		for (int liveVRregIdx = 0; liveVRregIdx < mContext->mVRegInfo.size(); liveVRregIdx++)
		{
			auto vregInfo = mContext->mVRegInfo[liveVRregIdx];
			auto node = &mNodes[liveVRregIdx];

			String name = mContext->ToString(BeMCOperand::FromVReg(liveVRregIdx));
			str += StrFormat("%24s", name.c_str());

			str += StrFormat(" %3d", node->mMemCost);

			for (int regNum = 0; regNum <= X64Reg_EDI; regNum++)
			{
				str += StrFormat(" %3d", node->mRegCost[regNum]);
			}
			str += "\n";
		}

		OutputDebugStr(str);
	}*/
}

bool BeMCColorizer::Validate()
{
#ifdef _DEBUG
	auto paramsLeft = mContext->mParamsUsedRegs;

	for (auto mcBlock : mContext->mBlocks)
	{
		for (auto inst : mcBlock->mInstructions)
		{
			if (paramsLeft.size() == 0)
				break;

			if (inst->IsMov())
			{
				if (inst->mArg1.IsNativeReg())
				{
					BF_ASSERT(inst->mArg0.IsVReg());

					BF_ASSERT(mContext->GetFullRegister(inst->mArg1.mReg) == paramsLeft[0]);
					paramsLeft.erase(paramsLeft.begin());

					auto vregInfo = mContext->mVRegInfo[inst->mArg0.mVRegIdx];
					if (vregInfo->mReg != X64Reg_None)
					{
						auto checkReg = mContext->GetFullRegister(vregInfo->mReg);
						//auto itr = std::find(paramsLeft.begin(), paramsLeft.end(), checkReg);
						//if (itr != paramsLeft.end())
						if (paramsLeft.Contains(checkReg))
						{
							// This will happen if we have assigned a 'wrong' register to a parameter-
							//  a register that is not the 'natural' register (ie: rcx for param0),
							//  but it's a register that is already bound to another parameter which
							//  will be needed later.  Given the 'natural' order of RCX, RDX, R8, R9,
							// This is a valid order still: R10, RCX, R8, R9 since RCX will get written
							//  to R10 first so it won't get clobbered, but: RDX, R10, R8, R9 is not
							//  valid because RDX gets clobbered before it can be written to R10.
							return false;
						}
					}
				}
			}
		}
	}

#endif
	return true;
}

//////////////////////////////////////////////////////////////////////////

BeMCLoopDetector::BeMCLoopDetector(BeMCContext* context) : mTrackingContext(context)
{
	mMCContext = context;
	mTrackingContext.mTrackKindCount = 1;
	mTrackingContext.Init((int)mMCContext->mMCBlockAlloc.size());
	mNodes.Resize(mMCContext->mMCBlockAlloc.size());
}

void BeMCLoopDetector::DetectLoops(BeMCBlock* mcBlock, BeVTrackingList* predBlocksSeen)
{
	mMCContext->mDetectLoopIdx++;

	auto node = &mNodes[mcBlock->mBlockIdx];
	auto blocksSeen = mTrackingContext.Merge(node->mPredBlocksSeen, predBlocksSeen);
	if (blocksSeen == node->mPredBlocksSeen)
		return;
	node->mPredBlocksSeen = blocksSeen;

	//SizedArray<int, 2> addVec = { mcBlock->mBlockIdx };
	//auto newBlocksSeen = mTrackingContext.Add(blocksSeen, addVec, false);

	auto newBlocksSeen = mTrackingContext.Add(blocksSeen, mcBlock->mBlockIdx, false);
	if (newBlocksSeen == blocksSeen)
	{
		// Our ID was already set, so this is a re-entry and thus we are looped
		mcBlock->mIsLooped = true;
	}
	blocksSeen = newBlocksSeen;

	for (auto succ : mcBlock->mSuccs)
	{
		DetectLoops(succ, blocksSeen);
	}
}

void BeMCLoopDetector::DetectLoops()
{
	for (auto block : mMCContext->mBlocks)
	{
		for (auto succ : block->mSuccs)
		{
			if (succ->mBlockIdx < block->mBlockIdx)
			{
				auto prevBlock = mMCContext->mBlocks[block->mBlockIdx - 1];
				//if ((!succ->mIsLooped) || (!prevBlock->mIsLooped))
				{
					for (int setBlockIdx = succ->mBlockIdx; setBlockIdx < block->mBlockIdx; setBlockIdx++)
						mMCContext->mBlocks[setBlockIdx]->mIsLooped = true;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void BeMCBlock::AddPred(BeMCBlock* pred)
{
	if (!mPreds.Contains(pred))
	{
		pred->mSuccs.push_back(this);
		mPreds.push_back(pred);
	}
}

int BeMCBlock::FindLabelInstIdx(int labelIdx)
{
	for (int instIdx = 0; instIdx < (int)mInstructions.size(); instIdx++)
	{
		auto inst = mInstructions[instIdx];
		if ((inst->mKind == BeMCInstKind_Label) && (inst->mArg0.mLabelIdx == labelIdx))
			return instIdx;
	}
	return -1;
}

//////////////////////////////////////////////////////////////////////////

void BeInstEnumerator::Next()
{
	if (mRemoveCurrent)
	{
		if ((mWriteIdx > 0) && (mReadIdx < mBlock->mInstructions.size() - 1))
			mContext->MergeInstFlags(mBlock->mInstructions[mWriteIdx - 1], mBlock->mInstructions[mReadIdx], mBlock->mInstructions[mReadIdx + 1]);

		mBlock->mInstructions[mReadIdx] = NULL;
		mRemoveCurrent = false;
	}
	else
	{
		if (mWriteIdx != mReadIdx)
		{
			mBlock->mInstructions[mWriteIdx] = mBlock->mInstructions[mReadIdx];
			mBlock->mInstructions[mReadIdx] = NULL;
		}
		mWriteIdx++;
	}

	mReadIdx++;
}

//////////////////////////////////////////////////////////////////////////

BeMCContext::BeMCContext(BeCOFFObject* coffObject) : mOut(coffObject->mTextSect.mData), mLivenessContext(this), mVRegInitializedContext(this), mColorizer(this)
{
	mLivenessContext.mTrackKindCount = 1;
	mCOFFObject = coffObject;
	mModule = NULL;
	mBeFunction = NULL;
	mActiveBeBlock = NULL;
	mActiveBlock = NULL;
	mActiveInst = NULL;
	mDbgFunction = NULL;
	mCompositeRetVRegIdx = -1;
	mTLSVRegIdx = -1;
	mStackSize = 0;
	mCurLabelIdx = 0;
	mCurPhiIdx = 0;
	mMaxCallParamCount = -1;
	mCurDbgLoc = NULL;
	mCurVRegsInit = NULL;
	mCurVRegsLive = NULL;
	mUseBP = false;
	mHasVAStart = false;
	mInsertInstIdxRef = NULL;
	mNativeIntType = mCOFFObject->mBeModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
	mDebugging = false;
	mFailed = false;
	mDetectLoopIdx = 0;
	mLegalizationIterations = 0;
}

void BeMCContext::NotImpl()
{
	Fail("Not implemented");
}

void BeMCContext::Fail(const StringImpl& str)
{
	String errStr = StrFormat("Failure during codegen of %s in %s: %s", mBeFunction->mName.c_str(), mModule->mModuleName.c_str(), str.c_str());

	if (mActiveBlock != NULL)
		errStr += StrFormat("\n MCBlock: %s", mActiveBlock->mName.c_str());
	if ((mActiveInst != NULL) && (mActiveInst->mDbgLoc != NULL))
	{
		BeDumpContext dumpCtx;
		errStr += "\n DbgLoc : ";
		dumpCtx.ToString(errStr, mActiveInst->mDbgLoc);
	}

	BfpSystem_FatalError(errStr.c_str(), "FATAL ERROR");
}

void BeMCContext::AssertFail(const StringImpl& str, int line)
{
	Fail(StrFormat("Assert '%s' failed on line %d", str.c_str(), line));
}

void BeMCContext::SoftFail(const StringImpl& str, BeDbgLoc* dbgLoc)
{
	if (mFailed)
		return;
	mFailed = true;

	String errStr = StrFormat("Failure during codegen of %s: %s", mBeFunction->mName.c_str(), str.c_str());

	if (dbgLoc != NULL)
	{
		auto dbgFile = dbgLoc->GetDbgFile();
		if (dbgFile != NULL)
			errStr += StrFormat(" at line %d:%d in %s/%s", dbgLoc->mLine + 1, dbgLoc->mColumn + 1, dbgFile->mDirectory.c_str(), dbgFile->mFileName.c_str());
	}

	mModule->mBeIRCodeGen->Fail(errStr);
}

String BeMCContext::ToString(const BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_NativeReg)
	{
		return String("%") + X64CPURegisters::GetRegisterName((int)operand.mReg);
	}

	if (operand.IsVRegAny())
	{
		auto vregInfo = GetVRegInfo(operand);

		String str;
		mModule->ToString(str, GetType(operand));
		str += " ";

		if (operand.IsVRegAny())
		{
			auto vregInfo = GetVRegInfo(operand);
			if (operand.mKind == BeMCOperandKind_VRegAddr)
				str += "&";
			if (operand.mKind == BeMCOperandKind_VRegLoad)
				str += "*";
			if (vregInfo->mDbgVariable != NULL)
				str += "#" + vregInfo->mDbgVariable->mName + StrFormat("/%d", operand.mVRegIdx);
			else
				str += StrFormat("%%vreg%d", operand.mVRegIdx);
		}

		if (vregInfo->mReg != X64Reg_None)
		{
			str += "<";
			str += X64CPURegisters::GetRegisterName((int)vregInfo->mReg);
			str += ">";
		}
		else if (vregInfo->mForceReg)
		{
			str += "<reg>";
		}

		if (vregInfo->mDisableR11)
		{
			str += "<NoR11>";
		}
		if (vregInfo->mDisableR12)
		{
			str += "<NoR12>";
		}
		if (vregInfo->mDisableR13)
		{
			str += "<NoR13>";
		}
		if (vregInfo->mDisableRAX)
		{
			str += "<NoRAX>";
		}
		if (vregInfo->mDisableRDX)
		{
			str += "<NoRDX>";
		}
		if (vregInfo->mDisableEx)
		{
			str += "<NoEx>";
		}

		if (vregInfo->mForceMem)
		{
			str += "<mem>";
		}

		if (vregInfo->mIsRetVal)
		{
			str += "<retval>";
		}

		if (vregInfo->mForceMerge)
		{
			str += "<canMerge>";
		}

		if (vregInfo->mRelTo)
		{
			str += "(";
			str += ToString(vregInfo->mRelTo);
			if (vregInfo->mRelOffset)
			{
				str += "+";
				str += ToString(vregInfo->mRelOffset);
			}
			if (vregInfo->mRelOffsetScale != 1)
				str += StrFormat("*%d", vregInfo->mRelOffsetScale);
			str += ")";
		}
		return str;
	}

	if (operand.IsImmediate())
	{
		String str;
		switch (operand.mKind)
		{
		case BeMCOperandKind_Immediate_i8: str += "i8 "; break;
		case BeMCOperandKind_Immediate_i16: str += "i16 "; break;
		case BeMCOperandKind_Immediate_i32: str += "i32 "; break;
		case BeMCOperandKind_Immediate_i64: str += "i64 "; break;
		case BeMCOperandKind_Immediate_HomeSize: str += "homesize"; break;
		case BeMCOperandKind_Immediate_Null:
			if (operand.mType != NULL)
			{
				mModule->ToString(str, operand.mType);
				str += " null";
			}
			else
				str += "null";
			return str;
		case BeMCOperandKind_Immediate_f32: return StrFormat("f32 %f", operand.mImmF32);
		case BeMCOperandKind_Immediate_f32_Packed128: return StrFormat("f32_packed %f", operand.mImmF32);
		case BeMCOperandKind_Immediate_f64: return StrFormat("f64 %f", operand.mImmF64);
		case BeMCOperandKind_Immediate_f64_Packed128: return StrFormat("f64_packed %f", operand.mImmF64);
		}
		//if (operand.mImmediate < 10)
		str += StrFormat("%lld", operand.mImmediate);
		/*else
			str += StrFormat("0x%llX", operand.mImmediate);*/
		return str;
	}
	if (operand.mKind == BeMCOperandKind_Block)
		return "%" + operand.mBlock->mName;
	if (operand.mKind == BeMCOperandKind_Label)
		return StrFormat("%%label%d", operand.mLabelIdx);
	if (operand.mKind == BeMCOperandKind_CmpKind)
		return BeDumpContext::ToString(operand.mCmpKind);
	if (operand.mKind == BeMCOperandKind_CmpResult)
	{
		String result = StrFormat("%%CmpResult%d", operand.mCmpResultIdx);
		auto& cmpResult = mCmpResults[operand.mCmpResultIdx];
		if (cmpResult.mResultVRegIdx != -1)
			result += StrFormat("<vreg%d>", cmpResult.mResultVRegIdx);
		result += " ";
		result += BeDumpContext::ToString(cmpResult.mCmpKind);
		return result;
	}
	if (operand.mKind == BeMCOperandKind_NotResult)
	{
		auto mcResult = GetOperand(operand.mNotResult->mValue, true, true);
		String result = "NOT ";
		result += ToString(mcResult);
		return result;
	}
	if (operand.mKind == BeMCOperandKind_Symbol)
		return mCOFFObject->mSymbols[operand.mSymbolIdx]->mName;
	if (operand.mKind == BeMCOperandKind_SymbolAddr)
		return "&" + mCOFFObject->mSymbols[operand.mSymbolIdx]->mName;
	if (operand.mKind == BeMCOperandKind_MemSetInfo)
		return StrFormat("size=%d val=%d align=%d", operand.mMemSetInfo.mSize, operand.mMemSetInfo.mValue, operand.mMemSetInfo.mAlign);
	if (operand.mKind == BeMCOperandKind_MemCpyInfo)
		return StrFormat("size=%d align=%d", operand.mMemCpyInfo.mSize, operand.mMemCpyInfo.mAlign);
	if (operand.mKind == BeMCOperandKind_VRegPair)
	{
		String str = "(";
		str += ToString(BeMCOperand::FromEncoded(operand.mVRegPair.mVRegIdx0));
		str += ", ";
		str += ToString(BeMCOperand::FromEncoded(operand.mVRegPair.mVRegIdx1));
		str += ")";
		return str;
	}
	if (operand.mKind == BeMCOperandKind_Phi)
		return StrFormat("%%PHI%d", operand.mPhi->mIdx);
	if (operand.mKind == BeMCOperandKind_PreserveFlag)
	{
		if (operand.mPreserveFlag == BeMCPreserveFlag_NoRestore)
			return "PreserveFlag:NoReturn";
	}
	if (operand.mKind == BeMCOperandKind_ConstAgg)
	{
		BeDumpContext dumpContext;
		String str = "const ";
		str += dumpContext.ToString(operand.mConstant);
		return str;
	}
	return "???";
}

BeMCOperand BeMCContext::GetOperand(BeValue* value, bool allowMetaResult, bool allowFail, bool skipForceVRegAddr)
{
	if (value == NULL)
		return BeMCOperand();

	switch (value->GetTypeId())
	{
	case BeGlobalVariable::TypeId:
	{
		auto globalVar = (BeGlobalVariable*)value;
		if ((globalVar->mIsTLS) && (mTLSVRegIdx == -1))
		{
			auto tlsVReg = AllocVirtualReg(mNativeIntType);
			auto vregInfo = GetVRegInfo(tlsVReg);
			vregInfo->mMustExist = true;
			vregInfo->mForceReg = true;
			vregInfo->mDisableR12 = true;
			vregInfo->mDisableR13 = true;
			mTLSVRegIdx = tlsVReg.mVRegIdx;
		}

		auto sym = mCOFFObject->GetSymbol(globalVar);
		if (sym != NULL)
		{
			BeMCOperand mcOperand;
			mcOperand.mKind = BeMCOperandKind_SymbolAddr;
			mcOperand.mSymbolIdx = sym->mIdx;
			return mcOperand;
		}
	}
	break;
	case BeCastConstant::TypeId:
	{
		auto constant = (BeCastConstant*)value;

		BeMCOperand mcOperand;
		auto relTo = GetOperand(constant->mTarget);
		if (relTo.mKind == BeMCOperandKind_Immediate_Null)
		{
			mcOperand.mKind = BeMCOperandKind_Immediate_Null;
			mcOperand.mType = constant->mType;
			return mcOperand;
		}

		mcOperand = AllocVirtualReg(constant->mType);
		auto vregInfo = GetVRegInfo(mcOperand);
		vregInfo->mDefOnFirstUse = true;
		vregInfo->mRelTo = relTo;
		vregInfo->mIsExpr = true;

		return mcOperand;
	}
	break;
	case BeConstant::TypeId:
	{
		auto constant = (BeConstant*)value;
		BeMCOperand mcOperand;
		switch (constant->mType->mTypeCode)
		{
		case BeTypeCode_Boolean:
		case BeTypeCode_Int8: mcOperand.mKind = BeMCOperandKind_Immediate_i8; break;
		case BeTypeCode_Int16: mcOperand.mKind = BeMCOperandKind_Immediate_i16; break;
		case BeTypeCode_Int32: mcOperand.mKind = BeMCOperandKind_Immediate_i32; break;
		case BeTypeCode_Int64: mcOperand.mKind = BeMCOperandKind_Immediate_i64; break;
		case BeTypeCode_Float:
			mcOperand.mImmF32 = constant->mDouble;
			mcOperand.mKind = BeMCOperandKind_Immediate_f32;
			return mcOperand;
		case BeTypeCode_Double:
			mcOperand.mImmF64 = constant->mDouble;
			mcOperand.mKind = BeMCOperandKind_Immediate_f64;
			return mcOperand;
		case BeTypeCode_Pointer:
			{
				if (constant->mTarget == NULL)
				{
					mcOperand.mKind = BeMCOperandKind_Immediate_Null;
					mcOperand.mType = constant->mType;
					return mcOperand;
				}
				else
				{
					auto relTo = GetOperand(constant->mTarget);

					if (relTo.mKind == BeMCOperandKind_Immediate_Null)
					{
						mcOperand.mKind = BeMCOperandKind_Immediate_Null;
						mcOperand.mType = constant->mType;
						return mcOperand;
					}

					mcOperand = AllocVirtualReg(constant->mType);
					auto vregInfo = GetVRegInfo(mcOperand);
					vregInfo->mDefOnFirstUse = true;
					vregInfo->mRelTo = relTo;
					vregInfo->mIsExpr = true;

					return mcOperand;
				}
			}
			break;
		case BeTypeCode_Struct:
		case BeTypeCode_SizedArray:
		case BeTypeCode_Vector:
			mcOperand.mImmediate = constant->mInt64;
			mcOperand.mKind = BeMCOperandKind_Immediate_i64;
			break;
		default:
			Fail("Unhandled constant type");
		}
		mcOperand.mImmediate = constant->mInt64;
		return mcOperand;
	}
	break;
	case BeStructConstant::TypeId:
	{
		auto structConstant = (BeStructConstant*)value;

		BeMCOperand mcOperand;
		mcOperand.mKind = BeMCOperandKind_ConstAgg;
		mcOperand.mConstant = structConstant;

		return mcOperand;
	}
	case BeGEP1Constant::TypeId:
	{
		auto gepConstant = (BeGEP1Constant*)value;

		auto mcVal = GetOperand(gepConstant->mTarget);

		BePointerType* ptrType = (BePointerType*)GetType(mcVal);
		BEMC_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

		auto result = mcVal;

		// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
		int byteOffset = 0;
		BeType* elementType = ptrType->mElementType;
		byteOffset += gepConstant->mIdx0 * ptrType->mElementType->GetStride();

		result = AllocRelativeVirtualReg(ptrType, result, GetImmediate(byteOffset), 1);
		// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
		auto vregInfo = GetVRegInfo(result);
		vregInfo->mDefOnFirstUse = true;
		result.mKind = BeMCOperandKind_VReg;

		return result;
	}
	break;
	case BeGEP2Constant::TypeId:
	{
		auto gepConstant = (BeGEP2Constant*)value;

		auto mcVal = GetOperand(gepConstant->mTarget);

		BePointerType* ptrType = (BePointerType*)GetType(mcVal);
		BEMC_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

		auto result = mcVal;

		// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
		int byteOffset = 0;
		BeType* elementType = NULL;
		byteOffset += gepConstant->mIdx0 * ptrType->mElementType->GetStride();

		if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
		{
			BeStructType* structType = (BeStructType*)ptrType->mElementType;
			auto& structMember = structType->mMembers[gepConstant->mIdx1];
			elementType = structMember.mType;
			byteOffset = structMember.mByteOffset;
		}
		else if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
		{
			BEMC_ASSERT(ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray);
			auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
			elementType = arrayType->mElementType;
			byteOffset = gepConstant->mIdx1 * elementType->GetStride();
		}
		else
		{
			BEMC_ASSERT(ptrType->mElementType->mTypeCode == BeTypeCode_Vector);
			auto arrayType = (BeVectorType*)ptrType->mElementType;
			elementType = arrayType->mElementType;
			byteOffset = gepConstant->mIdx1 * elementType->GetStride();
		}

		auto elementPtrType = mModule->mContext->GetPointerTo(elementType);
		result = AllocRelativeVirtualReg(elementPtrType, result, GetImmediate(byteOffset), 1);
		// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
		auto vregInfo = GetVRegInfo(result);
		vregInfo->mDefOnFirstUse = true;
		result.mKind = BeMCOperandKind_VReg;

		return result;
	}
	break;
	case BeExtractValueConstant::TypeId:
	{
		// Note: this only handles zero-aggregates
		auto extractConstant = (BeExtractValueConstant*)value;
		auto elementType = extractConstant->GetType();

		auto mcVal = GetOperand(extractConstant->mTarget);
		auto valType = GetType(mcVal);

		BeConstant beConstant;
		beConstant.mType = elementType;
		beConstant.mUInt64 = 0;
		return GetOperand(&beConstant);
	}
	break;
	case BeFunction::TypeId:
	{
		auto sym = mCOFFObject->GetSymbol(value);
		BEMC_ASSERT(sym != NULL);
		if (sym != NULL)
		{
			BeMCOperand mcOperand;
			mcOperand.mKind = BeMCOperandKind_SymbolAddr;
			mcOperand.mSymbolIdx = sym->mIdx;
			return mcOperand;
		}
	}
	break;
	case BeCallInst::TypeId:
	{
		auto callInst = (BeCallInst*)value;
		if (callInst->mInlineResult != NULL)
			return GetOperand(callInst->mInlineResult);
	}
	break;
	case BeDbgVariable::TypeId:
	{
	}
	}

	BeMCOperand* operandPtr = NULL;
	mValueToOperand.TryGetValue(value, &operandPtr);

	//auto itr = mValueToOperand.find(value);
	if (!allowFail)
	{
		if (operandPtr == NULL)
		{
			BeDumpContext dumpCtx;
			String str;
			dumpCtx.ToString(str, value);
			Fail(StrFormat("Unable to find bevalue for operand: %s", str.c_str()));
		}
	}
	if (operandPtr == NULL)
	{
		if (allowFail)
			return BeMCOperand();
		BeMCOperand mcOperand;
		mcOperand.mKind = BeMCOperandKind_Immediate_i64;
		mcOperand.mImmediate = 0;
		return mcOperand;
	}

	auto operand = *operandPtr;
	if ((operand.mKind == BeMCOperandKind_Phi) && (!allowMetaResult))
	{
		auto phi = operand.mPhi;

		int phiInstIdx = 0;

		auto mcBlock = phi->mBlock;
		for (auto instIdx = 0; instIdx < mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			if (inst->mKind == BeMCInstKind_DefPhi)
			{
				BEMC_ASSERT(inst->mArg0.mPhi == phi);
				phiInstIdx = instIdx;
				RemoveInst(mcBlock, phiInstIdx);
				break;
			}
		}

		SetAndRestoreValue<BeMCBlock*> prevBlock(mActiveBlock, mcBlock);
		SetAndRestoreValue<int*> prevInstIdxRef(mInsertInstIdxRef, &phiInstIdx);

		auto resultType = value->GetType();
		auto result = AllocVirtualReg(resultType);
		auto vregInfo = GetVRegInfo(result);
		vregInfo->mHasDynLife = true; // No specific 'def' location
		mValueToOperand[value] = result;

		if (resultType->mTypeCode == BeTypeCode_Boolean)
		{
			CreateDefineVReg(result);

			BeMCOperand falseLabel = BeMCOperand::FromLabel(mCurLabelIdx++);
			BeMCOperand trueLabel = BeMCOperand::FromLabel(mCurLabelIdx++);
			BeMCOperand endLabel = BeMCOperand::FromLabel(mCurLabelIdx++);
			CreateCondBr(mActiveBlock, operand, trueLabel, falseLabel);

			AllocInst(BeMCInstKind_Label, falseLabel);
			AllocInst(BeMCInstKind_Mov, result, BeMCOperand::FromImmediate(0));
			AllocInst(BeMCInstKind_Br, endLabel);
			AllocInst(BeMCInstKind_Label, trueLabel);
			AllocInst(BeMCInstKind_Mov, result, BeMCOperand::FromImmediate(1));
			AllocInst(BeMCInstKind_Label, endLabel);
		}
		else
		{
			// Attempt to find common ancestor to insert a 'def' at
			SizedArray<BeMCBlock*, 16> blockSearch;
			blockSearch.reserve(phi->mValues.size());
			BeMCBlock* lowestBlock = NULL;
			for (auto& phiValue : phi->mValues)
			{
				if ((lowestBlock == NULL) || (phiValue.mBlockFrom->mBlockIdx < lowestBlock->mBlockIdx))
					lowestBlock = phiValue.mBlockFrom;
				blockSearch.push_back(phiValue.mBlockFrom);
			}
			while (true)
			{
				bool allMatched = true;
				bool didWork = false;
				for (int searchIdx = 0; searchIdx < (int)blockSearch.size(); searchIdx++)
				{
					auto& blockRef = blockSearch[searchIdx];
					if (blockRef != lowestBlock)
					{
						allMatched = false;

						for (auto& pred : blockRef->mPreds)
						{
							// Try find a block closer to start, but not below the current lowestBlock
							if ((pred->mBlockIdx >= lowestBlock->mBlockIdx) && (pred->mBlockIdx < blockRef->mBlockIdx))
							{
								blockRef = pred;
								didWork = true;
							}
						}
					}
				}

				if (allMatched)
				{
					SetAndRestoreValue<BeMCBlock*> prevActiveBlock(mActiveBlock, lowestBlock);
					SetAndRestoreValue<int*> prevInstIdxRef(mInsertInstIdxRef, NULL);
					auto inst = CreateDefineVReg(result);
					inst->mVRegsInitialized = NULL;
					inst->mDbgLoc = NULL;
					break;
				}

				if (!didWork)
				{
					BeMCBlock* nextLowestBlock = NULL;

					// Find the next candidate block
					for (auto& blockRef : blockSearch)
					{
						for (auto& pred : blockRef->mPreds)
						{
							if (pred->mBlockIdx < lowestBlock->mBlockIdx)
							{
								if ((nextLowestBlock == NULL) || (pred->mBlockIdx > nextLowestBlock->mBlockIdx))
									nextLowestBlock = pred;
							}
						}
					}

					if (nextLowestBlock == NULL)
						break;
					lowestBlock = nextLowestBlock;
				}
			}

			BeMCOperand doneLabel = BeMCOperand::FromLabel(mCurLabelIdx++);
			CreatePhiAssign(mActiveBlock, operand, result, doneLabel);

			// Don't use an explicit dbgLoc
			SetAndRestoreValue<BeDbgLoc*> prevDbgLoc(mCurDbgLoc, NULL);
			AllocInst(BeMCInstKind_Label, doneLabel);
		}

		return result;
	}

	if ((operand.mKind == BeMCOperandKind_CmpResult) && (!allowMetaResult))
	{
		auto& cmpResult = mCmpResults[operand.mCmpResultIdx];
		if (cmpResult.mResultVRegIdx == -1)
		{
			// Create the vreg now, and insert the CmpToBool during legalization
			BeType* boolType = mModule->mContext->GetPrimitiveType(BeTypeCode_Boolean);
			operand = AllocVirtualReg(boolType);
			cmpResult.mResultVRegIdx = operand.mVRegIdx;

			auto vregInfo = GetVRegInfo(operand);
			vregInfo->mDefOnFirstUse = true;
		}

		operand = BeMCOperand::FromVReg(cmpResult.mResultVRegIdx);
	}

	if ((operand.mKind == BeMCOperandKind_NotResult) && (!allowMetaResult))
	{
		auto mcValue = GetOperand(operand.mNotResult->mValue, false, allowFail);

		operand = AllocVirtualReg(GetType(mcValue));
		CreateDefineVReg(operand);
		AllocInst(BeMCInstKind_Mov, operand, mcValue);

		BeMCOperand xorVal;
		xorVal.mKind = BeMCOperandKind_Immediate_i8;
		xorVal.mImmediate = 0x1;
		AllocInst(BeMCInstKind_Xor, operand, xorVal);
	}

	if ((operand.mKind == BeMCOperandKind_VRegAddr) && (!skipForceVRegAddr))
	{
		auto vregInfo = GetVRegInfo(operand);
		if (!vregInfo->mForceMem)
			vregInfo->mForceMem = true;
	}

	return operand;
}

BeMCOperand BeMCContext::TryToVector(BeValue* value)
{
	auto operand = GetOperand(value);
	auto type = GetType(operand);
	if (!type->IsPointer())
		return operand;
	return CreateLoad(operand);
}

BeType* BeMCContext::GetType(const BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_NativeReg)
	{
		if ((operand.mReg >= X64Reg_RAX) && (operand.mReg <= X64Reg_EFL))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
		if ((operand.mReg >= X64Reg_EAX) && (operand.mReg <= X64Reg_R15D))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
		if ((operand.mReg >= X64Reg_AX) && (operand.mReg <= X64Reg_R15W))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Int16);
		if ((operand.mReg >= X64Reg_AL) && (operand.mReg <= X64Reg_R15B))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Int8);
		if ((operand.mReg >= X64Reg_XMM0_f64) && (operand.mReg <= X64Reg_XMM15_f64))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Double);
		if ((operand.mReg >= X64Reg_XMM0_f32) && (operand.mReg <= X64Reg_XMM15_f32))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_Float);
		if ((operand.mReg >= X64Reg_M128_XMM0) && (operand.mReg <= X64Reg_M128_XMM15))
			return mModule->mContext->GetPrimitiveType(BeTypeCode_M128);
	}

	if (operand.mKind == BeMCOperandKind_VReg)
		return mVRegInfo[operand.mVRegIdx]->mType;
	if (operand.mKind == BeMCOperandKind_VRegAddr)
		return mModule->mContext->GetPointerTo(mVRegInfo[operand.mVRegIdx]->mType);
	if (operand.mKind == BeMCOperandKind_VRegLoad)
	{
		auto type = mVRegInfo[operand.mVRegIdx]->mType;
		BEMC_ASSERT(type->IsPointer());
		return ((BePointerType*)type)->mElementType;
	}

	switch (operand.mKind)
	{
	case BeMCOperandKind_Immediate_i8: return mModule->mContext->GetPrimitiveType(BeTypeCode_Int8); break;
	case BeMCOperandKind_Immediate_i16: return mModule->mContext->GetPrimitiveType(BeTypeCode_Int16); break;
	case BeMCOperandKind_Immediate_i32: return mModule->mContext->GetPrimitiveType(BeTypeCode_Int32); break;
	case BeMCOperandKind_Immediate_i64:
	case BeMCOperandKind_Immediate_HomeSize: return mModule->mContext->GetPrimitiveType(BeTypeCode_Int64); break;
	case BeMCOperandKind_Immediate_Null: return operand.mType; break;
	case BeMCOperandKind_Immediate_f32:
	case BeMCOperandKind_Immediate_f32_Packed128: return mModule->mContext->GetPrimitiveType(BeTypeCode_Float); break;
	case BeMCOperandKind_Immediate_f64:
	case BeMCOperandKind_Immediate_f64_Packed128: return mModule->mContext->GetPrimitiveType(BeTypeCode_Double); break;
	}

	if (operand.mKind == BeMCOperandKind_SymbolAddr)
	{
		auto symbol = mCOFFObject->mSymbols[operand.mSymbolIdx];
		if (symbol->mType == NULL)
			return NULL;
		return mModule->mContext->GetPointerTo(symbol->mType);
	}

	if (operand.mKind == BeMCOperandKind_Symbol)
	{
		auto symbol = mCOFFObject->mSymbols[operand.mSymbolIdx];
		return symbol->mType;
	}

	if (operand.mKind == BeMCOperandKind_CmpResult)
	{
		return mModule->mContext->GetPrimitiveType(BeTypeCode_Boolean);
	}

	if (operand.mKind == BeMCOperandKind_ConstAgg)
	{
		return operand.mConstant->mType;
	}

	return NULL;
}

bool BeMCContext::AreTypesEquivalent(BeType* type0, BeType* type1)
{
	if ((type0->IsFloat()) != (type1->IsFloat()))
		return false;
	return type0->mSize == type1->mSize;
}

void BeMCContext::AddRelRefs(BeMCOperand& operand, int refCount)
{
	if (!operand.IsVRegAny())
		return;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	vregInfo->mRefCount += refCount;
	if (vregInfo->mRelTo)
		AddRelRefs(vregInfo->mRelTo, vregInfo->mRefCount + refCount);
	if (vregInfo->mRelOffset)
		AddRelRefs(vregInfo->mRelOffset, vregInfo->mRefCount + refCount);
}

int BeMCContext::FindSafeInstInsertPos(int instIdx, bool forceSafeCheck)
{
	auto inst = mActiveBlock->mInstructions[instIdx];
	bool doSafeCheck = false;
	if (forceSafeCheck)
		doSafeCheck = true;
	if ((inst->mKind == BeMCInstKind_Call) ||
		(inst->mKind == BeMCInstKind_MemSet) ||
		(inst->mKind == BeMCInstKind_MemCpy))
		doSafeCheck = true;
	if ((inst->IsMov()) && (inst->mArg0.IsNativeReg()))
		doSafeCheck = true;

	if (!doSafeCheck)
		return instIdx;

	// If we're in a CALL then move this Def before the param loads
	int bestIdx = instIdx;
	while (bestIdx > 0)
	{
		inst = mActiveBlock->mInstructions[bestIdx - 1];
		bestIdx--;

		if (inst->mKind == BeMCInstKind_PreserveVolatiles)
			return bestIdx;
	}

	return bestIdx;
}

BeMCInst* BeMCContext::AllocInst(int insertIdx)
{
	auto mcInst = mAlloc.Alloc<BeMCInst>();
	mcInst->mKind = BeMCInstKind_None;
	mcInst->mDbgLoc = mCurDbgLoc;
	mcInst->mVRegsInitialized = mCurVRegsInit;
	mcInst->mLiveness = mCurVRegsLive;
	if ((insertIdx == -1) && (mInsertInstIdxRef != NULL))
		insertIdx = (*mInsertInstIdxRef)++;
	if (insertIdx == -1)
		mActiveBlock->mInstructions.push_back(mcInst);
	else if ((insertIdx < mActiveBlock->mInstructions.mSize) && (mActiveBlock->mInstructions[insertIdx] == NULL))
		mActiveBlock->mInstructions[insertIdx] = mcInst;
	else
		mActiveBlock->mInstructions.Insert(insertIdx, mcInst);
	return mcInst;
}

BeMCInst* BeMCContext::AllocInst(BeMCInstKind instKind, int insertIdx)
{
	auto mcInst = AllocInst(insertIdx);
	mcInst->mKind = instKind;
	return mcInst;
}

BeMCInst* BeMCContext::AllocInst(BeMCInstKind instKind, const BeMCOperand& arg0, int insertIdx)
{
	auto mcInst = AllocInst(insertIdx);
	mcInst->mKind = instKind;
	mcInst->mArg0 = arg0;

	return mcInst;
}

BeMCInst* BeMCContext::AllocInst(BeMCInstKind instKind, const BeMCOperand& arg0, const BeMCOperand& arg1, int insertIdx)
{
	auto mcInst = AllocInst(insertIdx);
	mcInst->mKind = instKind;
	mcInst->mArg0 = arg0;
	mcInst->mArg1 = arg1;
	return mcInst;
}

void BeMCContext::MergeInstFlags(BeMCInst* prevInst, BeMCInst* inst, BeMCInst* nextInst)
{
	if (prevInst->mVRegsInitialized == inst->mVRegsInitialized)
		return;

	if ((inst->mVRegsInitialized != NULL) && (nextInst->mVRegsInitialized != NULL))
		nextInst->mVRegsInitialized = mVRegInitializedContext.MergeChanges(nextInst->mVRegsInitialized, inst->mVRegsInitialized);
}

void BeMCContext::RemoveInst(BeMCBlock* block, int instIdx, bool needChangesMerged, bool removeFromList)
{
	// If neither the instruction before or after this one shares the vregsInitialized flags, then we need to
	//  merge down our Changes to the next instruction
	auto inst = block->mInstructions[instIdx];
	if (instIdx > 0)
	{
		auto prevInst = block->mInstructions[instIdx - 1];
		if (prevInst->mVRegsInitialized == inst->mVRegsInitialized)
			needChangesMerged = false;
	}

	if (needChangesMerged)
	{
		if (instIdx < (int)block->mInstructions.size() - 1)
		{
			auto nextInst = block->mInstructions[instIdx + 1];
			if ((inst->mVRegsInitialized != NULL) && (nextInst->mVRegsInitialized != NULL))
			{
				nextInst->mVRegsInitialized = mVRegInitializedContext.MergeChanges(nextInst->mVRegsInitialized, inst->mVRegsInitialized);
			}
		}
	}

	if (removeFromList)
		block->mInstructions.RemoveAt(instIdx);
}

BeMCOperand BeMCContext::GetCallArgVReg(int argIdx, BeTypeCode typeCode)
{
	int pIdx = argIdx;

	BeMCNativeTypeCode vregType = BeMCNativeTypeCode_Int64;
	switch (typeCode)
	{
	case BeTypeCode_Int8:
		vregType = BeMCNativeTypeCode_Int8;
		break;
	case BeTypeCode_Int16:
		vregType = BeMCNativeTypeCode_Int16;
		break;
	case BeTypeCode_Int32:
		vregType = BeMCNativeTypeCode_Int32;
		break;
	case BeTypeCode_Int64:
		vregType = BeMCNativeTypeCode_Int64;
		break;
	case BeTypeCode_Float:
		vregType = BeMCNativeTypeCode_Float;
		break;
	case BeTypeCode_Double:
		vregType = BeMCNativeTypeCode_Double;
		break;
	case BeTypeCode_M128:
		vregType = BeMCNativeTypeCode_M128;
		break;
	case BeTypeCode_M256:
		vregType = BeMCNativeTypeCode_M256;
		break;
	case BeTypeCode_M512:
		vregType = BeMCNativeTypeCode_M512;
		break;
	default:
		typeCode = BeTypeCode_Int64;
		vregType = BeMCNativeTypeCode_Int64;
		break;
	}

	Array<int>& callArgVRegs = mCallArgVRegs[vregType];
	while (pIdx >= (int)callArgVRegs.size())
		callArgVRegs.push_back(-1);
	if (callArgVRegs[pIdx] == -1)
	{
		auto nativeType = mModule->mContext->GetPrimitiveType(typeCode);
		auto nativePtrType = mModule->mContext->GetPointerTo(nativeType);
		auto mcArg = AllocVirtualReg(nativePtrType);
		auto vregInfo = mVRegInfo[mcArg.mVRegIdx];
		vregInfo->mMustExist = true;
		vregInfo->mIsExpr = true;
		vregInfo->mRelTo = BeMCOperand::FromReg(X64Reg_RSP);
		vregInfo->mRelOffset = BeMCOperand::FromImmediate(argIdx * 8);
		callArgVRegs[pIdx] = mcArg.mVRegIdx;
		mcArg.mKind = BeMCOperandKind_VRegLoad;
		return mcArg;
	}
	else
	{
		auto mcArg = BeMCOperand::FromVReg(callArgVRegs[pIdx]);
		mcArg.mKind = BeMCOperandKind_VRegLoad;
		return mcArg;
	}
}

BeMCOperand BeMCContext::CreateCall(const BeMCOperand &func, const SizedArrayImpl<BeValue*>& args, BeType* retType, BfIRCallingConv callingConv, bool structRet, bool noReturn, bool isVarArg)
{
	SizedArray<BeMCOperand, 4> opArgs;
	for (auto itr = args.begin(); itr != args.end(); ++itr)
	{
		auto& arg = *itr;
		opArgs.push_back(GetOperand(arg));
	}
	return CreateCall(func, opArgs, retType, callingConv, structRet, noReturn, isVarArg);
}

BeMCOperand BeMCContext::CreateLoad(const BeMCOperand& mcTarget)
{
	if (mcTarget.mKind == BeMCOperandKind_Immediate_Null)
	{
		auto fakeType = GetType(mcTarget);
		auto fakePtr = AllocVirtualReg(fakeType);
		CreateDefineVReg(fakePtr);
		AllocInst(BeMCInstKind_Mov, fakePtr, BeMCOperand::FromImmediate(0));
		return CreateLoad(fakePtr);
	}

	BeMCOperand result;

	auto loadedTarget = BeMCOperand::ToLoad(mcTarget);
	auto targetType = GetType(loadedTarget);
	result = AllocVirtualReg(targetType);
	auto vregInfo = GetVRegInfo(result);
	vregInfo->mIsExpr = true;
	vregInfo->mRelTo = loadedTarget;
	result.mKind = BeMCOperandKind_VReg;
	AllocInst(BeMCInstKind_DefLoad, result);

	return result;
}

static bool NeedsDecompose(BeConstant* constant)
{
	if (auto arrayConst = BeValueDynCast<BeStructConstant>(constant))
	{
		for (auto& val : arrayConst->mMemberValues)
		{
			if (NeedsDecompose(val))
				return true;
		}
		return false;
	}

	if (auto globalVar = BeValueDynCast<BeGlobalVariable>(constant))
	{
		return true;
	}
	else if (auto castConst = BeValueDynCast<BeCastConstant>(constant))
	{
		return true;
	}
	else if (auto castConst = BeValueDynCast<BeBitCastInst>(constant))
	{
		if (auto targetConstant = BeValueDynCast<BeConstant>(castConst->mValue))
			return NeedsDecompose(targetConstant);
	}
	else if (auto castConst = BeValueDynCast<BeGEP1Constant>(constant))
	{
		return NeedsDecompose(castConst->mTarget);
	}
	else if (auto castConst = BeValueDynCast<BeGEP2Constant>(constant))
	{
		return NeedsDecompose(castConst->mTarget);
	}

	return false;
}

void BeMCContext::CreateStore(BeMCInstKind instKind, const BeMCOperand& val, const BeMCOperand& ptr)
{
	BeMCOperand mcVal = val;
	BeMCOperand mcPtr = ptr;

	if (mcVal.mKind == BeMCOperandKind_ConstAgg)
	{
		if (auto aggConst = BeValueDynCast<BeStructConstant>(mcVal.mConstant))
		{
			if (NeedsDecompose(mcVal.mConstant))
			{
				int offset = 0;

				auto aggType = aggConst->GetType();

				for (int memberIdx = 0; memberIdx < (int)aggConst->mMemberValues.size(); memberIdx++)
				{
					auto val = aggConst->mMemberValues[memberIdx];
					BeType* elemType = NULL;
					if (aggType->IsSizedArray())
						elemType = ((BeSizedArrayType*)aggType)->mElementType;
					else
					{
						auto& memberInfo = ((BeStructType*)aggType)->mMembers[memberIdx];
						offset = memberInfo.mByteOffset;
						elemType = memberInfo.mType;
					}
					if (elemType->mSize == 0)
						continue;

					auto destOperand = AllocVirtualReg(mModule->mContext->GetPointerTo(elemType));
					auto vregInfo = GetVRegInfo(destOperand);
					vregInfo->mDefOnFirstUse = true;
					vregInfo->mRelTo = mcPtr;
					vregInfo->mIsExpr = true;

					vregInfo->mRelOffset = BeMCOperand::FromImmediate(offset);

					//destOperand.mKind = BeMCOperandKind_VRegLoad;

					auto elementVal = GetOperand(val);
					//AllocInst(instKind, destOperand, elementVal);
					CreateStore(instKind, elementVal, destOperand);

					offset += elemType->GetStride();
				}
				return;
			}
		}
	}

	// Addr mov infers something like a "int* intPtr = &intVal"
	if (mcVal.mKind == BeMCOperandKind_VRegAddr)
	{
		auto vregInfo = GetVRegInfo(mcVal);
		vregInfo->mForceMem = true;
		CheckForce(vregInfo);
	}

	if (mcPtr.mKind == BeMCOperandKind_VRegAddr)
	{
		mcPtr.mKind = BeMCOperandKind_VReg;
		AllocInst(instKind, mcPtr, mcVal);
	}
	else if (mcPtr.mKind == BeMCOperandKind_VReg)
	{
		mcPtr.mKind = BeMCOperandKind_VRegLoad;
		AllocInst(instKind, mcPtr, mcVal);
	}
	else if (mcPtr.mKind == BeMCOperandKind_SymbolAddr)
	{
		mcPtr.mKind = BeMCOperandKind_Symbol;
		AllocInst(instKind, mcPtr, mcVal);
	}
	else
	{
		mcPtr = CreateLoad(mcPtr);
		AllocInst(instKind, mcPtr, mcVal);
	}
}

BeMCOperand BeMCContext::CreateCall(const BeMCOperand& func, const SizedArrayImpl<BeMCOperand>& args, BeType* retType, BfIRCallingConv callingConv, bool structRet, bool noReturn, bool isVarArg)
{
	BeMCOperand mcResult;
	//TODO: Allow user to directly specify ret addr with "sret" attribute
	int argOfs = 0;
	X64CPURegister compositeRetReg = X64Reg_None;
	bool flipFirstRegs = (structRet) && (callingConv == BfIRCallingConv_ThisCall);

	if ((retType != NULL) && (retType->IsNonVectorComposite()))
	{
		mcResult = AllocVirtualReg(retType);
		auto vregInfo = GetVRegInfo(mcResult);
		vregInfo->mMustExist = true;
		CreateDefineVReg(mcResult);

		// 'this' always goes in RCX, so push compositeRetReg out by one
		compositeRetReg = (callingConv == BfIRCallingConv_ThisCall) ? X64Reg_RDX : X64Reg_RCX;
		AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(compositeRetReg), BeMCOperand::FromVRegAddr(mcResult.mVRegIdx));
		argOfs = 1;
	}

	bool didPreserveRegs = false;
	auto _AddPreserveRegs = [&]()
	{
		if (noReturn)
			AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromPreserveFlag(BeMCPreserveFlag_NoRestore));
		else
			AllocInst(BeMCInstKind_PreserveVolatiles);
		didPreserveRegs = true;
	};

	int argCount = (int)args.size() + argOfs;
	int dynStackSize = 0;

	if (dynStackSize > 0)
		AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromImmediate(dynStackSize));

	struct _ShadowReg
	{
		X64CPURegister mFReg;
		X64CPURegister mIReg;
	};

	SizedArray<_ShadowReg, 8> shadowRegs;

	mMaxCallParamCount = BF_MAX(mMaxCallParamCount, argCount);
	for (int argIdx = args.size() - 1; argIdx >= 0; argIdx--)
	{
		if ((argIdx == 0) && (compositeRetReg == X64Reg_RDX))
			argOfs = 0;

		auto mcValue = args[argIdx];
		auto argType = GetType(mcValue);
		X64CPURegister useReg = X64Reg_None;
		int useArgIdx = argIdx + argOfs;

		if (argType->IsVector())
		{
			switch (useArgIdx)
			{
			case 0:
				useReg = X64Reg_M128_XMM0;
				break;
			case 1:
				useReg = X64Reg_M128_XMM1;
				break;
			case 2:
				useReg = X64Reg_M128_XMM2;
				break;
			case 3:
				useReg = X64Reg_M128_XMM3;
				break;
			}
		}
		else if (argType->IsFloat())
		{
			switch (useArgIdx)
			{
			case 0:
				useReg = (argType->mTypeCode == BeTypeCode_Float) ? X64Reg_XMM0_f32 : X64Reg_XMM0_f64;
				break;
			case 1:
				useReg = (argType->mTypeCode == BeTypeCode_Float) ? X64Reg_XMM1_f32 : X64Reg_XMM1_f64;
				break;
			case 2:
				useReg = (argType->mTypeCode == BeTypeCode_Float) ? X64Reg_XMM2_f32 : X64Reg_XMM2_f64;
				break;
			case 3:
				useReg = (argType->mTypeCode == BeTypeCode_Float) ? X64Reg_XMM3_f32 : X64Reg_XMM3_f64;
				break;
			}

			if (isVarArg)
			{
				X64CPURegister shadowReg = X64Reg_None;
				switch (useArgIdx)
				{
				case 0:
					shadowReg = !flipFirstRegs ? X64Reg_RCX : X64Reg_RDX;
					break;
				case 1:
					shadowReg = !flipFirstRegs ? X64Reg_RDX : X64Reg_RCX;
					break;
				case 2:
					shadowReg = X64Reg_R8;
					break;
				case 3:
					shadowReg = X64Reg_R9;
					break;
				}

				if ((shadowReg != X64Reg_None) && (useReg != X64Reg_None))
				{
					shadowRegs.push_back(_ShadowReg{ useReg, shadowReg });
				}
			}
		}
		else
		{
			switch (useArgIdx)
			{
			case 0:
				useReg = !flipFirstRegs ? X64Reg_RCX : X64Reg_RDX;
				break;
			case 1:
				useReg = !flipFirstRegs ? X64Reg_RDX : X64Reg_RCX;
				break;
			case 2:
				useReg = X64Reg_R8;
				break;
			case 3:
				useReg = X64Reg_R9;
				break;
			}
		}

		if ((!argType->IsNonVectorComposite()) && (!isVarArg)) // VarArg uses full 64 bits
			useReg = ResizeRegister(useReg, argType);

		if (mcValue.mKind == BeMCOperandKind_VRegAddr)
		{
			auto vregInfo = GetVRegInfo(mcValue);
			vregInfo->mForceMem = true;
			CheckForce(vregInfo);
		}

		if (useReg != X64Reg_None)
		{
			// We want to do the non-register params before the PreserveRegs call,
			//  because those may be a memory-to-memory mov, which will result in
			//  a temporary variable which will allocate a register which may
			//  conflict with our param regs
			if (!didPreserveRegs)
				_AddPreserveRegs();

			AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(useReg), mcValue);
		}
		else
		{
			auto useTypeCode = argType->mTypeCode;
			if (isVarArg)
			{
				if (argType->IsFloat())
					useTypeCode = BeTypeCode_Double;
				else
					useTypeCode = BeTypeCode_Int64;
			}

			auto callArgVReg = GetCallArgVReg(useArgIdx, useTypeCode);
			// Do a 'def' for every usage, to clear out the 'init' at all paths
			CreateDefineVReg(BeMCOperand::FromVReg(callArgVReg.mVRegIdx));

			if (argType->IsNonVectorComposite())
			{
				BEMC_ASSERT(mcValue.mKind == BeMCOperandKind_VReg);
				AllocInst(BeMCInstKind_Mov, callArgVReg, BeMCOperand::FromVRegAddr(mcValue.mVRegIdx));
			}
			else
				AllocInst(BeMCInstKind_Mov, callArgVReg, mcValue);
		}
	}

	if (!didPreserveRegs)
		_AddPreserveRegs();

	auto mcFunc = func;

	for (auto& shadowReg : shadowRegs) // Do float shadowing
	{
		AllocInst(BeMCInstKind_MovRaw, BeMCOperand::FromReg(shadowReg.mIReg), BeMCOperand::FromReg(shadowReg.mFReg));
	}

	AllocInst(BeMCInstKind_Call, mcFunc);

	if (dynStackSize > 0)
		AllocInst(BeMCInstKind_Add, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromImmediate(dynStackSize));

	if ((!mcResult) && (retType != NULL) && (retType->mTypeCode != BeTypeCode_None))
	{
		mcResult = AllocVirtualReg(retType);
		CreateDefineVReg(mcResult);

		X64CPURegister resultReg;
		if (retType->IsFloat())
		{
			resultReg = ResizeRegister(X64Reg_M128_XMM0, retType->mSize);
		}
		else if (retType->IsVector())
		{
			resultReg = X64Reg_M128_XMM0;
		}
		else
		{
			BEMC_ASSERT(retType->IsIntable());
			resultReg = ResizeRegister(X64Reg_RAX, retType->mSize);
		}

		AllocInst(BeMCInstKind_Mov, mcResult, BeMCOperand::FromReg(resultReg));
	}

	if (noReturn)
		AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromPreserveFlag(BeMCPreserveFlag_NoRestore));
	else
		AllocInst(BeMCInstKind_RestoreVolatiles);
	return mcResult;
}

void BeMCContext::CreateMemSet(const BeMCOperand& addr, uint8 val, int size, int align)
{
	if ((size == 8) || (size == 4) || (size == 2) || (size == 1))
	{
		BeType* type = NULL;
		BeMCOperand zeroVal;
		zeroVal.mImmediate = 0;

		switch (size)
		{
		case 8:
			type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
			zeroVal.mKind = BeMCOperandKind_Immediate_i64;
			zeroVal.mImmediate = ((int64)val << 56) | ((int64)val << 48) | ((int64)val << 40) | ((int64)val << 32) |
				(val << 24) | (val << 16) | (val << 8) | val;
			break;
		case 4:
			type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
			zeroVal.mKind = BeMCOperandKind_Immediate_i32;
			zeroVal.mImmediate = (val << 24) | (val << 16) | (val << 8) | val;
			break;
		case 2:
			type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int16);
			zeroVal.mKind = BeMCOperandKind_Immediate_i16;
			zeroVal.mImmediate = (val << 8) | val;
			break;
		case 1:
			type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int8);
			zeroVal.mKind = BeMCOperandKind_Immediate_i8;
			zeroVal.mImmediate = val;
			break;
		}

		auto ptrType = mModule->mContext->GetPointerTo(type);

		auto result = AllocVirtualReg(ptrType);
		CreateDefineVReg(result);
		auto vregInfo = GetVRegInfo(result);
		vregInfo->mRelTo = addr;
		vregInfo->mIsExpr = true;

		BeMCOperand dest;
		dest.mKind = BeMCOperandKind_VRegLoad;
		dest.mVRegIdx = result.mVRegIdx;

		AllocInst(BeMCInstKind_Mov, dest, zeroVal);
		return;
	}

	if (addr.IsVRegAny())
	{
		auto vregInfo = GetVRegInfo(addr);
		if (!vregInfo->mIsRetVal)
			vregInfo->mForceMem = true;
		CheckForce(vregInfo);
	}

	if (size <= 256)
	{
		auto temp = addr;

		BeMCOperand mcMemSetInfo;
		mcMemSetInfo.mKind = BeMCOperandKind_MemSetInfo;
		mcMemSetInfo.mMemSetInfo.mSize = size;
		mcMemSetInfo.mMemSetInfo.mAlign = align;
		mcMemSetInfo.mMemSetInfo.mValue = val;

		AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_R11));
		AllocInst(BeMCInstKind_MemSet, mcMemSetInfo, temp);
		AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_R11));
		return;
	}

	SizedArray<BeMCOperand, 3> args = { addr, BeMCOperand::FromImmediate(val), BeMCOperand::FromImmediate(size) };
	auto mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memset")->mIdx);
	CreateCall(mcFunc, args);
}

void BeMCContext::CreateMemCpy(const BeMCOperand& dest, const BeMCOperand& src, int size, int align)
{
	auto destVRegInfo = GetVRegInfo(dest);
	auto srcVRegInfo = GetVRegInfo(src);

	if ((destVRegInfo != NULL) && (srcVRegInfo != NULL) && (destVRegInfo->mIsRetVal) && (srcVRegInfo->mIsRetVal))
		return;

	if (size <= 256)
	{
		int destIdx = -1;
		if (!GetEncodedOperand(dest, destIdx))
		{
			BeMCOperand tempDest = AllocVirtualReg(GetType(dest), 1);
			CreateDefineVReg(tempDest);
			AllocInst(BeMCInstKind_Mov, tempDest, dest);
			auto vregInfo = mVRegInfo[tempDest.mVRegIdx];
			vregInfo->mForceReg = true;
			vregInfo->mDisableR11 = true;
			destIdx = tempDest.mVRegIdx;
		}

		int srcIdx = -1;
		if (!GetEncodedOperand(src, srcIdx))
		{
			BeMCOperand tempSrc = AllocVirtualReg(GetType(src), 1);
			CreateDefineVReg(tempSrc);
			AllocInst(BeMCInstKind_Mov, tempSrc, src);
			auto vregInfo = mVRegInfo[tempSrc.mVRegIdx];
			vregInfo->mForceReg = true;
			vregInfo->mDisableR11 = true;
			srcIdx = tempSrc.mVRegIdx;
		}

		BeMCOperand mcMemCpyInfo;
		mcMemCpyInfo.mKind = BeMCOperandKind_MemCpyInfo;
		mcMemCpyInfo.mMemCpyInfo.mSize = size;
		mcMemCpyInfo.mMemCpyInfo.mAlign = align;

		AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_R11));
		BeMCOperand mcVRegPair;
		mcVRegPair.mKind = BeMCOperandKind_VRegPair;
		mcVRegPair.mVRegPair.mVRegIdx0 = destIdx;
		mcVRegPair.mVRegPair.mVRegIdx1 = srcIdx;
		AllocInst(BeMCInstKind_MemCpy, mcMemCpyInfo, mcVRegPair);
		AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_R11));
		return;
	}

	SizedArray<BeMCOperand, 3> args = { dest, src, BeMCOperand::FromImmediate(size) };
	auto mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memcpy")->mIdx);
	CreateCall(mcFunc, args);
}

void BeMCContext::CreateTableSwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx)
{
	auto& sect = mCOFFObject->mRDataSect;

	auto defaultBlock = GetOperand(switchInst->mDefaultBlock);

	auto int32Type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
	auto int32PtrType = mModule->mContext->GetPointerTo(int32Type);
	auto nativeType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
	auto nativePtrType = mModule->mContext->GetPointerTo(nativeType);

	int64 loVal = switchInst->mCases.front().mValue->mInt64;
	int64 hiVal = switchInst->mCases.back().mValue->mInt64;
	int numVals = switchInst->mCases.size();

	BeMCSymbol* sym = mCOFFObject->mSymbols.Alloc();
	sym->mType = int32Type;
	sym->mName = StrFormat("@jumpTab%d", mCOFFObject->mCurJumpTableIdx++);
	sym->mIsStatic = true;
	sym->mSymKind = BeMCSymbolKind_External;
	sym->mIdx = (int)mCOFFObject->mSymbols.size() - 1;

	mCOFFObject->MarkSectionUsed(sect);
	sym->mSectionNum = sect.mSectionIdx + 1;
	sect.mData.Align(4);
	sect.mAlign = BF_MAX(sect.mAlign, 4);
	sym->mValue = sect.mData.GetSize();

	auto mcValue = GetOperand(switchInst->mValue);

	auto beType = GetType(mcValue);

	auto mcOfsValue = AllocVirtualReg(nativeType);
	CreateDefineVReg(mcOfsValue);
	if (beType->mSize < 8)
		AllocInst(BeMCInstKind_MovSX, mcOfsValue, mcValue);
	else
		AllocInst(BeMCInstKind_Mov, mcOfsValue, mcValue);
	AllocInst(BeMCInstKind_Sub, mcOfsValue, BeMCOperand::FromImmediate(loVal));
	AllocInst(BeMCInstKind_CondBr, defaultBlock, BeMCOperand::FromCmpKind(BeCmpKind_SLT));
	AllocInst(BeMCInstKind_Cmp, mcOfsValue, BeMCOperand::FromImmediate(hiVal - loVal));
	AllocInst(BeMCInstKind_CondBr, defaultBlock, BeMCOperand::FromCmpKind(BeCmpKind_SGT));

	auto jumpVReg = AllocVirtualReg(nativeType);
	CreateDefineVReg(jumpVReg);
	AllocInst(BeMCInstKind_Mov, jumpVReg, BeMCOperand::FromSymbolAddr(sym->mIdx));

	auto jumpRelVReg = AllocVirtualReg(int32PtrType);
	CreateDefineVReg(jumpRelVReg);
	auto vregInfo = mVRegInfo[jumpRelVReg.mVRegIdx];
	vregInfo->mIsExpr = true;
	vregInfo->mRelTo = jumpVReg;
	vregInfo->mRelOffset = mcOfsValue;
	vregInfo->mRelOffsetScale = 4;
	jumpRelVReg.mKind = BeMCOperandKind_VRegLoad;

	AllocInst(BeMCInstKind_Mov, jumpVReg, jumpRelVReg);

	auto imageBaseSym = mCOFFObject->GetSymbolRef("__ImageBase");
	imageBaseSym->mType = nativeType;

	auto baseVReg = AllocVirtualReg(nativeType);
	CreateDefineVReg(baseVReg);
	AllocInst(BeMCInstKind_Mov, baseVReg, BeMCOperand::FromSymbolAddr(imageBaseSym->mIdx));
	AllocInst(BeMCInstKind_Add, jumpVReg, baseVReg);
	AllocInst(BeMCInstKind_Br, jumpVReg);

	int64 lastVal = loVal - 1;

	defaultBlock.mBlock->AddPred(mActiveBlock);

	for (int caseIdx = 0; caseIdx < (int)switchInst->mCases.size(); caseIdx++)
	{
		auto& switchCase = switchInst->mCases[caseIdx];
		int64 newVal = switchCase.mValue->mInt64;

		for (int64 val = lastVal + 1; val <= newVal; val++)
		{
			BeMCSwitchEntry switchEntry;
			switchEntry.mOfs = sect.mData.GetSize();
			if (val == newVal)
			{
				auto switchBlock = GetOperand(switchCase.mBlock);
				switchBlock.mBlock->AddPred(mActiveBlock);
				switchEntry.mBlock = switchBlock.mBlock;
			}
			else
			{
				switchEntry.mBlock = defaultBlock.mBlock;
			}

			mSwitchEntries.push_back(switchEntry);

			// Placeholder
			sect.mData.Write((int32)0);
		}

		lastVal = newVal;
	}
}

void BeMCContext::CreateBinarySwitchSection(BeSwitchInst* switchInst, int startIdx, int endIdx)
{
	// This is an empirically determined binary switching limit
	if (endIdx - startIdx >= 18)
	{
		int gteLabel = mCurLabelIdx++;

		auto mcDefaultBlock = GetOperand(switchInst->mDefaultBlock);

		int midIdx = startIdx + (endIdx - startIdx) / 2;
		auto& switchCase = switchInst->mCases[midIdx];
		auto switchBlock = GetOperand(switchCase.mBlock);
		auto mcValue = GetOperand(switchInst->mValue);
		auto valueType = GetType(mcValue);

		AllocInst(BeMCInstKind_Cmp, mcValue, GetOperand(switchCase.mValue));
		AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(gteLabel), BeMCOperand::FromCmpKind(BeCmpKind_SGE));
		switchBlock.mBlock->AddPred(mActiveBlock);

		CreateBinarySwitchSection(switchInst, startIdx, midIdx);
		AllocInst(BeMCInstKind_Br, mcDefaultBlock);
		CreateLabel(-1, gteLabel);
		CreateBinarySwitchSection(switchInst, midIdx, endIdx);
		return;
	}

	for (int caseIdx = startIdx; caseIdx < endIdx; caseIdx++)
	{
		auto& switchCase = switchInst->mCases[caseIdx];
		auto switchBlock = GetOperand(switchCase.mBlock);
		auto mcValue = GetOperand(switchInst->mValue);
		AllocInst(BeMCInstKind_Cmp, mcValue, GetOperand(switchCase.mValue));
		AllocInst(BeMCInstKind_CondBr, switchBlock, BeMCOperand::FromCmpKind(BeCmpKind_EQ));
		switchBlock.mBlock->AddPred(mActiveBlock);
	}
}

void BeMCContext::CreateCondBr(BeMCBlock* mcBlock, BeMCOperand& testVal, const BeMCOperand& trueBlock, const BeMCOperand& falseBlock)
{
	if (testVal.IsImmediate())
	{
		if (testVal.mImmediate != 0)
			AllocInst(BeMCInstKind_Br, trueBlock);
		else
			AllocInst(BeMCInstKind_Br, falseBlock);
	}
	else if (testVal.mKind == BeMCOperandKind_CmpResult)
	{
		// Beef-specific: assuming CMP results aren't stomped
		auto& cmpResult = mCmpResults[testVal.mCmpResultIdx];
		AllocInst(BeMCInstKind_CondBr, trueBlock, BeMCOperand::FromCmpKind(cmpResult.mCmpKind));
		AllocInst(BeMCInstKind_Br, falseBlock);
	}
	else if (testVal.mKind == BeMCOperandKind_Phi)
	{
		auto phi = testVal.mPhi;
		auto phiBlock = phi->mBlock;

		if ((phi->mBrTrue != NULL) && (phi->mBrFalse != NULL))
		{
			// Redirect branches
			for (auto instIdx = 0; instIdx < phiBlock->mInstructions.size(); instIdx++)
			{
				auto inst = phiBlock->mInstructions[instIdx];
				if (inst->mKind == BeMCInstKind_Br)
				{
					if (inst->mArg0 == phi->mBrTrue)
						inst->mArg0 = trueBlock;
					else if (inst->mArg0 == phi->mBrFalse)
						inst->mArg0 = falseBlock;
				}
			}

			phi->mBrTrue = trueBlock;
			phi->mBrFalse = falseBlock;
			return;
		}

		phi->mBrTrue = trueBlock;
		phi->mBrFalse = falseBlock;

		// Using a Phi for a CondBr in a different block is not supported
		if (phiBlock != mcBlock)
		{
			// Special case if our using block directly leads into us
			BEMC_ASSERT(mcBlock->mPreds.size() == 1);
			BEMC_ASSERT(mcBlock->mPreds[0] == phiBlock);
		}

		for (auto instIdx = 0; instIdx < phiBlock->mInstructions.size(); instIdx++)
		{
			auto inst = phiBlock->mInstructions[instIdx];
			if (inst->mKind == BeMCInstKind_DefPhi)
			{
				BEMC_ASSERT(inst->mArg0.mPhi == phi);
				RemoveInst(phiBlock, instIdx);
				break;
			}
		}

		for (auto& phiVal : phi->mValues)
		{
			BeMCOperand landinglabel;
			if (phiVal.mValue.mKind != BeMCOperandKind_Phi)
			{
				landinglabel = BeMCOperand::FromLabel(mCurLabelIdx++);
				AllocInst(BeMCInstKind_Label, landinglabel);
			}

			int brInstIdx = -1;
			bool isFalseCmpResult = false;
			if (landinglabel)
			{
				bool found = false;

				auto _CheckBlock = [&](BeMCBlock* block)
				{
					for (int checkIdx = (int)block->mInstructions.size() - 1; checkIdx >= 0; checkIdx--)
					{
						auto checkInst = block->mInstructions[checkIdx];
						if ((checkInst->mArg0.mKind == BeMCOperandKind_Block) &&
							(checkInst->mArg0.mBlock == phi->mBlock))
						{
							brInstIdx = checkIdx;
							checkInst->mArg0 = landinglabel;
							found = true;
							// Don't break, if we're are chained to another PHI then we need to modify all the labels

							isFalseCmpResult = false;
							if ((checkIdx >= 2) && (checkInst->mKind == BeMCInstKind_Br))
							{
								auto prevInst = block->mInstructions[checkIdx - 1];
								auto prevPrevInst = block->mInstructions[checkIdx - 2];
								if ((prevPrevInst->mKind == BeMCInstKind_Cmp) && (prevPrevInst->mResult == phiVal.mValue) &&
									(prevInst->mKind == BeMCInstKind_CondBr) && (prevInst->mArg1.mCmpKind == BeCmpKind_EQ))
								{
									isFalseCmpResult = true;
								}
							}
						}
					}
				};

				_CheckBlock(phiVal.mBlockFrom);

				BEMC_ASSERT(found);
			}

			if (isFalseCmpResult)
			{
				BEMC_ASSERT(phiVal.mValue.mKind == BeMCOperandKind_CmpResult);
				AllocInst(BeMCInstKind_Br, falseBlock);
			}
			else if ((phiVal.mValue.IsImmediate()) ||
				(phiVal.mValue.mKind == BeMCOperandKind_CmpResult) ||
				(phiVal.mValue.mKind == BeMCOperandKind_Phi) ||
				(phiVal.mValue.mKind == BeMCOperandKind_NotResult))
			{
				CreateCondBr(phiVal.mBlockFrom, phiVal.mValue, trueBlock, falseBlock);
			}
			else
			{
				// Do the 'test' in the preceding block.  This is needed for liveness checking.  Using a vreg in this
				//  block would require the def to be hoisted
				SetAndRestoreValue<BeMCBlock*> prevActiveBlock(mActiveBlock, phiVal.mBlockFrom);
				BeMCOperand testImm;
				testImm.mKind = BeMCOperandKind_Immediate_i8;
				testImm.mImmediate = 1;
				auto mcInst = AllocInst(BeMCInstKind_Test, phiVal.mValue, testImm, brInstIdx);
				prevActiveBlock.Restore();

				mcInst = AllocInst(BeMCInstKind_CondBr, trueBlock, BeMCOperand::FromCmpKind(BeCmpKind_NE));
				mcInst = AllocInst(BeMCInstKind_Br, falseBlock);
			}
		}
	}
	else if (testVal.mKind == BeMCOperandKind_NotResult)
	{
		// Just get the original block and swap the true/false blocks
		auto invResult = GetOperand(testVal.mNotResult->mValue, true);
		if (invResult)
		{
			CreateCondBr(mcBlock, invResult, falseBlock, trueBlock);
		}
	}
	else
	{
		BeMCOperand testImm;
		testImm.mKind = BeMCOperandKind_Immediate_i8;
		testImm.mImmediate = 1;
		auto mcInst = AllocInst(BeMCInstKind_Test, testVal, testImm);
		mcInst = AllocInst(BeMCInstKind_CondBr, trueBlock, BeMCOperand::FromCmpKind(BeCmpKind_NE));
		mcInst = AllocInst(BeMCInstKind_Br, falseBlock);
	}
}

void BeMCContext::CreatePhiAssign(BeMCBlock* mcBlock, const BeMCOperand& testVal, const BeMCOperand& result, const BeMCOperand& doneLabel)
{
	if (testVal.mKind == BeMCOperandKind_Phi)
	{
		auto phi = testVal.mPhi;

		for (auto& phiVal : phi->mValues)
		{
			BeMCOperand landinglabel;
			if (phiVal.mValue.mKind == BeMCOperandKind_Phi)
			{
				// Remove PhiDef
				auto phi = phiVal.mValue.mPhi;
				auto phiBlock = phi->mBlock;
				for (auto instIdx = 0; instIdx < phiBlock->mInstructions.size(); instIdx++)
				{
					auto inst = phiBlock->mInstructions[instIdx];
					if (inst->mKind == BeMCInstKind_DefPhi)
					{
						BEMC_ASSERT(inst->mArg0.mPhi == phi);
						RemoveInst(phiBlock, instIdx);
						break;
					}
				}

				CreatePhiAssign(phiVal.mBlockFrom, phiVal.mValue, result, doneLabel);
			}
			else
			{
				bool found = false;

				auto _CheckBlock = [&](BeMCBlock* block)
				{
					SetAndRestoreValue<BeMCBlock*> prevActiveBlock(mActiveBlock, block);
					for (int checkIdx = (int)block->mInstructions.size() - 1; checkIdx >= 0; checkIdx--)
					{
						auto checkInst = block->mInstructions[checkIdx];
						if ((checkInst->mArg0.mKind == BeMCOperandKind_Block) &&
							(checkInst->mArg0.mBlock == phi->mBlock))
						{
							// Don't use an explicit dbgLoc
							SetAndRestoreValue<BeDbgLoc*> prevDbgLoc(mCurDbgLoc, NULL);

							if (checkInst->mKind == BeMCInstKind_CondBr)
							{
								auto falseLabel = BeMCOperand::FromLabel(mCurLabelIdx++);
								auto prevDest = checkInst->mArg0;

								checkInst->mArg0 = falseLabel;
								checkInst->mArg1.mCmpKind = BeModule::InvertCmp(checkInst->mArg1.mCmpKind);

								int insertIdx = checkIdx + 1;
								SetAndRestoreValue<int*> prevInsertIdxRef(mInsertInstIdxRef, &insertIdx);
								CreateStore(BeMCInstKind_Mov, phiVal.mValue, OperandToAddr(result));
								AllocInst(BeMCInstKind_Br, prevDest);
								AllocInst(BeMCInstKind_Label, falseLabel);
							}
							else
							{
								int insertIdx = checkIdx;
								SetAndRestoreValue<int*> prevInsertIdxRef(mInsertInstIdxRef, &insertIdx);
								CreateStore(BeMCInstKind_Mov, phiVal.mValue, OperandToAddr(result));
							}

							found = true;
						}
					}
				};

				_CheckBlock(phiVal.mBlockFrom);

				BEMC_ASSERT(found);
			}

			if (landinglabel)
			{
				AllocInst(BeMCInstKind_Br, doneLabel);
			}
		}
	}
	else
	{
		SoftFail("Unhandled CreatePhiAssign value");
	}
}

BeMCOperand BeMCContext::GetImmediate(int64 val)
{
	BeMCOperand operand;
	operand.mKind = BeMCOperandKind_Immediate_i64;
	operand.mImmediate = val;
	return operand;
}

BeMCOperand BeMCContext::OperandToAddr(const BeMCOperand& operand)
{
	BeMCOperand loadedOperand = operand;
	if (loadedOperand.mKind == BeMCOperandKind_VRegLoad)
		loadedOperand.mKind = BeMCOperandKind_VReg;
	else if (loadedOperand.mKind == BeMCOperandKind_VReg)
		loadedOperand.mKind = BeMCOperandKind_VRegAddr;
	else if (loadedOperand.mKind == BeMCOperandKind_Symbol)
		loadedOperand.mKind = BeMCOperandKind_SymbolAddr;
	else
		Fail("Invalid operand in OperandToAddr");
	return loadedOperand;
}

BeMCOperand BeMCContext::GetVReg(int regNum)
{
	auto vregInfo = mVRegInfo[regNum];
	if (vregInfo->mReg != X64Reg_None)
		return BeMCOperand::FromReg(vregInfo->mReg);

	BeMCOperand operand;
	operand.mKind = BeMCOperandKind_VReg;
	operand.mVRegIdx = regNum;
	return operand;
}

BeMCOperand BeMCContext::AllocVirtualReg(BeType* type, int refCount, bool mustBeReg)
{
	BEMC_ASSERT(type->mTypeCode != BeTypeCode_Function); // We can only have pointers to these

	if (mustBeReg)
	{
		BEMC_ASSERT(!type->IsNonVectorComposite());
		BEMC_ASSERT(type->mSize != 0);
	}

	int vregIdx = (int)mVRegInfo.size();
	BeMCVRegInfo* vregInfo = mAlloc.Alloc<BeMCVRegInfo>();
	vregInfo->mType = type;
	vregInfo->mAlign = type->mAlign;
	vregInfo->mRefCount = refCount;
	vregInfo->mForceReg = mustBeReg;
	mVRegInfo.push_back(vregInfo);

	BeMCOperand mcOperand;
	mcOperand.mKind = BeMCOperandKind_VReg;
	mcOperand.mVRegIdx = vregIdx++;

	if (mDebugging)
	{
		if (mcOperand.mVRegIdx == 8)
		{
			NOP;
		}
	}

	return mcOperand;
}

int BeMCContext::GetUnderlyingVReg(int vregIdx)
{
	while (true)
	{
		auto vregInfo = mVRegInfo[vregIdx];
		if (!vregInfo->mRelTo.IsVRegAny())
			return vregIdx;
		if (vregInfo->mRelOffset)
			return vregIdx;
		vregIdx = vregInfo->mRelTo.mVRegIdx;
	}
}

bool BeMCContext::HasForceRegs(const BeMCOperand &operand)
{
	if (!operand.IsVRegAny())
		return false;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (vregInfo->mForceReg)
		return true;
	return HasForceRegs(vregInfo->mRelTo) || HasForceRegs(vregInfo->mRelOffset);
}

bool BeMCContext::OperandsEqual(const BeMCOperand& op0, const BeMCOperand& op1, bool exact)
{
	bool isVReg0 = op0.mKind == BeMCOperandKind_VReg;
	bool isVReg1 = op1.mKind == BeMCOperandKind_VReg;
	if (!isVReg0 || !isVReg1)
	{
		return op0 == op1;
	}

	if (exact)
	{
		int vregIdx0 = op0.mVRegIdx;
		int vregIdx1 = op1.mVRegIdx;

		while (true)
		{
			auto vregInfo = mVRegInfo[vregIdx0];
			if (!vregInfo->IsDirectRelTo())
				break;
			if (vregInfo->mRelTo.mKind != BeMCOperandKind_VReg)
				return false;
			vregIdx0 = vregInfo->mRelTo.mVRegIdx;
		}

		while (true)
		{
			auto vregInfo = mVRegInfo[vregIdx1];
			if (!vregInfo->IsDirectRelTo())
				break;
			if (vregInfo->mRelTo.mKind != BeMCOperandKind_VReg)
				return false;
			vregIdx1 = vregInfo->mRelTo.mVRegIdx;
		}

		return vregIdx0 == vregIdx1;
	}

	int vregIdx0 = GetUnderlyingVReg(op0.mVRegIdx);
	int vregIdx1 = GetUnderlyingVReg(op1.mVRegIdx);

	return vregIdx0 == vregIdx1;
}

bool BeMCContext::ContainsNonOffsetRef(const BeMCOperand& checkOperand, const BeMCOperand& findOperand)
{
	if (!checkOperand.IsVRegAny())
		return false;
	if (checkOperand == findOperand)
		return true;
	auto vregInfo = GetVRegInfo(checkOperand);
	if (ContainsNonOffsetRef(vregInfo->mRelTo, findOperand))
		return true;
	return false;
}

// For all values that we are certain we will immediately use, we directly do a Def preceding its first use.
//  For Allocas in the head, however, we may not use that memory for a long time so we imply the Def location
//  in DoDefPass.  That allows us to limit how long that vreg will hold onto a register, reducing register
//  contention.
BeMCInst* BeMCContext::CreateDefineVReg(const BeMCOperand& vreg, int insertIdx)
{
	auto mcInst = AllocInst(insertIdx);
	mcInst->mKind = BeMCInstKind_Def;
	mcInst->mArg0 = vreg;
	return mcInst;
}

int BeMCContext::CreateLabel(int insertIdx, int labelIdx)
{
	auto inst = AllocInst(insertIdx);
	inst->mKind = BeMCInstKind_Label;
	inst->mArg0.mKind = BeMCOperandKind_Label;
	if (labelIdx != -1)
		inst->mArg0.mLabelIdx = labelIdx;
	else
		inst->mArg0.mLabelIdx = mCurLabelIdx++;

	return inst->mArg0.mLabelIdx;
}

bool BeMCContext::FindTarget(const BeMCOperand& loc, BeMCBlock*& outBlock, int& outInstIdx)
{
	if (loc.mKind == BeMCOperandKind_Block)
	{
		outBlock = loc.mBlock;
		outInstIdx = -1;
		return true;
	}

	if (loc.mKind == BeMCOperandKind_Label)
	{
		// Linear search :-(
		for (auto mcBlock : mBlocks)
		{
			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			{
				auto inst = mcBlock->mInstructions[instIdx];
				if ((inst->mKind == BeMCInstKind_Label) && (inst->mArg0.mLabelIdx == loc.mLabelIdx))
				{
					outBlock = mcBlock;
					outInstIdx = instIdx;
					return true;
				}
			}
		}
	}

	return false;
}

BeMCOperand BeMCContext::AllocRelativeVirtualReg(BeType* type, const BeMCOperand& relTo, const BeMCOperand& relOffset, int relScale)
{
	if (!relTo.IsVRegAny())
	{
		auto relToType = GetType(relTo);
		auto tempRelTo = AllocVirtualReg(relToType, 1);
		CreateDefineVReg(tempRelTo);
		AllocInst(BeMCInstKind_Mov, tempRelTo, relTo);
		tempRelTo.mKind = BeMCOperandKind_VReg;
		return AllocRelativeVirtualReg(type, tempRelTo, relOffset, relScale);
	}

	BEMC_ASSERT(relTo.IsVRegAny());
	auto relToVRegInfo = GetVRegInfo(relTo);

	if ((relToVRegInfo->mRelTo) && (relToVRegInfo->mRelOffsetScale == 1) && (relToVRegInfo->mRelOffset.IsImmediate()) &&
		(relOffset.IsImmediate()) && (relScale == 1))
	{
		if (relTo.mKind == BeMCOperandKind_VReg)
		{
			int offset = (int)relOffset.mImmediate;
			if (relToVRegInfo->mRelOffset)
				offset += relToVRegInfo->mRelOffset.mImmediate;
			return AllocRelativeVirtualReg(type, relToVRegInfo->mRelTo, GetImmediate(offset), 1);
		}
	}

	if ((relTo.mKind == BeMCOperandKind_VRegAddr) && (!relToVRegInfo->mRelOffset) && (relOffset.IsZero()) && (relScale == 1))
	{
		auto vregInfo = GetVRegInfo(relTo);
		if (vregInfo->IsDirectRelToAny())
		{
			if (vregInfo->mRelTo.mKind == BeMCOperandKind_Symbol)
			{
				return AllocRelativeVirtualReg(type, BeMCOperand::FromSymbolAddr(vregInfo->mRelTo.mSymbolIdx), BeMCOperand(), 1);
			}
		}
	}

	auto mcVReg = AllocVirtualReg(type);
	auto vregInfo = GetVRegInfo(mcVReg);
	vregInfo->mIsExpr = true;
	vregInfo->mRelTo = relTo;
	if (!relOffset.IsZero())
		vregInfo->mRelOffset = relOffset;
	vregInfo->mRelOffsetScale = relScale;
	mcVReg.mKind = BeMCOperandKind_VReg;
	return mcVReg;
}

BeMCVRegInfo* BeMCContext::GetVRegInfo(const BeMCOperand& operand)
{
	if (!operand.IsVRegAny())
		return NULL;
	return mVRegInfo[operand.mVRegIdx];
}

bool BeMCContext::HasSymbolAddr(const BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_SymbolAddr)
		return true;

	auto vregInfo = GetVRegInfo(operand);
	if (vregInfo == NULL)
		return false;

	if ((vregInfo->mRelTo) && (HasSymbolAddr(vregInfo->mRelTo)))
		return true;
	if ((vregInfo->mRelOffset) && (HasSymbolAddr(vregInfo->mRelOffset)))
		return true;

	return false;
}

BeMCOperand BeMCContext::ReplaceWithNewVReg(BeMCOperand& operand, int& instIdx, bool isInput, bool mustBeReg, bool preserveDeref)
{
	if ((isInput) && (operand.mKind == BeMCOperandKind_VRegLoad) && (preserveDeref))
	{
		BeMCOperand addrOperand = OperandToAddr(operand);
		BeMCOperand scratchReg = AllocVirtualReg(GetType(addrOperand), 2, mustBeReg);
		CreateDefineVReg(scratchReg, instIdx++);
		AllocInst(BeMCInstKind_Mov, scratchReg, addrOperand, instIdx++);
		operand = BeMCOperand::ToLoad(scratchReg);
		return scratchReg;
	}

	BeMCOperand scratchReg = AllocVirtualReg(GetType(operand), 2, mustBeReg);
	CreateDefineVReg(scratchReg, instIdx++);
	if (isInput)
		AllocInst(BeMCInstKind_Mov, scratchReg, operand, instIdx++);
	else
		AllocInst(BeMCInstKind_Mov, operand, scratchReg, instIdx++ + 1);
	operand = scratchReg;
	return scratchReg;
}

BeMCOperand BeMCContext::RemapOperand(BeMCOperand& operand, BeMCRemapper& regRemaps)
{
	if (!operand.IsVRegAny())
		return operand;

	int regIdx = regRemaps.GetHead(operand.mVRegIdx);
	if (regIdx < 0)
	{
		BeMCOperand newOperand;
		newOperand.mKind = operand.mKind;
		if (newOperand.mKind == BeMCOperandKind_VRegLoad)
			newOperand.mKind = BeMCOperandKind_VReg;
		else
			NotImpl();
		newOperand.mVRegIdx = -regIdx;
		return newOperand;
	}
	else
	{
		BeMCOperand newOperand;
		newOperand.mKind = operand.mKind;
		newOperand.mVRegIdx = regIdx;
		return newOperand;
	}
}

bool BeMCContext::IsLive(BeVTrackingList* liveRegs, int origVRegIdx, BeMCRemapper& regRemaps)
{
	// Check the whole remap chain to determine liveness
	int vregIdx = regRemaps.GetHead(origVRegIdx);
	while (vregIdx != -1)
	{
		if (mLivenessContext.IsSet(liveRegs, vregIdx))
			return true;
		vregIdx = regRemaps.GetNext(vregIdx);
	}
	return false;
}

void BeMCContext::AddRegRemap(int from, int to, BeMCRemapper& regRemaps, bool allowRemapToDbgVar)
{
	auto vregInfoFrom = mVRegInfo[from];
	auto vregInfoTo = mVRegInfo[to];

	BEMC_ASSERT(vregInfoFrom->mDbgVariable == NULL);

	if (vregInfoTo->mDbgVariable != NULL)
	{
		// We can't do a direct remap due to lifetime issues, so do an indirect one
		vregInfoFrom->mIsExpr = true;
		if (!vregInfoFrom->mIsRetVal)
			vregInfoFrom->mForceReg = false;
		vregInfoFrom->mRelTo = BeMCOperand::FromVReg(to);
		vregInfoFrom->mRelOffset = BeMCOperand();
		vregInfoFrom->mRelOffsetScale = 1;
		return;
	}

	regRemaps.Add(from, to);
}

bool BeMCContext::GetEncodedOperand(const BeMCOperand& operand, int& encodedVal)
{
	if (operand.mKind == BeMCOperandKind_VReg)
	{
		auto vregInfo = mVRegInfo[operand.mVRegIdx];
		encodedVal = operand.mVRegIdx;
		return true;
	}

	if (operand.mKind != BeMCOperandKind_VRegAddr)
		return false;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (vregInfo->IsDirectRelTo())
	{
		if (!GetEncodedOperand(vregInfo->mRelTo, encodedVal))
			return false;
		encodedVal |= BeMCOperand::EncodeFlag_Addr;
		return true;
	}
	if (vregInfo->mIsExpr)
		return false;
	encodedVal = operand.mVRegIdx | BeMCOperand::EncodeFlag_Addr;
	return true;
}

bool BeMCContext::HasPointerDeref(const BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_VRegLoad)
		return true;

	auto vregInfo = GetVRegInfo(operand);
	if (vregInfo == NULL)
		return false;
	if (!vregInfo->mRelTo)
		return false;

	if ((vregInfo->mRelTo.mKind == BeMCOperandKind_VRegLoad) ||
		(vregInfo->mRelTo.mKind == BeMCOperandKind_VReg))
		return true;

	return false;
}

bool BeMCContext::AreOperandsEquivalent(const BeMCOperand& origOp0, const BeMCOperand& origOp1, BeMCRemapper& regRemaps)
{
	auto op0 = origOp0;
	RemapOperand(op0, regRemaps);
	auto op1 = origOp1;
	RemapOperand(op1, regRemaps);

	if (op0.mKind != op1.mKind)
		return false;
	if (op0.mKind == BeMCOperandKind_None)
		return true;
	if (op0.IsSymbol())
		return op0.mSymbolIdx == op1.mSymbolIdx;
	if (op0.IsImmediate())
		return op0.mImmediate == op1.mImmediate;
	if (op0.IsNativeReg())
		return op0.mReg == op1.mReg;
	if (op0.IsVRegAny())
	{
		if (op0.mVRegIdx == op1.mVRegIdx)
			return true;

		auto vregInfo0 = mVRegInfo[op0.mVRegIdx];
		auto vregInfo1 = mVRegInfo[op1.mVRegIdx];
		if (vregInfo0->mType != vregInfo1->mType)
			return false;
		if ((!vregInfo0->mIsExpr) || (!vregInfo1->mIsExpr))
			return false;

		return
			(vregInfo0->mRelOffsetScale == vregInfo1->mRelOffsetScale) &&
			(AreOperandsEquivalent(vregInfo0->mRelTo, vregInfo1->mRelTo, regRemaps)) &&
			(AreOperandsEquivalent(vregInfo0->mRelOffset, vregInfo1->mRelOffset, regRemaps));
	}

	return false;
}

bool BeMCContext::CouldBeReg(const BeMCOperand& operand)
{
	if (operand.mKind != BeMCOperandKind_VReg)
		return false;

	auto vregInfo = GetVRegInfo(operand);
	if (vregInfo->mForceMem)
		return false;

	if (vregInfo->mIsExpr)
	{
		if (vregInfo->mRelOffset)
			return false;
	}

	return true;
}

// bool BeMCContext::CouldBeReg(const BeMCOperand& operand)
// {
// 	if (operand.mKind != BeMCOperandKind_VReg)
// 		return false;
//
// 	auto vregInfo = GetVRegInfo(operand);
// 	if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (mCompositeRetVRegIdx != operand.mVRegIdx))
// 	{
// 		return CouldBeReg(BeMCOperand::FromVReg(mCompositeRetVRegIdx));
// 	}
//
// 	if (vregInfo->mReg != X64Reg_None)
// 		return true;
//
// 	if (vregInfo->mForceMem)
// 		return false;
//
// 	if (vregInfo->mIsExpr)
// 	{
// 		if (vregInfo->mRelOffset)
// 			return false;
// 		return CouldBeReg(vregInfo->mRelTo);
// 	}
//
// 	return !vregInfo->mType->IsNonVectorComposite();
// }

bool BeMCContext::CheckForce(BeMCVRegInfo* vregInfo)
{
	if (!vregInfo->mIsRetVal)
	{
		if (vregInfo->mForceMem && vregInfo->mForceReg)
			SoftFail("vreg forceMem/forceReg collision");
	}
	return false;
}

void BeMCContext::MarkLive(BeVTrackingList* liveRegs, SizedArrayImpl<int>& newRegs, BeVTrackingList*& vregsInitialized, const BeMCOperand& operand)
{
	int vregIdx = operand.mVRegIdx;
	auto vregInfo = mVRegInfo[vregIdx];

	if (mLivenessContext.IsSet(liveRegs, operand.mVRegIdx))
		return;

	for (auto newReg : newRegs)
		if (newReg == operand.mVRegIdx)
			return;

	if (!mColorizer.mNodes.empty())
	{
		// Is new
		for (int i = 0; i < liveRegs->mSize; i++)
		{
			int checkReg = liveRegs->mEntries[i];
			if (checkReg >= mLivenessContext.mNumItems)
				continue;
			mColorizer.AddEdge(checkReg, operand.mVRegIdx);
		}

		for (auto checkReg : newRegs)
			mColorizer.AddEdge(checkReg, operand.mVRegIdx);
	}

	if (vregInfo->mHasDynLife)
	{
		if (!mVRegInitializedContext.IsSet(vregsInitialized, vregIdx))
		{
			// This indicates that this is a 'def' usage, meaning the value wasn't actually set yet
			//  so don't propagate this index upward
			return;
		}
	}

	if ((vregInfo->mRelTo) && (vregInfo->mRelTo.IsVRegAny()))
		MarkLive(liveRegs, newRegs, vregsInitialized, vregInfo->mRelTo);
	if ((vregInfo->mRelOffset) && (vregInfo->mRelOffset.IsVRegAny()))
		MarkLive(liveRegs, newRegs, vregsInitialized, vregInfo->mRelOffset);

	newRegs.push_back(operand.mVRegIdx);
}

BeVTrackingList* BeMCContext::MergeLiveRegs(BeVTrackingList* prevDestEntry, BeVTrackingList* mergeFrom)
{
	int vregIdx = -1;

	SizedArray<int, 16> newNodes;
	SizedArray<int, 16> prevExclusiveNodes;

	// Take nodes that were exclusive to the new set and add edges to nodes that were exclusive the old set
	/*while (true)
	{
		vregIdx = mLivenessContext.GetNextDiffSetIdx(prevDestEntry.mBits, mergeFrom, vregIdx);
		if (vregIdx == -1)
			break;

		newNodes.push_back(vregIdx);

		if (!mColorizer.mNodes.empty())
		{
			int checkReg = -1;
			while (true)
			{
				checkReg = mLivenessContext.GetNextDiffSetIdx(mergeFrom, prevDestEntry.mBits, checkReg);
				if (checkReg == -1)
					break;
				mColorizer.AddEdge(checkReg, vregIdx);
			}
		}
	}*/

	auto prevItr = prevDestEntry->begin();
	auto prevEnd = prevDestEntry->end();
	auto mergeFromItr = mergeFrom->begin();
	auto mergeFromEnd = mergeFrom->end();
	while ((prevItr != prevEnd) && (mergeFromItr != mergeFromEnd))
	{
		int prevIdx = *prevItr;
		int mergeIdx = *mergeFromItr;
		bool done = false;

		while (mergeIdx < prevIdx)
		{
			newNodes.push_back(mergeIdx);
			++mergeFromItr;
			if (mergeFromItr == mergeFromEnd)
			{
				done = true;
				break;
			}
			mergeIdx = *mergeFromItr;
		}
		if (done)
			break;

		while (prevIdx < mergeIdx)
		{
			prevExclusiveNodes.push_back(prevIdx);
			++prevItr;
			if (prevItr == prevEnd)
			{
				done = true;
				break;
			}
			prevIdx = *prevItr;
		}
		if (done)
			break;

		if (prevIdx == mergeIdx)
		{
			++prevItr;
			++mergeFromItr;
		}
	}
	while (prevItr != prevEnd)
	{
		prevExclusiveNodes.push_back(*prevItr);
		++prevItr;
	}
	while (mergeFromItr != mergeFromEnd)
	{
		newNodes.push_back(*mergeFromItr);
		++mergeFromItr;
	}

	if (!mColorizer.mNodes.empty())
	{
		for (int newIdx : newNodes)
		{
			for (int prevExclusiveIdx : prevExclusiveNodes)
			{
				mColorizer.AddEdge(newIdx, prevExclusiveIdx);
			}
		}
	}

	return mLivenessContext.Add(prevDestEntry, newNodes, false);
}

static void DedupPushBack(SizedArrayImpl<int>& vec, int val)
{
	if (vec.Contains(val))
		return;
	vec.push_back(val);
}

static int genLivenessIdx = 0;

void BeMCContext::GenerateLiveness(BeMCBlock* block, BeVTrackingGenContext* genCtx, bool& modifiedBlockBefore, bool& modifiedBlockAfter)
{
	genCtx->mBlocks[block->mBlockIdx].mGenerateCount++;

	modifiedBlockBefore = false;
	modifiedBlockAfter = false;
	genCtx->mCalls++;

	bool debugging = false;
	//if (mDebugging) debugging = true;
	//if (mBeFunction->mName == "?Draw@DarkEditWidgetContent@dark@theme@Beefy@@UEAAXPEAVGraphics@gfx@4@@Z")
		//debugging = true;
		//"?DrawEntry@DrawContext@PerfView@BeefPerf@@QEAAXPEAVGraphics@gfx@Beefy@@PEAVTrackNodeEntry@23@MM@Z")
	//debugging &= mcColorizer != NULL;

	if (debugging)
	{
		if (block->mBlockIdx == 220)
		{
			//BF_ASSERT(mLivenessContext.IsSet(succLiveRegs, 36));
		}
	}

	if (debugging)
	{
		OutputDebugStrF("GenerateLiveness %s(%d)\n", ToString(BeMCOperand::FromBlock(block)).c_str(), block->mBlockIdx);
	}

	genCtx->mHandledCalls++;

	BeMDNode* curDbgScope = NULL;

	// Don't use 'ref' here, it's important that mSuccLiveRegs actually means 'successor', otherwise
	//  we could miss a later-occurring liveness overlap on a different successor
	BeVTrackingList* liveRegs = block->mSuccLiveness;
	BeVTrackingList* vregsInitialized = block->mSuccVRegsInitialized;
	// When we process our first vregsInitialized (at the bottom of the block), there won't be Change entries
	//  for us to use to determine the delta from mSuccVRegsInitialized to inst->mVRegsInitialized
	bool needsManualVRegInitDiff = true;

	for (int instIdx = (int)block->mInstructions.size() - 1; instIdx >= 0; instIdx--)
	{
		genLivenessIdx++;
		auto inst = block->mInstructions[instIdx];
		auto prevLiveness = inst->mLiveness;

		genCtx->mInstructions++;

		SizedArray<int, 16> removeVec;
		SizedArray<int, 16> addVec;
		SizedArray<int, 16> filteredRemoveVec;
		SizedArray<int, 16> filteredAddVec;

		inst->mLiveness = liveRegs;

		if ((inst->mVRegsInitialized != NULL) && (inst->mVRegsInitialized != vregsInitialized))
		{
			auto _VRegUninit = [&](int vregIdxEx)
			{
				int vregIdx = vregIdxEx % mVRegInitializedContext.mNumItems;

				auto vregInfo = mVRegInfo[vregIdx];

				if (!vregInfo->mHasDynLife)
					return;

				bool doClear = false;

				if (vregInfo->mDoConservativeLife)
				{
					// Only do liveness clear when both 'init' and 'non-init' is set
					int otherVRegIdxEx = vregIdx;
					if (vregIdxEx < mVRegInitializedContext.mNumItems)
						otherVRegIdxEx += mVRegInitializedContext.mNumItems;
					if (!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, otherVRegIdxEx))
						doClear = true;
				}
				else
				{
					// Only do liveness clear on 'init' clearing out
					if (vregIdx < mVRegInitializedContext.mNumItems)
						doClear = true;
				}

				if (doClear)
				{
					DedupPushBack(removeVec, vregIdx);
				}
			};

			if (needsManualVRegInitDiff)
			{
				BEMC_ASSERT(vregsInitialized == block->mSuccVRegsInitialized);
				// Manually compare
				auto vregsInit0 = vregsInitialized;
				auto vregsInit1 = inst->mVRegsInitialized;
				if (vregsInit0 != vregsInit1)
				{
					int idx0 = 0;
					int idx1 = 0;

					while ((idx0 != vregsInit0->mSize) && (idx1 != vregsInit1->mSize))
					{
						int val0 = vregsInit0->mEntries[idx0];
						int val1 = vregsInit1->mEntries[idx1];
						if (val0 == val1)
						{
							idx0++;
							idx1++;
							continue;
						}
						bool done = false;
						while (val0 < val1)
						{
							_VRegUninit(val0);
							idx0++;
							if (idx0 >= vregsInit0->mSize)
							{
								done = true;
								break;
							}
							val0 = vregsInit0->mEntries[idx0];
						}
						if (done)
							break;

						while (val1 < val0)
						{
							idx1++;
							if (idx1 >= vregsInit1->mSize)
							{
								done = true;
								break;
							}
							val1 = vregsInit1->mEntries[idx1];
						}
						if (done)
							break;
					}
					while (idx0 < vregsInit0->mSize)
					{
						_VRegUninit(vregsInit0->mEntries[idx0++]);
					}
				}
			}
			else
			{
				for (int changeIdx = 0; changeIdx < vregsInitialized->mNumChanges; changeIdx++)
				{
					int vregIdxEx = vregsInitialized->GetChange(changeIdx);
					if (vregIdxEx < 0)
						continue;

					// It's possible this vregsInitialized is equivalent to the one in 'inst', but also merged with another 'inst'
					//  during legalization.  We need to avoid uninitializing vregs if that's the case.
					//  the entry above this
					if (inst->mVRegsInitialized->ContainsChange(vregIdxEx))
						continue;

					_VRegUninit(vregIdxEx);
				}
			}

			vregsInitialized = inst->mVRegsInitialized;
		}
		if (inst->mVRegsInitialized != NULL)
			needsManualVRegInitDiff = false;

		if (inst->mKind == BeMCInstKind_DbgDecl)
		{
			if (!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, inst->mArg0.mVRegIdx))
			{
				// There are some rare cases with conditional branches where one branch will have a vreg marked as
				//  initialized, which causes the variable to be marked as live, which propagates upward into the block
				//  containing a variable declaration, before the actual def point
				DedupPushBack(removeVec, inst->mArg0.mVRegIdx);
			}
			liveRegs = mLivenessContext.Modify(liveRegs, addVec, removeVec, filteredAddVec, filteredRemoveVec);
			continue;
		}

		// This is used for clearing out things like usage of inline return values, which will be accessed after their
		//  lifetime end (the lifetime ends inside the inlined method but the value is used afterward, in the inlining
		//  function.  This will emit as a load of a dbgVar, so we need to drill down into the relTo values
		if (inst->mKind == BeMCInstKind_LifetimeStart)
		{
			BEMC_ASSERT(inst->mArg0.IsVRegAny());
			int vregIdx = inst->mArg0.mVRegIdx;
			while (true)
			{
				DedupPushBack(removeVec, vregIdx);
				auto vregInfo = mVRegInfo[vregIdx];
				if (!vregInfo->IsDirectRelTo())
					break;
				BEMC_ASSERT(vregInfo->mRelTo.IsVReg());
				vregIdx = vregInfo->mRelTo.mVRegIdx;
			}
			liveRegs = mLivenessContext.Modify(liveRegs, addVec, removeVec, filteredAddVec, filteredRemoveVec);
			continue;
		}

		if (inst->mKind == BeMCInstKind_LifetimeEnd)
		{
			auto vregInfo = GetVRegInfo(inst->mArg0);
			if (vregInfo->mDbgVariable == NULL)
			{
				// Only extend lifetime down through LifetimeEnd when we have a debug variable we want to keep alive,
				//  otherwise constrict lifetime to actual usage
				liveRegs = mLivenessContext.Modify(liveRegs, addVec, removeVec, filteredAddVec, filteredRemoveVec);
				continue;
			}
		}

		if (inst->IsDef())
		{
			DedupPushBack(removeVec, inst->mArg0.mVRegIdx);
			liveRegs = mLivenessContext.Modify(liveRegs, addVec, removeVec, filteredAddVec, filteredRemoveVec);
			continue;
		}

		auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
		for (auto operand : operands)
		{
			if (operand->IsSymbol())
			{
				auto sym = mCOFFObject->mSymbols[operand->mSymbolIdx];
				if (sym->mIsTLS)
				{
					MarkLive(liveRegs, addVec, vregsInitialized, BeMCOperand::FromVReg(mTLSVRegIdx));
				}
			}

			if (operand->mKind == BeMCOperandKind_VRegPair)
			{
				auto mcOperand = BeMCOperand::FromEncoded(operand->mVRegPair.mVRegIdx0);
				if (mcOperand.IsVRegAny())
					MarkLive(liveRegs, addVec, vregsInitialized, mcOperand);
				mcOperand = BeMCOperand::FromEncoded(operand->mVRegPair.mVRegIdx1);
				if (mcOperand.IsVRegAny())
					MarkLive(liveRegs, addVec, vregsInitialized, mcOperand);
			}

			if (operand->IsVRegAny())
			{
				MarkLive(liveRegs, addVec, vregsInitialized, *operand);
			}
		}

		liveRegs = mLivenessContext.Modify(liveRegs, addVec, removeVec, filteredAddVec, filteredRemoveVec);

		/*if ((!isFirstEntry) && (liveRegs == prevLiveness))
		{
			// We've already been here before and nothing changed
			return;
		}*/

		inst->mLiveness = liveRegs;
	}

	if (block == mBlocks[0])
	{
		BEMC_ASSERT(block->mBlockIdx == 0);
		if (!mLivenessContext.IsEmpty(liveRegs))
		{
			for (int vregIdx = 0; vregIdx < mLivenessContext.mNumEntries; vregIdx++)
			{
				if (mLivenessContext.IsSet(liveRegs, vregIdx))
				{
					auto vregInfo = mVRegInfo[vregIdx];
					// If we are still alive then the only valid reason is because we have mHasDynLife and our 'init' flag is still set
					if (vregInfo->mHasDynLife)
					{
						if (!mVRegInitializedContext.IsSet(vregsInitialized, vregIdx))
						{
							if (vregInfo->mDoConservativeLife)
								BEMC_ASSERT(mVRegInitializedContext.IsSet(vregsInitialized, vregIdx, BeTrackKind_Uninitialized));
							else
								SoftFail("VReg lifetime error");
						}
					}
					else
					{
						SoftFail("VReg lifetime error");
					}
				}
			}
		}
	}

	for (auto pred : block->mPreds)
	{
		auto& entry = genCtx->mBlocks[pred->mBlockIdx];
		BEMC_ASSERT(pred == mBlocks[pred->mBlockIdx]);

		auto newSuccLiveness = MergeLiveRegs(pred->mSuccLiveness, liveRegs);
		if (newSuccLiveness == pred->mSuccLiveness)
			continue;
		pred->mSuccLiveness = newSuccLiveness;

		pred->mSuccVRegsInitialized = mVRegInitializedContext.Merge(pred->mSuccVRegsInitialized, vregsInitialized);

		if (pred->mBlockIdx > block->mBlockIdx)
			modifiedBlockAfter = true;
		else
			modifiedBlockBefore = true;
		entry.mGenerateQueued = true;
	}
}

void BeMCContext::GenerateLiveness()
{
	//GenerateLiveness_OLD(mcColorizer);
	//return;

	mLivenessContext.mStats = BeVTrackingContext::Stats();

#ifdef _DEBUG
	// So we can Print() while generating liveness (the old values would have been unallocated)
	for (auto mcBlock : mBlocks)
	{
		for (auto mcInst : mcBlock->mInstructions)
		{
			mcInst->mLiveness = NULL;
		}
	}
#endif

	BP_ZONE("BeMCContext::GenerateLiveness");

	mLivenessContext.Clear();
	mLivenessContext.Init((int)mVRegInfo.size());

	auto emptyList = mLivenessContext.AllocEmptyList();

	BeVTrackingGenContext genCtx;
	genCtx.mEmptyList = emptyList;
	genCtx.mBlocks.Resize(mBlocks.size());

	for (auto mcBlock : mBlocks)
	{
		mcBlock->mSuccLiveness = emptyList;
		mcBlock->mSuccVRegsInitialized = emptyList;

		if (mTLSVRegIdx != -1)
		{
			// Keep TLS alive
			SizedArray<int, 1> vec = { mTLSVRegIdx };
			mcBlock->mSuccLiveness = mLivenessContext.Add(mcBlock->mSuccLiveness, vec, false);
		}
	}

	while (true)
	{
		bool didWork = false;

		// Handle any outstanding pred entries
		for (int blockIdx = (int)mBlocks.size() - 1; blockIdx >= 0; blockIdx--)
		{
			auto& entry = genCtx.mBlocks[blockIdx];
			if (entry.mGenerateQueued)
			{
				entry.mGenerateQueued = false;
				didWork = true;
				auto block = mBlocks[blockIdx];

				bool modifiedBlockBefore;
				bool modifiedBlockAfter;
				GenerateLiveness(block, &genCtx, modifiedBlockBefore, modifiedBlockAfter);
				if (modifiedBlockAfter)
					break;
			}
		}

		// If no pred entries, find blocks that haven't been processed yet
		if (!didWork)
		{
			for (int blockIdx = (int)mBlocks.size() - 1; blockIdx >= 0; blockIdx--)
			{
				auto& entry = genCtx.mBlocks[blockIdx];
				if (entry.mGenerateCount == 0)
				{
					didWork = true;
					auto block = mBlocks[blockIdx];
					bool modifiedBlockBefore;
					bool modifiedBlockAfter;
					GenerateLiveness(block, &genCtx, modifiedBlockBefore, modifiedBlockAfter);
					if (modifiedBlockBefore || modifiedBlockAfter)
						break;
				}
			}
		}

		if (!didWork)
			break;
	}

	int instCount = 0;
	for (auto block : mBlocks)
		instCount += (int)block->mInstructions.size();

	BpEvent("GenerateLiveness Results",
		StrFormat("Blocks: %d\nInstructions: %d\nVRegs: %d\nCalls: %d\nHandled Calls: %d\nProcessed Instructions: %d\nLiveness Bytes: %d\n"
			"Temp Bytes: %d\nBits Bytes: %d\nList Bytes: %d\nSucc Bytes: %d",
			mBlocks.size(), instCount, mVRegInfo.size(), genCtx.mCalls, genCtx.mHandledCalls, genCtx.mInstructions, mLivenessContext.mAlloc.GetAllocSize(),
			genCtx.mAlloc.GetAllocSize(), mLivenessContext.mStats.mBitsBytes, mLivenessContext.mStats.mListBytes, mLivenessContext.mStats.mSuccBytes).c_str());
}

void BeMCContext::IntroduceVRegs(const BeMCOperand& newVReg, BeMCBlock* block, int startInstIdx, int lastInstIdx)
{
	return;

	/*BF_ASSERT((block->mInstructions[startInstIdx]->mKind == BeMCInstKind_Def) && (block->mInstructions[startInstIdx]->mArg0 == newVReg));
	BF_ASSERT(mColorizer.mNodes.size() == newVReg.mVRegIdx);

	mColorizer.mNodes.resize(mColorizer.mNodes.size() + 1);

	BeVTrackingBits* lastLiveness = NULL;

	for (int instIdx = startInstIdx + 1; instIdx <= lastInstIdx; instIdx++)
	{
		auto inst = block->mInstructions[instIdx];

		if (lastLiveness == NULL)
		{
			for (int vregIdx : *inst->mLiveness)
			{
				if (vregIdx >= mLivenessContext.mNumItems)
					continue;
				mColorizer.AddEdge(newVReg.mVRegIdx, vregIdx);
			}
		}
		else
		{
			int vregIdx = -1;
			while (true)
			{
				vregIdx = mLivenessContext.GetNextDiffSetIdx(lastLiveness, inst->mLiveness, vregIdx);
				if (vregIdx == -1)
					break;
				mColorizer.AddEdge(newVReg.mVRegIdx, vregIdx);
			}
		}

		if (inst->mVRegsInitializedEx != NULL)
		{
			auto vregArray = (BeMCVRegArray*)mAlloc.AllocBytes(sizeof(int) * (inst->mVRegsInitializedEx->mSize + 1 + 1), sizeof(int));
			vregArray->mSize = inst->mVRegsInitializedEx->mSize + 1;

			for (int listIdx = 0; listIdx < inst->mVRegsInitializedEx->mSize; listIdx++)
			{
				mColorizer.AddEdge(newVReg.mVRegIdx, inst->mVRegsInitializedEx->mVRegIndices[listIdx]);
				vregArray->mVRegIndices[listIdx] = inst->mVRegsInitializedEx->mVRegIndices[listIdx];
			}
			vregArray->mVRegIndices[vregArray->mSize - 1] = newVReg.mVRegIdx;
			inst->mVRegsInitializedEx = vregArray;
		}
		else
		{
			auto vregArray = (BeMCVRegArray*)mAlloc.AllocBytes(sizeof(int) * 2, sizeof(int));
			vregArray->mSize = 1;
			vregArray->mVRegIndices[0] = newVReg.mVRegIdx;
			inst->mVRegsInitializedEx = vregArray;
		}

		lastLiveness = inst->mLiveness;
	}*/
}

bool BeMCContext::IsVolatileReg(X64CPURegister reg)
{
	switch (ResizeRegister(reg, 8))
	{
	case X64Reg_RAX:
	case X64Reg_RCX:
	case X64Reg_RDX:
	case X64Reg_R8:
	case X64Reg_R9:
	case X64Reg_R10:
	case X64Reg_R11:
	case X64Reg_XMM0_f64:
	case X64Reg_XMM1_f64:
	case X64Reg_XMM2_f64:
	case X64Reg_XMM3_f64:
	case X64Reg_XMM4_f64:
	case X64Reg_XMM5_f64:
		return true;
	default:
		return false;
	}
}

bool BeMCContext::IsXMMReg(X64CPURegister reg)
{
	return (reg >= X64Reg_XMM0_f64) && (reg <= X64Reg_M128_XMM15);
}

X64CPURegister BeMCContext::ResizeRegister(X64CPURegister reg, int numBytes)
{
	if (numBytes == 16)
	{
		switch (reg)
		{
		case X64Reg_XMM0_f32:
		case X64Reg_XMM0_f64:
		case X64Reg_M128_XMM0: return X64Reg_M128_XMM0;
		case X64Reg_XMM1_f32:
		case X64Reg_XMM1_f64:
		case X64Reg_M128_XMM1: return X64Reg_M128_XMM1;
		case X64Reg_XMM2_f32:
		case X64Reg_XMM2_f64:
		case X64Reg_M128_XMM2: return X64Reg_M128_XMM2;
		case X64Reg_XMM3_f32:
		case X64Reg_XMM3_f64:
		case X64Reg_M128_XMM3: return X64Reg_M128_XMM3;
		case X64Reg_XMM4_f32:
		case X64Reg_XMM4_f64:
		case X64Reg_M128_XMM4: return X64Reg_M128_XMM4;
		case X64Reg_XMM5_f32:
		case X64Reg_XMM5_f64:
		case X64Reg_M128_XMM5: return X64Reg_M128_XMM5;
		case X64Reg_XMM6_f32:
		case X64Reg_XMM6_f64:
		case X64Reg_M128_XMM6: return X64Reg_M128_XMM6;
		case X64Reg_XMM7_f32:
		case X64Reg_XMM7_f64:
		case X64Reg_M128_XMM7: return X64Reg_M128_XMM7;
		case X64Reg_XMM8_f32:
		case X64Reg_XMM8_f64:
		case X64Reg_M128_XMM8: return X64Reg_M128_XMM8;
		case X64Reg_XMM9_f32:
		case X64Reg_XMM9_f64:
		case X64Reg_M128_XMM9: return X64Reg_M128_XMM9;
		case X64Reg_XMM10_f32:
		case X64Reg_XMM10_f64:
		case X64Reg_M128_XMM10: return X64Reg_M128_XMM10;
		case X64Reg_XMM11_f32:
		case X64Reg_XMM11_f64:
		case X64Reg_M128_XMM11: return X64Reg_M128_XMM11;
		case X64Reg_XMM12_f32:
		case X64Reg_XMM12_f64:
		case X64Reg_M128_XMM12: return X64Reg_M128_XMM12;
		case X64Reg_XMM13_f32:
		case X64Reg_XMM13_f64:
		case X64Reg_M128_XMM13: return X64Reg_M128_XMM13;
		case X64Reg_XMM14_f32:
		case X64Reg_XMM14_f64:
		case X64Reg_M128_XMM14: return X64Reg_M128_XMM14;
		case X64Reg_XMM15_f32:
		case X64Reg_XMM15_f64:
		case X64Reg_M128_XMM15: return X64Reg_M128_XMM15;
		}
	}

	if (numBytes == 8)
	{
		switch (reg)
		{
		case X64Reg_DIL:
		case X64Reg_DI:
		case X64Reg_EDI:
		case X64Reg_RDI: return X64Reg_RDI;
		case X64Reg_SIL:
		case X64Reg_SI:
		case X64Reg_ESI:
		case X64Reg_RSI: return X64Reg_RSI;
		case X64Reg_AL:
		case X64Reg_AH:
		case X64Reg_AX:
		case X64Reg_EAX:
		case X64Reg_RAX: return X64Reg_RAX;
		case X64Reg_DL:
		case X64Reg_DH:
		case X64Reg_DX:
		case X64Reg_EDX:
		case X64Reg_RDX: return X64Reg_RDX;
		case X64Reg_CL:
		case X64Reg_CH:
		case X64Reg_CX:
		case X64Reg_ECX:
		case X64Reg_RCX: return X64Reg_RCX;
		case X64Reg_BL:
		case X64Reg_BH:
		case X64Reg_BX:
		case X64Reg_EBX:
		case X64Reg_RBX: return X64Reg_RBX;
		case X64Reg_R8B:
		case X64Reg_R8W:
		case X64Reg_R8D:
		case X64Reg_R8: return X64Reg_R8;
		case X64Reg_R9B:
		case X64Reg_R9W:
		case X64Reg_R9D:
		case X64Reg_R9: return X64Reg_R9;
		case X64Reg_R10B:
		case X64Reg_R10W:
		case X64Reg_R10D:
		case X64Reg_R10: return X64Reg_R10;
		case X64Reg_R11B:
		case X64Reg_R11W:
		case X64Reg_R11D:
		case X64Reg_R11: return X64Reg_R11;
		case X64Reg_R12B:
		case X64Reg_R12W:
		case X64Reg_R12D:
		case X64Reg_R12: return X64Reg_R12;
		case X64Reg_R13B:
		case X64Reg_R13W:
		case X64Reg_R13D:
		case X64Reg_R13: return X64Reg_R13;
		case X64Reg_R14B:
		case X64Reg_R14W:
		case X64Reg_R14D:
		case X64Reg_R14: return X64Reg_R14;
		case X64Reg_R15B:
		case X64Reg_R15W:
		case X64Reg_R15D:
		case X64Reg_R15: return X64Reg_R15;

		case X64Reg_XMM0_f32:
		case X64Reg_XMM0_f64:
		case X64Reg_M128_XMM0: return X64Reg_XMM0_f64;
		case X64Reg_XMM1_f32:
		case X64Reg_XMM1_f64:
		case X64Reg_M128_XMM1: return X64Reg_XMM1_f64;
		case X64Reg_XMM2_f32:
		case X64Reg_XMM2_f64:
		case X64Reg_M128_XMM2: return X64Reg_XMM2_f64;
		case X64Reg_XMM3_f32:
		case X64Reg_XMM3_f64:
		case X64Reg_M128_XMM3: return X64Reg_XMM3_f64;
		case X64Reg_XMM4_f32:
		case X64Reg_XMM4_f64:
		case X64Reg_M128_XMM4: return X64Reg_XMM4_f64;
		case X64Reg_XMM5_f32:
		case X64Reg_XMM5_f64:
		case X64Reg_M128_XMM5: return X64Reg_XMM5_f64;
		case X64Reg_XMM6_f32:
		case X64Reg_XMM6_f64:
		case X64Reg_M128_XMM6: return X64Reg_XMM6_f64;
		case X64Reg_XMM7_f32:
		case X64Reg_XMM7_f64:
		case X64Reg_M128_XMM7: return X64Reg_XMM7_f64;
		case X64Reg_XMM8_f32:
		case X64Reg_XMM8_f64:
		case X64Reg_M128_XMM8: return X64Reg_XMM8_f64;
		case X64Reg_XMM9_f32:
		case X64Reg_XMM9_f64:
		case X64Reg_M128_XMM9: return X64Reg_XMM9_f64;
		case X64Reg_XMM10_f32:
		case X64Reg_XMM10_f64:
		case X64Reg_M128_XMM10: return X64Reg_XMM10_f64;
		case X64Reg_XMM11_f32:
		case X64Reg_XMM11_f64:
		case X64Reg_M128_XMM11: return X64Reg_XMM11_f64;
		case X64Reg_XMM12_f32:
		case X64Reg_XMM12_f64:
		case X64Reg_M128_XMM12: return X64Reg_XMM12_f64;
		case X64Reg_XMM13_f32:
		case X64Reg_XMM13_f64:
		case X64Reg_M128_XMM13: return X64Reg_XMM13_f64;
		case X64Reg_XMM14_f32:
		case X64Reg_XMM14_f64:
		case X64Reg_M128_XMM14: return X64Reg_XMM14_f64;
		case X64Reg_XMM15_f32:
		case X64Reg_XMM15_f64:
		case X64Reg_M128_XMM15: return X64Reg_XMM15_f64;
		}
		return reg;
	}
	if (numBytes == 4)
	{
		switch (reg)
		{
		case X64Reg_DIL:
		case X64Reg_DI:
		case X64Reg_EDI:
		case X64Reg_RDI: return X64Reg_EDI;
		case X64Reg_SIL:
		case X64Reg_SI:
		case X64Reg_ESI:
		case X64Reg_RSI: return X64Reg_ESI;
		case X64Reg_AL:
		case X64Reg_AH:
		case X64Reg_AX:
		case X64Reg_EAX:
		case X64Reg_RAX: return X64Reg_EAX;
		case X64Reg_DL:
		case X64Reg_DH:
		case X64Reg_DX:
		case X64Reg_EDX:
		case X64Reg_RDX: return X64Reg_EDX;
		case X64Reg_CL:
		case X64Reg_CH:
		case X64Reg_CX:
		case X64Reg_ECX:
		case X64Reg_RCX: return X64Reg_ECX;
		case X64Reg_BL:
		case X64Reg_BH:
		case X64Reg_BX:
		case X64Reg_EBX:
		case X64Reg_RBX: return X64Reg_EBX;
		case X64Reg_R8B:
		case X64Reg_R8W:
		case X64Reg_R8D:
		case X64Reg_R8: return X64Reg_R8D;
		case X64Reg_R9B:
		case X64Reg_R9W:
		case X64Reg_R9D:
		case X64Reg_R9: return X64Reg_R9D;
		case X64Reg_R10B:
		case X64Reg_R10W:
		case X64Reg_R10D:
		case X64Reg_R10: return X64Reg_R10D;
		case X64Reg_R11B:
		case X64Reg_R11W:
		case X64Reg_R11D:
		case X64Reg_R11: return X64Reg_R11D;
		case X64Reg_R12B:
		case X64Reg_R12W:
		case X64Reg_R12D:
		case X64Reg_R12: return X64Reg_R12D;
		case X64Reg_R13B:
		case X64Reg_R13W:
		case X64Reg_R13D:
		case X64Reg_R13: return X64Reg_R13D;
		case X64Reg_R14B:
		case X64Reg_R14W:
		case X64Reg_R14D:
		case X64Reg_R14: return X64Reg_R14D;
		case X64Reg_R15B:
		case X64Reg_R15W:
		case X64Reg_R15D:
		case X64Reg_R15: return X64Reg_R15D;

		case X64Reg_XMM0_f32:
		case X64Reg_XMM0_f64:
		case X64Reg_M128_XMM0: return X64Reg_XMM0_f32;
		case X64Reg_XMM1_f32:
		case X64Reg_XMM1_f64:
		case X64Reg_M128_XMM1: return X64Reg_XMM1_f32;
		case X64Reg_XMM2_f32:
		case X64Reg_XMM2_f64:
		case X64Reg_M128_XMM2: return X64Reg_XMM2_f32;
		case X64Reg_XMM3_f32:
		case X64Reg_XMM3_f64:
		case X64Reg_M128_XMM3: return X64Reg_XMM3_f32;
		case X64Reg_XMM4_f32:
		case X64Reg_XMM4_f64:
		case X64Reg_M128_XMM4: return X64Reg_XMM4_f32;
		case X64Reg_XMM5_f32:
		case X64Reg_XMM5_f64:
		case X64Reg_M128_XMM5: return X64Reg_XMM5_f32;
		case X64Reg_XMM6_f32:
		case X64Reg_XMM6_f64:
		case X64Reg_M128_XMM6: return X64Reg_XMM6_f32;
		case X64Reg_XMM7_f32:
		case X64Reg_XMM7_f64:
		case X64Reg_M128_XMM7: return X64Reg_XMM7_f32;
		case X64Reg_XMM8_f32:
		case X64Reg_XMM8_f64:
		case X64Reg_M128_XMM8: return X64Reg_XMM8_f32;
		case X64Reg_XMM9_f32:
		case X64Reg_XMM9_f64:
		case X64Reg_M128_XMM9: return X64Reg_XMM9_f32;
		case X64Reg_XMM10_f32:
		case X64Reg_XMM10_f64:
		case X64Reg_M128_XMM10: return X64Reg_XMM10_f32;
		case X64Reg_XMM11_f32:
		case X64Reg_XMM11_f64:
		case X64Reg_M128_XMM11: return X64Reg_XMM11_f32;
		case X64Reg_XMM12_f32:
		case X64Reg_XMM12_f64:
		case X64Reg_M128_XMM12: return X64Reg_XMM12_f32;
		case X64Reg_XMM13_f32:
		case X64Reg_XMM13_f64:
		case X64Reg_M128_XMM13: return X64Reg_XMM13_f32;
		case X64Reg_XMM14_f32:
		case X64Reg_XMM14_f64:
		case X64Reg_M128_XMM14: return X64Reg_XMM14_f32;
		case X64Reg_XMM15_f32:
		case X64Reg_XMM15_f64:
		case X64Reg_M128_XMM15: return X64Reg_XMM15_f32;
		}
	}
	if (numBytes == 2)
	{
		switch (reg)
		{
		case X64Reg_DIL:
		case X64Reg_DI:
		case X64Reg_EDI:
		case X64Reg_RDI: return X64Reg_DI;
		case X64Reg_SIL:
		case X64Reg_SI:
		case X64Reg_ESI:
		case X64Reg_RSI: return X64Reg_SI;
		case X64Reg_AL:
		case X64Reg_AH:
		case X64Reg_AX:
		case X64Reg_EAX:
		case X64Reg_RAX: return X64Reg_AX;
		case X64Reg_DL:
		case X64Reg_DH:
		case X64Reg_DX:
		case X64Reg_EDX:
		case X64Reg_RDX: return X64Reg_DX;
		case X64Reg_CL:
		case X64Reg_CH:
		case X64Reg_CX:
		case X64Reg_ECX:
		case X64Reg_RCX: return X64Reg_CX;
		case X64Reg_BL:
		case X64Reg_BH:
		case X64Reg_BX:
		case X64Reg_EBX:
		case X64Reg_RBX: return X64Reg_BX;
		case X64Reg_R8B:
		case X64Reg_R8W:
		case X64Reg_R8D:
		case X64Reg_R8: return X64Reg_R8W;
		case X64Reg_R9B:
		case X64Reg_R9W:
		case X64Reg_R9D:
		case X64Reg_R9: return X64Reg_R9W;
		case X64Reg_R10B:
		case X64Reg_R10W:
		case X64Reg_R10D:
		case X64Reg_R10: return X64Reg_R10W;
		case X64Reg_R11B:
		case X64Reg_R11W:
		case X64Reg_R11D:
		case X64Reg_R11: return X64Reg_R11W;
		case X64Reg_R12B:
		case X64Reg_R12W:
		case X64Reg_R12D:
		case X64Reg_R12: return X64Reg_R12W;
		case X64Reg_R13B:
		case X64Reg_R13W:
		case X64Reg_R13D:
		case X64Reg_R13: return X64Reg_R13W;
		case X64Reg_R14B:
		case X64Reg_R14W:
		case X64Reg_R14D:
		case X64Reg_R14: return X64Reg_R14W;
		case X64Reg_R15B:
		case X64Reg_R15W:
		case X64Reg_R15D:
		case X64Reg_R15: return X64Reg_R15W;
		}
	}
	if (numBytes == 1)
	{
		switch (reg)
		{
		case X64Reg_DIL:
		case X64Reg_DI:
		case X64Reg_EDI:
		case X64Reg_RDI: return X64Reg_DIL;
		case X64Reg_SIL:
		case X64Reg_SI:
		case X64Reg_ESI:
		case X64Reg_RSI: return X64Reg_SIL;
		case X64Reg_AH: return X64Reg_AH;
		case X64Reg_AL:
		case X64Reg_AX:
		case X64Reg_EAX:
		case X64Reg_RAX: return X64Reg_AL;
		case X64Reg_DH: return X64Reg_DH;
		case X64Reg_DL:
		case X64Reg_DX:
		case X64Reg_EDX:
		case X64Reg_RDX: return X64Reg_DL;
		case X64Reg_CH: return X64Reg_CH;
		case X64Reg_CL:
		case X64Reg_CX:
		case X64Reg_ECX:
		case X64Reg_RCX: return X64Reg_CL;
		case X64Reg_BH: return X64Reg_BH;
		case X64Reg_BL:
		case X64Reg_BX:
		case X64Reg_EBX:
		case X64Reg_RBX: return X64Reg_BL;
		case X64Reg_R8B:
		case X64Reg_R8W:
		case X64Reg_R8D:
		case X64Reg_R8: return X64Reg_R8B;
		case X64Reg_R9B:
		case X64Reg_R9W:
		case X64Reg_R9D:
		case X64Reg_R9: return X64Reg_R9B;
		case X64Reg_R10B:
		case X64Reg_R10W:
		case X64Reg_R10D:
		case X64Reg_R10: return X64Reg_R10B;
		case X64Reg_R11B:
		case X64Reg_R11W:
		case X64Reg_R11D:
		case X64Reg_R11: return X64Reg_R11B;
		case X64Reg_R12B:
		case X64Reg_R12W:
		case X64Reg_R12D:
		case X64Reg_R12: return X64Reg_R12B;
		case X64Reg_R13B:
		case X64Reg_R13W:
		case X64Reg_R13D:
		case X64Reg_R13: return X64Reg_R13B;
		case X64Reg_R14B:
		case X64Reg_R14W:
		case X64Reg_R14D:
		case X64Reg_R14: return X64Reg_R14B;
		case X64Reg_R15B:
		case X64Reg_R15W:
		case X64Reg_R15D:
		case X64Reg_R15: return X64Reg_R15B;
		}
	}
	return X64Reg_None;
}

X64CPURegister BeMCContext::ResizeRegister(X64CPURegister reg, BeType* type)
{
	if (type->IsVector())
		return ResizeRegister(reg, 16);
	return ResizeRegister(reg, type->mSize);
}

X64CPURegister BeMCContext::GetFullRegister(X64CPURegister reg)
{
	switch (reg)
	{
	case X64Reg_XMM0_f32:
	case X64Reg_XMM0_f64:
	case X64Reg_M128_XMM0: return X64Reg_M128_XMM0;
	case X64Reg_XMM1_f32:
	case X64Reg_XMM1_f64:
	case X64Reg_M128_XMM1: return X64Reg_M128_XMM1;
	case X64Reg_XMM2_f32:
	case X64Reg_XMM2_f64:
	case X64Reg_M128_XMM2: return X64Reg_M128_XMM2;
	case X64Reg_XMM3_f32:
	case X64Reg_XMM3_f64:
	case X64Reg_M128_XMM3: return X64Reg_M128_XMM3;
	case X64Reg_XMM4_f32:
	case X64Reg_XMM4_f64:
	case X64Reg_M128_XMM4: return X64Reg_M128_XMM4;
	case X64Reg_XMM5_f32:
	case X64Reg_XMM5_f64:
	case X64Reg_M128_XMM5: return X64Reg_M128_XMM5;
	case X64Reg_XMM6_f32:
	case X64Reg_XMM6_f64:
	case X64Reg_M128_XMM6: return X64Reg_M128_XMM6;
	case X64Reg_XMM7_f32:
	case X64Reg_XMM7_f64:
	case X64Reg_M128_XMM7: return X64Reg_M128_XMM7;
	case X64Reg_XMM8_f32:
	case X64Reg_XMM8_f64:
	case X64Reg_M128_XMM8: return X64Reg_M128_XMM8;
	case X64Reg_XMM9_f32:
	case X64Reg_XMM9_f64:
	case X64Reg_M128_XMM9: return X64Reg_M128_XMM9;
	case X64Reg_XMM10_f32:
	case X64Reg_XMM10_f64:
	case X64Reg_M128_XMM10: return X64Reg_M128_XMM10;
	case X64Reg_XMM11_f32:
	case X64Reg_XMM11_f64:
	case X64Reg_M128_XMM11: return X64Reg_M128_XMM11;
	case X64Reg_XMM12_f32:
	case X64Reg_XMM12_f64:
	case X64Reg_M128_XMM12: return X64Reg_M128_XMM12;
	case X64Reg_XMM13_f32:
	case X64Reg_XMM13_f64:
	case X64Reg_M128_XMM13: return X64Reg_M128_XMM13;
	case X64Reg_XMM14_f32:
	case X64Reg_XMM14_f64:
	case X64Reg_M128_XMM14: return X64Reg_M128_XMM14;
	case X64Reg_XMM15_f32:
	case X64Reg_XMM15_f64:
	case X64Reg_M128_XMM15: return X64Reg_M128_XMM15;
	}
	return ResizeRegister(reg, 8);
}

bool BeMCContext::IsAddress(BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_VRegAddr)
		return true;
	if (operand.mKind != BeMCOperandKind_VReg)
		return false;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	return IsAddress(vregInfo->mRelTo);
}

bool BeMCContext::IsAddressable(BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_Symbol)
		return true;
	if (operand.mKind == BeMCOperandKind_NativeReg)
		return true;
	if (operand.mKind == BeMCOperandKind_VRegAddr)
		return true;
	if (operand.mKind != BeMCOperandKind_VReg)
		return false;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	return IsAddressable(vregInfo->mRelTo);
}

bool BeMCContext::IsVRegExpr(BeMCOperand& operand)
{
	if (!operand.IsVRegAny())
		return false;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	return vregInfo->mIsExpr;
}

// Depth -1 is reserved explicitly for reg finalization, so we leave the vector type for emission
void BeMCContext::FixOperand(BeMCOperand& operand, int depth)
{
	// We don't want to check for VRegLoad, that would erase the dereference
	if ((operand.mKind != BeMCOperandKind_VReg) && (operand.mKind != BeMCOperandKind_VRegAddr))
		return;

	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (vregInfo->mReg != X64Reg_None)
	{
		// For vectors we need the explicit type info
		if ((depth != -1) || (!vregInfo->mType->IsVector()))
		{
			operand.mKind = BeMCOperandKind_NativeReg;
			operand.mReg = vregInfo->mReg;
			return;
		}
	}

	if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (mCompositeRetVRegIdx != operand.mVRegIdx))
	{
		BF_ASSERT(mCompositeRetVRegIdx != -1);
		BeMCOperand origOperand = operand;
		operand = BeMCOperand::FromVReg(mCompositeRetVRegIdx);
		if ((origOperand.mKind == BeMCOperandKind_VReg) && (vregInfo->mType->IsNonVectorComposite()))
			operand.mKind = BeMCOperandKind_VRegLoad;
		FixOperand(operand, depth + 1);

		//auto retVRegInfo = mVRegInfo[mCompositeRetVRegIdx];
		//operand = BeMCOperand::FromReg(retVRegInfo->mReg);
	}

	if (vregInfo->IsDirectRelToAny())
	{
		auto checkOperand = vregInfo->mRelTo;
		if (checkOperand.IsVReg())
			FixOperand(checkOperand, depth + 1);

		if (checkOperand.IsNativeReg())
		{
			auto resizedReg = ResizeRegister(checkOperand.mReg, vregInfo->mType);
			if (resizedReg != X64Reg_None)
				operand = BeMCOperand::FromReg(resizedReg);
		}
		else if (checkOperand.mKind == BeMCOperandKind_Symbol)
		{
			auto symbol = mCOFFObject->mSymbols[checkOperand.mSymbolIdx];
			if (AreTypesEquivalent(vregInfo->mType, symbol->mType))
			{
				if (checkOperand.mKind == BeMCOperandKind_VRegAddr)
				{
					operand = OperandToAddr(checkOperand);
				}
				else
					operand = checkOperand;
			}
		}
		else if (checkOperand.mKind == BeMCOperandKind_SymbolAddr)
		{
			operand = checkOperand;
		}
		else if (checkOperand.IsImmediate())
		{
			operand = checkOperand;
		}
	}
}

BeMCOperand BeMCContext::GetFixedOperand(const BeMCOperand& operand)
{
	BeMCOperand copyOp = operand;
	FixOperand(copyOp);
	return copyOp;
}

uint8 BeMCContext::GetREX(const BeMCOperand& r, const BeMCOperand& rm, bool is64Bit)
{
	//bool is64Bit = false;
	bool is64BitExR = false;
	bool is64BitExRM = false;
	bool forceRex = false;
	if (r.mKind == BeMCOperandKind_NativeReg)
	{
		/*if ((r.mReg >= X64Reg_RAX) && (r.mReg <= X64Reg_EFL))
			BF_ASSERT(is64Bit);		*/
		switch (r.mReg)
		{
		case X64Reg_SIL:
		case X64Reg_DIL:
			forceRex = true;
			break;
		case X64Reg_R8: case X64Reg_R9: case X64Reg_R10: case X64Reg_R11: case X64Reg_R12: case X64Reg_R13: case X64Reg_R14: case X64Reg_R15:
		case X64Reg_R8D: case X64Reg_R9D: case X64Reg_R10D: case X64Reg_R11D: case X64Reg_R12D: case X64Reg_R13D: case X64Reg_R14D: case X64Reg_R15D:
		case X64Reg_R8W: case X64Reg_R9W: case X64Reg_R10W: case X64Reg_R11W: case X64Reg_R12W: case X64Reg_R13W: case X64Reg_R14W: case X64Reg_R15W:
		case X64Reg_R8B: case X64Reg_R9B: case X64Reg_R10B: case X64Reg_R11B: case X64Reg_R12B: case X64Reg_R13B: case X64Reg_R14B: case X64Reg_R15B:
		case X64Reg_XMM8_f64: case X64Reg_XMM9_f64: case X64Reg_XMM10_f64: case X64Reg_XMM11_f64:
		case X64Reg_XMM12_f64: case X64Reg_XMM13_f64: case X64Reg_XMM14_f64: case X64Reg_XMM15_f64:
		case X64Reg_XMM8_f32: case X64Reg_XMM9_f32: case X64Reg_XMM10_f32: case X64Reg_XMM11_f32:
		case X64Reg_XMM12_f32: case X64Reg_XMM13_f32: case X64Reg_XMM14_f32: case X64Reg_XMM15_f32:
		case X64Reg_M128_XMM8: case X64Reg_M128_XMM9: case X64Reg_M128_XMM10: case X64Reg_M128_XMM11:
		case X64Reg_M128_XMM12: case X64Reg_M128_XMM13: case X64Reg_M128_XMM14: case X64Reg_M128_XMM15:
			is64BitExR = true;
		}
	}

	bool hasSibExRM = false;
	if (rm.mKind == BeMCOperandKind_NativeReg)
	{
		switch (rm.mReg)
		{
		case X64Reg_SIL:
		case X64Reg_DIL:
			forceRex = true;
			break;
		case X64Reg_R8: case X64Reg_R9: case X64Reg_R10: case X64Reg_R11: case X64Reg_R12: case X64Reg_R13: case X64Reg_R14: case X64Reg_R15:
		case X64Reg_R8D: case X64Reg_R9D: case X64Reg_R10D: case X64Reg_R11D: case X64Reg_R12D: case X64Reg_R13D: case X64Reg_R14D: case X64Reg_R15D:
		case X64Reg_R8W: case X64Reg_R9W: case X64Reg_R10W: case X64Reg_R11W: case X64Reg_R12W: case X64Reg_R13W: case X64Reg_R14W: case X64Reg_R15W:
		case X64Reg_R8B: case X64Reg_R9B: case X64Reg_R10B: case X64Reg_R11B: case X64Reg_R12B: case X64Reg_R13B: case X64Reg_R14B: case X64Reg_R15B:
		case X64Reg_XMM8_f64: case X64Reg_XMM9_f64: case X64Reg_XMM10_f64: case X64Reg_XMM11_f64:
		case X64Reg_XMM12_f64: case X64Reg_XMM13_f64: case X64Reg_XMM14_f64: case X64Reg_XMM15_f64:
		case X64Reg_XMM8_f32: case X64Reg_XMM9_f32: case X64Reg_XMM10_f32: case X64Reg_XMM11_f32:
		case X64Reg_XMM12_f32: case X64Reg_XMM13_f32: case X64Reg_XMM14_f32: case X64Reg_XMM15_f32:
		case X64Reg_M128_XMM8: case X64Reg_M128_XMM9: case X64Reg_M128_XMM10: case X64Reg_M128_XMM11:
		case X64Reg_M128_XMM12: case X64Reg_M128_XMM13: case X64Reg_M128_XMM14: case X64Reg_M128_XMM15:
			is64BitExRM = true;
		}
	}
	else if (rm.IsVRegAny())
	{
		auto vregInfo = mVRegInfo[rm.mVRegIdx];
		if (vregInfo->IsDirectRelTo())
			return GetREX(r, vregInfo->mRelTo, is64Bit);

		BeRMParamsInfo rmInfo;
		GetRMParams(rm, rmInfo);
		is64BitExRM |= ((rmInfo.mRegA >= X64Reg_R8) && (rmInfo.mRegA <= X64Reg_R15)) || ((rmInfo.mRegA >= X64Reg_R8D) && (rmInfo.mRegA <= X64Reg_R15D));
		hasSibExRM |= ((rmInfo.mRegB >= X64Reg_R8) && (rmInfo.mRegB <= X64Reg_R15)) || ((rmInfo.mRegB >= X64Reg_R8D) && (rmInfo.mRegB <= X64Reg_R15D));
	}
	else if (rm.IsSymbol())
	{
		auto sym = mCOFFObject->mSymbols[rm.mSymbolIdx];
		if (sym->mIsTLS)
		{
			auto tlsReg = mVRegInfo[mTLSVRegIdx]->mReg;
			is64BitExRM |= (tlsReg >= X64Reg_R8) && (tlsReg <= X64Reg_R15);
		}
	}

	uint8 flags = 0;
	if (is64Bit)
		flags |= 8;
	if (is64BitExR)
		flags |= 4;
	if (hasSibExRM)
		flags |= 2;
	if (is64BitExRM)
		flags |= 1;
	if ((flags != 0) || (forceRex))
		return (uint8)(0x40 | flags);
	return 0;
}

void BeMCContext::EmitREX(const BeMCOperand& r, const BeMCOperand& rm, bool is64Bit)
{
	uint8 rex = GetREX(r, rm, is64Bit);
	if (rex != 0)
		mOut.Write(rex);
}

uint8 BeMCContext::EncodeRegNum(X64CPURegister regNum)
{
	switch (regNum)
	{
	case X64Reg_AL:
	case X64Reg_AX:
	case X64Reg_EAX:
	case X64Reg_RAX:
	case X64Reg_R8:
	case X64Reg_R8D:
	case X64Reg_R8W:
	case X64Reg_R8B:
	case X64Reg_MM0:
	case X64Reg_XMM0_f32:
	case X64Reg_XMM0_f64:
	case X64Reg_M128_XMM0:
	case X64Reg_XMM8_f32:
	case X64Reg_XMM8_f64:
	case X64Reg_M128_XMM8:
		return 0;
	case X64Reg_CL:
	case X64Reg_CX:
	case X64Reg_ECX:
	case X64Reg_RCX:
	case X64Reg_R9:
	case X64Reg_R9D:
	case X64Reg_R9W:
	case X64Reg_R9B:
	case X64Reg_MM1:
	case X64Reg_XMM1_f32:
	case X64Reg_XMM1_f64:
	case X64Reg_M128_XMM1:
	case X64Reg_XMM9_f32:
	case X64Reg_XMM9_f64:
	case X64Reg_M128_XMM9:
		return 1;
	case X64Reg_DL:
	case X64Reg_DX:
	case X64Reg_EDX:
	case X64Reg_RDX:
	case X64Reg_R10:
	case X64Reg_R10D:
	case X64Reg_R10W:
	case X64Reg_R10B:
	case X64Reg_MM2:
	case X64Reg_XMM2_f32:
	case X64Reg_XMM2_f64:
	case X64Reg_M128_XMM2:
	case X64Reg_XMM10_f32:
	case X64Reg_XMM10_f64:
	case X64Reg_M128_XMM10:
		return 2;
	case X64Reg_BL:
	case X64Reg_BX:
	case X64Reg_EBX:
	case X64Reg_RBX:
	case X64Reg_R11:
	case X64Reg_R11D:
	case X64Reg_R11W:
	case X64Reg_R11B:
	case X64Reg_MM3:
	case X64Reg_XMM3_f32:
	case X64Reg_XMM3_f64:
	case X64Reg_M128_XMM3:
	case X64Reg_XMM11_f32:
	case X64Reg_XMM11_f64:
	case X64Reg_M128_XMM11:
		return 3;
	case X64Reg_None: // Useful for SIB/RM addr encodings
	case X64Reg_AH:
		//case X64Reg_SP:
		//case X64Reg_ESP:
	case X64Reg_RSP:
	case X64Reg_R12:
	case X64Reg_R12D:
	case X64Reg_R12W:
	case X64Reg_R12B:
	case X64Reg_MM4:
	case X64Reg_XMM4_f32:
	case X64Reg_XMM4_f64:
	case X64Reg_M128_XMM4:
	case X64Reg_XMM12_f32:
	case X64Reg_XMM12_f64:
	case X64Reg_M128_XMM12:
		return 4;
	case X64Reg_CH:
		//case X64Reg_BP:
		//case X64Reg_EBP:
	case X64Reg_RBP:
	case X64Reg_R13:
	case X64Reg_R13D:
	case X64Reg_R13W:
	case X64Reg_R13B:
	case X64Reg_MM5:
	case X64Reg_XMM5_f32:
	case X64Reg_XMM5_f64:
	case X64Reg_M128_XMM5:
	case X64Reg_XMM13_f32:
	case X64Reg_XMM13_f64:
	case X64Reg_M128_XMM13:
		return 5;
	case X64Reg_DH:
	case X64Reg_SIL:
	case X64Reg_SI:
	case X64Reg_ESI:
	case X64Reg_RSI:
	case X64Reg_R14:
	case X64Reg_R14D:
	case X64Reg_R14W:
	case X64Reg_R14B:
	case X64Reg_MM6:
	case X64Reg_XMM6_f32:
	case X64Reg_XMM6_f64:
	case X64Reg_M128_XMM6:
	case X64Reg_XMM14_f32:
	case X64Reg_XMM14_f64:
	case X64Reg_M128_XMM14:
		return 6;
	case X64Reg_BH:
	case X64Reg_DIL:
	case X64Reg_DI:
	case X64Reg_EDI:
	case X64Reg_RDI:
	case X64Reg_R15:
	case X64Reg_R15D:
	case X64Reg_R15W:
	case X64Reg_R15B:
	case X64Reg_MM7:
	case X64Reg_XMM7_f32:
	case X64Reg_XMM7_f64:
	case X64Reg_M128_XMM7:
	case X64Reg_XMM15_f32:
	case X64Reg_XMM15_f64:
	case X64Reg_M128_XMM15:
		return 7;
	}
	SoftFail("Invalid reg");
	return -1;
}

int BeMCContext::GetRegSize(int regNum)
{
	if ((regNum >= X64Reg_EAX) && (regNum <= X64Reg_EDI))
		return 4;
	if ((regNum >= X64Reg_AX) && (regNum <= X64Reg_BX))
		return 2;
	if ((regNum >= X64Reg_AL) && (regNum <= X64Reg_BH))
		return 1;
	return 8;
}

void BeMCContext::ValidateRMResult(const BeMCOperand& operand, BeRMParamsInfo& rmInfo, bool doValidate)
{
	if (!doValidate)
		return;
	if (rmInfo.mMode == BeMCRMMode_Invalid)
		return;

	//TODO: WTF- this previous version just seems to be wrong! Why did think this was true?  the REX.X and REX.B flags fix these
	// in a SIB, the base can't be R13 (which is RBP+REX), and the scaled index can't be R12 (which is RSP+REX)
	//if ((regB != X64Reg_None) &&
	//	((regA == X64Reg_R13) || (regB == X64Reg_R12)))
	//{
	//	// We can't just swap the regs if we have a scale applied
	//	if (bScale != 1)
	//	{
	//		if (errorVReg != NULL)
	//			*errorVReg = -2; // Scale error
	//		return BeMCRMMode_Invalid;
	//	}

	//	BF_SWAP(regA, regB);
	//}

	// In a SIB, the base can't be RBP, and the scaled index can't be RSP
	if ((rmInfo.mRegB != X64Reg_None) &&
		((rmInfo.mRegA == X64Reg_RBP) || (rmInfo.mRegB == X64Reg_RSP)))
	{
		// We can't just swap the regs if we have a scale applied
		if (rmInfo.mBScale != 1)
		{
			rmInfo.mErrorVReg = -2; // Scale error
			rmInfo.mMode = BeMCRMMode_Invalid;
			return;
		}

		BF_SWAP(rmInfo.mRegA, rmInfo.mRegB);
	}

	return;
}

void BeMCContext::GetRMParams(const BeMCOperand& operand, BeRMParamsInfo& rmInfo, bool doValidate)
{
	BeMCRMMode rmMode = BeMCRMMode_Invalid;
	if (operand.mKind == BeMCOperandKind_NativeReg)
	{
		if (rmInfo.mRegA == X64Reg_None)
			rmInfo.mRegA = operand.mReg;
		else if (rmInfo.mRegB == X64Reg_None)
			rmInfo.mRegB = operand.mReg;
		else
		{
			rmInfo.mMode = BeMCRMMode_Invalid;
			return;
		}
		rmInfo.mMode = BeMCRMMode_Direct;
		return ValidateRMResult(operand, rmInfo, doValidate);
	}
	else if (operand.IsImmediateInt())
	{
		rmInfo.mDisp += (int)operand.mImmediate;
		rmInfo.mMode = BeMCRMMode_Direct;
		return ValidateRMResult(operand, rmInfo, doValidate);
	}

	if (operand.mKind == BeMCOperandKind_VReg)
	{
		auto vregInfo = mVRegInfo[operand.mVRegIdx];
		if (!vregInfo->mIsExpr)
		{
			auto reg = vregInfo->mReg;
			if (reg != X64Reg_None)
			{
				if (rmInfo.mRegA == X64Reg_None)
					rmInfo.mRegA = reg;
				else if (rmInfo.mRegB == X64Reg_None)
					rmInfo.mRegB = reg;
				else
				{
					rmInfo.mMode = BeMCRMMode_Invalid;
					return;
				}
				rmInfo.mMode = BeMCRMMode_Direct;
				return ValidateRMResult(operand, rmInfo, doValidate);
			}

			GetRMParams(OperandToAddr(operand), rmInfo, doValidate);
			if (rmInfo.mMode == BeMCRMMode_Invalid)
				return;
			BF_ASSERT(rmInfo.mMode == BeMCRMMode_Direct);
			rmInfo.mMode = BeMCRMMode_Deref;
			return ValidateRMResult(OperandToAddr(operand), rmInfo, doValidate);
		}
		// Fall through
	}
	else if (operand.mKind == BeMCOperandKind_VRegAddr)
	{
		auto vregInfo = mVRegInfo[operand.mVRegIdx];

		if (vregInfo->mIsExpr)
		{
			if (vregInfo->IsDirectRelToAny())
			{
				if (vregInfo->mRelTo.mKind == BeMCOperandKind_VReg)
					return GetRMParams(BeMCOperand::FromVRegAddr(vregInfo->mRelTo.mVRegIdx), rmInfo, doValidate);
				else if (vregInfo->mRelTo.mKind == BeMCOperandKind_VRegLoad)
					return GetRMParams(BeMCOperand::FromVReg(vregInfo->mRelTo.mVRegIdx), rmInfo, doValidate);
			}

			rmInfo.mErrorVReg = operand.mVRegIdx;
			rmInfo.mMode = BeMCRMMode_Invalid;
			return;
		}

		BF_ASSERT(!vregInfo->mIsExpr);
		BF_ASSERT(vregInfo->mReg == X64Reg_None);
		X64CPURegister reg = X64Reg_None;

		if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (mCompositeRetVRegIdx != operand.mVRegIdx))
		{
			return GetRMParams(BeMCOperand::FromVReg(mCompositeRetVRegIdx), rmInfo, doValidate);
		}

		reg = mUseBP ? X64Reg_RBP : X64Reg_RSP;
		rmInfo.mDisp = mStackSize + vregInfo->mFrameOffset;

		if (rmInfo.mRegA == X64Reg_None)
			rmInfo.mRegA = reg;
		else if (rmInfo.mRegB == X64Reg_None)
			rmInfo.mRegB = reg;
		rmInfo.mMode = BeMCRMMode_Direct;
		return ValidateRMResult(operand, rmInfo, doValidate);
	}
	else if (operand.mKind == BeMCOperandKind_VRegLoad)
	{
		auto vregInfo = mVRegInfo[operand.mVRegIdx];
		if (!vregInfo->mIsExpr)
		{
			auto reg = vregInfo->mReg;
			if (reg == X64Reg_None)
			{
				rmInfo.mErrorVReg = operand.mVRegIdx;
				rmInfo.mMode = BeMCRMMode_Invalid;
				return;
			}
			if (rmInfo.mRegA == X64Reg_None)
				rmInfo.mRegA = reg;
			else if (rmInfo.mRegB == X64Reg_None)
				rmInfo.mRegB = reg;
			rmInfo.mMode = BeMCRMMode_Deref;
			return ValidateRMResult(operand, rmInfo, doValidate);
		}
	}
	else
	{
		rmInfo.mMode = BeMCRMMode_Invalid;
		return;
	}

	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	BF_ASSERT(vregInfo->mIsExpr);

	if (vregInfo->mRelTo)
	{
		auto oldRegA = rmInfo.mRegA;
		GetRMParams(vregInfo->mRelTo, rmInfo, false);
		if (rmInfo.mMode == BeMCRMMode_Invalid)
		{
			if (rmInfo.mErrorVReg == -1)
			{
				rmInfo.mErrorVReg = operand.mVRegIdx;
			}
			return;
		}

		if (rmInfo.mMode == BeMCRMMode_Deref)
		{
			// A deref can only stand alone, and no double-derefs
			if ((vregInfo->mRelOffset) || (vregInfo->mRelOffsetScale != 1) || (operand.mKind == BeMCOperandKind_VRegLoad))
			{
				BF_ASSERT(vregInfo->mRelTo.IsVRegAny());
				rmInfo.mErrorVReg = vregInfo->mRelTo.mVRegIdx;
				// For some reason we had changed this to:
				//*errorVReg = operand.mVRegIdx;
				//  This doesn't work, it's the deref that we want to isolate, otherwise we just end up creating another invalid expression
				rmInfo.mMode = BeMCRMMode_Invalid;
				return;
			}
			if (operand.mKind == BeMCOperandKind_VRegAddr)
			{
				rmInfo.mMode = BeMCRMMode_Direct;
				return ValidateRMResult(vregInfo->mRelTo, rmInfo, doValidate);
			}
			else if (operand.mKind == BeMCOperandKind_VReg)
			{
				rmInfo.mMode = BeMCRMMode_Deref;
				return ValidateRMResult(vregInfo->mRelTo, rmInfo, doValidate);
			}
			else
				NotImpl();
		}
	}

	if (vregInfo->mRelOffset)
	{
		if (vregInfo->mRelOffsetScale != 1)
			rmInfo.mVRegWithScaledOffset = operand.mVRegIdx;

		bool relToComplicated = (rmInfo.mRegB != X64Reg_None) || (rmInfo.mBScale != 1);
		GetRMParams(vregInfo->mRelOffset, rmInfo, false);
		if (rmInfo.mMode == BeMCRMMode_Invalid)
		{
			// Pick the "most complicated" between relOffset and relTo?
			if (relToComplicated)
			{
				BF_ASSERT(vregInfo->mRelTo.IsVRegAny());
				rmInfo.mErrorVReg = vregInfo->mRelTo.mVRegIdx;
			}
			else
			{
				BF_ASSERT(vregInfo->mRelOffset.IsVRegAny());
				rmInfo.mErrorVReg = vregInfo->mRelOffset.mVRegIdx;
			}
			rmInfo.mMode = BeMCRMMode_Invalid;
			return;
		}
		if (rmInfo.mMode == BeMCRMMode_Deref) // Deref only allowed on relTo
		{
			BF_ASSERT(vregInfo->mRelOffset.IsVRegAny());
			rmInfo.mErrorVReg = vregInfo->mRelOffset.mVRegIdx;
			rmInfo.mMode = BeMCRMMode_Invalid;
			return;
		}
	}
	bool success = true;
	if (vregInfo->mRelOffsetScale != 1)
	{
		if (rmInfo.mBScale != 1)
			success = false;
		rmInfo.mBScale = vregInfo->mRelOffsetScale;
		if ((rmInfo.mBScale != 2) && (rmInfo.mBScale != 4) && (rmInfo.mBScale != 8))
			success = false;
		if (rmInfo.mRegB == X64Reg_None)
		{
			rmInfo.mRegB = rmInfo.mRegA;
			rmInfo.mRegA = X64Reg_None;
		}
	}
	if (!success)
	{
		if (rmInfo.mErrorVReg == -1)
			rmInfo.mErrorVReg = operand.mVRegIdx;
	}
	if (success)
	{
		if (operand.mKind == BeMCOperandKind_VRegLoad)
		{
			rmInfo.mMode = BeMCRMMode_Deref;
			return ValidateRMResult(vregInfo->mRelOffset, rmInfo, doValidate);
		}
		else
		{
			rmInfo.mMode = BeMCRMMode_Direct;
			return ValidateRMResult(vregInfo->mRelOffset, rmInfo, doValidate);
		}
	}
	else
	{
		rmInfo.mMode = BeMCRMMode_Invalid;
		return;
	}
}

void BeMCContext::DisableRegister(const BeMCOperand& operand, X64CPURegister reg)
{
	auto vregInfo = GetVRegInfo(operand);
	if (vregInfo == NULL)
		return;
	switch (reg)
	{
	case X64Reg_RAX:
		vregInfo->mDisableRAX = true;
		break;
	case X64Reg_RDX:
		vregInfo->mDisableRDX = true;
		break;
	case X64Reg_SIL:
		vregInfo->mDisableEx = true;
		break;
	default:
		NotImpl();
	}

	if (vregInfo->mRelTo)
		DisableRegister(vregInfo->mRelTo, reg);
	if (vregInfo->mRelOffset)
		DisableRegister(vregInfo->mRelOffset, reg);
}

void BeMCContext::MarkInvalidRMRegs(const BeMCOperand& operand)
{
	if (!operand.IsVRegAny())
		return;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (vregInfo->mReg == X64Reg_R12) // Can't allow a scaled R12, it's illegal in ModRM
		vregInfo->mDisableR12 = true;
	if (vregInfo->mReg == X64Reg_R13) // Can't allow a base R13, it's illegal in ModRM
		vregInfo->mDisableR13 = true;

	MarkInvalidRMRegs(vregInfo->mRelTo);
	MarkInvalidRMRegs(vregInfo->mRelOffset);
}

void BeMCContext::GetUsedRegs(const BeMCOperand& operand, X64CPURegister& regA, X64CPURegister& regB)
{
	BeRMParamsInfo rmInfo;
	GetRMParams(operand, rmInfo);
	if (rmInfo.mRegA != X64Reg_None)
		regA = GetFullRegister(rmInfo.mRegA);
	if (rmInfo.mRegB != X64Reg_None)
		regB = GetFullRegister(rmInfo.mRegB);
}

BeMCRMMode BeMCContext::GetRMForm(const BeMCOperand& operand, bool& isMulti)
{
	BeRMParamsInfo rmInfo;
	GetRMParams(operand, rmInfo);
	isMulti = (rmInfo.mRegB != X64Reg_None) || (rmInfo.mDisp != 0);
	return rmInfo.mMode;
}

void BeMCContext::GetValAddr(const BeMCOperand& operand, X64CPURegister& reg, int& offset)
{
	if (operand.IsNativeReg())
	{
		reg = operand.mReg;
		return;
	}

	BF_ASSERT(operand.IsVRegAny());
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (operand.mKind == BeMCOperandKind_VReg)
	{
		if (vregInfo->mRelTo)
		{
			GetValAddr(vregInfo->mRelTo, reg, offset);
			if (vregInfo->mRelOffset)
			{
				BF_ASSERT(vregInfo->mRelOffset.IsImmediateInt());
				offset += vregInfo->mRelOffset.mImmediate;
			}
			return;
		}

		BF_ASSERT(vregInfo->mType->IsPointer());
		BF_ASSERT(vregInfo->mReg != X64Reg_None);
		reg = vregInfo->mReg;
		return;
	}

	if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (mCompositeRetVRegIdx != operand.mVRegIdx))
	{
		BF_ASSERT(mCompositeRetVRegIdx != -1);
		GetValAddr(BeMCOperand::FromVRegAddr(mCompositeRetVRegIdx), reg, offset);
		return;
	}

	while (vregInfo->IsDirectRelTo())
	{
		vregInfo = GetVRegInfo(vregInfo->mRelTo);
	}

	if ((mCompositeRetVRegIdx == operand.mVRegIdx) && (vregInfo->mReg != X64Reg_None))
	{
		reg = vregInfo->mReg;
		return;
	}

	reg = mUseBP ? X64Reg_RBP : X64Reg_RSP;
	offset = mStackSize + vregInfo->mFrameOffset;
}

int BeMCContext::GetHighestVRegRef(const BeMCOperand& operand)
{
	if (!operand.IsVRegAny())
		return -1;
	int highestIdx = operand.mVRegIdx;
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	if (vregInfo->mRelTo)
		highestIdx = std::max(highestIdx, GetHighestVRegRef(vregInfo->mRelTo));
	if (vregInfo->mRelOffset)
		highestIdx = std::max(highestIdx, GetHighestVRegRef(vregInfo->mRelOffset));
	return highestIdx;
}

uint8 BeMCContext::GetJumpOpCode(BeCmpKind cmpKind, bool isLong)
{
	if (isLong)
	{
		switch (cmpKind)
		{
		case BeCmpKind_None: // JMP
			return 0xE9;
		case BeCmpKind_SLT: // JL
			return 0x8C;
		case BeCmpKind_ULT: // JB
			return 0x82;
		case BeCmpKind_SLE: // JLE
			return 0x8E;
		case BeCmpKind_ULE: // JBE
			return 0x86;
		case BeCmpKind_EQ: // JE
			return 0x84;
		case BeCmpKind_NE: // JNE
			return 0x85;
		case BeCmpKind_SGT: // JG
			return 0x8F;
		case BeCmpKind_UGT: // JA
			return 0x87;
		case BeCmpKind_SGE: // JGE
			return 0x8D;
		case BeCmpKind_UGE: // JAE
			return 0x83;
		case BeCmpKind_NB: // JNB
			return 0x83;
		case BeCmpKind_NO: // JNO
			return 0x81;
		}
	}
	else
	{
		switch (cmpKind)
		{
		case BeCmpKind_None: // JMP
			return 0xEB;
		case BeCmpKind_SLT: // JL
			return 0x7C;
		case BeCmpKind_ULT: // JB
			return 0x72;
		case BeCmpKind_SLE: // JLE
			return 0x7E;
		case BeCmpKind_ULE: // JBE
			return 0x76;
		case BeCmpKind_EQ: // JE
			return 0x74;
		case BeCmpKind_NE: // JNE
			return 0x75;
		case BeCmpKind_SGT: // JG
			return 0x7F;
		case BeCmpKind_UGT: // JA
			return 0x77;
		case BeCmpKind_SGE: // JGE
			return 0x7D;
		case BeCmpKind_UGE: // JAE
			return 0x73;
		case BeCmpKind_NB: // JNB
			return 0x73;
		case BeCmpKind_NO: // JNO
			return 0x71;
		}
	}

	return 0;
}

void BeMCContext::Emit(uint8 val)
{
	mOut.Write(val);
}

void BeMCContext::EmitModRM(int mod, int reg, int rm)
{
	mOut.Write((uint8)((mod << 6) | (reg << 3) | rm));
}

void BeMCContext::EmitModRMRel(int rx, X64CPURegister regA, X64CPURegister regB, int bScale, int relOffset)
{
	if (regB != X64Reg_None)
	{
		// We can't encode regA as RBP in the SIB base
		BF_ASSERT(regA != X64Reg_RBP);

		// We can't encode RegB as RSP in the SIB index
		BF_ASSERT(regB != X64Reg_RSP);
	}

	uint8 modRM = (rx << 3);

	if ((regB == X64Reg_None) && (regA != X64Reg_RSP) && (regA != X64Reg_R12)) // RSP/R12 can't be encoded without a SIB
	{
		if ((relOffset == 0) && (regA != X64Reg_RBP) && (regA != X64Reg_R13)) // RBP/R13 can't be encoded with Mod0
		{
			modRM |= (0x0 << 6) | (EncodeRegNum(regA)); // [regA]
			mOut.Write(modRM);
			return;
		}
		else if ((relOffset >= -0x80) && (relOffset <= 0x7F))
		{
			modRM |= (0x1 << 6) | (EncodeRegNum(regA)); // [regA]+disp8
			mOut.Write(modRM);
			mOut.Write((uint8)relOffset);
			return;
		}
		else
		{
			modRM |= (0x2 << 6) | (EncodeRegNum(regA)); // [regA]+disp32
			mOut.Write(modRM);
			mOut.Write((int32)relOffset);
			return;
		}
	}
	else if (regA == X64Reg_None) // The only option is disp32
	{
		modRM |= (0x0 << 6) | (0x4); // [--][--]+disp32
		mOut.Write(modRM);
		uint8 sib = ((bScale == 2) ? 1 : (bScale == 4) ? 2 : (bScale == 8) ? 3 : 0) << 6;
		sib |= (EncodeRegNum(regB) << 3) | 5;
		mOut.Write(sib);
		mOut.Write((int32)relOffset);
		return;
	}
	else
	{
		// Do no-offset version UNLESS we have a base of R13, which has its representation stolen by '[--][--]+disp32',
		//  so we must use the longer +disp8 version in that case
		if ((relOffset == 0) && (regA != X64Reg_R13))
		{
			modRM |= (0x0 << 6) | (0x4); // [--][--]
			mOut.Write(modRM);
			uint8 sib = ((bScale == 2) ? 1 : (bScale == 4) ? 2 : (bScale == 8) ? 3 : 0) << 6;
			sib |= (EncodeRegNum(regB) << 3) | EncodeRegNum(regA);
			mOut.Write(sib);
			return;
		}
		else if ((relOffset >= -0x80) && (relOffset <= 0x7F))
		{
			modRM |= (0x1 << 6) | (0x4); // [--][--]+disp8
			mOut.Write(modRM);
			uint8 sib = ((bScale == 2) ? 1 : (bScale == 4) ? 2 : (bScale == 8) ? 3 : 0) << 6;
			sib |= (EncodeRegNum(regB) << 3) | EncodeRegNum(regA);
			mOut.Write(sib);
			mOut.Write((uint8)relOffset);
			return;
		}
		else
		{
			modRM |= (0x2 << 6) | (0x4); // [--][--]+disp32
			mOut.Write(modRM);
			uint8 sib = ((bScale == 2) ? 1 : (bScale == 4) ? 2 : (bScale == 8) ? 3 : 0) << 6;
			sib |= (EncodeRegNum(regB) << 3) | EncodeRegNum(regA);
			mOut.Write(sib);
			mOut.Write((int32)relOffset);
			return;
		}
	}
}

void BeMCContext::EmitModRMRelStack(int rx, int regOffset, int scale)
{
	EmitModRMRel(rx, mUseBP ? X64Reg_RBP : X64Reg_RSP, X64Reg_None, scale, regOffset);
}

void BeMCContext::EmitModRM(int rx, BeMCOperand rm, int relocOfs)
{
	uint8 modRM = (rx << 3);
	//int relocIdx = -1;

	if (rm.IsImmediateFloat())
	{
		EmitModRM_XMM_IMM(rx, rm);
		return;
	}
	else if ((rm.mKind == BeMCOperandKind_Symbol) || (rm.mKind == BeMCOperandKind_SymbolAddr))
	{
		BeMCRelocation reloc;

		auto sym = mCOFFObject->mSymbols[rm.mSymbolIdx];
		if (sym->mIsTLS)
		{
			auto vregInfo = mVRegInfo[mTLSVRegIdx];
			modRM |= (2 << 6) | EncodeRegNum(vregInfo->mReg);
			reloc.mKind = BeMCRelocationKind_SECREL;
			relocOfs = 0;
		}
		else
		{
			modRM |= 0x5; // RIP + <X>
			reloc.mKind = BeMCRelocationKind_REL32;
		}

		Emit(modRM);

		reloc.mOffset = mOut.GetPos();
		reloc.mSymTableIdx = rm.mSymbolIdx;
		mCOFFObject->mTextSect.mRelocs.push_back(reloc);
		mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);
		//relocIdx = (int)mOut.GetSize();
		mOut.Write((int32)relocOfs);
		return;
	}
	else if (rm.mKind == BeMCOperandKind_NativeReg)
	{
		modRM |= (0x3 << 6) | (EncodeRegNum(rm.mReg));
	}
	else if (rm.IsVRegAny())
	{
		auto vregInfo = mVRegInfo[rm.mVRegIdx];
		if ((vregInfo->mRelTo.mKind == BeMCOperandKind_SymbolAddr) &&
			(vregInfo->mRelOffset.IsImmediateInt()) &&
			(vregInfo->mRelOffsetScale == 1))
		{
			return EmitModRM(rx, vregInfo->mRelTo, relocOfs + vregInfo->mRelOffset.mImmediate);
		}

		if ((rm.IsVReg()) && (vregInfo->IsDirectRelToAny()))
			return EmitModRM(rx, vregInfo->mRelTo, relocOfs);

		BeRMParamsInfo rmInfo;
		GetRMParams(rm, rmInfo);
		//BF_ASSERT(resultType != BeMCRMMode_Invalid);
		BF_ASSERT(rmInfo.mMode == BeMCRMMode_Deref);
		EmitModRMRel(rx, rmInfo.mRegA, rmInfo.mRegB, rmInfo.mBScale, rmInfo.mDisp);
		return;
	}
	else
	{
		SoftFail("Invalid rm");
	}
	mOut.Write(modRM);
}

void BeMCContext::EmitModRM(BeMCOperand r, BeMCOperand rm, int relocOfs)
{
	uint8 modRM = 0;
	BF_ASSERT(r.mKind == BeMCOperandKind_NativeReg);
	EmitModRM(EncodeRegNum(r.mReg), rm, relocOfs);
}

void BeMCContext::EmitModRM_Addr(BeMCOperand r, BeMCOperand rm)
{
	uint8 modRM = 0;
	BF_ASSERT(r.mKind == BeMCOperandKind_NativeReg);
	modRM = EncodeRegNum(r.mReg) << 3;

	if (rm.mKind == BeMCOperandKind_NativeReg)
	{
		modRM |= (0x0 << 6) | (EncodeRegNum(rm.mReg));
	}
	else if ((rm.mKind == BeMCOperandKind_Symbol) || (rm.mKind == BeMCOperandKind_SymbolAddr))
	{
		EmitModRM(r, rm);
		return;
	}
	else if (rm.mKind == BeMCOperandKind_VReg)
	{
		auto vregInfo = mVRegInfo[rm.mVRegIdx];
		if (vregInfo->mIsExpr)
		{
			if (vregInfo->mRelOffsetScale == 1)
			{
				X64CPURegister relToReg = X64Reg_None;
				int regOffset = 0;
				if (vregInfo->mRelOffset)
				{
					BF_ASSERT(vregInfo->mRelOffset.IsImmediate());
					regOffset = (int)vregInfo->mRelOffset.mImmediate;
				}

				if (vregInfo->mRelTo.IsNativeReg())
				{
					relToReg = vregInfo->mRelTo.mReg;
				}
				else if (vregInfo->mRelTo.IsVRegAny())
				{
					auto relVRegInfo = GetVRegInfo(vregInfo->mRelTo);
					if (relVRegInfo->mRelTo)
					{
						BeRMParamsInfo rmInfo;
						GetRMParams(rm, rmInfo);
						BF_ASSERT(rmInfo.mMode != BeMCRMMode_Invalid);
						EmitModRMRel(EncodeRegNum(r.mReg), rmInfo.mRegA, rmInfo.mRegB, rmInfo.mBScale, rmInfo.mDisp);
						return;
					}
					else
						NotImpl();
				}
				else
					NotImpl();
				EmitModRMRel(EncodeRegNum(r.mReg), relToReg, X64Reg_None, 1, regOffset);
				return;
			}
			else
			{
				if (vregInfo->mRelOffset)
				{
					BF_ASSERT(vregInfo->mRelOffset.IsImmediate());
					int regOffset = vregInfo->mRelOffset.mImmediate;
					int scaleVal = 0;
					if (vregInfo->mRelOffsetScale == 2)
						scaleVal = 0x1;
					else if (vregInfo->mRelOffsetScale == 4)
						scaleVal = 0x2;
					else if (vregInfo->mRelOffsetScale == 8)
						scaleVal = 0x3;

					modRM |= (0x0 << 6) | (0x4); // [--][--]
					mOut.Write(modRM);
					uint8 sib = (scaleVal << 6) | (EncodeRegNum(vregInfo->mRelTo.mReg) << 3) | (0x5); // [<reg>*<scale> + imm32]
					mOut.Write(sib);
					mOut.Write((int32)regOffset);
					return;
				}
			}
		}
		else
		{
			SoftFail("Illegal expression in EmitModRM_Addr");
		}
	}
	else
	{
		SoftFail("Invalid rm in EmitModRM_Addr");
	}
	mOut.Write(modRM);
}

void BeMCContext::EmitModRM_XMM_IMM(int rx, BeMCOperand& imm)
{
	Emit((rx << 3) | (0x5)); // RIP + <X>

	BeMCSymbol* sym = NULL;
	if (imm.mKind == BeMCOperandKind_Immediate_f32)
	{
		String name = StrFormat("__real@%08x", *(int*)&imm.mImmF32);
		sym = mCOFFObject->GetCOMDAT(name, &imm.mImmF32, 4, 4);
	}
	else if (imm.mKind == BeMCOperandKind_Immediate_f64)
	{
		String name = StrFormat("__real@%016llx", *(int64*)&imm.mImmF64);
		sym = mCOFFObject->GetCOMDAT(name, &imm.mImmF64, 8, 8);
	}
	else if (imm.mKind == BeMCOperandKind_Immediate_f32_Packed128)
	{
		String name = StrFormat("__real@%08x_packed", *(int*)&imm.mImmF32);
		float data[4] = { imm.mImmF32, 0, 0, 0 };
		sym = mCOFFObject->GetCOMDAT(name, data, 16, 16);
	}
	else if (imm.mKind == BeMCOperandKind_Immediate_f64_Packed128)
	{
		String name = StrFormat("__real@%016llx_packed", *(int64*)&imm.mImmF64);
		double data[2] = { imm.mImmF64, 0 };
		sym = mCOFFObject->GetCOMDAT(name, data, 16, 16);
	}
	else if (imm.mKind == BeMCOperandKind_Immediate_int32x4)
	{
		String name = StrFormat("__real@%016llx_x4_packed", *(int*)&imm.mImmediate);
		int32 data[4] = { (int32)imm.mImmediate, (int32)imm.mImmediate, (int32)imm.mImmediate, (int32)imm.mImmediate };
		sym = mCOFFObject->GetCOMDAT(name, data, 16, 16);
	}
	else
		SoftFail("Unhandled value type in EmitModRM_XMM_IMM");

	BeMCRelocation reloc;
	reloc.mKind = BeMCRelocationKind_REL32;
	reloc.mOffset = mOut.GetPos();
	reloc.mSymTableIdx = sym->mIdx;
	mCOFFObject->mTextSect.mRelocs.push_back(reloc);
	mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);

	mOut.Write((int32)0);
}

void BeMCContext::VRegSetInitialized(BeMCBlock* mcBlock, BeMCInst* inst, const BeMCOperand& operand, SizedArrayImpl<int>& addVec, SizedArrayImpl<int>& removeVec, bool deepSet, bool doSet)
{
	if (operand.IsSymbol())
	{
		auto sym = mCOFFObject->mSymbols[operand.mSymbolIdx];
		if (sym->mIsTLS)
		{
			VRegSetInitialized(mcBlock, inst, BeMCOperand::FromVReg(mTLSVRegIdx), addVec, removeVec, deepSet, doSet);
			return;
		}
	}

	if (operand.mKind == BeMCOperandKind_CmpResult)
	{
		auto& cmpResult = mCmpResults[operand.mCmpResultIdx];
		if (cmpResult.mResultVRegIdx != -1)
			VRegSetInitialized(mcBlock, inst, BeMCOperand::FromVReg(cmpResult.mResultVRegIdx), addVec, removeVec, deepSet, doSet);
		return;
	}

	if (operand.mKind == BeMCOperandKind_VRegPair)
	{
		VRegSetInitialized(mcBlock, inst, BeMCOperand::FromVReg(operand.mVRegPair.mVRegIdx0), addVec, removeVec, deepSet, doSet);
		VRegSetInitialized(mcBlock, inst, BeMCOperand::FromVReg(operand.mVRegPair.mVRegIdx1), addVec, removeVec, deepSet, doSet);
	}

	if (!operand.IsVRegAny())
		return;

	auto vregInfo = mVRegInfo[operand.mVRegIdx];

	if (vregInfo->mDefOnFirstUse)
	{
		int insertIdx = FindSafeInstInsertPos(*mInsertInstIdxRef);
		AllocInst(BeMCInstKind_Def, operand, insertIdx);
		vregInfo->mDefOnFirstUse = false;
		if (insertIdx <= *mInsertInstIdxRef)
			(*mInsertInstIdxRef)++;
	}

	if (vregInfo->mRelTo)
		VRegSetInitialized(mcBlock, inst, vregInfo->mRelTo, addVec, removeVec, deepSet, doSet && deepSet);
	if (vregInfo->mRelOffset)
		VRegSetInitialized(mcBlock, inst, vregInfo->mRelOffset, addVec, removeVec, deepSet, doSet && deepSet);

	if (doSet)
	{
		if (!removeVec.Contains(operand.mVRegIdx))
			addVec.push_back(operand.mVRegIdx);
		removeVec.push_back(mVRegInitializedContext.GetIdx(operand.mVRegIdx, BeTrackKind_Uninitialized));
	}
}

BeMCInst* BeMCContext::FindSafePreBranchInst(BeMCBlock* mcBlock)
{
	for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
	{
		auto inst = mcBlock->mInstructions[instIdx];
		// We set the definition to just right before the first branch
		if ((inst->mKind == BeMCInstKind_Br) || (inst->mKind == BeMCInstKind_CondBr))
		{
			// Don't separate out Def/Cmp/CmpToBool/Test calls
			while (instIdx > 0)
			{
				auto checkInst = mcBlock->mInstructions[instIdx - 1];
				if ((checkInst->mKind != BeMCInstKind_Def) &&
					(checkInst->mKind != BeMCInstKind_DefLoad) &&
					(checkInst->mKind != BeMCInstKind_Test) &&
					(checkInst->mKind != BeMCInstKind_Cmp) &&
					(checkInst->mKind != BeMCInstKind_CmpToBool))
					return inst;
				inst = checkInst;
				instIdx--;
			}

			break;
		}
	}

	return mcBlock->mInstructions[0];
}

void BeMCContext::InitializedPassHelper(BeMCBlock* mcBlock, BeVTrackingGenContext* genCtx, bool& modifiedBlockBefore, bool& modifiedBlockAfter)
{
	modifiedBlockBefore = false;
	modifiedBlockAfter = false;

	genCtx->mBlocks[mcBlock->mBlockIdx].mGenerateCount++;
	genCtx->mCalls++;

	genCtx->mHandledCalls++;

	BeMDNode* curDbgScope = NULL;

	BeVTrackingList* vregsInitialized = mcBlock->mPredVRegsInitialized;

	//OutputDebugStrF("InitializedPassHelper %@\n", vregsInitialized.mList);

	mActiveBlock = mcBlock;
	for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
	{
		genCtx->mInstructions++;

		mInsertInstIdxRef = &instIdx;
		auto inst = mcBlock->mInstructions[instIdx];

		inst->mVRegsInitialized = vregsInitialized;

		bool deepSet = false;

		if (inst->mKind == BeMCInstKind_LifetimeStart)
			continue;

		SizedArray<int, 16> removeVec;
		SizedArray<int, 16> addVec;
		SizedArray<int, 16> filteredRemoveVec;
		SizedArray<int, 16> filteredAddVec;

		//
		{
			auto checkLastUseRecord = inst->mVRegLastUseRecord;
			while (checkLastUseRecord != NULL)
			{
				removeVec.Add(checkLastUseRecord->mVRegIdx);
				checkLastUseRecord = checkLastUseRecord->mNext;
			}
		}

		if ((inst->mKind == BeMCInstKind_ValueScopeSoftEnd) || (inst->mKind == BeMCInstKind_ValueScopeHardEnd))
		{
			bool isSoft = inst->mKind == BeMCInstKind_ValueScopeSoftEnd;

			int startVRegIdx = (int)inst->mArg0.mImmediate;
			int endVRegIdx = (int)inst->mArg1.mImmediate;

			int listIdx = mVRegInitializedContext.FindIndex(vregsInitialized, startVRegIdx);
			if (listIdx < 0)
				listIdx = ~listIdx;
			for (; listIdx < vregsInitialized->mSize; listIdx++)
			{
				int vregIdx = vregsInitialized->mEntries[listIdx];
				if (vregIdx >= endVRegIdx)
					break;

				auto vregInfo = mVRegInfo[vregIdx];

				if (isSoft)
				{
					if (vregInfo->mValueScopeRetainedKind >= BeMCValueScopeRetainKind_Soft)
						continue;
				}
				else
				{
					if (vregInfo->mValueScopeRetainedKind >= BeMCValueScopeRetainKind_Hard)
						continue;
				}

				filteredRemoveVec.push_back(vregIdx);
			}
		}

		if ((inst->mResult) && (inst->mResult.mKind == BeMCOperandKind_CmpResult))
		{
			auto& cmpResult = mCmpResults[inst->mResult.mCmpResultIdx];
			if (cmpResult.mResultVRegIdx != -1)
			{
				auto cmpToBoolInst = AllocInst(BeMCInstKind_CmpToBool, BeMCOperand::FromCmpKind(cmpResult.mCmpKind), BeMCOperand(), instIdx + 1);
				cmpToBoolInst->mResult = BeMCOperand::FromVReg(cmpResult.mResultVRegIdx);
			}

			inst->mResult = BeMCOperand();
		}

		if (inst->mKind == BeMCInstKind_DbgDecl)
		{
			if (!mVRegInitializedContext.IsSet(vregsInitialized, inst->mArg0.mVRegIdx))
			{
				auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
				if (vregInfo->mIsExpr) // For shadowed inlined params, set as initialized
					addVec.push_back(inst->mArg0.mVRegIdx);
				else
				{
					if ((vregInfo->mDbgVariable != NULL) && (vregInfo->mDbgVariable->mInitType == BfIRInitType_NotNeeded_AliveOnDecl))
						addVec.push_back(inst->mArg0.mVRegIdx);
					else
						addVec.push_back(mVRegInitializedContext.GetIdx(inst->mArg0.mVRegIdx, BeTrackKind_Uninitialized));
				}
			}

			vregsInitialized = mVRegInitializedContext.Modify(vregsInitialized, addVec, removeVec, filteredAddVec, filteredRemoveVec);
			continue;
		}

		if (inst->mKind == BeMCInstKind_Def)
		{
			//deepSet = true;

			// If we take a pointer to a local variable before initializing it, we need to consider it as being "ambiguously initialized"
			auto vregInfo = GetVRegInfo(inst->mArg0);
			if (vregInfo->mRelTo.mKind == BeMCOperandKind_VRegAddr)
			{
				auto relVRegInfo = GetVRegInfo(vregInfo->mRelTo);
				if ((relVRegInfo->mDbgVariable != NULL) && (!mVRegInitializedContext.IsSet(vregsInitialized, vregInfo->mRelTo.mVRegIdx)))
					relVRegInfo->mHasAmbiguousInitialization = true;
			}
		}

		if (inst->mKind == BeMCInstKind_LifetimeEnd)
		{
			// In some cases we can have a dbg variable that actually points to a global variable (due to macros/inlining/etc), so this check is for that case:
			if (inst->mArg0.IsVRegAny())
			{
				DedupPushBack(removeVec, inst->mArg0.mVRegIdx);
				DedupPushBack(removeVec, mVRegInitializedContext.GetIdx(inst->mArg0.mVRegIdx, BeTrackKind_Uninitialized));
			}
			vregsInitialized = mVRegInitializedContext.Modify(vregsInitialized, addVec, removeVec, filteredAddVec, filteredRemoveVec);
			continue;
		}

		if (inst->mKind == BeMCInstKind_DefLoad)
		{
			// This is also an address-taking operation, OR it's just a usage of an explicitly-undefined value
			auto loadRegInfo = GetVRegInfo(inst->mArg0);
			if (loadRegInfo->mRelTo.IsVRegAny())
			{
				auto relVRegInfo = GetVRegInfo(loadRegInfo->mRelTo);
				if ((relVRegInfo->mDbgVariable != NULL) && (!mVRegInitializedContext.IsSet(vregsInitialized, loadRegInfo->mRelTo.mVRegIdx)))
					relVRegInfo->mHasAmbiguousInitialization = true;
			}
		}

		if (inst->IsMov())
		{
			// This is also an address-taking operation, OR it's just a usage of an explicitly-undefined value
			if (inst->mArg1.IsVRegAny())
			{
				auto srcRegInfo = GetVRegInfo(inst->mArg1);
				if ((srcRegInfo->mDbgVariable != NULL) && (!mVRegInitializedContext.IsSet(vregsInitialized, inst->mArg1.mVRegIdx)))
					srcRegInfo->mHasAmbiguousInitialization = true;
			}
		}

		int cpyDestVRegIdx = -1;
		if ((inst->mKind == BeMCInstKind_MemCpy) || (inst->mKind == BeMCInstKind_MemSet))
		{
			cpyDestVRegIdx = inst->mArg1.mVRegPair.mVRegIdx0;
		}

		//deepSet = true;
		auto destArg = inst->GetDest();
		auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
		for (auto op : operands)
		{
			VRegSetInitialized(mcBlock, inst, *op, addVec, removeVec, deepSet || (op == destArg), true);
		}

		for (int removeIdx = 0; removeIdx < (int)filteredRemoveVec.size(); removeIdx++)
		{
			int vregIdx = filteredRemoveVec[removeIdx];
			if (vregIdx >= mVRegInitializedContext.mNumItems)
				continue;

			auto vregInfo = mVRegInfo[vregIdx];
			if (vregInfo->mChainLifetimeEnd)
			{
				auto mcCheck = vregInfo->mRelTo;
				while (mcCheck.IsVRegAny())
				{
					auto checkInfo = mVRegInfo[mcCheck.mVRegIdx];
					removeVec.push_back(mcCheck.mVRegIdx);
					mcCheck = checkInfo->mRelTo;
				}
			}
		}

		vregsInitialized = mVRegInitializedContext.Modify(vregsInitialized, addVec, removeVec, filteredAddVec, filteredRemoveVec);
	}

	mInsertInstIdxRef = NULL;
	mActiveBlock = NULL;

	for (int succIdx = 0; succIdx < (int)mcBlock->mSuccs.size(); succIdx++)
	{
		BeMCBlock* succ = mcBlock->mSuccs[succIdx];
		auto& entry = genCtx->mBlocks[succ->mBlockIdx];

		bool forceRun = (entry.mGenerateCount == 0) && (!entry.mGenerateQueued);
		//if ((!mVRegInitializedContext.HasExtraBitsSet(succ->mPredVRegsInitialized, vregsInitialized)) && (!forceRun))
			//continue;

		auto newPredVRegsInitialized = mVRegInitializedContext.Merge(succ->mPredVRegsInitialized, vregsInitialized);
		if ((newPredVRegsInitialized == succ->mPredVRegsInitialized) && (!forceRun))
			continue;

		int vregIdx = -1;

		/*SizedArray<int, 16> newNodes;

		auto& prevDestEntry = succ->mPredVRegsInitialized;
		auto& mergeFrom = vregsInitialized.mBits;

		// Take nodes that were exclusive to the new set and add edges to nodes that were exclusive the old set
		while (true)
		{
			vregIdx = mVRegInitializedContext.GetNextDiffSetIdx(prevDestEntry.mBits, mergeFrom, vregIdx);
			if (vregIdx == -1)
				break;
			newNodes.push_back(vregIdx);
		}
		BF_ASSERT((!newNodes.empty()) || (forceRun));*/

		/*if ((mDebugging) || (mBeFunction->mName == "?DrawEntry@DrawContext@PerfView@BeefPerf@@QEAAXPEAVGraphics@gfx@Beefy@@PEAVTrackNodeEntry@23@MM@Z"))
		{
			String str;
			str += StrFormat("%s -> %s (Gen=%d) ", ToString(BeMCOperand::FromBlock(mcBlock)).c_str(), ToString(BeMCOperand::FromBlock(succ)).c_str(), genCtx->mBlocks[succ->mBlockIdx].mGenerateCount);

			for (auto newNode : newNodes)
				str += StrFormat(" %d", newNode);
			str += "\r\n";
			OutputDebugStringA(str.c_str());
		}*/

		succ->mPredVRegsInitialized = newPredVRegsInitialized;

		//TODO: This was a bad comparison
// 		if (succ->mBlockIdx > succ->mBlockIdx)
// 			modifiedBlockAfter = true;
// 		else
// 			modifiedBlockBefore = true;

		// What does this do?
		if (succ->mBlockIdx > mcBlock->mBlockIdx)
			modifiedBlockAfter = true;
		else
			modifiedBlockBefore = true;

		entry.mGenerateQueued = true;
	}
}

void BeMCContext::FixVRegInitFlags(BeMCInst* inst, const BeMCOperand& operand)
{
	if (operand.IsVReg())
	{
		SizedArray<int, 1> addVec = { operand.mVRegIdx };
		inst->mVRegsInitialized = mVRegInitializedContext.Add(inst->mVRegsInitialized, addVec, true);
	}
}

void BeMCContext::SetVTrackingValue(BeMCOperand& operand, BeVTrackingValue& vTrackingValue)
{
	if (!operand.IsVRegAny())
		return;
	vTrackingValue.mEntry->Set(operand.mVRegIdx);
	auto vregInfo = mVRegInfo[operand.mVRegIdx];
	SetVTrackingValue(vregInfo->mRelTo, vTrackingValue);
	SetVTrackingValue(vregInfo->mRelOffset, vTrackingValue);
}

void BeMCContext::DoInitInjectionPass()
{
	BP_ZONE("BeMCContext::DoInitInjectionPass");

	for (auto mcBlock : mBlocks)
	{
		mActiveBlock = mcBlock;
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			BfIRInitType pendingInitKind = BfIRInitType_NotNeeded;

			if (inst->mKind == BeMCInstKind_DbgDecl)
			{
				int vregIdx = inst->mArg0.mVRegIdx;
				auto vregInfo = mVRegInfo[vregIdx];
				auto dbgVar = vregInfo->mDbgVariable;

				if (!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, vregIdx))
				{
					if ((dbgVar->mPendingInitType != BfIRInitType_NotNeeded) && (dbgVar->mPendingInitType != BfIRInitType_NotNeeded_AliveOnDecl))
					{
						pendingInitKind = dbgVar->mPendingInitType;
						// We don't need dynLife anymore since we're explicitly writing this
						//vregInfo->mHasDynLife = false;
						//AllocInst(BeMCInstKind_Def, inst->mArg0, instIdx);
					}
				}
			}

			if (pendingInitKind != BfIRInitType_NotNeeded)
			{
				int vregIdx = inst->mArg0.mVRegIdx;
				auto dbgVar = mVRegInfo[vregIdx]->mDbgVariable;
				auto varType = mVRegInfo[vregIdx]->mType;
				auto initType = dbgVar->mPendingInitType;

				if (varType->IsFloat())
				{
					BeMCOperand zeroVal;
					if (varType->mTypeCode == BeTypeCode_Float)
						zeroVal.mKind = BeMCOperandKind_Immediate_f32;
					else
						zeroVal.mKind = BeMCOperandKind_Immediate_f64;
					if (initType == BfIRInitType_Uninitialized)
					{
						if (varType->mTypeCode == BeTypeCode_Float)
							zeroVal.mImmF32 = NAN;
						else
							zeroVal.mImmF64 = NAN;
					}
					else
					{
						if (varType->mTypeCode == BeTypeCode_Float)
							zeroVal.mImmF32 = 0;
						else
							zeroVal.mImmF64 = 0;
					}
					AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), zeroVal, instIdx + 1);
				}
				else if (varType->IsNonVectorComposite())
				{
					SetAndRestoreValue<int*> insertPtr(mInsertInstIdxRef, &instIdx);
					instIdx++;
					uint8 val = (initType == BfIRInitType_Uninitialized) ? 0xCC : 0;
					CreateMemSet(BeMCOperand::FromVRegAddr(vregIdx), val, varType->mSize, varType->mAlign);
					instIdx--;
				}
				else
				{
					int64 val = (initType == BfIRInitType_Uninitialized) ? (int64)0xCCCCCCCCCCCCCCCCLL : 0;
					AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), BeMCOperand::FromImmediate(val), instIdx + 1);
				}

				dbgVar->mPendingInitType = BfIRInitType_NotNeeded;
			}
		}
		mActiveBlock = NULL;
	}

	SetCurrentInst(NULL);
}

void BeMCContext::ReplaceVRegsInit(BeMCBlock* mcBlock, int startInstIdx, BeVTrackingList* prevList, BeVTrackingList* newList)
{
	for (int instIdx = startInstIdx; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
	{
		auto inst = mcBlock->mInstructions[instIdx];
		if (inst->mVRegsInitialized != prevList)
			return;
		inst->mVRegsInitialized = newList;
	}
}

// This pass does bottom-up initialization for "simple" vregs (ie: not variables)
void BeMCContext::SimpleInitializedPass()
{
}

void BeMCContext::GenerateVRegInitFlags(BeVTrackingGenContext& genCtx)
{
	BP_ZONE("BeMCContext::GenerateVRegInitFlags");

	mVRegInitializedContext.Clear();
	mVRegInitializedContext.Init((int)mVRegInfo.size());

	auto emptyList = mVRegInitializedContext.AllocEmptyList();

	genCtx.mEmptyList = emptyList;
	genCtx.mBlocks.Resize(mBlocks.size());

	for (int blockIdx = 0; blockIdx < (int)mBlocks.size(); blockIdx++)
	{
		auto mcBlock = mBlocks[blockIdx];
		mcBlock->mPredVRegsInitialized = emptyList;
	}

	SimpleInitializedPass();

	bool modifiedBlockBefore;
	bool modifiedBlockAfter;
	InitializedPassHelper(mBlocks[0], &genCtx, modifiedBlockBefore, modifiedBlockAfter);

	while (true)
	{
		bool didWork = false;

		// Handle any outstanding pred entries
		for (int blockIdx = 0; blockIdx < (int)mBlocks.size(); blockIdx++)
		{
			auto& entry = genCtx.mBlocks[blockIdx];
			if (entry.mGenerateQueued)
			{
				entry.mGenerateQueued = false;
				didWork = true;
				auto block = mBlocks[blockIdx];

				bool modifiedBlockBefore;
				bool modifiedBlockAfter;
				InitializedPassHelper(block, &genCtx, modifiedBlockBefore, modifiedBlockAfter);
				if (modifiedBlockBefore)
					break;
			}
		}

		if (!didWork)
			break;
	}

	// Fix up the vregsInit changes to represent changes from the last instruction in one block to the first instruction on the next block
	for (int blockIdx = 0; blockIdx < (int)mBlocks.size() - 1; blockIdx++)
	{
		auto fromBlock = mBlocks[blockIdx];
		auto toBlock = mBlocks[blockIdx + 1];

		if ((fromBlock->mInstructions.empty()) || (toBlock->mInstructions.empty()))
			continue;

		BeMCInst* fromInst = NULL;
		// Last inst may not have vregsInit set
		for (int instIdx = (int)fromBlock->mInstructions.size() - 1; instIdx >= 0; instIdx--)
		{
			fromInst = fromBlock->mInstructions[instIdx];
			if (fromInst->mVRegsInitialized != NULL)
				break;
		}

		auto toInst = toBlock->mInstructions.front();

		if ((fromInst == NULL) || (fromInst->mVRegsInitialized == NULL) ||
			(toInst == NULL) || (toInst->mVRegsInitialized == NULL))
			continue;

		auto prevVRegsInit = toInst->mVRegsInitialized;
		auto newVRegsInit = mVRegInitializedContext.SetChanges(toInst->mVRegsInitialized, fromInst->mVRegsInitialized);;

		for (int instIdx = 0; instIdx < (int)toBlock->mInstructions.size(); instIdx++)
		{
			auto inst = toBlock->mInstructions[instIdx];
			if (inst->mVRegsInitialized != prevVRegsInit)
				break;
			inst->mVRegsInitialized = newVRegsInit;
		}
	}

	int instCount = 0;
	for (auto block : mBlocks)
		instCount += (int)block->mInstructions.size();

	BpEvent("GenerateVRegInitFlags Results",
		StrFormat("Blocks: %d\nInstructions: %d\nVRegs: %d\nCalls: %d\nHandled Calls: %d\nProcessed Instructions: %d\nVRegInit Bytes: %d\nTemp Bytes: %d",
		(int)mBlocks.size(), instCount, (int)mVRegInfo.size(), genCtx.mCalls, genCtx.mHandledCalls, genCtx.mInstructions, mVRegInitializedContext.mAlloc.GetAllocSize(), genCtx.mAlloc.GetAllocSize()).c_str());
}

bool BeMCContext::DoInitializedPass()
{
	BP_ZONE("BeMCContext::DoInitializedPass");

	BeVTrackingGenContext genCtx;
	GenerateVRegInitFlags(genCtx);

	// Search blocks for instances of ambiguous initialization
	for (int blockIdx = 0; blockIdx < (int)mBlocks.size(); blockIdx++)
	{
		auto mcBlock = mBlocks[blockIdx];

		// If mPredVRegsInitialized is NULL, this blocks is unreachable.  It could still be in some other block's preds so we don't
		//  completely erase it, just empty it
		if (genCtx.mBlocks[mcBlock->mBlockIdx].mGenerateCount == 0)
		{
			// Unused block - clear almost all instructions
			int newIdx = 0;
			// 			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			// 			{
			// 				auto inst = mcBlock->mInstructions[instIdx];
			// 				if (inst->mKind == BeMCInstKind_ValueScopeHardEnd)
			// 				{
			// 					mcBlock->mInstructions[newIdx++] = inst;
			// 				}
			// 			}
			mcBlock->mInstructions.mSize = newIdx;
			continue;
		}

		for (int vregIdx : *mcBlock->mPredVRegsInitialized)
		{
			if (vregIdx >= mVRegInitializedContext.mNumItems)
				continue;

			if (mVRegInitializedContext.IsSet(mcBlock->mPredVRegsInitialized, vregIdx, BeTrackKind_Uninitialized))
			{
				// If a register has both the "initialized" and "uninitialized" flags set, that means that there will be debugging range
				//  where the variable could show an uninitialized value.  We zero out the value at the def position to make
				//  variable viewing less confusing.  This state does not affect actual program execution because we've already statically
				//  checked against usage of unassigned variables -- unless overridden with a "?" uninitialized expression, in which case
				//  one could argue that 0xCC would be a more useful value, but we go with the argument for the "less confusing" zero in this case.
				auto vregInfo = mVRegInfo[vregIdx];
				vregInfo->mHasAmbiguousInitialization = true;
			}
		}
	}

	bool needsReRun = false;

	bool hasInits = false;
	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		auto vregInfo = mVRegInfo[vregIdx];
		if (vregInfo->mDbgVariable != NULL)
		{
			auto initType = vregInfo->mDbgVariable->mInitType;

			if (initType == BfIRInitType_NotSet)
			{
				if (vregInfo->mHasAmbiguousInitialization)
					initType = BfIRInitType_Zero;
				else
					initType = BfIRInitType_NotNeeded;
			}
			else if ((initType == BfIRInitType_Uninitialized) && (!vregInfo->mHasAmbiguousInitialization))
			{
				// If we have an explicitly-uninitialized variable but it doesn't have ambiguous initialization
				//  then we don't need to do the 0xCC, we can control visibility in the debugger through scope
				initType = BfIRInitType_NotNeeded;
			}

			if (vregInfo->mDbgVariable->mPendingInitType == initType)
				continue;

			if ((initType != BfIRInitType_NotNeeded) ||
				(vregInfo->mDbgVariable->mPendingInitType == BfIRInitType_NotSet))
			{
				vregInfo->mDbgVariable->mPendingInitType = initType;
			}

			if ((initType != BfIRInitType_NotNeeded) && (initType != BfIRInitType_NotNeeded_AliveOnDecl))
			{
				vregInfo->mDbgVariable->mPendingInitDef = true;
				BF_ASSERT(vregInfo->mHasDynLife);
				vregInfo->mDoConservativeLife = true;
				hasInits = true;
			}
		}
	}

	if (hasInits)
	{
		DoInitInjectionPass();

		// We need to re-execute the init flag generation
		BeVTrackingGenContext genCtx;
		GenerateVRegInitFlags(genCtx);
	}

	return !needsReRun;
}

void BeMCContext::DoTLSSetup()
{
	// Perform at the end of the 'entry' block

	if (mTLSVRegIdx == -1)
		return;

	auto mcBlock = mBlocks[0];
	mActiveBlock = mcBlock;

	for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
	{
		auto inst = mcBlock->mInstructions[instIdx];

		// Skip past param setup
		if (inst->mKind == BeMCInstKind_Def)
			continue;
		if ((inst->mKind == BeMCInstKind_Mov) && (inst->mArg1.IsNativeReg()))
			continue;

		auto int32Type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
		auto intType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
		auto intPtrType = mModule->mContext->GetPointerTo(intType);

		auto mcTlsIndex = AllocVirtualReg(intType);
		CreateDefineVReg(mcTlsIndex, instIdx++);

		auto sym = mCOFFObject->GetSymbolRef("_tls_index");
		if (sym->mType == NULL)
			sym->mType = int32Type;
		AllocInst(BeMCInstKind_Mov, mcTlsIndex, BeMCOperand::FromSymbol(sym->mIdx), instIdx++);

		AllocInst(BeMCInstKind_Def, BeMCOperand::FromVReg(mTLSVRegIdx), instIdx++);
		AllocInst(BeMCInstKind_TLSSetup, BeMCOperand::FromVReg(mTLSVRegIdx), BeMCOperand::FromImmediate(0), instIdx++);

		auto mcTlsVal = AllocVirtualReg(intPtrType);
		CreateDefineVReg(mcTlsVal, instIdx++);
		auto tlsValInfo = GetVRegInfo(mcTlsVal);
		tlsValInfo->mIsExpr = true;
		tlsValInfo->mRelTo = BeMCOperand::FromVReg(mTLSVRegIdx);
		tlsValInfo->mRelOffset = mcTlsIndex;
		tlsValInfo->mRelOffsetScale = 8;

		auto mcLoadVal = mcTlsVal;
		mcLoadVal.mKind = BeMCOperandKind_VRegLoad;
		AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(mTLSVRegIdx), mcLoadVal, instIdx++);

		break;
	}

	mActiveBlock = NULL;
}

void BeMCContext::DoChainedBlockMerge()
{
	for (int blockIdx = 0; blockIdx < mBlocks.size() - 1; blockIdx++)
	{
		auto mcBlock = mBlocks[blockIdx];
		if (mcBlock->mHasFakeBr)
			continue;

		auto nextMcBlock = mBlocks[blockIdx + 1];

		// We only branch into one block, and the the next block only has current block as a predecessor?
		if ((mcBlock->mSuccs.size() == 1) && (nextMcBlock->mPreds.size() == 1) && (nextMcBlock->mPreds[0] == mcBlock))
		{
			auto lastInst = mcBlock->mInstructions.back();
			// Last instruction of current block is an unconditional branch to the next block
			if ((lastInst->mKind == BeMCInstKind_Br) && (lastInst->mArg0.mBlock == nextMcBlock))
			{
				mcBlock->mSuccs.Remove(nextMcBlock);
				for (auto succ : nextMcBlock->mSuccs)
				{
					if (!mcBlock->mSuccs.Contains(succ))
					{
						mcBlock->mSuccs.push_back(succ);
						succ->mPreds.push_back(mcBlock);
					}
					succ->mPreds.Remove(nextMcBlock);
				}

				mcBlock->mMaxDeclBlockId = nextMcBlock->mMaxDeclBlockId;
				mcBlock->mInstructions.pop_back();
				for (auto inst : nextMcBlock->mInstructions)
					mcBlock->mInstructions.push_back(inst);
				mBlocks.RemoveAt(blockIdx + 1);
				blockIdx--;
			}
		}
	}

	for (int blockIdx = 0; blockIdx < mBlocks.size(); blockIdx++)
		mBlocks[blockIdx]->mBlockIdx = blockIdx;
}

void BeMCContext::DoSplitLargeBlocks()
{
	Dictionary<int, int> blockEndRemap;

	int splitSize = 4096;
	int maxBlockSize = splitSize + splitSize / 4;
	bool hadSplits = false;

	for (int blockIdx = 0; blockIdx < mBlocks.size(); blockIdx++)
	{
		int blockBreakIdx = 0;

		auto srcBlock = mBlocks[blockIdx];
		if (srcBlock->mInstructions.size() < maxBlockSize)
			continue;
		hadSplits = true;
		int extensionCount = srcBlock->mInstructions.size() / splitSize;
		// Don't allow the last block to have too few instructions
		if (srcBlock->mInstructions.size() % splitSize < splitSize / 4)
			extensionCount--;
		if (extensionCount == 0) // Shouldn't happen, really
			continue;
		for (int extIdx = 0; extIdx < extensionCount; extIdx++)
		{
			BeMCBlock* extBlock = mMCBlockAlloc.Alloc();
			extBlock->mName = srcBlock->mName + StrFormat("__EXT%d", extIdx);
			mBlocks.Insert(blockIdx + 1 + extIdx, extBlock);

			int startIdx = (extIdx + 1) * splitSize;
			int endIdx = BF_MIN((extIdx + 2) * splitSize, srcBlock->mInstructions.size());
			if (extIdx == extensionCount - 1)
				endIdx = (int)srcBlock->mInstructions.size();

			extBlock->mInstructions.Insert(0, &srcBlock->mInstructions[startIdx], endIdx - startIdx);
			extBlock->mPreds.Add(mBlocks[blockIdx + extIdx]);

			if (extIdx > 0)
			{
				auto prevBlock = mBlocks[blockIdx + extIdx];
				mActiveBlock = prevBlock;
				AllocInst(BeMCInstKind_Br, BeMCOperand::FromBlock(extBlock));
				mActiveBlock = NULL;

				mBlocks[blockIdx + extIdx]->mSuccs.Add(extBlock);
			}

			if (extIdx == extensionCount - 1)
			{
				blockEndRemap[blockIdx] = blockIdx + extIdx + 1;

				for (auto succ : srcBlock->mSuccs)
				{
					succ->mPreds.Remove(srcBlock);
					succ->mPreds.Add(extBlock);
				}
				extBlock->mSuccs = srcBlock->mSuccs;
				srcBlock->mSuccs.Clear();
				srcBlock->mSuccs.Add(mBlocks[blockIdx + 1]);
			}
		}

		mActiveBlock = srcBlock;
		srcBlock->mInstructions.RemoveRange(splitSize, (int)srcBlock->mInstructions.size() - splitSize);
		AllocInst(BeMCInstKind_Br, BeMCOperand::FromBlock(mBlocks[blockIdx + 1]));
		mActiveBlock = NULL;
	}

	if (!hadSplits)
		return;

	// 	for (int blockIdx = 0; blockIdx < mBlocks.size(); blockIdx++)
	// 	{
	// 		auto mcBlock = mBlocks[blockIdx];
	// 		for (auto inst : mcBlock->mInstructions)
	// 		{
	// 			if (inst->mResult.mKind == BeMCOperandKind_Phi)
	// 			{
	// 				inst->mResult.mP
	// 			}
	// 		}
	// 	}

	for (int blockIdx = 0; blockIdx < mBlocks.size(); blockIdx++)
		mBlocks[blockIdx]->mBlockIdx = blockIdx;

	for (auto phi : mPhiAlloc)
	{
		for (auto& phiValue : phi->mValues)
		{
			int remappedBlock = -1;
			if (blockEndRemap.TryGetValue(phiValue.mBlockFrom->mBlockIdx, &remappedBlock))
			{
				phiValue.mBlockFrom = mBlocks[remappedBlock];
			}
		}
	}
}

void BeMCContext::DetectLoops()
{
	BP_ZONE("BeMCContext::DetectLoops");
	BeMCLoopDetector loopDetector(this);
	loopDetector.DetectLoops();
}

void BeMCContext::DoLastUsePassHelper(BeMCInst* inst, const BeMCOperand& operand)
{
	if (operand.mKind == BeMCOperandKind_VRegPair)
	{
		auto mcOperand = BeMCOperand::FromEncoded(operand.mVRegPair.mVRegIdx0);
		if (mcOperand.IsVRegAny())
			DoLastUsePassHelper(inst, mcOperand);
		mcOperand = BeMCOperand::FromEncoded(operand.mVRegPair.mVRegIdx1);
		if (mcOperand.IsVRegAny())
			DoLastUsePassHelper(inst, mcOperand);
		return;
	}

	if (!operand.IsVRegAny())
		return;

	int vregIdx = operand.mVRegIdx;
	auto vregInfo = mVRegInfo[vregIdx];

	if ((vregInfo->mDbgVariable == NULL) && (!vregInfo->mHasDynLife) && (!vregInfo->mFoundLastUse))
	{
		vregInfo->mFoundLastUse = true;
		auto lastUseRecord = mAlloc.Alloc<BeVRegLastUseRecord>();
		lastUseRecord->mVRegIdx = vregIdx;
		lastUseRecord->mNext = inst->mVRegLastUseRecord;
		inst->mVRegLastUseRecord = lastUseRecord;
	}

	if (vregInfo->mRelTo)
		DoLastUsePassHelper(inst, vregInfo->mRelTo);
	if (vregInfo->mRelOffset)
		DoLastUsePassHelper(inst, vregInfo->mRelOffset);
}

void BeMCContext::DoLastUsePass()
{
	for (int blockIdx = (int)mBlocks.size() - 1; blockIdx >= 0; blockIdx--)
	{
		auto mcBlock = mBlocks[blockIdx];

		for (int instIdx = (int)mcBlock->mInstructions.size() - 1; instIdx >= 0; instIdx--)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
			for (auto operand : operands)
			{
				DoLastUsePassHelper(inst, *operand);
			}
		}
	}
}

void BeMCContext::RefreshRefCounts()
{
	for (auto vregInfo : mVRegInfo)
	{
		if (vregInfo->mMustExist)
			vregInfo->mRefCount = 1;
		else
			vregInfo->mRefCount = 0;
		vregInfo->mAssignCount = 0;
	}

	for (auto mcBlock : mBlocks)
	{
		for (auto& inst : mcBlock->mInstructions)
		{
			if (inst->IsDef())
				continue;

			auto operands = { inst->mResult, inst->mArg0, inst->mArg1 };
			for (auto op : operands)
			{
				if (op.IsVRegAny())
					mVRegInfo[op.mVRegIdx]->mRefCount++;
				if (op.mKind == BeMCOperandKind_VRegPair)
				{
					auto vreg0 = BeMCOperand::FromEncoded(op.mVRegPair.mVRegIdx0);
					if (vreg0.IsVRegAny())
					{
						mVRegInfo[vreg0.mVRegIdx]->mRefCount++;
						mVRegInfo[vreg0.mVRegIdx]->mAssignCount++;
					}

					auto vreg1 = BeMCOperand::FromEncoded(op.mVRegPair.mVRegIdx1);
					if (vreg1.IsVRegAny())
						mVRegInfo[vreg1.mVRegIdx]->mRefCount++;
				}
			}

			if (inst->IsAssignment())
			{
				if (inst->mArg0.IsVRegAny())
					mVRegInfo[inst->mArg0.mVRegIdx]->mAssignCount++;
			}
			if (inst->mResult)
			{
				if (inst->mResult.IsVRegAny())
					mVRegInfo[inst->mResult.mVRegIdx]->mAssignCount++;
			}
		}
	}

	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		auto vregInfo = mVRegInfo[vregIdx];
		if (vregInfo->mRefCount > 0)
			AddRelRefs(BeMCOperand::FromVReg(vregIdx), 0);
	}
}

bool BeMCContext::CheckVRegEqualityRange(BeMCBlock* mcBlock, int instIdx, const BeMCOperand& vreg0, const BeMCOperand& vreg1, BeMCRemapper& regRemaps, bool onlyCheckFirstLifetime)
{
	// The algorithm here is that we check to see if either of these registers are ever written to
	//  up to the point that one of them dies.  If not, we can map them to each other
	bool foundSplitPoint = false;
	bool hadPointerDeref = false;
	for (int checkInstIdx = instIdx + 1; checkInstIdx < (int)mcBlock->mInstructions.size(); checkInstIdx++)
	{
		auto checkInst = mcBlock->mInstructions[checkInstIdx];

		if ((!IsLive(checkInst->mLiveness, vreg0.mVRegIdx, regRemaps)) ||
			((!onlyCheckFirstLifetime) && (!IsLive(checkInst->mLiveness, vreg1.mVRegIdx, regRemaps))))
		{
			return true;
		}

		if ((checkInst->mKind == BeMCInstKind_LifetimeEnd) &&
			((checkInst->mArg0.mVRegIdx == vreg0.mVRegIdx) || (checkInst->mArg0.mVRegIdx == vreg1.mVRegIdx)))
		{
			if (checkInstIdx == (int)mcBlock->mInstructions.size() - 1)
			{
				// There are cases where this might be the last instruction in a block, so we won't see the mLiveness change
				//  so this catches that case
				return true;
			}

			// We have cases like when have a "defer delete val;", val will get used by the dtor after the LifetimeEnd
			return false;
		}

		if (hadPointerDeref)
		{
			// Pointer deref is okay as long as liveness ends one instruction afterwards
			return false;
		}

		// Anything can happen during a call...
		if (checkInst->mKind == BeMCInstKind_Call)
			return false;

		auto mcDest = checkInst->GetDest();
		if ((mcDest != NULL) && (mcDest->mKind == BeMCOperandKind_VReg))
		{
			// Any assignment to either of these regs disqualifies them
			if ((mcDest->mVRegIdx == vreg0.mVRegIdx) || (mcDest->mVRegIdx == vreg1.mVRegIdx))
				return false;

			// Any writing into memory could be aliased, so we don't replace across that boundary
			if (HasPointerDeref(*mcDest))
			{
				// Delay the 'return false' until we determine if our liveness ends on the next instruction.
				//  If it does, then this is okay because it didn't affect the source of any operations
				hadPointerDeref = true;
			}
		}
	}
	return false;
}

void BeMCContext::DoInstCombinePass()
{
	BP_ZONE("BeMCContext::DoInstCombinePass");

	BeMCRemapper regRemaps;
	regRemaps.Init((int)mVRegInfo.size());

	BeVTrackingValue vTrackingVal((int)mVRegInfo.size());
	HashSet<int> wantUnwrapVRegs;

	for (auto mcBlock : mBlocks)
	{
		BeDbgLoc* curDbgLoc = NULL;
		int safeMergeLocStart = 0;
		bool lastDefDidMerge = false;
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			bool reprocessInst = false;

			auto inst = mcBlock->mInstructions[instIdx];
			BeMCInst* prevInst = NULL;
			if (instIdx > 0)
				prevInst = mcBlock->mInstructions[instIdx - 1];

			if ((inst->mDbgLoc != curDbgLoc) && (inst->mDbgLoc != NULL))
			{
				safeMergeLocStart = instIdx;
				curDbgLoc = inst->mDbgLoc;
			}

			if ((inst->mKind == BeMCInstKind_Call) || (inst->mKind == BeMCInstKind_MemCpy) || (inst->mKind == BeMCInstKind_MemSet))
			{
				// Don't merge anything across callsites or memory writes
				safeMergeLocStart = instIdx;
			}

			auto instDest = inst->GetDest();
			if ((instDest != NULL) && (HasPointerDeref(*instDest)))
			{
				// Memory aliasing may invalidate any accesses, don't merge expressions across here
				safeMergeLocStart = instIdx + 1;
			}

			SetCurrentInst(inst);

			//TODO: Remove
			// if we've remapped other vregs onto this that aren't dead, then we can't end the value lifetime but we
			//  must end the lifetime of the visible debug variable
			/*if (inst->mKind == BeMCInstKind_LifetimeEnd)
			{
				bool extendValue = false;

				int vregIdx = inst->mArg0.mVRegIdx;
				int checkIdx = regRemaps.GetHead(vregIdx);
				while (checkIdx != -1)
				{
					if ((checkIdx != vregIdx) && (mLivenessContext.IsSet(inst->mLiveness, checkIdx)))
						extendValue = true;
					checkIdx = regRemaps.GetNext(checkIdx);
				}

				if (extendValue)
				{
					//Throw error
				}
					//inst->mKind = BeMCInstKind_LifetimeDbgEnd;
			}*/

			// Handle movs into .addrs
			if (inst->mKind == BeMCInstKind_Mov)
			{
				auto vregInfoDest = GetVRegInfo(inst->mArg0);
				if (vregInfoDest != NULL)
				{
					auto vregInfoSrc = GetVRegInfo(inst->mArg1);
					if ((vregInfoSrc != NULL) && (vregInfoSrc->mForceMerge))
					{
						vregInfoSrc->mForceMerge = false;
						if ((vregInfoDest->mDbgVariable != NULL) && (vregInfoSrc->CanEliminate()))
						{
							BF_ASSERT(!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, inst->mArg0.mVRegIdx));
							AddRegRemap(inst->mArg1.mVRegIdx, inst->mArg0.mVRegIdx, regRemaps);
							// Remove any LifetimeStarts since our source may have been alive before us
							//if (inst->mArg1.mVRegIdx < inst->mArg0.mVRegIdx)
								//removeLifetimeStarts.insert(inst->mArg0.mVRegIdx);
						}
					}
				}
			}

			// The following code only applies when we aren't on the last instruction
			int nextIdx = instIdx + 1;
			BeMCInst* nextInst = NULL;
			while (true)
			{
				if (nextIdx >= (int)mcBlock->mInstructions.size())
					break;
				nextInst = mcBlock->mInstructions[nextIdx];
				if (!nextInst->IsDef())
					break;
				nextIdx++;
			}
			if (nextInst == NULL)
				continue;

			// The following code only applies when we have at least 2 more instructions
			// Find a non-Def instruction
			int nextNextIdx = nextIdx + 1;
			BeMCInst* nextNextInst = NULL;
			while (true)
			{
				if (nextNextIdx >= (int)mcBlock->mInstructions.size())
					break;
				nextNextInst = mcBlock->mInstructions[nextNextIdx];
				if (!nextNextInst->IsDef())
					break;
				nextNextIdx++;
			}

			auto result = RemapOperand(inst->mResult, regRemaps);
			auto arg0 = RemapOperand(inst->mArg0, regRemaps);
			auto arg1 = RemapOperand(inst->mArg1, regRemaps);

			if (inst->IsDef())
			{
				auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
				if (vregInfo->mIsExpr)
				{
					for (int checkInstIdx = safeMergeLocStart; checkInstIdx < instIdx; checkInstIdx++)
					{
						auto checkInst = mcBlock->mInstructions[checkInstIdx];
						if (checkInst->IsDef())
						{
							auto checkArg0 = RemapOperand(checkInst->mArg0, regRemaps);
							auto checkVRegInfo = mVRegInfo[checkArg0.mVRegIdx];

							if (checkVRegInfo->mIsExpr)
							{
								// Remap to use the earlier definition of this expression
								if (AreOperandsEquivalent(inst->mArg0, checkArg0, regRemaps))
								{
									vTrackingVal.Clear();
									SetVTrackingValue(checkArg0, vTrackingVal);

									// Check to make sure we didn't do any writes to any of the values used in the expression.  This happens
									//  during "iVal++", for example.
									bool hadBadWrite = false;
									for (int verifyInstIdx = safeMergeLocStart; verifyInstIdx < instIdx; verifyInstIdx++)
									{
										auto verifyInst = mcBlock->mInstructions[verifyInstIdx];
										auto verifyDest = verifyInst->GetDest();
										if ((verifyDest != NULL) && (verifyDest->IsVRegAny()) && (vTrackingVal.mEntry->IsSet(verifyDest->mVRegIdx)))
										{
											hadBadWrite = true;
											break;
										}
									}

									if (!hadBadWrite)
									{
										auto vregInfoFrom = GetVRegInfo(inst->mArg0);
										if (vregInfoFrom->mDbgVariable == NULL)
											AddRegRemap(inst->mArg0.mVRegIdx, checkArg0.mVRegIdx, regRemaps, true);
									}
									break;
								}
							}
						}
					}
				}
			}

			BeMCOperand origNextDestOperand;
			if (nextInst->IsAssignment())
				origNextDestOperand = nextInst->mArg0;
			else if (nextInst->mResult)
				origNextDestOperand = nextInst->mResult;
			BeMCOperand nextDestOperand = RemapOperand(origNextDestOperand, regRemaps);

			if (inst->mKind == BeMCInstKind_DefLoad)
			{
				auto vregInfoDest = GetVRegInfo(inst->mArg0);

				auto mcLoaded = inst->mArg0;
				auto mcAddr = vregInfoDest->mRelTo;

				// We can only do a regRemap replace if we aren't the product of a pointer dereference
				bool mapToDef = false;
				if (mcAddr.mKind == BeMCOperandKind_VReg)
				{
					auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
					// We do 'onlyCheckFirstLifetime' because we have cases like when have a "defer delete val;", because val will get used by the dtor after the LiftimeEnd
					if ((vregInfo->IsDirectRelTo()) && (vregInfo->mDbgVariable == NULL) && (CheckVRegEqualityRange(mcBlock, instIdx, mcLoaded, mcAddr, regRemaps, true)))
					{
						// If the source value doesn't change in the lifetime of the loaded value then
						//  we can just map directly to the source - no copy needed.
						inst->mKind = BeMCInstKind_Def;
						AddRegRemap(inst->mArg0.mVRegIdx, vregInfoDest->mRelTo.mVRegIdx, regRemaps, true);
						continue;
					}
				}
				else if (mcAddr.mKind == BeMCOperandKind_SymbolAddr)
				{
					// No actual load needed - just keep it as a Direct reference
					inst->mKind = BeMCInstKind_Def;
					//wantUnwrapVRegs.Add(inst->mArg0.mVRegIdx);
					continue;
				}
				else if (mcAddr.mKind == BeMCOperandKind_Symbol)
				{
					//TODO: We used to have this as a 'inst->mKind = BeMCInstKind_Def;' case also, but that messed up post-increment (ie: gVal++) values
					continue;
				}

				bool hadPointerDeref = false;
				for (int checkInstIdx = instIdx + 1; checkInstIdx < (int)mcBlock->mInstructions.size(); checkInstIdx++)
				{
					auto checkInst = mcBlock->mInstructions[checkInstIdx];
					BF_ASSERT(mcLoaded.IsVRegAny());
					if (!IsLive(checkInst->mLiveness, mcLoaded.mVRegIdx, regRemaps))
					{
						break;
					}

					if ((mcAddr.IsVRegAny()) && (!IsLive(checkInst->mLiveness, mcAddr.mVRegIdx, regRemaps)))
						break;

					if (hadPointerDeref)
					{
						// Pointer deref is okay as long as liveness ends one instruction afterwards
						break;
					}

					auto mcDest = checkInst->GetDest();
					if ((mcDest != NULL) &&
						((mcDest->mKind == BeMCOperandKind_VReg) || (mcDest->mKind == mcAddr.mKind)))
					{
						// Any assignment to either of these regs ends our remapping loop
						if ((mcDest->mVRegIdx == mcLoaded.mVRegIdx) || (mcDest->mVRegIdx == mcAddr.mVRegIdx))
						{
							break;
						}

						// Any writing into memory could be aliased, so we don't replace across that boundary
						if (HasPointerDeref(*mcDest))
						{
							// Delay the 'return false' until we determine if our liveness ends on the next instruction.
							//  If it does, then this is okay because it didn't affect the source of any operations
							hadPointerDeref = true;
						}
					}

					if (checkInst->IsInformational())
						continue;

					if (checkInst->mKind == BeMCInstKind_Call) // Anything can happen during a call
						break;

					if (checkInst->mDbgLoc != inst->mDbgLoc)
						break;

					auto operands = { &checkInst->mArg0, &checkInst->mArg1 };
					for (auto& operand : operands)
					{
						// If we reference the loaded version during the range where the value hasn't changed,
						//  then just refer back to the original value.  This is useful in cases like i++,
						//  where we want to be able to still do an INC on the source variable rather than
						//  "loadedI = i; i = loadedI + 1; ... use loadedI ..."
						if ((operand->mKind == BeMCOperandKind_VReg) && (operand->mVRegIdx == mcLoaded.mVRegIdx))
						{
							*operand = mcAddr;
						}

						if ((operand->mKind == BeMCOperandKind_VRegLoad) && (operand->mVRegIdx == mcLoaded.mVRegIdx))
						{
							if (mcAddr.mKind == BeMCOperandKind_VReg)
							{
								operand->mVRegIdx = mcAddr.mVRegIdx;
							}
						}
					}
				}
			}

			// Check for the special case of a single move to a SRET dereference - if so, then make the source composite be the source value
			if ((inst->mKind == BeMCInstKind_Mov) && (inst->mArg0.mKind == BeMCOperandKind_VRegLoad) && (inst->mArg1.mKind == BeMCOperandKind_VReg))
			{
				auto vregInfoDest = GetVRegInfo(inst->mArg0);
				auto vregInfoSrc = GetVRegInfo(inst->mArg1);
				if ((vregInfoDest->IsDirectRelTo()) && (vregInfoSrc->IsDirectRelTo()))
				{
					auto destRelTo = GetVRegInfo(vregInfoDest->mRelTo);
					auto srcRelTo = GetVRegInfo(vregInfoSrc->mRelTo);
					if ((destRelTo->mIsRetVal) && (vregInfoDest->mAssignCount == 1) &&
						(!destRelTo->mIsExpr) && (!srcRelTo->mIsExpr))
					{
						mCompositeRetVRegIdx = vregInfoDest->mRelTo.mVRegIdx;
						RemoveInst(mcBlock, instIdx);
						srcRelTo->SetRetVal();
						instIdx--;
						continue;
					}
				}
			}

			// For the form:
			//   @Def %vreg0
			//   [%result] = <op> %vreg0, %vreg1
			// Remap %vreg0 to %vreg1 if this is the only assignment, and thus an immutable definition, and vreg1 doesn't change
			if ((nextDestOperand) &&
				(((inst->mKind == BeMCInstKind_Def) && (arg0 == nextDestOperand)) ||
				((inst->mKind == BeMCInstKind_LifetimeStart) && (BeMCOperand::ToLoad(arg0) == nextDestOperand))))
			{
				lastDefDidMerge = false;

				auto vregInfoDest = GetVRegInfo(nextDestOperand);
				auto checkOperands = { &nextInst->mArg0, &nextInst->mArg1 };

				bool checkArg1 = true;
				if (nextInst->mResult)
				{
					if ((nextInst->mKind == BeMCInstKind_Add) || (nextInst->mKind == BeMCInstKind_Mul))
					{
						// These are commutative, so we can remap to either the LHS or RHS
						checkArg1 = true;
					}
					else
					{
						// We can only remap to LHS
						checkArg1 = false;
					}
				}

				for (int argCheckIdx = (nextInst->mResult ? 0 : 1); argCheckIdx < (checkArg1 ? 2 : 1); argCheckIdx++)
				{
					BeMCOperand origCheckOperand;

					if (argCheckIdx == 0)
						origCheckOperand = nextInst->mArg0;
					else
						origCheckOperand = nextInst->mArg1;
					auto checkOperand = RemapOperand(origCheckOperand, regRemaps);

					if (checkOperand.mKind != BeMCOperandKind_VReg) // Loads/Addrs not applicable
						continue;
					auto vregInfoCheck = GetVRegInfo(checkOperand);
					if (vregInfoCheck == NULL)
						continue;

					if ((vregInfoDest->mIsExpr) || (vregInfoCheck->mIsExpr))
						continue;
					if ((vregInfoCheck->mForceMem) && (!vregInfoDest->mForceMem))
						continue;

					if ((!vregInfoDest->mForceMerge) && (!vregInfoDest->mIsRetVal))
					{
						// If we merge to a vreg that has a pointer ref, that could be a deoptimization by not allowing
						//  either to bind to a register
						if (vregInfoDest->mForceMem != vregInfoCheck->mForceMem)
						{
							if (!GetType(checkOperand)->IsNonVectorComposite())
								continue;
						}
					}

					// We have a special retVal case where we can allow the __returnVal to share an address with the
					//  actual 'ret' param.  This is most useful when returning structs by value, to avoid a copy.
					//  The only downside is that editing the return value in the debugger will also show the value
					//  of the ret param changing, which is normally not acceptable but in this case we allow it.
					bool allowCoexist = ((vregInfoDest->mIsRetVal) ||
						(vregInfoCheck->CanEliminate()) || (vregInfoDest->CanEliminate()));

					// Only vregs of the same types can be merged
					if (!AreTypesEquivalent(vregInfoCheck->mType, vregInfoDest->mType))
						continue;

					bool canMerge = false;
					if ((nextInst->IsMov()) && (allowCoexist))
					{
						if ((vregInfoCheck->mForceMerge) || (vregInfoCheck->mIsRetVal))
							canMerge = true; // This is to allow the incoming vregs to combine with the ".addr" debug variables
						else // Always use orig operands for liveness checking
							canMerge = CheckVRegEqualityRange(mcBlock, nextIdx, origCheckOperand, origNextDestOperand, regRemaps);
					}
					else
					{
						// If we're dealing with a non-Mov instruction, then the values of both of these vregs will
						//  be different after the instruction, so we can only map them if the incoming vreg will be dead
						//  afterwards.  If both are dbgVariables then we can't allow them to coeexist because the user
						//  could edit one of the values in the debugger and we don't want the other one changing.
						if ((nextNextInst != NULL) &&
							(!IsLive(nextNextInst->mLiveness, origCheckOperand.mVRegIdx, regRemaps)) &&
							(!IsLive(nextNextInst->mLiveness, checkOperand.mVRegIdx, regRemaps)))
							canMerge = true;
					}

					if (canMerge)
					{
						if (vregInfoCheck->mForceMerge)
							vregInfoCheck->mForceMerge = false; // Only allow one merge
						lastDefDidMerge = true;

						if (vregInfoDest->mIsRetVal)
						{
							// We don't do a remap for this return value optimization, because we want both vregs
							//  to still show in the debugger - even though they will point to the same address
							//  now (in the case of a struct return)
							vregInfoCheck->SetRetVal();
							//BF_ASSERT(mCompositeRetVRegIdx != -1);
							//vregInfoCheck->mRelTo = BeMCOperand::FromVReg(mCompositeRetVRegIdx);
							break;
						}

						if ((vregInfoCheck->mDbgVariable != NULL) && (vregInfoDest->mDbgVariable != NULL))
						{
							// We want separate vreg info so we can track the mDbgVariable separately, but we do want
							//  the same location
							vregInfoDest->mIsExpr = true;
							vregInfoDest->mRelTo.mKind = BeMCOperandKind_VReg;
							vregInfoDest->mRelTo.mVRegIdx = checkOperand.mVRegIdx;
						}
						else
						{
							// Replace %vreg0 with %vreg1?
							if (vregInfoDest->CanEliminate())
							{
								bool canReplace = true;
								if (canReplace)
								{
									AddRegRemap(nextDestOperand.mVRegIdx, checkOperand.mVRegIdx, regRemaps);
									break;
								}
							}

							// Or go the other way, and make %vreg1 use our variable directly since we're just handing off anyway
							if (vregInfoCheck->CanEliminate())
							{
								if (vregInfoCheck->mIsRetVal)
								{
									vregInfoDest->SetRetVal();
									//BF_ASSERT(mCompositeRetVRegIdx != -1);
									//vregInfoCheck->mRelTo = BeMCOperand::FromVReg(mCompositeRetVRegIdx);
								}

								AddRegRemap(checkOperand.mVRegIdx, nextDestOperand.mVRegIdx, regRemaps);
								if (vregInfoDest->mNaturalReg == X64Reg_None)
									vregInfoDest->mNaturalReg = vregInfoCheck->mNaturalReg;
								break;
							}
						}
					}
				}
			}

			//if (mDebugging)
			{
				// For the form
				//  Def %vreg0
				//  Mov %vreg0, %vreg1
				//  <Op> <Arg>, %vreg0
				// Where vreg0 has no other references, convert to
				//  <Op> <arg>, &vreg1
				if ((inst->mKind == BeMCInstKind_Def) && (nextInst->mKind == BeMCInstKind_Mov) && (nextNextInst != NULL))
				{
					auto vregInfo = GetVRegInfo(inst->mArg0);
					if ((vregInfo->mRefCount == 2) && (inst->mArg0 == nextInst->mArg0) && (!nextInst->mArg1.IsNativeReg()) &&
						(nextNextInst->mArg0 != nextInst->mArg0) && (nextNextInst->mArg1 == nextInst->mArg0) &&
						(AreTypesEquivalent(GetType(inst->mArg0), GetType(nextInst->mArg1))))
					{
						nextNextInst->mArg1 = nextInst->mArg1;
						RemoveInst(mcBlock, instIdx);
						RemoveInst(mcBlock, instIdx);
						instIdx--;
						continue;
					}
				}
			}

			// For the form
			//  <BinOp> %vreg0, %vreg1
			//  Test %vreg0, 1
			// Remove test, because the BinOp will already set the correct flags
			if ((nextInst->mKind == BeMCInstKind_Test) && (nextInst->mArg1.IsImmediateInt()) && (nextInst->mArg1.mImmediate == 1) &&
				(nextInst->mArg0 == inst->mArg0))
			{
				if ((inst->mKind == BeMCInstKind_Xor) || (inst->mKind == BeMCInstKind_Or) || (inst->mKind == BeMCInstKind_And))
				{
					BF_ASSERT(!inst->mResult);
					RemoveInst(mcBlock, instIdx + 1);
					instIdx--;
					continue;
				}
			}

			if (inst->mResult)
			{
				auto nextArg0 = RemapOperand(nextInst->mArg0, regRemaps);
				auto nextArg1 = RemapOperand(nextInst->mArg1, regRemaps);

				// For the form:
				//   %vreg2 = <op> %vreg0, %vreg1
				//   Mov %vreg3, %vreg2
				// If %vreg2 has no other references, combine into
				//   %vreg3 = <op> %vreg0, %vreg1
				if ((nextInst->IsMov()) && (result == nextArg1))
				{
					// We purposely check inst->mResult here rather than 'result', because that ref count is our
					//  indication of whether the result was actually used again
					auto vregInfo = GetVRegInfo(inst->mResult);
					if ((vregInfo != NULL) && (vregInfo->mRefCount == 2) && (vregInfo->CanEliminate()))
					{
						// Make sure the MOV isn't a conversion
						auto movArg0Type = GetType(nextInst->mArg0);
						auto movArg1Type = GetType(nextInst->mArg1);
						if (AreTypesEquivalent(movArg0Type, movArg1Type))
						{
							// We actually eliminate the current inst incase there was a Def between the two,
							//  otherwise the Def would occur AFTER the assignment which would be bad

							nextInst->mResult = nextInst->mArg0;
							nextInst->mKind = inst->mKind;
							nextInst->mArg0 = inst->mArg0;
							nextInst->mArg1 = inst->mArg1;

							RemoveInst(mcBlock, instIdx);
							instIdx--;
							continue;
						}
					}
				}

				if (inst->IsCommutable())
				{
					if ((nextInst->IsMov()) && (arg1 == nextArg0) && (result == nextArg1))
					{
						break;
					}
				}

				// For the form:
				//   %vreg2 = <op> %vreg0, %vreg1
				// If %vreg0 ends its life on this instruction,
				//   Replace all instances of %vreg2 with %vreg0
				if ((inst->mResult.mKind == BeMCOperandKind_VReg) && (inst->mArg0.mKind == BeMCOperandKind_VReg) &&
					(inst->mResult != inst->mArg0))
				{
					// Check liveness against both the orig arg0 and the remap
					//if ((!nextInst->mLiveness->IsSet(inst->mArg0.mVRegIdx)) && (!nextInst->mLiveness->IsSet(arg0.mVRegIdx)))
					if (!IsLive(nextInst->mLiveness, inst->mArg0.mVRegIdx, regRemaps))
					{
						// Only do a full regRemap if this is the def initialization
						if ((prevInst != NULL) && (prevInst->mKind == BeMCInstKind_Def) && (prevInst->mArg0.mVRegIdx == inst->mResult.mVRegIdx))
						{
							auto vregInfoResult = GetVRegInfo(result);
							auto vregInfo0 = GetVRegInfo(arg0);
							if ((vregInfoResult->CanEliminate()) && (!vregInfo0->mIsExpr))
							{
								AddRegRemap(inst->mResult.mVRegIdx, arg0.mVRegIdx, regRemaps);
							}
						}
					}
				}
			}

			if (inst->mKind == BeMCInstKind_RestoreVolatiles)
			{
				if (mcBlock == mBlocks.back())
				{
					bool hadInvalidInst = false;
					for (int checkInstIdx = instIdx + 1; checkInstIdx < (int)mcBlock->mInstructions.size(); checkInstIdx++)
					{
						auto checkInst = mcBlock->mInstructions[checkInstIdx];

						if ((checkInst->mKind != BeMCInstKind_DbgDecl) &&
							(checkInst->mKind != BeMCInstKind_EnsureInstructionAt) &&
							(checkInst->mKind != BeMCInstKind_Ret))
						{
							hadInvalidInst = true;
							break;
						}
					}

					if (!hadInvalidInst)
					{
						// We return after this, we don't need to restore volatiles
						inst->mArg1 = BeMCOperand::FromPreserveFlag(BeMCPreserveFlag_NoRestore);
						continue;
					}
				}
			}

			if (nextNextInst == NULL)
			{
				if (reprocessInst)
					instIdx--;
				continue;
			}

			// For the form
			//  Xor &vreg0, 1
			//  CondBr <label>, <op>
			// If vreg0 dies at the end, convert to to:
			//  Test &veg0, 1
			//  CondBr <label>, ~<op>
			// This is advantageous because Test is a non-modifying instruction so we can hopefully elide a copy
			if ((inst->mKind == BeMCInstKind_Xor) &&
				(nextInst->mKind == BeMCInstKind_CondBr))
			{
				auto vregInfo = GetVRegInfo(inst->mArg0);
				if ((vregInfo != NULL) && (!IsLive(nextNextInst->mLiveness, inst->mArg0.mVRegIdx, regRemaps)))
				{
					inst->mKind = BeMCInstKind_Test;
					BF_ASSERT(nextInst->mArg1.mKind == BeMCOperandKind_CmpKind);
					nextInst->mArg1.mCmpKind = BeModule::InvertCmp(nextInst->mArg1.mCmpKind);

					if ((instIdx >= 2) && (!lastDefDidMerge))
					{
						auto defInst = mcBlock->mInstructions[instIdx - 2];
						if ((defInst->mKind == BeMCInstKind_Def) && (defInst->mArg0 == inst->mArg0))
						{
							// Go back and try to eliminate this vreg
							instIdx -= 3;
							continue;
						}
					}
				}
			}

			// For the form:
			//   %vreg0 = CmpToBool <cmpType>
			//   Test %vreg0, %vreg0
			//   CondBr %label, eq
			// If %vreg0 has no other references, convert to:
			//   CondBr %label, <cmpType>
			if ((inst->mKind == BeMCInstKind_CmpToBool) &&
				(nextInst->mKind == BeMCInstKind_Test) && (nextInst->mArg0 == nextInst->mArg1) &&
				(nextNextInst->mKind == BeMCInstKind_CondBr) &&
				(nextInst->mArg0 == inst->mResult))
			{
				auto vregInfo = GetVRegInfo(inst->mResult);
				if ((vregInfo != NULL) && (vregInfo->mRefCount == 3))
				{
					BF_ASSERT(nextNextInst->mArg1.mKind == BeMCOperandKind_CmpKind);
					nextNextInst->mArg1.mCmpKind = inst->mArg0.mCmpKind;
					RemoveInst(mcBlock, nextIdx);
					RemoveInst(mcBlock, instIdx);
					instIdx--;
				}
			}

			if (reprocessInst)
				instIdx--;
		}
	}
	SetCurrentInst(NULL);

	Dictionary<int, int> regRemapMap;
	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		int remapIdx = regRemaps.GetHead(vregIdx);
		if (remapIdx != vregIdx)
			regRemapMap[vregIdx] = remapIdx;
	}

	// We want to keep the earliest @Def and remap (in necessary) the later one
	//  For remaps, the liveVRregIdx will always indicate which @Def occurs earlier,
	//  unless there's a case I haven't considered
	Dictionary<int, int> defMap;
	for (auto& remapPair : regRemapMap)
	{
		int remapFrom = remapPair.mKey;
		int remapTo = remapPair.mValue;
		if (remapTo < 0)
			continue;
		auto vregInfoFrom = mVRegInfo[remapFrom];
		auto vregInfoTo = mVRegInfo[remapTo];
		int bestVRegIdx = remapTo;
		//auto itr = defMap.find(remapTo);
		//if (itr != defMap.end())

		int* prevBestVRegIdxPtr = NULL;
		if (defMap.TryGetValue(remapTo, &prevBestVRegIdxPtr))
		{
		}
		else
			defMap[remapTo] = bestVRegIdx;
	}

	struct _RemapEntry
	{
		int mVRegFrom;
		int mVRegTo;
	};

	Array<_RemapEntry> initRemaps;

	// We have a many-to-one relation so we have to use both a
	HashSet<int> keepDefs;
	for (auto& defPair : defMap)
		keepDefs.Add(defPair.mValue);
	HashSet<int> removeDefs;
	for (auto& remapPair : regRemapMap)
	{
		removeDefs.Add(remapPair.mKey);
		keepDefs.Add(remapPair.mValue);

		auto vregInfoFrom = mVRegInfo[remapPair.mKey];
		auto vregInfoTo = mVRegInfo[remapPair.mValue];

		if (vregInfoFrom->mHasDynLife)
		{
			vregInfoTo->mHasDynLife = true;

			_RemapEntry remapEntry = { remapPair.mKey, remapPair.mValue };
			initRemaps.Add(remapEntry);
		}

		if (vregInfoFrom->mForceMem)
			vregInfoTo->mForceMem = true;
	}

	auto _RemapVReg = [&](int& vregIdx)
	{
		int* regIdxPtr = NULL;
		if (regRemapMap.TryGetValue(vregIdx, &regIdxPtr))
		{
			int regIdx = *regIdxPtr;
			if (regIdx < 0)
			{
				Fail("Invalid reg remap");
			}
			else
				vregIdx = regIdx;
		}
	};

	auto _RemapOperand = [&](BeMCOperand* operand)
	{
		if (operand->IsVRegAny())
		{
			int* regIdxPtr = NULL;
			if (regRemapMap.TryGetValue(operand->mVRegIdx, &regIdxPtr))
			{
				int regIdx = *regIdxPtr;
				if (regIdx < 0)
				{
					if (operand->mKind == BeMCOperandKind_VRegLoad)
						operand->mKind = BeMCOperandKind_VReg;
					else
						NotImpl();
					operand->mVRegIdx = -regIdx;
				}
				else
				{
					operand->mVRegIdx = regIdx;
				}
			}
		}

		if (operand->mKind == BeMCOperandKind_VRegPair)
		{
			_RemapVReg(operand->mVRegPair.mVRegIdx0);
			_RemapVReg(operand->mVRegPair.mVRegIdx1);
		}

		if (operand->IsVRegAny())
		{
			if (wantUnwrapVRegs.Contains(operand->mVRegIdx))
			{
				// Replace any rel-to symbol info with direct references to those symbols
				auto vregInfo = mVRegInfo[operand->mVRegIdx];
				BF_ASSERT(vregInfo->IsDirectRelToAny());
				if (operand->mKind == BeMCOperandKind_VReg)
					*operand = vregInfo->mRelTo;
				else if (operand->mKind == BeMCOperandKind_VRegAddr)
					*operand = OperandToAddr(vregInfo->mRelTo);
			}
		}
	};

	Dictionary<BeVTrackingList*, BeVTrackingList*> initRemapMap;

	if ((!regRemapMap.IsEmpty()) || (!wantUnwrapVRegs.IsEmpty()))
	{
		for (auto mcBlock : mBlocks)
		{
			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			{
				auto inst = mcBlock->mInstructions[instIdx];
				if (inst->IsDef())
				{
					if ((removeDefs.Contains(inst->mArg0.mVRegIdx)) || (wantUnwrapVRegs.Contains(inst->mArg0.mVRegIdx)))
					{
						RemoveInst(mcBlock, instIdx);
						instIdx--;
						continue;
					}
				}

				auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
				for (auto& operand : operands)
				{
					_RemapOperand(operand);
				}

				if ((!initRemaps.IsEmpty()) && (inst->mVRegsInitialized != NULL))
				{
					BeVTrackingList** vregsInitializedValuePtr = NULL;
					if (initRemapMap.TryAdd(inst->mVRegsInitialized, NULL, &vregsInitializedValuePtr))
					{
						SizedArray<int, 16> removeVec;
						SizedArray<int, 16> addVec;
						SizedArray<int, 16> filteredRemoveVec;
						SizedArray<int, 16> filteredAddVec;

						for (auto& entry : initRemaps)
						{
							if (mVRegInitializedContext.IsSet(inst->mVRegsInitialized, entry.mVRegFrom))
							{
								removeVec.Add(entry.mVRegFrom);
								if (!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, entry.mVRegTo))
									addVec.Add(entry.mVRegTo);
							}
						}

						BeVTrackingList* vregsInitialized = mVRegInitializedContext.Modify(inst->mVRegsInitialized, addVec, removeVec, filteredAddVec, filteredRemoveVec, true);
						*vregsInitializedValuePtr = vregsInitialized;
					}
					inst->mVRegsInitialized = *vregsInitializedValuePtr;
				}
			}
		}

		for (auto vregInfo : mVRegInfo)
		{
			auto operands = { &vregInfo->mRelTo, &vregInfo->mRelOffset };
			for (auto& operand : operands)
			{
				if (operand->IsVRegAny())
				{
					RemapOperand(*operand, regRemaps);
					_RemapOperand(operand);
				}
			}
		}
	}
}

void BeMCContext::DoRegAssignPass()
{
	BP_ZONE("BeMCContext::DoRegAssignPass");

	bool generateLiveness = true;
	//
	if (generateLiveness)
	{
		for (auto& node : mColorizer.mNodes)
		{
			node.mEdges.Clear();
		}
		mColorizer.Prepare();
		GenerateLiveness();
	}
	else
	{
		mColorizer.Prepare();
	}

	mColorizer.GenerateRegCosts();
	//
	{
		BP_ZONE("BeMCContext::DoRegAssignPass:ints");
		mColorizer.AssignRegs(BeMCColorizer::RegKind_Ints);
	}
	//
	{
		BP_ZONE("BeMCContext::DoRegAssignPass:float");
		mColorizer.AssignRegs(BeMCColorizer::RegKind_Floats);
	}

#ifdef _DEBUG
	BF_ASSERT(mColorizer.Validate());

	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		auto vregInfo = mVRegInfo[vregIdx];
		if (vregInfo->mRefCount == 0)
			continue;
		if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (vregIdx != mCompositeRetVRegIdx))
			continue;
		if ((vregInfo->mForceReg) && (!vregInfo->mRelTo) && (vregInfo->mReg == X64Reg_None))
		{
			SoftFail("Failed to assign register to ForceReg vreg");
		}
		if (vregInfo->mDisableRAX)
		{
			BF_ASSERT(ResizeRegister(vregInfo->mReg, 8) != X64Reg_RAX);
		}
		if (vregInfo->mDisableRDX)
		{
			BF_ASSERT(ResizeRegister(vregInfo->mReg, 8) != X64Reg_RDX);
		}
		if (vregInfo->mDisableR11)
		{
			BF_ASSERT(ResizeRegister(vregInfo->mReg, 8) != X64Reg_R11);
		}
		if (vregInfo->mDisableR12)
		{
			BF_ASSERT(ResizeRegister(vregInfo->mReg, 8) != X64Reg_R12);
		}
		if (vregInfo->mDisableR13)
		{
			BF_ASSERT(ResizeRegister(vregInfo->mReg, 8) != X64Reg_R13);
		}
	}
#endif
}

void BeMCContext::DoFrameObjPass()
{
	BF_ASSERT(mBlocks.size() == 1);

	SetCurrentInst(NULL);

	// MS x64 ABI requires a "home address" of 4 intptrs when we call a function, plus whatever
	//  we need for calls with more than 4 params.
	// If we're doing UseBP, we have to allocate these at call time
	int homeSize = BF_ALIGN(BF_MAX(mMaxCallParamCount, 4) * 8, 16);

	if (mMaxCallParamCount == -1)
		homeSize = 0;

	mStackSize = 0;

	if (mUseBP)
		mUsedRegs.Add(X64Reg_RBP);

	int regStackOffset = 0;
	int xmmRegStackSize = 0;
	for (auto usedReg : mUsedRegs)
	{
		if (!IsVolatileReg(usedReg))
		{
			BF_ASSERT(usedReg != X64Reg_RSP);
			if (IsXMMReg(usedReg))
				xmmRegStackSize += 16;
			else
				regStackOffset += 8;
		}
	}

	int xmmStackOffset = -1;
	if (xmmRegStackSize > 0)
	{
		int align = 16;
		int alignOffset = regStackOffset + 8;
		int alignedPosition = (mStackSize + alignOffset + (align - 1)) & ~(align - 1);
		mStackSize = alignedPosition - alignOffset;
		mStackSize += xmmRegStackSize;
		xmmStackOffset = -mStackSize - regStackOffset;
	}

	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		auto vregInfo = mVRegInfo[vregIdx];

		if (vregInfo->mRelOffset.mKind == BeMCOperandKind_Immediate_HomeSize)
		{
			vregInfo->mRelOffset = BeMCOperand::FromImmediate(homeSize);
		}

		if ((vregInfo->mIsRetVal) && (vregIdx != mCompositeRetVRegIdx))
			continue;

		if ((vregInfo->mRefCount > 0) && (!vregInfo->mIsExpr) && (vregInfo->mReg == X64Reg_None) && (vregInfo->mFrameOffset == INT_MIN))
		{
			BF_ASSERT(vregInfo->mAlign != -1);
			int align = BF_MAX(vregInfo->mAlign, 1);
			int alignOffset = regStackOffset + 8;
			int alignedPosition = (mStackSize + alignOffset + (align - 1)) & ~(align - 1);
			mStackSize = alignedPosition - alignOffset;
			//vregInfo->mFrameOffset = -mStackSize - regStackOffset - 8;
			mStackSize += BF_ALIGN(vregInfo->mType->mSize, vregInfo->mAlign);
			vregInfo->mFrameOffset = -mStackSize - regStackOffset;
		}
	}

	// If we have dynamic stack resizing then we have a stack frame and must be 16-byte aligned
	//  even if we're a leaf function
	bool mHasFramePointer = false;

	// Potentially pull off retaddr bytes if alignment allows --
	//  Stack must be aligned to 16 bytes and retaddr offsets us by 8,
	//  so we must always offset by 0x?8
	int align = 8;
	mStackSize = (mStackSize + (align - 1)) & ~(align - 1);

	//if (!mUseBP)
	{
		// MS x64 ABI requires a "home address" of 4 intptrs when we call a function, plus whatever
		//  we need for calls with more than 4 params.
		// If we're doing UseBP, we have to allocate these at call time
		if (mMaxCallParamCount != -1)
		{
			mStackSize += homeSize;

			// Even param counts, to align to 16 bytes.
			//  Minimum is space for 4 params
			//int paramSpace = (std::max(mMaxCallParamCount, 4) + 1) & ~1;
			//mStackSize += paramSpace * 8;
		}
	}

	int stackAdjust = mStackSize;
	mStackSize += regStackOffset;
	if ((mStackSize != 0) && (mStackSize % 16 == 0))
	{
		mStackSize += 8;
		stackAdjust += 8;
	}

	mActiveBlock = mBlocks[0];

	if (mUseBP)
	{
		AllocInst(BeMCInstKind_Unwind_SetBP, 0);
		AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RBP), BeMCOperand::FromReg(X64Reg_RSP), 0);
	}

	int xmmRegIdx = 0;
	for (auto usedReg : mUsedRegs)
	{
		if ((!IsVolatileReg(usedReg)) && (IsXMMReg(usedReg)))
		{
			AllocInst(BeMCInstKind_Unwind_SaveXMM, BeMCOperand::FromReg(usedReg), BeMCOperand::FromImmediate(mStackSize + xmmStackOffset + xmmRegIdx * 16), 0);
			AllocInst(BeMCInstKind_Push, BeMCOperand::FromReg(usedReg), BeMCOperand::FromImmediate(mStackSize + xmmStackOffset + xmmRegIdx * 16), 0);
			xmmRegIdx++;
		}
	}

	if (stackAdjust > 0)
	{
		AllocInst(BeMCInstKind_Unwind_Alloc, BeMCOperand::FromImmediate(stackAdjust), 0);
		AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromImmediate(stackAdjust), 0);

		if (stackAdjust >= 4096)
		{
			BeMCOperand mcFunc;
			mcFunc.mKind = BeMCOperandKind_SymbolAddr;
			mcFunc.mSymbolIdx = mCOFFObject->GetSymbolRef("__chkstk")->mIdx;
			AllocInst(BeMCInstKind_Call, mcFunc, 0);
			AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand::FromImmediate(stackAdjust), 0);
		}
	}

	for (auto usedReg : mUsedRegs)
	{
		if ((!IsVolatileReg(usedReg)) && (!IsXMMReg(usedReg)))
		{
			AllocInst(BeMCInstKind_Unwind_PushReg, BeMCOperand::FromReg(usedReg), 0);
			AllocInst(BeMCInstKind_Push, BeMCOperand::FromReg(usedReg), 0);
		}
	}

	if (mHasVAStart)
	{
		for (int i = 0; i < 4; i++)
		{
			auto regSaveVReg = AllocVirtualReg(mModule->mContext->GetPointerTo(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64)));
			auto vregInfo = GetVRegInfo(regSaveVReg);
			vregInfo->mRelTo = BeMCOperand::FromReg(X64Reg_RSP);
			vregInfo->mRelOffset = BeMCOperand::FromImmediate(i * 8 + 8);
			vregInfo->mIsExpr = true;

			X64CPURegister reg;
			switch (i)
			{
			case 0:
				reg = X64Reg_RCX;
				break;
			case 1:
				reg = X64Reg_RDX;
				break;
			case 2:
				reg = X64Reg_R8;
				break;
			case 3:
				reg = X64Reg_R9;
				break;
			}
			AllocInst(BeMCInstKind_Mov, BeMCOperand::ToLoad(regSaveVReg), BeMCOperand::FromReg(reg), 0);
		}
	}

	BeMCOperand restoreBPVal;
	if (mUseBP)
	{
		auto nativeType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
		restoreBPVal = AllocVirtualReg(nativeType);
		auto restoreBPVReg = mVRegInfo[restoreBPVal.mVRegIdx];
		restoreBPVReg->mIsExpr = true;
		restoreBPVReg->mRelTo = BeMCOperand::FromReg(X64Reg_RBP);
		restoreBPVReg->mRelOffset = BeMCOperand::FromImmediate(stackAdjust);
		restoreBPVal.mKind = BeMCOperandKind_VReg;
	}

	for (int instIdx = 0; instIdx < (int)mActiveBlock->mInstructions.size(); instIdx++)
	{
		auto checkInst = mActiveBlock->mInstructions[instIdx];
		if (checkInst->mKind == BeMCInstKind_Ret)
		{
			mCurDbgLoc = checkInst->mDbgLoc;
			int insertIdx = instIdx;

			int xmmRegIdx = 0;
			for (auto usedReg : mUsedRegs)
			{
				if ((!IsVolatileReg(usedReg)) && (IsXMMReg(usedReg)))
				{
					AllocInst(BeMCInstKind_Pop, BeMCOperand::FromReg(usedReg), BeMCOperand::FromImmediate(mStackSize + xmmStackOffset + xmmRegIdx * 16), insertIdx++);
					xmmRegIdx++;
				}
			}

			if (mUseBP)
			{
				AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RSP), restoreBPVal, insertIdx++);
			}
			else if (stackAdjust > 0)
			{
				AllocInst(BeMCInstKind_Add, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromImmediate(stackAdjust), insertIdx++);
				instIdx++;
			}

			for (auto usedReg : mUsedRegs)
			{
				if ((!IsVolatileReg(usedReg)) && (!IsXMMReg(usedReg)))
					AllocInst(BeMCInstKind_Pop, BeMCOperand::FromReg(usedReg), insertIdx++);
			}

			instIdx = insertIdx;
		}
	}
}

static int gLegalizeIdx = 0;

void BeMCContext::DoActualization()
{
	while (true)
	{
		bool hasMoreWork = false;

		for (auto mcBlock : mBlocks)
		{
			mActiveBlock = mcBlock;
			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			{
				auto inst = mcBlock->mInstructions[instIdx];
				SetCurrentInst(inst);

				bool forceMove = false;

				if (inst->mKind == BeMCInstKind_DbgDecl)
				{
					auto vregInfo = GetVRegInfo(inst->mArg0);
					if (vregInfo->mWantsExprActualize)
					{
						if (inst->mArg0.mKind == BeMCOperandKind_VReg)
						{
							if (vregInfo->mRelTo.IsSymbol())
							{
								forceMove = true;
							}
							else if (vregInfo->mDbgVariable->mIsValue)
							{
								vregInfo->mDbgVariable->mIsValue = false;
								inst->mArg0.mKind = BeMCOperandKind_VRegAddr;
							}
							else
							{
								inst->mArg0.mKind = BeMCOperandKind_VRegAddr;
								vregInfo->mDbgVariable->mType = mModule->mDbgModule->CreateReferenceType(BeValueDynCast<BeDbgType>(vregInfo->mDbgVariable->mType));
							}
						}
						vregInfo->mWantsExprActualize = false;
					}
				}

				if ((inst->IsDef()) || (forceMove))
				{
					int vregIdx = inst->mArg0.mVRegIdx;
					auto vregInfo = GetVRegInfo(inst->mArg0);

					if (vregInfo->mWantsExprOffsetActualize)
					{
						vregInfo->mWantsExprOffsetActualize = false;

						auto offsetType = GetType(vregInfo->mRelOffset);

						auto scratchReg = AllocVirtualReg(offsetType, 2, false);
						CreateDefineVReg(scratchReg, instIdx++);

						AllocInst(BeMCInstKind_Mov, scratchReg, vregInfo->mRelOffset, instIdx++);
						AllocInst(BeMCInstKind_IMul, scratchReg, BeMCOperand::FromImmediate(vregInfo->mRelOffsetScale), instIdx++);

						vregInfo->mRelOffset = scratchReg;
						vregInfo->mRelOffsetScale = 1;
					}

					if ((vregInfo->mWantsExprActualize) || (forceMove))
					{
						if (vregInfo->mDbgVariable != NULL)
						{
							// Wait until dbgDecl so we can potentially set to VRegAddr
						}
						else
						{
							vregInfo->mWantsExprActualize = false;
						}

						BF_ASSERT((!vregInfo->IsDirectRelTo()) || (vregInfo->mDbgVariable != NULL));

						if (vregInfo->mIsExpr)
						{
							if (vregInfo->IsDirectRelToAny())
							{
								AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), vregInfo->mRelTo, instIdx++ + 1);

								vregInfo->mIsExpr = false;
								vregInfo->mRelTo = BeMCOperand();
								vregInfo->mRelOffset = BeMCOperand();
								vregInfo->mRelOffsetScale = 1;
							}
							else
							{
								// Create a new reg with the expression, and then load the value into the old non-expressionized reg
								//  This has the advantage of fixing any other references to this expr, too
								auto scratchReg = AllocVirtualReg(vregInfo->mType, 2, false);
								auto scratchVRegInfo = mVRegInfo[scratchReg.mVRegIdx];

								scratchVRegInfo->mIsExpr = true;
								scratchVRegInfo->mRelTo = vregInfo->mRelTo;
								scratchVRegInfo->mRelOffset = vregInfo->mRelOffset;
								scratchVRegInfo->mRelOffsetScale = vregInfo->mRelOffsetScale;

								vregInfo->mIsExpr = false;
								vregInfo->mRelTo = BeMCOperand();
								vregInfo->mRelOffset = BeMCOperand();
								vregInfo->mRelOffsetScale = 1;

								CreateDefineVReg(scratchReg, instIdx++ + 1);
								AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), scratchReg, instIdx++ + 1);
							}
						}
					}
				}
			}
		}

		if (!hasMoreWork)
			break;
	}
}

void BeMCContext::DoLoads()
{
	for (auto mcBlock : mBlocks)
	{
		mActiveBlock = mcBlock;
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			SetCurrentInst(inst);

			if (inst->mKind == BeMCInstKind_DefLoad)
			{
				// Almost all DefLoads should be eliminated during InstCombine, but for 'persistent loads', we need
				//  to actualize them now
				inst->mKind = BeMCInstKind_Def;
				auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
				if (vregInfo->mRefCount == 0)
					continue;

				auto relLoaded = vregInfo->mRelTo;
				auto loadTo = inst->mArg0;
				BF_ASSERT(loadTo.mKind == BeMCOperandKind_VReg);
				AllocInst(BeMCInstKind_Mov, loadTo, relLoaded, instIdx++ + 1);
				vregInfo->mIsExpr = false;
				vregInfo->mRelTo = BeMCOperand();
			}

			if (inst->mKind == BeMCInstKind_Def)
			{
				auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
				if (vregInfo->mRefCount == 0)
					continue;

				if (vregInfo->mRelTo.mKind == BeMCOperandKind_VRegAddr)
				{
					auto vregRelInfo = mVRegInfo[vregInfo->mRelTo.mVRegIdx];
					vregRelInfo->mForceMem = true;
					CheckForce(vregInfo);
				}
			}
		}
	}
}

static int sLegalizationIdx = 0;

// Returning false means we generated some new vregs that need to be assigned to registers
bool BeMCContext::DoLegalization()
{
	BP_ZONE("BeMCContext::DoLegalization");

	bool debugging = false;
	//if (mBeFunction->mName == "?DrawEntry@DrawContext@PerfView@BeefPerf@@QEAAXPEAVGraphics@gfx@Beefy@@PEAVTrackNodeEntry@23@MM@Z")
		//debugging = true;

	if (debugging)
		OutputDebugStrF("DoLegalization\n");

	bool isFinalRun = true;

	int startingVRegCount = (int)mVRegInfo.size();

	HashSet<int> vregExprChangeSet;
	bool hasPendingActualizations = false;

	SetCurrentInst(NULL);

	int regPreserveDepth = 0;

	for (auto mcBlock : mBlocks)
	{
		mActiveBlock = mcBlock;
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			BfIRInitType pendingInitKind = BfIRInitType_NotNeeded;
			gLegalizeIdx++;

			auto inst = mcBlock->mInstructions[instIdx];
			SetCurrentInst(inst);
			mActiveInst = inst;

			if (inst->mKind == BeMCInstKind_Mov)
			{
				// Useless mov, remove it
				if (OperandsEqual(inst->mArg0, inst->mArg1, true))
				{
					// We leave the instructions alone here and remove them in DoRegFinalization.
					//  There are some cases with DirectRelTo vregs where we need to preserve liveness that a mov
					//  generates. We just want to keep from inserting any additional legalization instructions here (ie: movs to temps)
					continue;
				}
			}

			BeMCInst* nextInst = NULL;
			if (instIdx < mcBlock->mInstructions.size() - 1)
			{
				// It's important that we do this at the top before any instructions might get split, so liveness of the next instruction can be analyzed
				nextInst = mcBlock->mInstructions[instIdx + 1];
			}

			auto arg0 = GetFixedOperand(inst->mArg0);
			auto arg1 = GetFixedOperand(inst->mArg1);
			auto arg0Type = GetType(inst->mArg0);
			auto arg1Type = GetType(inst->mArg1);

			if ((arg0Type != NULL) && (arg0Type->mSize == 0) && (!inst->IsPsuedo()))
			{
				RemoveInst(mcBlock, instIdx);
				instIdx--;
				continue;
			}

			// Check operands
			if ((!inst->IsPsuedo()) && (arg0Type != NULL) && (!arg0Type->IsNonVectorComposite()))
			{
				bool replacedOpr = false;
				for (int oprIdx = 0; oprIdx < 2; oprIdx++)
				{
					BeMCOperand& origOperand = (oprIdx == 0) ? inst->mArg0 : inst->mArg1;
					BeMCOperand& remappedOperand = (oprIdx == 0) ? arg0 : arg1;

					if (remappedOperand.IsVRegAny())
					{
						bool isMulti = false;

						BeRMParamsInfo rmInfo;
						GetRMParams(remappedOperand, rmInfo);

						bool badOperand = false;
						bool scratchForceReg = false;

						if (rmInfo.mMode == BeMCRMMode_Invalid)
						{
							badOperand = true;
							scratchForceReg = true;
						}

						// We can only allow a "complex direct" expression such as (RAX+RBX) on the SRC side of
						//  a mov, where it can be emitted as a LEA.  All other uses are invalid.
						bool allowComplexDirect = (inst->mKind == BeMCInstKind_Mov) && (oprIdx == 1);
						if (allowComplexDirect)
						{
							auto vregInfo = GetVRegInfo(remappedOperand);
							if ((vregInfo->mRelTo.mKind == BeMCOperandKind_SymbolAddr) &&
								(vregInfo->mRelOffset.IsImmediateInt()) &&
								(vregInfo->mRelOffsetScale == 1))
							{
								badOperand = false;
							}
						}
						else
						{
							if ((rmInfo.mMode == BeMCRMMode_Direct) &&
								((rmInfo.mRegB != X64Reg_None) || (rmInfo.mBScale != 1) || (rmInfo.mDisp != 0)))
							{
								badOperand = true;
							}

							// 							bool isSimple = (rmForm == BeMCRMMode_Direct) && (regB == X64Reg_None) && (bScale == 1) && (disp == 0);
							// 							if ((!isSimple) && (oprIdx == 1) && (!arg0isSimple))
							// 							{
							// 								NOP;
							// 							}
							// 							if (oprIdx == 0)
							// 								arg0isSimple = isSimple;
						}

						if (badOperand)
						{
							BeMCOperand checkOperand = remappedOperand;
							while (checkOperand.mKind == BeMCOperandKind_VReg)
							{
								auto vregInfo = GetVRegInfo(checkOperand);
								if (!vregInfo->IsDirectRelToAny())
									break;
								checkOperand = vregInfo->mRelTo;
							}

							if (inst->mKind == BeMCInstKind_Mov)
							{
								if ((checkOperand.mKind == BeMCOperandKind_Symbol) || (checkOperand.mKind == BeMCOperandKind_SymbolAddr))
								{
									// SymbolAddr can be emitted as an LEA
									badOperand = false;
								}

								if (checkOperand.mKind == BeMCOperandKind_VRegAddr)
								{
									auto vregInfo = GetVRegInfo(checkOperand);
									if ((vregInfo->IsDirectRelToAny()) && (vregInfo->mRelTo.mKind == BeMCOperandKind_Symbol))
									{
										badOperand = false;
									}
								}
							}
						}

						if (badOperand)
						{
							if ((rmInfo.mErrorVReg == -2) && (rmInfo.mVRegWithScaledOffset != -1))
							{
								auto offsetVRegInfo = mVRegInfo[rmInfo.mVRegWithScaledOffset];
								offsetVRegInfo->mWantsExprOffsetActualize = true;
								hasPendingActualizations = true;
							}
							else if (!vregExprChangeSet.Contains(rmInfo.mErrorVReg))
							{
								auto remappedVRegInfo = mVRegInfo[remappedOperand.mVRegIdx];
								BeMCOperand savedOperand = BeMCOperand::FromVReg(remappedOperand.mVRegIdx);
								BeMCOperand newOperand;

								if (origOperand.mKind == BeMCOperandKind_VRegLoad)
								{
									// Loads keep their load-ness
									savedOperand = BeMCOperand::FromVReg(remappedOperand.mVRegIdx);
									newOperand = origOperand;
								}
								else
								{
									savedOperand = origOperand;
									newOperand.mKind = BeMCOperandKind_VReg;
								}

								auto type = GetType(savedOperand);

								//TODO: Are there circumstances when this needs to be forceReg?
								BeMCOperand scratchReg = AllocVirtualReg(type, 2, scratchForceReg);
								int insertPos = FindSafeInstInsertPos(instIdx);
								CreateDefineVReg(scratchReg, insertPos);
								AllocInst(BeMCInstKind_Mov, scratchReg, savedOperand, insertPos + 1);
								newOperand.mVRegIdx = scratchReg.mVRegIdx;
								IntroduceVRegs(scratchReg, mcBlock, insertPos, insertPos + 1);

								auto scratchVRegInfo = mVRegInfo[scratchReg.mVRegIdx];
								// If a scratch vreg needs to preserve a register like the remapped vreg
								scratchVRegInfo->mDisableR11 |= remappedVRegInfo->mDisableR11;
								scratchVRegInfo->mDisableR12 |= remappedVRegInfo->mDisableR12;
								scratchVRegInfo->mDisableR13 |= remappedVRegInfo->mDisableR13;
								scratchVRegInfo->mDisableRAX |= remappedVRegInfo->mDisableRAX;
								scratchVRegInfo->mDisableRDX |= remappedVRegInfo->mDisableRDX;
								scratchVRegInfo->mDisableEx |= remappedVRegInfo->mDisableEx;

								if ((insertPos == instIdx) || (!scratchForceReg))
								{
									// Only allow short-lived forceRegs
									instIdx += 2;
									//origOperand.mVRegIdx = scratchReg.mVRegIdx;
									origOperand = newOperand;
								}
								else
								{
									// .. otherwise we need yet another layer of indirection for the load
									auto loadedType = GetType(inst->mArg0);
									BeMCOperand loadedReg = AllocVirtualReg(loadedType, 2, false);
									CreateDefineVReg(loadedReg, insertPos + 2);
									AllocInst(BeMCInstKind_Mov, loadedReg, origOperand, insertPos + 3);
									IntroduceVRegs(scratchReg, mcBlock, insertPos + 2, insertPos + 3);
									instIdx += 4;
									origOperand = loadedReg;
								}

								replacedOpr = true;
								isFinalRun = false;
								if (debugging)
									OutputDebugStrF(" BadOperand %d\n", rmInfo.mErrorVReg);
							}
						}
					}
				}
				if (replacedOpr)
				{
					//instIdx--;
					continue;
				}
			}

			if (!inst->IsPsuedo())
			{
				if ((HasSymbolAddr(inst->mArg0)) && (inst->mKind != BeMCInstKind_Call))
				{
					ReplaceWithNewVReg(inst->mArg0, instIdx, true, false, true);
					isFinalRun = false;
					// 				if (debugging)
					// 					OutputDebugStrF(" SymbolAddr0\n");
				}

				// We can only allow symbol addresses on a MOV where they can be made into a LEA
				if ((HasSymbolAddr(inst->mArg1)) && (inst->mKind != BeMCInstKind_Mov))
				{
					ReplaceWithNewVReg(inst->mArg1, instIdx, true, false);
					isFinalRun = false;
					// 				if (debugging)
					// 					OutputDebugStrF(" SymbolAddr1\n");
				}
			}

			if (!inst->IsPsuedo())
			{
				if ((arg0.IsImmediate()) && (arg1) && (inst->IsCommutable()))
				{
					// Always make the immediate be the second arg
					BF_SWAP(inst->mArg0, inst->mArg1);
					BF_SWAP(arg0, arg1);
				}

				bool isIntMul = (inst->IsMul()) && (arg0Type->IsInt());
				bool isIntDiv = (((inst->mKind == BeMCInstKind_IDiv) || (inst->mKind == BeMCInstKind_Div))) && (arg0Type->IsInt());
				bool isMov_R64_IMM64 = ((inst->mKind == BeMCInstKind_Mov) && (inst->mArg0.IsNativeReg()) && (inst->mArg1.IsImmediateInt()));

				bool doStdCheck = true;
				if (isMov_R64_IMM64)
					doStdCheck = false;
				bool is3FormMul = false;
				bool badOps = false;

				// Two immediates are never allowed. This can happen in cases like '(a % 1) * 2'
				if ((arg0.IsImmediate()) && (arg1.IsImmediate()))
				{
					badOps = true;
					doStdCheck = true;
				}
				else if ((isIntMul) && (inst->mResult))
				{
					is3FormMul = true;
					if (inst->mArg1.IsImmediateInt())
					{
						bool isLegal = true;
						int64 immediateInt = inst->mArg1.GetImmediateInt();
						if (inst->mKind == BeMCInstKind_Mul)
						{
							if (immediateInt > 0xFFFFFFFFLL)
								isLegal = false;
						}
						else
						{
							if ((immediateInt < -0x80000000LL) || (immediateInt > 0x7FFFFFFF))
								isLegal = false;
						}

						if (isLegal)
							doStdCheck = false;
					}
				}

				// Int 3-form mul does not follow these rules
				if (doStdCheck)
				{
					bool isIncOrDec = false;
					isIncOrDec = (((inst->mKind == BeMCInstKind_Add) || (inst->mKind == BeMCInstKind_Sub)) &&
						(arg1.IsImmediateInt()) && (arg1.mImmediate == 1));

					if ((!isIncOrDec) && (!isIntMul) && (!isIntDiv))
					{
						if ((arg0.MayBeMemory()) && (arg1.MayBeMemory()))
						{
							if (arg0 == arg1)
							{
								if (inst->mKind != BeMCInstKind_Mov)
								{
									badOps = true;
								}
							}
							else
								badOps = true;
						}

						//TODO: For what instructions was this true?  CMP, MOV, ADD, etc... seems to have it

						if ((arg1Type != NULL) && (arg1Type->IsFloatOrVector()))
						{
							// MOV is allowed on '<r/m>, <imm>'
							if ((inst->mKind != BeMCInstKind_Mov) && (arg0.MayBeMemory()) && (arg1.IsImmediate()))
								badOps = true;
						}
					}

					// "MOV <r>, <imm64>" is the only instruction that allows an IMM64 immediate
					if (((inst->mKind != BeMCInstKind_Mov) || (!arg0.IsNativeReg())) &&
						(arg1.mKind == BeMCOperandKind_Immediate_i64))
					{
						if ((arg1.mImmediate < -0x80000000LL) || (arg1.mImmediate > 0x7FFFFFFF))
							badOps = true;
					}

					if (badOps)
					{
						// On X64 we can never have an instruction where both args are memory so we create a short-lived scratch vreg
						//  and run another reg pass to generate register access

						// From: <op> a, b
						// To: @Def scratch
						//     mov scratch, b
						//     <op> a, scratch
						auto targetType = GetType(inst->mArg0);

						if ((targetType->IsFloat()) && (!inst->mResult) && (arg0.IsVReg()) && (arg1.IsImmediateFloat()))
						{
							auto vregInfo0 = GetVRegInfo(arg0);
							if (!vregInfo0->mIsExpr)
							{
								// Convert from "<op> %reg0, %reg1"
								// To
								//   Mov %vreg2<reg>, %vreg0
								//   <op> %vreg2<reg>, %vreg1
								//   Mov %vreg0, %vreg2<reg>
								auto prevDest = inst->mArg0;
								ReplaceWithNewVReg(inst->mArg0, instIdx, true);
								AllocInst(BeMCInstKind_Mov, prevDest, inst->mArg0, instIdx + 1);
								IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 2);
								instIdx++;
								isFinalRun = false;
								continue;
							}
						}

						if (!targetType->IsNonVectorComposite())
						{
							auto scratchType = GetType(inst->mArg1);
							BeMCOperand scratchReg = AllocVirtualReg(scratchType, 2, true);
							CreateDefineVReg(scratchReg, instIdx);

							AllocInst(BeMCInstKind_Mov, scratchReg, inst->mArg1, instIdx + 1);
							inst->mArg1 = scratchReg;
							IntroduceVRegs(scratchReg, mcBlock, instIdx, instIdx + 2);

							// Don't process the changed instructions until after another reg pass
							instIdx += 2;
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" BadOps\n");
							continue;
						}
					}

					if (inst->mResult)
					{
						if (inst->mResult == inst->mArg0)
						{
							inst->mResult = BeMCOperand();
						}
						else if ((arg0Type->IsFloatOrVector()) && (!CouldBeReg(inst->mResult)))
						{
							// We need a REG on the dest for sure, so just create a scratch here, otherwise we end up
							//  requiring additional scratch vregs later
							// FROM:
							//  %vreg0 = op %vreg1, %vreg2
							// To:
							//  Mov %scratch<reg>, %vreg1
							//  Op %scratch<reg>, %vreg2
							//  Mov %result, %scatch<reg>

							ReplaceWithNewVReg(inst->mArg0, instIdx, true);
							AllocInst(BeMCInstKind_Mov, inst->mResult, inst->mArg0, instIdx + 1);
							IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 2);
							inst->mResult = BeMCOperand();
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Float RegOnDest\n");
							instIdx++;
							continue;
						}
						else if ((inst->mResult == inst->mArg1) &&
							((inst->mKind == BeMCInstKind_Add) || (inst->mKind == BeMCInstKind_Mul) || (inst->mKind == BeMCInstKind_IMul)))
						{
							BF_SWAP(inst->mArg0, inst->mArg1);
							BF_SWAP(arg0, arg1);
							inst->mResult = BeMCOperand();
						}
						else
						{
							bool addCanBeLEA =
								((inst->mKind == BeMCInstKind_Sub) || (inst->mKind == BeMCInstKind_Add)) && (!inst->mDisableShortForm) &&
								(arg0Type->mSize >= 2) && // Int8s don't have an LEA
								(GetFixedOperand(inst->mResult).IsNativeReg()) && (arg0.IsNativeReg()) && (arg1.IsImmediateInt());

							if (addCanBeLEA)
							{
								// LEA is add-only
								if (inst->mKind == BeMCInstKind_Sub)
								{
									BF_ASSERT(inst->mArg1.IsImmediate());
									inst->mKind = BeMCInstKind_Add;
									inst->mArg1.mImmediate = -inst->mArg1.mImmediate;
								}
							}
							else
							{
								bool handled = false;
								if (OperandsEqual(inst->mResult, inst->mArg1))
								{
									if (inst->mKind == BeMCInstKind_Sub)
									{
										// From: b = Sub a, b
										// To: Neg b
										//     Add b, a
										AllocInst(BeMCInstKind_Add, inst->mResult, inst->mArg0, instIdx + 1);
										inst->mKind = BeMCInstKind_Neg;
										inst->mArg0 = inst->mResult;
										inst->mArg1 = BeMCOperand();
										inst->mResult = BeMCOperand();
										handled = true;

										instIdx--; // Rerun on the Neg
									}
									else
									{
										// We need a scratch reg for this
										ReplaceWithNewVReg(inst->mArg1, instIdx, true);
										IntroduceVRegs(inst->mArg1, mcBlock, instIdx, instIdx + 2);
									}
								}

								if (!handled)
								{
									// From: result = <op> a, b
									// To: mov result, a
									//     <op> result, b

									AllocInst(BeMCInstKind_Mov, inst->mResult, inst->mArg0, instIdx++);
									inst->mArg0 = inst->mResult;
									inst->mResult = BeMCOperand();
									FixVRegInitFlags(inst, inst->mArg0);
									if (debugging)
										OutputDebugStrF(" Result\n");
									isFinalRun = false;
								}
							}
							continue;
						}
					}
				}

				if ((inst->mKind != BeMCInstKind_Cmp) &&
					(inst->mKind != BeMCInstKind_CmpToBool) &&
					(inst->mKind != BeMCInstKind_Mov))
				{
					if ((arg0Type != NULL) && (arg0Type->IsFloatOrVector()))
					{
						// <r/m>, <xmm> is not valid, <xmm>, <r/m>
						if (!arg0.IsNativeReg())
						{
							if ((arg1.IsNativeReg()) && (inst->mArg1.IsVReg()) && (nextInst != NULL) && (nextInst->mLiveness != NULL) && (inst->IsCommutable()))
							{
								int underlyingVRegIdx = GetUnderlyingVReg(inst->mArg1.mVRegIdx);
								if (!mLivenessContext.IsSet(nextInst->mLiveness, underlyingVRegIdx))
								{
									// If Arg1 is a temporary vreg, then we can use that as our destination so we don't need
									//  to create another temporary vreg
									BF_SWAP(inst->mArg0, inst->mArg1);
									AllocInst(BeMCInstKind_Mov, inst->mArg1, inst->mArg0, instIdx + 1);
									continue;
								}
							}

							BeMCOperand prevDest;
							auto prevDestPtr = inst->GetDest();
							if (prevDestPtr != NULL)
								prevDest = *prevDestPtr;

							ReplaceWithNewVReg(inst->mArg0, instIdx, true);
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Float Arg0\n");

							if (prevDest)
							{
								// This is a modifying instruction so we need to store the result afterward
								AllocInst(BeMCInstKind_Mov, prevDest, inst->mArg0, instIdx + 1);
								IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 2);
								instIdx++; // Skip this instruction until next pass
							}
							else
							{
								IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 1);
							}

							continue;
						}
					}
				}

				if ((!inst->IsMov()) && (arg0.IsVReg()) && (arg0Type->IsFloatOrVector()))
				{
					BF_ASSERT(!inst->mResult);
					// XMM instructions (besides MOVs) require native register destinations

					if ((inst->IsCommutable()) && (arg1.IsNativeReg()) && (inst->mArg1.IsVReg()))
					{
						auto nextInst = mcBlock->mInstructions[instIdx + 1];
						int underlyingVRegIdx = GetUnderlyingVReg(inst->mArg1.mVRegIdx);
						if (!mLivenessContext.IsSet(nextInst->mLiveness, underlyingVRegIdx))
						{
							// Convert a "Mul %vreg0, %vreg1<reg>"
							// To
							//   Mul %reg1<reg>, %vreg0
							//   Mov %vreg0, %reg1<reg>
							// This only works if %reg1 dies after this instruction and this is
							// cheaper than the more general case below
							AllocInst(BeMCInstKind_Mov, inst->mArg0, inst->mArg1, instIdx++ + 1);
							BF_SWAP(inst->mArg0, inst->mArg1);
							BF_SWAP(arg0, arg1);
							continue;
						}
					}

					// Convert from "<op> %reg0, %reg1"
					// To
					//   Mov %vreg2<reg>, %vreg0
					//   <op> %vreg2<reg>, %vreg1
					//   Mov %vreg0, %vreg2<reg>
					auto prevDest = inst->mArg0;
					ReplaceWithNewVReg(inst->mArg0, instIdx, true);
					AllocInst(BeMCInstKind_Mov, prevDest, inst->mArg0, instIdx + 1);
					IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 2);
					instIdx++;
					isFinalRun = false;
					if (debugging)
						OutputDebugStrF(" Float reg dest\n");
					continue;
				}
			}

			if (inst->mKind == BeMCInstKind_Call)
			{
				// Convert from
				//   Mov %reg0, <x>
				//   ..
				//   Call %reg0
				// To "Call <x>"
				// This is a common case for virtual dispatch where complex address expressions get actualized
				//  but then we end up with an 'extra' vreg
				if (inst->mArg0.IsVReg())
				{
					auto vregInfo = GetVRegInfo(inst->mArg0);
					if (vregInfo->mRefCount == 1)
					{
						for (int checkInstIdx = instIdx - 1; checkInstIdx >= 0; checkInstIdx--)
						{
							auto checkInst = mcBlock->mInstructions[checkInstIdx];
							if ((checkInst->mKind == BeMCInstKind_Mov) && (checkInst->mArg0 == inst->mArg0))
							{
								// We can't extend down any ForceRegs, those must be confined to very short intervals
								if (!HasForceRegs(checkInst->mArg1))
								{
									inst->mArg0 = checkInst->mArg1;
									RemoveInst(mcBlock, checkInstIdx);
									instIdx--;
									continue;
								}
							}
						}
					}
				}
			}

			switch (inst->mKind)
			{
			case BeMCInstKind_PreserveVolatiles:
				regPreserveDepth++;
				break;
			case BeMCInstKind_RestoreVolatiles:
				regPreserveDepth--;
				break;
			case BeMCInstKind_DefLoad:
				{
					SoftFail("DefLoad- DoLoads should have removed these");
				}
				break;
			case BeMCInstKind_Def:
				{
					auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
					// Legalize vreg exprs
					if ((vregInfo->mRefCount > 0) && (vregInfo->mRelTo) && (!vregInfo->IsDirectRelToAny()))
					{
						if (vregInfo->mRelTo.mKind == BeMCOperandKind_SymbolAddr)
							continue; // Just leave it
						if (vregInfo->mRelTo.mKind == BeMCOperandKind_Symbol)
						{
							continue;
						}

						if (vregInfo->IsDirectRelTo())
						{
							if (vregInfo->mRelTo.IsVRegAny())
							{
								// Propagate change to the directRel
								//auto itr = vregExprChangeSet.find(vregInfo->mRelTo.mVRegIdx);
								//if (itr != vregExprChangeSet.end())

								if (vregExprChangeSet.Contains(vregInfo->mRelTo.mVRegIdx))
								{
									BF_ASSERT(!isFinalRun);
									vregExprChangeSet.Add(inst->mArg0.mVRegIdx);
									isFinalRun = false;
								}
							}
						}
						else
						{
							if (vregInfo->mRelOffset)
							{
								auto relOfsType = GetType(vregInfo->mRelOffset);
								// We can only do offsets by int64s
								if (relOfsType->mTypeCode != BeTypeCode_Int64)
								{
									bool didExtend = false;
									if (vregInfo->mRelOffset.IsVReg())
									{
										// First we try to resize the original vreg, which we are allowed to do
										//  if there's only one mov and the vreg dies by the end of the block
										int relVRegIdx = vregInfo->mRelOffset.mVRegIdx;
										bool foundDef = false;
										BeMCInst* setInst = NULL;
										for (int checkIdx = 0; checkIdx < (int)mcBlock->mInstructions.size(); checkIdx++)
										{
											auto checkInst = mcBlock->mInstructions[checkIdx];
											if ((checkInst->mKind == BeMCInstKind_Def) && (checkInst->mArg0.mVRegIdx == relVRegIdx))
												foundDef = true;
											if (foundDef)
											{
												if ((checkInst->mKind == BeMCInstKind_Mov) && (checkInst->mArg0 == vregInfo->mRelOffset))
												{
													if (setInst != NULL)
														break; // Only one set is allowed
													setInst = checkInst;
												}
												else
												{
													// If we also use this reg for a non-offset reason then we can't just extend the size
													bool hasNonOffsetRef = false;
													auto operands = { &checkInst->mResult, &checkInst->mArg0, &checkInst->mArg1 };
													for (auto operand : operands)
														if (ContainsNonOffsetRef(*operand, vregInfo->mRelOffset))
															hasNonOffsetRef = true;
													if (hasNonOffsetRef)
													{
														break;
													}
												}

												if (!mLivenessContext.IsSet(checkInst->mLiveness, relVRegIdx))
												{
													if (setInst != NULL)
													{
														didExtend = true;
														auto relVRegInfo = mVRegInfo[relVRegIdx];
														setInst->mKind = BeMCInstKind_MovSX;
														relVRegInfo->mType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
														relVRegInfo->mAlign = relVRegInfo->mType->mAlign;
														if (debugging)
															OutputDebugStrF(" Def MovSX\n");
														isFinalRun = false;
													}
													break;
												}
											}
										}
									}

									if (!didExtend)
									{
										auto relOfs64 = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64), 2);
										CreateDefineVReg(relOfs64, instIdx);
										AllocInst(BeMCInstKind_MovSX, relOfs64, vregInfo->mRelOffset, instIdx + 1);
										IntroduceVRegs(inst->mArg0, mcBlock, instIdx, instIdx + 2);

										instIdx += 2;
										vregInfo->mRelOffset = relOfs64;
										vregExprChangeSet.Add(relOfs64.mVRegIdx);
										if (debugging)
											OutputDebugStrF(" Def MovSX 2\n");
										isFinalRun = false;
									}
								}
							}

							BeRMParamsInfo rmInfo;
							GetRMParams(inst->mArg0, rmInfo);
							bool isValid = rmInfo.mMode != BeMCRMMode_Invalid;

							if (!isValid)
							{
								if (rmInfo.mErrorVReg == -1)
								{
									BF_ASSERT(!vregInfo->mRelOffset);
									if (vregInfo->mType->IsPointer())
									{
										// This must be a cast like "(void*)gGlobalVar".  We need to actualize the symbol address
										//  in a native register now.
										vregExprChangeSet.Add(inst->mArg0.mVRegIdx);
										AllocInst(BeMCInstKind_Mov, inst->mArg0, vregInfo->mRelTo, instIdx++ + 1);
										vregInfo->mIsExpr = false;
										vregInfo->mRelTo = BeMCOperand();
										vregInfo->mForceReg = true;
										CheckForce(vregInfo);
										isFinalRun = false;
										if (debugging)
											OutputDebugStrF(" Symbol Addr\n");
										break;
									}
									else
									{
										// This could be a special case like
										//  MOV (int64)floatVal, reg64
										// For two-step loading of immediates into memory without using .rdata
										continue;
									}
								}
								else if (rmInfo.mErrorVReg == -2)
								{
									vregExprChangeSet.Add(inst->mArg0.mVRegIdx);
									MarkInvalidRMRegs(inst->mArg0);
									if (debugging)
										OutputDebugStrF(" GetRMParams invalid indices\n");
									isFinalRun = false;
									break;
								}

								//if (vregExprChangeSet.find(errorVRegIdx) != vregExprChangeSet.end())
								if (vregExprChangeSet.Contains(rmInfo.mErrorVReg))
								{
									// This means we have already modified some dependent vregs, so we may be legalized already.
									//  Wait till next iteration to determine that.
									BF_ASSERT(!isFinalRun);
									isValid = true; //
								}
							}

							// The only valid form is [<reg>*<1/2/4/8>+<imm>]
							//  If we violate that then we have to break it up
							//if ((vregInfo->mRelOffsetScale != 1) && (vregInfo->mRelOffsetScale != 2) && (vregInfo->mRelOffsetScale != 4) && (vregInfo->mRelOffsetScale != 8))
							if ((!isValid) && (rmInfo.mErrorVReg == inst->mArg0.mVRegIdx))
							{
								vregExprChangeSet.Add(rmInfo.mErrorVReg);
								if ((vregInfo->mRelOffsetScale != 1) && (vregInfo->mRelOffsetScale != 2) && (vregInfo->mRelOffsetScale != 4) && (vregInfo->mRelOffsetScale != 8))
								{
									auto relOffsetType = GetType(vregInfo->mRelOffset);
									BeMCOperand scratchReg = AllocVirtualReg(relOffsetType, 2, true);
									CreateDefineVReg(scratchReg, instIdx++);
									auto newInst = AllocInst(BeMCInstKind_IMul, vregInfo->mRelOffset, BeMCOperand::FromImmediate(vregInfo->mRelOffsetScale), instIdx++);
									newInst->mResult = scratchReg;
									vregInfo->mRelOffset = scratchReg;
									vregInfo->mRelOffsetScale = 1;
									if (debugging)
										OutputDebugStrF(" Invalid RM combo\n");

									vregExprChangeSet.Add(scratchReg.mVRegIdx);
								}
								else if (!vregInfo->mRelTo.IsVRegAny())
								{
									vregInfo->mWantsExprActualize = true;
									hasPendingActualizations = true;
								}
								isFinalRun = false;
							}
							else if (!isValid)
							{
								auto errorVRegInfo = mVRegInfo[rmInfo.mErrorVReg];

								if ((errorVRegInfo->mIsExpr) && (!errorVRegInfo->IsDirectRelTo()))
								{
									errorVRegInfo->mWantsExprActualize = true;
									hasPendingActualizations = true;
									vregExprChangeSet.Add(rmInfo.mErrorVReg);
									isFinalRun = false;
									if (debugging)
										OutputDebugStrF(" RM not valid, actualize\n");
								}
								else
								{
									// We don't want to have too many concurrent ForceReg vregs at once, since that causes too much register pressure and
									//  can cause register allocation to fail at the extreme end.  The scratchReg adds another ForceReg for the lifetime
									//  of the def vreg, so if the def vreg doesn't immediately die and there are already too many ForceRegs active then
									//  we need to actualize ourselves
									bool actualizeSelf = false;
									if (instIdx < mcBlock->mInstructions.size() - 2)
									{
										auto checkInst = mcBlock->mInstructions[instIdx + 2];
										if (mLivenessContext.IsSet(checkInst->mLiveness, inst->mArg0.mVRegIdx))
										{
											actualizeSelf = true;
										}
									}
									else
									{
										actualizeSelf = true;
									}

									if (actualizeSelf)
									{
										auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
										// DirectRel cannot actualize
										if (vregInfo->IsDirectRelTo())
											actualizeSelf = false;
									}

									if (actualizeSelf)
									{
										auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
										vregInfo->mWantsExprActualize = true;
										hasPendingActualizations = true;
									}
									else
									{
										// This may be a local variable that failed to be assigned to a reg, create a scratch local with a forced reg
										auto errorVReg = BeMCOperand::FromVReg(rmInfo.mErrorVReg);
										auto errorVRegLoad = BeMCOperand::ToLoad(errorVReg);

										if ((vregInfo->mRelTo == errorVReg) || (vregInfo->mRelOffset == errorVReg))
										{
											auto scratchReg = AllocVirtualReg(errorVRegInfo->mType, 2, false);
											auto scratchVRegInfo = mVRegInfo[scratchReg.mVRegIdx];

											CreateDefineVReg(scratchReg, instIdx++);
											AllocInst(BeMCInstKind_Mov, scratchReg, errorVReg, instIdx++);
											isFinalRun = false;
											if (debugging)
												OutputDebugStrF(" RM failed, scratch vreg\n");
											vregExprChangeSet.Add(scratchReg.mVRegIdx);

											if (vregInfo->mRelTo == errorVReg)
											{
												scratchVRegInfo->mForceReg = true;
												CheckForce(scratchVRegInfo);
												vregInfo->mRelTo.mVRegIdx = scratchReg.mVRegIdx;
											}
											else if (vregInfo->mRelOffset == errorVReg)
											{
												scratchVRegInfo->mForceReg = true;
												CheckForce(scratchVRegInfo);
												vregInfo->mRelOffset.mVRegIdx = scratchReg.mVRegIdx;
											}
										}
										else if ((vregInfo->mRelTo == errorVRegLoad) || (vregInfo->mRelOffset == errorVRegLoad))
										{
											auto scratchType = GetType(errorVRegLoad);
											auto scratchReg = AllocVirtualReg(scratchType, 2, false);
											auto scratchVRegInfo = mVRegInfo[scratchReg.mVRegIdx];

											CreateDefineVReg(scratchReg, instIdx++);
											AllocInst(BeMCInstKind_Mov, scratchReg, errorVRegLoad, instIdx++);
											isFinalRun = false;
											if (debugging)
												OutputDebugStrF(" RM failed, scratch vreg\n");
											vregExprChangeSet.Add(scratchReg.mVRegIdx);

											if (vregInfo->mRelTo == errorVRegLoad)
											{
												scratchVRegInfo->mForceReg = true;
												CheckForce(scratchVRegInfo);
												vregInfo->mRelTo = scratchReg;
											}
											else if (vregInfo->mRelOffset == errorVRegLoad)
											{
												scratchVRegInfo->mForceReg = true;
												CheckForce(scratchVRegInfo);
												vregInfo->mRelOffset = scratchReg;
											}
										}
										else
										{
											// This should be impossible - a previous def for an inner expr should have caught this case
											if ((vregExprChangeSet.IsEmpty()) && (!hasPendingActualizations))
											{
												SoftFail("Error legalizing vreg expression", NULL);
											}
										}
									}
								}
							}
						}
					}

					if ((vregInfo->mDbgVariable != NULL) && (vregInfo->mDbgVariable->mPendingInitType != BfIRInitType_NotNeeded))
						pendingInitKind = vregInfo->mDbgVariable->mPendingInitType;
				}
				break;
			case BeMCInstKind_DbgDecl:
				{
					bool isInvalid = false;

					int vregIdx = inst->mArg0.mVRegIdx;
					auto vregInfo = mVRegInfo[vregIdx];
					auto dbgVar = vregInfo->mDbgVariable;

					BeRMParamsInfo rmInfo;

					if (dbgVar->mIsValue)
						GetRMParams(inst->mArg0, rmInfo);
					else
					{
						if (inst->mArg0.mKind == BeMCOperandKind_VRegAddr)
							GetRMParams(BeMCOperand::ToLoad(inst->mArg0), rmInfo);
						else
						{
							GetRMParams(inst->mArg0, rmInfo);
							if ((rmInfo.mMode != BeMCRMMode_Direct) && (rmInfo.mBScale != 1) && (rmInfo.mDisp != 0))
								isInvalid = true;
						}
					}

					if (rmInfo.mMode == BeMCRMMode_Invalid)
					{
						if (vregInfo->mType->mSize != 0)
						{
							isInvalid = true;
						}
					}
					else if (rmInfo.mMode == BeMCRMMode_Direct)
					{
						if ((rmInfo.mRegB != X64Reg_None) || (rmInfo.mDisp != 0) || (rmInfo.mBScale != 1))
						{
							isInvalid = true;
						}
					}
					else if (rmInfo.mMode == BeMCRMMode_Deref)
					{
						if ((rmInfo.mRegB != X64Reg_None) || (rmInfo.mBScale != 1))
						{
							isInvalid = true;
						}
					}

					if (isInvalid)
					{
						isFinalRun = false;
						/*ReplaceWithNewVReg(inst->mArg0, instIdx, true, false);

						// The debug value is not representable, so copy it.  This can only occur within mixins where we are pointing
						//  to composed values
						BF_ASSERT(inst->mArg0.mKind == BeMCOperandKind_VReg);
						inst->mArg0.mKind = BeMCOperandKind_VRegAddr;
						dbgVar->mIsValue = false;
						auto newVRegInfo = mVRegInfo[inst->mArg0.mVRegIdx];
						vregInfo->mDbgVariable = NULL;
						newVRegInfo->mDbgVariable = dbgVar;*/

						// The debug value is not representable
						inst->mArg0.mKind = BeMCOperandKind_VReg;
						vregInfo->mWantsExprActualize = true;
						hasPendingActualizations = true;
						vregExprChangeSet.Add(inst->mArg0.mVRegIdx);
						isFinalRun = false;

						break;
					}

					if (!mVRegInitializedContext.IsSet(inst->mVRegsInitialized, vregIdx))
					{
						if ((dbgVar->mPendingInitType != BfIRInitType_NotNeeded) && (dbgVar->mPendingInitType != BfIRInitType_NotNeeded_AliveOnDecl))
						{
							Fail("Shouldn't happen here anymore");
							/*pendingInitKind = dbgVar->mPendingInitType;
							// We don't need dynLife anymore since we're explicitly writing this
							vregInfo->mHasDynLife = false;
							AllocInst(BeMCInstKind_Def, inst->mArg0, instIdx);

							// We don't definitively need this, it's just to make sure this doesn't cause a liveness error
							isFinalRun = false;*/
						}
					}
				}
				break;
			case BeMCInstKind_MemCpy:
				{
					if (inst->mArg1)
					{
						BF_ASSERT(inst->mArg1.mKind == BeMCOperandKind_VRegPair);

						int* vregIndices[] = { &inst->mArg1.mVRegPair.mVRegIdx0, &inst->mArg1.mVRegPair.mVRegIdx1 };
						for (int* argIdxPtr : vregIndices)
						{
							auto mcArg = BeMCOperand::FromEncoded(*argIdxPtr);
							BeRMParamsInfo rmInfo;
							GetRMParams(mcArg, rmInfo);
							if ((rmInfo.mMode != BeMCRMMode_Direct) || (rmInfo.mRegB != X64Reg_None) || (rmInfo.mRegA == X64Reg_R11))
							{
								BeMCOperand scratchReg = AllocVirtualReg(GetType(mcArg), 2, true);
								auto vregInfo = GetVRegInfo(scratchReg);
								vregInfo->mDisableR11 = true;
								int insertPos = FindSafeInstInsertPos(instIdx);
								CreateDefineVReg(scratchReg, insertPos);
								AllocInst(BeMCInstKind_Mov, scratchReg, mcArg, insertPos + 1);
								*argIdxPtr = scratchReg.mVRegIdx;
								instIdx += 2;
								isFinalRun = false;
								if (debugging)
									OutputDebugStrF(" MemCpy\n");
							}
						}
					}
				}
				break;
			case BeMCInstKind_MemSet:
				{
					if (inst->mArg1)
					{
						BeRMParamsInfo rmInfo;
						GetRMParams(inst->mArg1, rmInfo);
						if ((rmInfo.mMode != BeMCRMMode_Direct) || (rmInfo.mRegB != X64Reg_None) || (rmInfo.mRegA == X64Reg_R11) || (rmInfo.mDisp != 0))
						{
							BeMCOperand scratchReg = AllocVirtualReg(GetType(inst->mArg1), 2, true);
							auto vregInfo = GetVRegInfo(scratchReg);
							vregInfo->mDisableR11 = true;
							int insertPos = FindSafeInstInsertPos(instIdx);
							CreateDefineVReg(scratchReg, insertPos);
							AllocInst(BeMCInstKind_Mov, scratchReg, inst->mArg1, insertPos + 1);
							inst->mArg1 = scratchReg;
							instIdx += 2;
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" MemSet\n");
						}
					}
				}
				break;
			case BeMCInstKind_Neg:
				{
					if (arg0Type->IsFloat())
					{
						inst->mKind = BeMCInstKind_Xor;
						if (arg0Type->mTypeCode == BeTypeCode_Float)
						{
							inst->mArg1.mKind = BeMCOperandKind_Immediate_f32_Packed128;
							inst->mArg1.mImmF32 = -0.0;
						}
						else
						{
							inst->mArg1.mKind = BeMCOperandKind_Immediate_f64_Packed128;
							inst->mArg1.mImmF64 = -0.0;
						}
					}
				}
				break;
			case BeMCInstKind_Div:
			case BeMCInstKind_IDiv:
			case BeMCInstKind_Rem:
			case BeMCInstKind_IRem:
				{
					// Unsigned div and rem can be implemented with bitwise operations
					//  Negative values cannot (at least following C standards, because of rounding issues)
					if ((inst->mKind == BeMCInstKind_Div) || (inst->mKind == BeMCInstKind_Rem))
					{
						if ((!inst->mResult) && (inst->mArg1.IsImmediateInt()) && (IsPowerOfTwo(inst->mArg1.mImmediate)))
						{
							if (inst->mKind == BeMCInstKind_Div)
							{
								int64 divVal = inst->mArg1.mImmediate;
								int shiftCount = 0;
								while (divVal > 1)
								{
									shiftCount++;
									divVal >>= 1;
								}

								inst->mKind = BeMCInstKind_Shr;
								inst->mArg1 = BeMCOperand::FromImmediate(shiftCount);
								isFinalRun = false;
								if (debugging)
									OutputDebugStrF(" Div SHR\n");
								break;
							}
							else if (inst->mKind == BeMCInstKind_Rem)
							{
								inst->mKind = BeMCInstKind_And;
								inst->mArg1 = BeMCOperand::FromImmediate(inst->mArg1.mImmediate - 1);
								isFinalRun = false;
								if (debugging)
									OutputDebugStrF(" Div REM\n");
								break;
							}
						}
					}

					if (arg1.IsImmediate())
					{
						// Oops, must be 'rm'
						ReplaceWithNewVReg(inst->mArg1, instIdx, true, false);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Div/Rem not RM\n");
					}

					auto arg0Type = GetType(arg0);
					if (arg0Type->IsInt())
					{
						// We can't allow division by RDX because we need RAX:RDX for the dividend
						bool needRegDisable = false;
						std::function<void(BeMCOperand)> _CheckReg = [&](BeMCOperand operand)
						{
							if (!operand)
								return;

							if (operand.mKind == BeMCOperandKind_NativeReg)
							{
								auto divisorReg = GetFullRegister(operand.mReg);
								if ((divisorReg == X64Reg_RDX) || (divisorReg == X64Reg_RAX))
									needRegDisable = true;
							}

							auto vregInfo = GetVRegInfo(operand);
							if (vregInfo != NULL)
							{
								auto divisorReg = GetFullRegister(vregInfo->mReg);
								if ((divisorReg == X64Reg_RDX) || (divisorReg == X64Reg_RAX))
									needRegDisable = true;

								_CheckReg(vregInfo->mRelTo);
								_CheckReg(vregInfo->mRelOffset);

								if ((needRegDisable) &&
									((!vregInfo->mDisableRAX) || (!vregInfo->mDisableRDX)))
								{
									vregInfo->mDisableRAX = true;
									vregInfo->mDisableRDX = true;
									isFinalRun = false;
									if (debugging)
										OutputDebugStrF(" Div/Rem invalid reg\n");
								}
							}
						};

						needRegDisable = false;
						_CheckReg(arg1);
						if ((instIdx > 0) && (regPreserveDepth > 0))
						{
							auto prevInst = mcBlock->mInstructions[instIdx - 1];
							if (prevInst->mKind == BeMCInstKind_Mov)
							{
								// Check replaced 'mov' (which is inside PreserveVolatile sections)
								needRegDisable = false;
								_CheckReg(prevInst->mArg1);
							}
						}

						///

						auto checkArg1 = arg1;
						if (checkArg1.mKind == BeMCOperandKind_VRegLoad)
						{
							// Handle '*vreg<RAX>' case
							checkArg1.mKind = BeMCOperandKind_VReg;
							checkArg1 = GetFixedOperand(checkArg1);
						}

						if (checkArg1.mKind == BeMCOperandKind_NativeReg)
						{
							// We can't allow division by RDX because we need RAX:RDX for the dividend
							auto divisorReg = GetFullRegister(checkArg1.mReg);
							if ((checkArg1.IsNativeReg()) &&
								((divisorReg == X64Reg_RDX) || (divisorReg == X64Reg_RAX)))
							{
								BF_ASSERT(inst->mArg1.IsVRegAny());
								int vregIdx = GetUnderlyingVReg(inst->mArg1.mVRegIdx);
								auto vregInfo = mVRegInfo[vregIdx];
								if (vregInfo != NULL)
								{
									vregInfo->mDisableRAX = true;
									vregInfo->mDisableRDX = true;
									isFinalRun = false;
									if (debugging)
										OutputDebugStrF(" Div/Rem invalid reg\n");
								}
							}
						}
						else
						{
							auto vregInfo = GetVRegInfo(arg1);
							if ((vregInfo != NULL) && (vregInfo->mIsExpr))
							{
								ReplaceWithNewVReg(inst->mArg1, instIdx, true);
							}
						}

						// DIV/IDIV can only operate on the RDX:RAX pair, except for i8 divide which just uses AX
						bool isRegADividend = (arg0.mReg == X64Reg_RAX) || (arg0.mReg == X64Reg_EAX) || (arg0.mReg == X64Reg_AX) || (arg0.mReg == X64Reg_AL);
						if ((!arg0.IsNativeReg()) || (!isRegADividend) ||
							(inst->mKind == BeMCInstKind_Rem) || (inst->mKind == BeMCInstKind_IRem))
						{
							bool preserveRDX = (arg0Type->mSize != 1) && (regPreserveDepth == 0);
							auto mcScratch = BeMCOperand::FromReg(ResizeRegister(X64Reg_RAX, arg0Type));
							BF_ASSERT(!inst->mResult);
							BeMCInst* preserveRAXInst = AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++);
							BeMCInst* preserveRDXInst = NULL;
							if (preserveRDX)
								preserveRDXInst = AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++);
							AllocInst(BeMCInstKind_Mov, mcScratch, inst->mArg0, instIdx++);
							if ((inst->mKind == BeMCInstKind_Rem) || (inst->mKind == BeMCInstKind_IRem))
							{
								if (inst->mKind == BeMCInstKind_Rem)
									inst->mKind = BeMCInstKind_Div;
								else
									inst->mKind = BeMCInstKind_IDiv;
								BeMCOperand mcRemaindier;
								if (arg0Type->mSize == 1)
								{
									mcRemaindier = BeMCOperand::FromReg(ResizeRegister(X64Reg_AL, arg0Type));
									preserveRAXInst->mArg1 = inst->mArg0; // RAX preserve elision exception
									DisableRegister(inst->mArg0, X64Reg_SIL); // Disable Hi8
									DisableRegister(inst->mArg0, X64Reg_RAX);
									AllocInst(BeMCInstKind_Shr, BeMCOperand::FromReg(X64Reg_AX), BeMCOperand::FromImmediate(8), instIdx++ + 1);
									AllocInst(BeMCInstKind_Mov, inst->mArg0, mcRemaindier, instIdx++ + 1);
								}
								else
								{
									mcRemaindier = BeMCOperand::FromReg(ResizeRegister(X64Reg_RDX, arg0Type));
									preserveRDXInst->mArg1 = inst->mArg0; // RDX preserve elision exception

									// This is to avoid overlap with PreserveRAX
									DisableRegister(inst->mArg0, X64Reg_RAX);
									if (preserveRDX)
										DisableRegister(inst->mArg0, X64Reg_RDX);
									AllocInst(BeMCInstKind_Mov, inst->mArg0, mcRemaindier, instIdx++ + 1);
								}
							}
							else
							{
								preserveRAXInst->mArg1 = inst->mArg0; // RAX preserve elision exception
								AllocInst(BeMCInstKind_Mov, inst->mArg0, mcScratch, instIdx++ + 1);

								// This is to avoid overlap with PreserveRAX
								DisableRegister(inst->mArg0, X64Reg_RAX);
								if (preserveRDX)
									DisableRegister(inst->mArg0, X64Reg_RDX);
							}
							inst->mArg0 = mcScratch;
							if (preserveRDX)
								AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++ + 1);
							AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++ + 1);

							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Div/Rem Setup\n");
						}
						else
						{
							if (regPreserveDepth == 0)
							{
								if (arg0Type->mSize != 1)
								{
									AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++);
									AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++ + 1);
									isFinalRun = false; // Reassign regs
									if (debugging)
										OutputDebugStrF(" Div/Rem Preserve\n");
								}
							}
						}
					}
					else if (inst->mKind == BeMCInstKind_IRem)
					{
						SetAndRestoreValue<int*> insertPtr(mInsertInstIdxRef, &instIdx);

						SizedArray<BeMCOperand, 3> args = { inst->mArg0, inst->mArg1 };
						auto mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef((arg0Type->mTypeCode == BeTypeCode_Double) ? "fmod" : "fmodf")->mIdx);
						auto fmodVal = CreateCall(mcFunc, args, arg0Type);

						inst->mKind = BeMCInstKind_Mov;
						inst->mResult = BeMCOperand();
						inst->mArg1 = fmodVal;

						isFinalRun = false;

						if (debugging)
							OutputDebugStrF(" FMod\n");
					}
				}
				break;
			case BeMCInstKind_Mul:
			case BeMCInstKind_IMul:
				{
					bool handled = false;

					if ((arg0Type->mSize == 1) && (arg0Type->IsIntable()))
					{
						if ((!arg0.IsNativeReg()) || (arg0.mReg != X64Reg_AL) || (inst->mResult))
						{
							auto srcVRegInfo = GetVRegInfo(inst->mArg0);
							// Int8 multiplies can only be done on AL
							AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++);

							DisableRegister(inst->mArg0, X64Reg_RAX);
							DisableRegister(inst->mArg1, X64Reg_RAX);

							AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_AL), inst->mArg1, instIdx++);
							AllocInst(BeMCInstKind_Shl, BeMCOperand::FromReg(X64Reg_AX), BeMCOperand::FromImmediate(8), instIdx++);
							AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_AL), inst->mArg0, instIdx++);
							AllocInst(BeMCInstKind_Mov, inst->mResult ? inst->mResult : inst->mArg0, BeMCOperand::FromReg(X64Reg_AL), instIdx++ + 1);
							inst->mArg0 = BeMCOperand::FromReg(X64Reg_AL);
							inst->mArg1 = BeMCOperand::FromReg(X64Reg_AH);
							inst->mResult = BeMCOperand();
							AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++ + 1);

							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Mul Size 1\n");
							break;
						}

						if (inst->mArg1.IsImmediateInt())
						{
							ReplaceWithNewVReg(inst->mArg1, instIdx, true, false);
						}

						BF_ASSERT(!inst->mResult);
						handled = true;
					}
					else if ((inst->mKind == BeMCInstKind_Mul) && (arg0Type->IsIntable()))
					{
						auto wantReg0 = ResizeRegister(X64Reg_RAX, arg0Type->mSize);

						if ((!arg0.IsNativeReg()) || (arg0.mReg != wantReg0) || (inst->mResult))
						{
							auto srcVRegInfo = GetVRegInfo(inst->mArg0);
							// unsigned multiplies can only be done on AX/EAX/RAX
							AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++);
							AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++);
							DisableRegister(inst->mArg0, X64Reg_RAX);
							DisableRegister(inst->mArg0, X64Reg_RDX);
							DisableRegister(inst->mArg1, X64Reg_RAX);
							DisableRegister(inst->mArg1, X64Reg_RDX);
							AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(wantReg0), inst->mArg0, instIdx++);
							AllocInst(BeMCInstKind_Mov, inst->mResult ? inst->mResult : inst->mArg0, BeMCOperand::FromReg(wantReg0), instIdx++ + 1);
							inst->mArg0 = BeMCOperand::FromReg(wantReg0);
							inst->mResult = BeMCOperand();
							AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RDX), instIdx++ + 1);
							AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX), instIdx++ + 1);

							isFinalRun = false;
							break;
						}

						if (inst->mArg1.IsImmediateInt())
						{
							ReplaceWithNewVReg(inst->mArg1, instIdx, true, false);
						}

						BF_ASSERT(!inst->mResult);
						handled = true;
					}

					if (inst->mArg0.IsNativeReg())
					{
						auto vregInfo1 = GetVRegInfo(inst->mArg1);
						if (vregInfo1 != NULL)
						{
							auto arg1 = GetFixedOperand(inst->mArg1);
							if ((arg1.IsNativeReg()) && (inst->mArg0.mReg == arg1.mReg) &&
								((ResizeRegister(arg1.mReg, 8) == X64Reg_RAX) || (ResizeRegister(arg1.mReg, 8) == X64Reg_RDX)))
							{
								DisableRegister(inst->mArg1, X64Reg_RAX);
								DisableRegister(inst->mArg1, X64Reg_RDX);
								isFinalRun = false;
							}
						}
					}

					if (!handled)
					{
						if (inst->mResult)
						{
							// The 3-op form of MUL must be in "reg, r/m64, imm" form
							if (!inst->mArg1.IsImmediateInt())
							{
								SoftFail("Not supported");
								//isFinalRun = false;
								break;
							}
						}

						// Convert from %vreg0 = mul %vreg1, %vreg2
						// To: %scratch = mul %vreg1, %vreg2
						//     mov %reg0, %scratch
						if (inst->mResult)
						{
							bool isLegal = true;
							int64 immediateInt = inst->mArg1.GetImmediateInt();

							// 							if (inst->mKind == BeMCInstKind_Mul)
							// 							{
							// 								if (immediateInt > 0xFFFFFFFFLL)
							// 									isLegal = false;
							// 							}
							// 							else
							// 							{
							// 								if ((immediateInt < -0x80000000LL) || (immediateInt > 0x7FFFFFFF))
							// 									isLegal = false;
							// 							}

							if (!GetFixedOperand(inst->mResult).IsNativeReg())
							{
								BeMCOperand scratchReg = AllocVirtualReg(GetType(inst->mResult), 2, true);
								CreateDefineVReg(scratchReg, instIdx);
								AllocInst(BeMCInstKind_Mov, inst->mResult, scratchReg, instIdx + 2);
								inst->mResult = scratchReg;
								if (debugging)
									OutputDebugStrF(" Mul 3-form not reg\n");

								isFinalRun = false;
								instIdx += 2;
								break;
							}
						}
						else
						{
							if (!GetFixedOperand(inst->mArg0).IsNativeReg())
							{
								auto prevArg0 = inst->mArg0;
								ReplaceWithNewVReg(inst->mArg0, instIdx, true);
								AllocInst(BeMCInstKind_Mov, prevArg0, inst->mArg0, instIdx + 1);
								isFinalRun = false;
								if (debugging)
									OutputDebugStrF(" Mul not reg\n");
								instIdx++;
								break;
							}
						}
					}
				}
				// Fallthrough
			case BeMCInstKind_Add:
			case BeMCInstKind_Sub:
				{
				}
				break;
			case BeMCInstKind_Shl:
			case BeMCInstKind_Shr:
			case BeMCInstKind_Sar:
				{
					if ((!inst->mArg1.IsNativeReg()) && (!inst->mArg1.IsImmediateInt()))
					{
						auto mcShift = inst->mArg1;
						auto shiftType = GetType(mcShift);
						BF_ASSERT(shiftType->IsInt());
						if (shiftType->mSize != 1)
						{
							auto mcShift8 = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int8), 2);
							CreateDefineVReg(mcShift8, instIdx++);
							auto vregInfo = GetVRegInfo(mcShift8);
							vregInfo->mIsExpr = true;
							vregInfo->mRelTo = mcShift;
							mcShift = mcShift8;
						}

						// The only non-constant shift is by 'CL'
						AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RCX), instIdx++);
						AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_CL), mcShift, instIdx++);
						AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RCX), instIdx++ + 1);
						inst->mArg1 = BeMCOperand::FromReg(X64Reg_RCX);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Shift by CL\n");
						continue;
					}

					if (inst->mArg1.IsNativeReg())
					{
						BF_ASSERT(inst->mArg1.mReg == X64Reg_RCX);

						X64CPURegister regs[2] = { X64Reg_None, X64Reg_None };
						GetUsedRegs(inst->mArg0, regs[0], regs[1]);
						if ((regs[0] == X64Reg_RCX) || (regs[1] == X64Reg_RCX))
						{
							// Bad reg!
							auto mcTarget = inst->mArg0;

							int insertIdx = instIdx - 1;
							ReplaceWithNewVReg(inst->mArg0, insertIdx, true);

							BF_ASSERT(mcBlock->mInstructions[instIdx + 3]->mKind == BeMCInstKind_RestoreVolatiles);
							AllocInst(BeMCInstKind_Mov, mcTarget, inst->mArg0, instIdx + 4);
						}
					}
				}
				break;
			case BeMCInstKind_Cmp:
				{
					bool needSwap = false;

					// Cmp <imm>, <r/m> is not legal, so we need to swap LHS/RHS, which means also modifying the instruction that uses the result of the cmp
					if (inst->mArg0.IsImmediate())
						needSwap = true;

					if (arg0Type->IsFloat())
					{
						// Cmp <r/m>, <xmm> is not valid, only Cmp <xmm>, <r/m>
						if ((!arg0.IsNativeReg()) && (arg1.IsNativeReg()))
						{
							needSwap = true;
						}
						else
						{
							if (!arg0.IsNativeReg())
							{
								SoftFail("xmm reg required");
							}
						}
					}

					if (needSwap)
					{
						bool hasConstResult = false;
						int constResult = 0;
						if (inst->mArg1.IsImmediate())
						{
							//constResult = inst->mArg0.mImmediate - inst->mArg1.mImmediate;
							// Should have been handled in the frontend
							ReplaceWithNewVReg(inst->mArg0, instIdx, true);
							break;
						}

						int checkInstIdx = instIdx + 1;
						auto checkBlock = mcBlock;

						for (; checkInstIdx < (int)checkBlock->mInstructions.size(); checkInstIdx++)
						{
							auto checkInst = checkBlock->mInstructions[checkInstIdx];
							if ((checkInst->mKind == BeMCInstKind_CondBr) || (checkInst->mKind == BeMCInstKind_CmpToBool))
							{
								if (constResult)
								{
									NotImpl();
									//mcBlock->RemoveInst(instIdx);
									//instIdx--;
								}
								else
								{
									BeMCOperand& cmpArg = (checkInst->mKind == BeMCInstKind_CondBr) ? checkInst->mArg1 : checkInst->mArg0;

									BF_SWAP(inst->mArg0, inst->mArg1);
									BF_ASSERT(cmpArg.mKind == BeMCOperandKind_CmpKind);
									cmpArg.mCmpKind = BeModule::SwapCmpSides(cmpArg.mCmpKind);
								}
								break;
							}
							else if (checkInst->mKind == BeMCInstKind_Br)
							{
								// Sometimes the matching use is in another block
								FindTarget(checkInst->mArg0, checkBlock, checkInstIdx);
							}
							else if (
								(checkInst->mKind != BeMCInstKind_Def) &&
								(checkInst->mKind != BeMCInstKind_DbgDecl) &&
								(checkInst->mKind != BeMCInstKind_ValueScopeSoftEnd) &&
								(checkInst->mKind != BeMCInstKind_ValueScopeHardEnd) &&
								(checkInst->mKind != BeMCInstKind_LifetimeEnd))
							{
								SoftFail("Malformed");
							}
						}
						break;
					}
				}
				break;
			case BeMCInstKind_CmpToBool:
				{
					if (inst->mResult.IsVRegAny())
					{
						if (!mVRegInitializedContext.IsSet(mCurVRegsInit, inst->mResult.mVRegIdx))
						{
							SizedArray<int, 1> addVec = { inst->mResult.mVRegIdx };
							mCurVRegsInit = mVRegInitializedContext.Add(mCurVRegsInit, addVec, true);

							BF_ASSERT(instIdx < (int)mcBlock->mInstructions.size() - 1);
							if (instIdx < (int)mcBlock->mInstructions.size() - 1)
							{
								auto nextInst = mcBlock->mInstructions[instIdx + 1];
								if (nextInst->mVRegsInitialized != NULL)
								{
									auto fixedVRegsInit = mVRegInitializedContext.RemoveChange(nextInst->mVRegsInitialized, inst->mResult.mVRegIdx);
									ReplaceVRegsInit(mcBlock, instIdx + 1, nextInst->mVRegsInitialized, fixedVRegsInit);
								}
							}
						}
					}

					// Do this with a CMOV instead?
					auto result = inst->mResult;
					inst->mResult.mKind = BeMCOperandKind_None;
					inst->mKind = BeMCInstKind_CondBr;
					inst->mArg1 = inst->mArg0; // Move cmpType
					inst->mArg0 = BeMCOperand::FromLabel(mCurLabelIdx);

					int insertInstIdx = instIdx + 1;
					//auto mcInst = AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(mCurLabelIdx), BeMCOperand::FromCmpKind(BeCmpKind_EQ), insertInstIdx++);
					auto mcInst = AllocInst(BeMCInstKind_Mov, result, BeMCOperand::FromImmediate(0), insertInstIdx++);
					mcInst = AllocInst(BeMCInstKind_Br, BeMCOperand::FromLabel(mCurLabelIdx + 1), insertInstIdx++);
					CreateLabel(insertInstIdx++);
					mcInst = AllocInst(BeMCInstKind_Mov, result, BeMCOperand::FromImmediate(1), insertInstIdx++);
					CreateLabel(insertInstIdx++);
				}
				break;
			case BeMCInstKind_MovSX:
			case BeMCInstKind_Mov:
				{
					bool isSignedExt = inst->mKind == BeMCInstKind_MovSX;

					auto arg0Type = GetType(inst->mArg0);
					auto arg1Type = GetType(inst->mArg1);

					if (inst->mArg1.mKind == BeMCOperandKind_VRegAddr)
					{
						auto vregInfo = mVRegInfo[inst->mArg1.mVRegIdx];
						if ((!vregInfo->mIsExpr) && (!vregInfo->mForceMem))
						{
							SoftFail("VRegAddr used without ForceMem");
						}
					}

					if (arg0Type->mSize == 0)
					{
						RemoveInst(mcBlock, instIdx);
						instIdx--;
						continue;
					}

					if (arg0Type->IsNonVectorComposite())
					{
						if (arg1.mKind == BeMCOperandKind_Immediate_i64)
						{
							// This is just a "zero initializer" marker
							BF_ASSERT(arg1.mImmediate == 0);
							SetAndRestoreValue<int*> insertPtr(mInsertInstIdxRef, &instIdx);
							RemoveInst(mcBlock, instIdx);
							CreateMemSet(OperandToAddr(arg0), 0, arg0Type->mSize, arg0Type->mAlign);
							instIdx--;
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Zero init\n");
							break;
						}
						else if (arg1.mKind == BeMCOperandKind_ConstAgg)
						{
							bool needsSaveRegs = true;

							if (instIdx > 0)
							{
								auto prevInst = mcBlock->mInstructions[instIdx - 1];
								if ((prevInst->mKind == BeMCInstKind_PreserveVolatiles) /*&& (prevInst->mArg0.mReg == X64Reg_R11)*/)
									needsSaveRegs = false;
							}

							auto argType = GetType(arg1);
							bool useRep = argType->mSize >= BF_REP_MOV_LIMIT;

							int movInstIdx = instIdx;
							if (needsSaveRegs)
							{
								AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_R11), BeMCOperand(), instIdx);
								instIdx += 1;

								if (useRep)
								{
									AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand(), instIdx);
									instIdx += 1;
									AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RDI), BeMCOperand(), instIdx);
									instIdx += 1;
									AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RCX), BeMCOperand(), instIdx);
									instIdx += 1;
								}

								movInstIdx = instIdx;

								if (useRep)
								{
									AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RCX), BeMCOperand(), instIdx + 1);
									instIdx += 1;
									AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RDI), BeMCOperand(), instIdx + 1);
									instIdx += 1;
									AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand(), instIdx + 1);
									instIdx += 1;
								}

								AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_R11), BeMCOperand(), instIdx + 1);
								instIdx += 1;
								isFinalRun = false;
							}

							BeRMParamsInfo rmInfo;
							GetRMParams(inst->mArg0, rmInfo);
							if (rmInfo.mMode == BeMCRMMode_Invalid)
							{
								if (inst->mArg0.IsSymbol())
								{
									// Just make it an addr so we can replace it with a temporary
									inst->mArg0.mKind = BeMCOperandKind_SymbolAddr;
								}

								ReplaceWithNewVReg(inst->mArg0, movInstIdx, true, false, true);
								auto vregInfo = GetVRegInfo(inst->mArg0);
								vregInfo->mDisableR11 = true;
								instIdx += 2;

								isFinalRun = false;
							}
							continue;
						}
						else // Struct = Struct
						{
							auto arg0Addr = OperandToAddr(arg0);
							auto arg1Addr = OperandToAddr(arg1);
							SetAndRestoreValue<int*> insertPtr(mInsertInstIdxRef, &instIdx);
							RemoveInst(mcBlock, instIdx);
							CreateMemCpy(arg0Addr, arg1Addr, BF_MIN(arg0Type->mSize, arg1Type->mSize), BF_MIN(arg0Type->mAlign, arg1Type->mAlign));
							instIdx--;
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Mov MemCpy\n");
							break;
						}
					}

					if ((arg0Type->IsFloat()) && (arg1Type->IsIntable()))
					{
						if ((arg1Type->mTypeCode == BeTypeCode_Int64) && (!isSignedExt))
						{
							//uint64->float

							AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand(), instIdx);
							AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RAX), inst->mArg1, instIdx + 1);
							inst->mArg1 = BeMCOperand::FromReg(X64Reg_RAX);
							inst->mKind = BeMCInstKind_MovSX;

							int labelIdx = CreateLabel(instIdx + 3);
							AllocInst(BeMCInstKind_Test, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand::FromReg(X64Reg_RAX), instIdx + 3);
							AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(labelIdx), BeMCOperand::FromCmpKind(BeCmpKind_SGE), instIdx + 4);
							BeMCOperand mcOffset;
							if (arg0Type->mSize == 8)
							{
								mcOffset.mKind = BeMCOperandKind_Immediate_f64;
								mcOffset.mImmF64 = 1.8446744073709552e+19;
							}
							else
							{
								mcOffset.mKind = BeMCOperandKind_Immediate_f32;
								mcOffset.mImmF32 = 1.8446744073709552e+19;
							}

							AllocInst(BeMCInstKind_Add, inst->mArg0, mcOffset, instIdx + 5);
							AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand(), instIdx + 7);

							if (debugging)
								OutputDebugStrF(" uint64->float\n");

							isFinalRun = false;
							instIdx--;
							continue; // Rerun from PreserveVolatiles
						}
						else if ((!isSignedExt) || (arg1Type->mSize < 4))
						{
							// Int->Float conversions only work on 32 and 64-bit signed values, so we
							// zero-extend into a larger scratch vreg and then we do the signed conversion on that.
							// Convert from
							//   mov float, uint<bits>
							// to
							//   mov scratch int<bits*2>, uint<bits>
							//   mov float, scratch int<bits*2>

							if ((arg1.IsNativeReg()) && (arg1Type->mSize == 4))
							{
								// If we already have a 32-bit register, then we know that the upper 32-bits are zeroed
								//  so during emission we just use the 64-bit version of that register
							}
							else
							{
								auto mcScratch = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64), 2, true);
								CreateDefineVReg(mcScratch, instIdx++);
								if ((isSignedExt) && (arg1Type->mSize < 8))
									AllocInst(BeMCInstKind_MovSX, mcScratch, inst->mArg1, instIdx++);
								else
									AllocInst(BeMCInstKind_Mov, mcScratch, inst->mArg1, instIdx++);
								inst->mKind = BeMCInstKind_MovSX;
								inst->mArg1 = mcScratch;
								isFinalRun = false;

								if (debugging)
									OutputDebugStrF(" MovSX\n");
							}
						}
					}

					if (!arg0.IsNativeReg())
					{
						// With f32s we can do a imm32 mov into memory, but there's no imm64 version of that
						if ((arg0Type->mTypeCode == BeTypeCode_Double) && (arg1.IsImmediate()))
						{
							// One option would be to do a "movsd xmm, .rdata" to load up the immediate...
							//  and that would leave arg0 with the possibility of binding to a register
							//  in a subsequent reg pass, but we don't do that.
							//ReplaceWithNewVReg(inst->mArg1, instIdx, true);

							// We do an int64 load/store, so arg0 must never be allowed to be a register
							if (arg0.IsVReg())
							{
								auto vregInfo = mVRegInfo[arg0.mVRegIdx];
								vregInfo->mForceMem = true;
								CheckForce(vregInfo);
							}

							BeMCOperand castedVReg;
							if (inst->mArg0.mKind == BeMCOperandKind_VRegLoad)
							{
								// Maintain the load-ness (don't wrap a load)
								castedVReg = AllocVirtualReg(mModule->mContext->GetPointerTo(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64)), 2, false);
								CreateDefineVReg(castedVReg, instIdx++);
								auto castedVRegInfo = GetVRegInfo(castedVReg);
								castedVRegInfo->mIsExpr = true;
								castedVRegInfo->mRelTo = BeMCOperand::FromVReg(inst->mArg0.mVRegIdx);
								castedVReg.mKind = BeMCOperandKind_VRegLoad;
							}
							else
							{
								castedVReg = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64), 2, false);
								CreateDefineVReg(castedVReg, instIdx++);
								auto castedVRegInfo = GetVRegInfo(castedVReg);
								castedVRegInfo->mIsExpr = true;
								castedVRegInfo->mRelTo = inst->mArg0;
							}

							BeMCOperand scratchReg = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64), 2, true);
							CreateDefineVReg(scratchReg, instIdx++);
							AllocInst(BeMCInstKind_Mov, scratchReg, BeMCOperand::FromImmediate(arg1.mImmediate), instIdx++);
							inst->mArg0 = castedVReg;
							inst->mArg1 = scratchReg;

							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Movsd\n");
						}

						// Conversion types only work in "r, rm" format
						auto arg0Type = GetType(inst->mArg0);
						auto arg1Type = GetType(inst->mArg1);
						bool isCompat = arg0Type->mSize == arg1Type->mSize;
						if ((arg0Type->IsFloat()) != arg1Type->IsFloat())
							isCompat = false;

						if (!isCompat)
						{
							ReplaceWithNewVReg(inst->mArg0, instIdx, false);
							isFinalRun = false;
							if (debugging)
								OutputDebugStrF(" Mov float rm\n");
						}
					}
				}
				break;
			case BeMCInstKind_XAdd:
				{
					for (int oprIdx = 0; oprIdx < 2; oprIdx++)
					{
						BeMCOperand& origOperand = (oprIdx == 0) ? inst->mArg0 : inst->mArg1;
						if (origOperand.IsVRegAny())
						{
							BeRMParamsInfo rmInfo;
							GetRMParams(origOperand, rmInfo);

							auto vregInfo = GetVRegInfo(origOperand);
							bool isValid = true;
							if (oprIdx == 1)
							{
								if (rmInfo.mMode != BeMCRMMode_Direct)
								{
									auto newVReg = ReplaceWithNewVReg(origOperand, instIdx, true, true);
									auto newVRegInfo = GetVRegInfo(newVReg);
									newVRegInfo->mDisableRAX = true;
									isFinalRun = false;
								}
							}
						}
					}
				}
				break;
			case BeMCInstKind_CmpXChg:
				{
					for (int oprIdx = 0; oprIdx < 2; oprIdx++)
					{
						BeMCOperand& origOperand = (oprIdx == 0) ? inst->mArg0 : inst->mArg1;
						if (origOperand.IsVRegAny())
						{
							BeRMParamsInfo rmInfo;
							GetRMParams(origOperand, rmInfo);

							auto vregInfo = GetVRegInfo(origOperand);

							if ((ResizeRegister(rmInfo.mRegA, 8) == X64Reg_RAX) || (ResizeRegister(rmInfo.mRegB, 8) == X64Reg_RAX))
							{
								if (!vregInfo->mIsExpr)
								{
									isFinalRun = false;
									vregInfo->mDisableRAX = true;
									continue;
								}
								else
								{
									int safeIdx = FindSafeInstInsertPos(instIdx, true);
									isFinalRun = false;

									auto origType = GetType(origOperand);
									BeMCOperand scratchReg = AllocVirtualReg(mModule->mContext->GetPointerTo(origType), 2, false);
									CreateDefineVReg(scratchReg, safeIdx++);
									AllocInst(BeMCInstKind_Mov, scratchReg, OperandToAddr(origOperand), safeIdx++);

									auto newVRegInfo = GetVRegInfo(scratchReg);
									newVRegInfo->mDisableRAX = true;

									origOperand = scratchReg;
									origOperand.mKind = BeMCOperandKind_VRegLoad;
									continue;
								}
							}

							bool isValid = true;
							if (oprIdx == 1)
							{
								if (rmInfo.mMode != BeMCRMMode_Direct)
								{
									int safeIdx = FindSafeInstInsertPos(instIdx, true);
									auto newVReg = ReplaceWithNewVReg(origOperand, safeIdx, true, true);
									auto newVRegInfo = GetVRegInfo(newVReg);
									newVRegInfo->mDisableRAX = true;
									isFinalRun = false;
								}
							}
						}
					}
				}
				break;
			case BeMCInstKind_Load:
				{
					// And Load gets converted to a "Load %reg0, [%reg1]"
					// So both mArg0 and mArg1 must be a register
					if (!IsAddressable(arg1))
					{
						// Convert to
						// Mov %scratch, %vreg1
						// Load %vreg0, [%scratch]
						ReplaceWithNewVReg(inst->mArg1, instIdx, true);
						arg1 = GetFixedOperand(inst->mArg1);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Load\n");
					}

					// Do this one second since the 'insert after' makes instIdx no longer point to the Load inst
					if (!arg0.IsNativeReg())
					{
						// Convert to
						// Load %scratch, [%vreg1]
						// Mov %vreg0, %scratch
						ReplaceWithNewVReg(inst->mArg0, instIdx, false);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Load 2\n");
					}
				}
				break;
			case BeMCInstKind_Store:
				{
					// Store gets converted to a "Store [reg], reg"
					if (!IsAddressable(arg0))
					{
						// Convert to
						// Mov %scratch, %vreg0
						// Store %scratch, [%vreg1]
						ReplaceWithNewVReg(inst->mArg0, instIdx, true);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Store\n");
					}

					if (!arg1.IsNativeReg())
					{
						// Convert to
						// Mov %scratch, %vreg1
						// Store %vreg0, [%scratch]
						ReplaceWithNewVReg(inst->mArg1, instIdx, true);
						isFinalRun = false;
						if (debugging)
							OutputDebugStrF(" Store2\n");
					}
				}
				break;
			case BeMCInstKind_DefPhi:
				{
					// This is from a PHI whose value was not used
					RemoveInst(mcBlock, instIdx);
					instIdx--;
					continue;
				}
				break;
			}

			if ((pendingInitKind != BfIRInitType_NotNeeded) && (pendingInitKind != BfIRInitType_NotNeeded_AliveOnDecl))
			{
				Fail("Shouldn't happen here anymore");
				/*int vregIdx = inst->mArg0.mVRegIdx;
				auto dbgVar = mVRegInfo[vregIdx]->mDbgVariable;
				auto varType = mVRegInfo[vregIdx]->mType;
				auto initType = dbgVar->mPendingInitType;

				if (varType->IsFloat())
				{
					BeMCOperand zeroVal;
					if (varType->mTypeCode == BeTypeCode_Float)
						zeroVal.mKind = BeMCOperandKind_Immediate_f32;
					else
						zeroVal.mKind = BeMCOperandKind_Immediate_f64;
					if (initType == BfIRInitType_Uninitialized)
						zeroVal.mImmFloat = NAN;
					else
						zeroVal.mImmFloat = 0;
					AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), zeroVal, instIdx + 1);
				}
				else if (varType->IsNonVectorComposite())
				{
					SetAndRestoreValue<int*> insertPtr(mInsertInstIdxRef, &instIdx);
					instIdx++;
					uint8 val = (initType == BfIRInitType_Uninitialized) ? 0xCC : 0;
					CreateMemSet(BeMCOperand::FromVRegAddr(vregIdx), val, varType->mSize, varType->mAlign);
					instIdx--;
					isFinalRun = false;
				}
				else
				{
					int64 val = (initType == BfIRInitType_Uninitialized) ? (int64)0xCCCCCCCCCCCCCCCCLL : 0;
					AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregIdx), BeMCOperand::FromImmediate(val), instIdx + 1);
				}

				dbgVar->mPendingInitType = BfIRInitType_NotNeeded;*/
			}
		}
		mActiveInst = NULL;
	}

	BF_ASSERT(regPreserveDepth == 0);
	SetCurrentInst(NULL);

	if (hasPendingActualizations)
	{
		DoActualization();
		isFinalRun = false;
		if (debugging)
			OutputDebugStrF(" DoActualizations\n");
	}

	mLegalizationIterations++;
	return isFinalRun;
}

void BeMCContext::DoSanityChecking()
{
	for (auto mcBlock : mBlocks)
	{
		mActiveBlock = mcBlock;
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size() - 1; instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			auto nextInst = mcBlock->mInstructions[instIdx + 1];

			auto dest = inst->GetDest();
			if (dest == NULL)
				continue;
			auto vregInfo = GetVRegInfo(*dest);

			if ((vregInfo != NULL) && (vregInfo->mHasDynLife))
			{
				if ((nextInst->mVRegsInitialized != NULL) && (!mVRegInitializedContext.IsSet(nextInst->mVRegsInitialized, dest->mVRegIdx)))
				{
					if (nextInst->mKind != BeMCInstKind_RestoreVolatiles)
					{
						// It's possible during legalization to extend the usage of a dynLife value 'upward', and we need
						//  to ensure we also extend the init flags up as well
						SoftFail("Invalid liveness - from init flag not being set properly?");
					}
				}
			}
		}
	}
}

void BeMCContext::DoBlockCombine()
{
	auto masterBlock = mMCBlockAlloc.Alloc();
	mActiveBlock = masterBlock;

	for (auto mcBlock : mBlocks)
	{
		mcBlock->mLabelIdx = mCurLabelIdx++;
	}

	for (auto mcBlock : mBlocks)
	{
		auto inst = AllocInst();
		inst->mKind = BeMCInstKind_Label;
		inst->mArg0.mKind = BeMCOperandKind_Label;
		inst->mArg0.mLabelIdx = mcBlock->mLabelIdx;

		for (auto blockInst : mcBlock->mInstructions)
		{
			if (inst->mDbgLoc == NULL)
				inst->mDbgLoc = blockInst->mDbgLoc;

			auto operands = { &blockInst->mArg0, &blockInst->mArg1 };
			for (auto op : operands)
			{
				if (op->mKind == BeMCOperandKind_Block)
				{
					op->mKind = BeMCOperandKind_Label;
					op->mLabelIdx = op->mBlock->mLabelIdx;
				}
			}
			masterBlock->mInstructions.push_back(blockInst);
		}
	}

	mBlocks.Clear();
	mBlocks.push_back(masterBlock);
}

bool BeMCContext::DoJumpRemovePass()
{
	struct LabelStats
	{
	public:
		bool mHasRefs;
		bool mForceKeepLabel;
		bool mFromMultipleDbgLocs;

		// If all branches share the dbgLoc with the first inst then we can potentially do a remap
		BeDbgLoc* mRefDbgLoc;

	public:
		LabelStats()
		{
			mHasRefs = false;
			mForceKeepLabel = false;
			mFromMultipleDbgLocs = false;
			mRefDbgLoc = NULL;
		}
	};

	bool didWork = false;

	SizedArray<LabelStats, 32> labelStats;
	labelStats.resize(mCurLabelIdx);
	Dictionary<int, int> labelRemaps;

	for (auto& switchEntry : mSwitchEntries)
	{
		labelStats[switchEntry.mBlock->mLabelIdx].mForceKeepLabel = true;
		labelStats[switchEntry.mBlock->mLabelIdx].mHasRefs = true;
	}

	for (auto mcBlock : mBlocks)
	{
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];

			if (inst->mKind == BeMCInstKind_Label)
			{
				if (instIdx < (int)mcBlock->mInstructions.size() - 2)
				{
					auto nextInst = mcBlock->mInstructions[instIdx + 1];
					if (nextInst->mDbgLoc != NULL)
					{
						auto& stats = labelStats[inst->mArg0.mLabelIdx];
						if ((stats.mRefDbgLoc != NULL) && (stats.mRefDbgLoc != nextInst->mDbgLoc))
							stats.mFromMultipleDbgLocs = true;
						else
							stats.mRefDbgLoc = nextInst->mDbgLoc;
					}
				}
			}
			else
			{
				auto operands = { &inst->mArg0, &inst->mArg1 };
				for (auto operand : operands)
				{
					if (operand->mKind == BeMCOperandKind_Label)
					{
						labelStats[operand->mLabelIdx].mHasRefs = true;

						if (inst->mDbgLoc != NULL)
						{
							auto& stats = labelStats[operand->mLabelIdx];
							if ((stats.mRefDbgLoc != NULL) && (stats.mRefDbgLoc != inst->mDbgLoc))
								stats.mFromMultipleDbgLocs = true;
							else
								stats.mRefDbgLoc = inst->mDbgLoc;
						}
					}
				}
			}
		}
	}

	for (auto mcBlock : mBlocks)
	{
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			bool goBackToLastBranch = false;

			bool removeInst = false;

			// If we're just jumping to the next label, remove jump
			//  It's possible there are several labels in a row here, so check against all of them
			if (inst->mKind == BeMCInstKind_Br)
			{
				if (inst->mArg0.mKind == BeMCOperandKind_Label)
				{
					for (int labelInstIdx = instIdx + 1; labelInstIdx < (int)mcBlock->mInstructions.size(); labelInstIdx++)
					{
						auto labelInst = mcBlock->mInstructions[labelInstIdx];
						if (labelInst->mKind != BeMCInstKind_Label)
						{
							if (labelInst->IsPsuedo())
								continue;
							break;
						}
						if (inst->mArg0 == labelInst->mArg0)
						{
							didWork = true;
							inst->mKind = inst->mArg1 ? BeMCInstKind_Nop : BeMCInstKind_EnsureInstructionAt;
							inst->mArg0 = BeMCOperand();
							inst->mArg1 = BeMCOperand();
							goBackToLastBranch = true;
							break;
						}
					}
				}
			}

			if (removeInst)
			{
				mcBlock->mInstructions.RemoveAt(instIdx);
				instIdx--;
				continue;
			}

			// Do we have a label that immediately branches?
			if (inst->mKind == BeMCInstKind_Label)
			{
				// We can only remap this if the contained instructions and all references share the same dbgLoc
				if ((instIdx < (int)mcBlock->mInstructions.size() - 2) && (!labelStats[inst->mArg0.mLabelIdx].mFromMultipleDbgLocs))
				{
					bool allowRemove = true;
					auto nextInst = mcBlock->mInstructions[instIdx + 1];
					auto nextNextInst = mcBlock->mInstructions[instIdx + 2];
					if ((nextInst->mKind == BeMCInstKind_Br) &&
						((inst->mDbgLoc == NULL) || (inst->mDbgLoc == nextNextInst->mDbgLoc)) &&
						(!labelStats[inst->mArg0.mLabelIdx].mForceKeepLabel))
					{
						int checkIdx = instIdx - 1;
						while (checkIdx >= 0)
						{
							auto prevInst = mcBlock->mInstructions[checkIdx];

							if (prevInst->mKind == BeMCInstKind_Label)
							{
								// Keep looking
							}
							else if (prevInst->mKind == BeMCInstKind_Br)
							{
								break;
							}
							else
							{
								// We flowed into here, so we can't remove this branch
								allowRemove = false;
								break;
							}
							checkIdx--;
						}

						/*if (labelStats[inst->mArg0.mLabelIdx].mFromMultipleDbgLocs)
							allowRemove = false;*/

						didWork = true;
						RemoveInst(mcBlock, instIdx); // Remove label
						labelRemaps.TryAdd(inst->mArg0.mLabelIdx, nextInst->mArg0.mLabelIdx);
						if (allowRemove)
						{
							RemoveInst(mcBlock, instIdx); // Remove branch
							goBackToLastBranch = true;
						}
					}
				}
			}

			if (goBackToLastBranch)
			{
				// We may be able to remove the previous branch now
				int checkIdx = instIdx - 1;
				while (checkIdx >= 0)
				{
					auto checkInst = mcBlock->mInstructions[checkIdx];
					if (checkInst->mKind == BeMCInstKind_Br)
					{
						instIdx = checkIdx - 1;
						break;
					}
					else if (checkInst->mKind != BeMCInstKind_Label)
						break;

					checkIdx--;
				}

				continue;
			}
		}
	}

	if (!labelRemaps.IsEmpty())
	{
		while (true)
		{
			bool didRemapMerge = false;
			for (auto& remapPair : labelRemaps)
			{
				//auto itr = labelRemaps.find(remapPair.second);
				//if (itr != labelRemaps.end())

				int* valuePtr = NULL;
				if (labelRemaps.TryGetValue(remapPair.mValue, &valuePtr))
				{
					//remapPair.second = itr->second;
					remapPair.mValue = *valuePtr;
					didRemapMerge = true;
				}
			}
			if (!didRemapMerge)
				break;
		}

		for (auto mcBlock : mBlocks)
		{
			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			{
				auto inst = mcBlock->mInstructions[instIdx];

				auto operands = { &inst->mArg0, &inst->mArg1 };
				for (auto operand : operands)
				{
					if (operand->mKind == BeMCOperandKind_Label)
					{
						//auto itr = labelRemaps.find(operand->mLabelIdx);
						//if (itr != labelRemaps.end())

						int* valuePtr = NULL;
						if (labelRemaps.TryGetValue(operand->mLabelIdx, &valuePtr))
						{
							labelStats[operand->mLabelIdx].mHasRefs = false;
							//operand->mLabelIdx = itr->second;
							operand->mLabelIdx = *valuePtr;
							labelStats[operand->mLabelIdx].mHasRefs = true;
						}
					}
				}
			}
		}
	}

	for (auto mcBlock : mBlocks)
	{
		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];
			bool goBackToLastBranch = false;
		}
	}

	// Remove unreferenced labels
	for (auto mcBlock : mBlocks)
	{
		bool doMorePasses = false;
		bool isUnreachable = false;

		for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		{
			auto inst = mcBlock->mInstructions[instIdx];

			if (inst->mKind == BeMCInstKind_Label)
			{
				isUnreachable = false;

				if (!labelStats[inst->mArg0.mLabelIdx].mHasRefs)
				{
					didWork = true;
					mcBlock->mInstructions.RemoveAt(instIdx);
					instIdx--;
				}
			}

			if (inst->mKind == BeMCInstKind_CondBr)
				doMorePasses = true;

			if (inst->mKind == BeMCInstKind_Br)
			{
				if (isUnreachable)
				{
					// We can't possible reach this branch.  This is an artifact of other br removals
					didWork = true;
					RemoveInst(mcBlock, instIdx);
					instIdx--;
					continue;
				}

				isUnreachable = true;
			}
		}

		if (doMorePasses)
		{
			for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
			{
				auto inst = mcBlock->mInstructions[instIdx];

				// For the form:
				//   CondBr %label0, eq
				//   Br %label1
				//   %label0:
				// Invert condition and convert that to
				//   CondBr %label1, neq
				//   %label0:
				// We perform this after the jump-removal process to ensure we compare against the correct
				//  final label positions, otherwise there are cases where we would incorrectly apply this
				//  transformation early and end up with worse results than just not doing it
				if (inst->mKind == BeMCInstKind_CondBr)
				{
					if (instIdx < (int)mcBlock->mInstructions.size() - 2)
					{
						int nextIdx = instIdx + 1;
						int nextNextIdx = instIdx + 2;
						auto nextInst = mcBlock->mInstructions[nextIdx];
						if (nextInst->mKind == BeMCInstKind_EnsureInstructionAt)
						{
							if (nextInst->mDbgLoc != inst->mDbgLoc)
								continue;
							nextIdx++;
							nextNextIdx++;
							nextInst = mcBlock->mInstructions[nextIdx];
						}

						auto nextNextInst = mcBlock->mInstructions[nextNextIdx];
						if ((nextInst->mKind == BeMCInstKind_Br) &&
							(nextNextInst->mKind == BeMCInstKind_Label) && (inst->mArg0 == nextNextInst->mArg0))
						{
							didWork = true;
							inst->mArg0 = nextInst->mArg0;
							BF_ASSERT(inst->mArg1.mKind == BeMCOperandKind_CmpKind);
							inst->mArg1.mCmpKind = BeModule::InvertCmp(inst->mArg1.mCmpKind);
							mcBlock->mInstructions.RemoveAt(nextIdx);
						}
					}
				}
			}
		}
	}

	return didWork;
}

void BeMCContext::DoRegFinalization()
{
	SizedArray<int, 32> savedVolatileVRegs;

	mUsedRegs.Clear();

	SetCurrentInst(NULL);

	// Remove the Def instructions, replace vreg
	for (auto mcBlock : mBlocks)
	{
		mActiveBlock = mcBlock;

		//for (int instIdx = 0; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
		for (auto instEnum = BeInstEnumerator(this, mcBlock); instEnum.HasMore(); instEnum.Next())
		{
			auto inst = instEnum.Get();
			SetCurrentInst(inst);
			if (inst->IsDef())
			{
				auto vregInfo = GetVRegInfo(inst->mArg0);
				if (vregInfo->mDbgVariable != NULL)
					vregInfo->mAwaitingDbgStart = true;
				instEnum.RemoveCurrent();
				continue;
			}

			// This is similar to an optimization we have in DoLegalization, but it allows for directRelTo's to match
			//  We can't match those in DoLegalization because it would alter the liveness
			if (inst->mKind == BeMCInstKind_Mov)
			{
				// Useless mov, remove it
				if (OperandsEqual(inst->mArg0, inst->mArg1, false))
				{
					if (inst->mDbgLoc != NULL)
					{
						inst->mKind = BeMCInstKind_EnsureInstructionAt;
						inst->mArg0 = BeMCOperand();
						inst->mArg1 = BeMCOperand();
					}
					else
					{
						//RemoveInst(mcBlock, instIdx);
						//instIdx--;
						instEnum.RemoveCurrent();
					}

					continue;
				}
			}

			if ((inst->mKind >= BeMCInstKind_Def) && (inst->mKind <= BeMCInstKind_LifetimeEnd))
				continue;

			if (inst->mResult == inst->mArg0)
			{
				// Destination is implicitly arg0
				inst->mResult.mKind = BeMCOperandKind_None;
			}

			// Unfortunate rule:
			//  if we end up with:
			//    PreserveVolatiles RAX
			//    Shl vreg0<RCX>, CL
			//    RestoreVolatiles RAXS
			//  Then convert that to
			//    PreserveVolatiles RAX
			//    Shl <SavedLoc>, RAX
			//    RestoreVolatiles RAX
			/*if ((inst->mKind == BeMCInstKind_Shl) ||
				(inst->mKind == BeMCInstKind_Shr) ||
				(inst->mKind == BeMCInstKind_Sar))
			{
				if (inst->mArg1.IsNativeReg())
				{
				}
			}*/

			auto operands = { &inst->mResult, &inst->mArg0, &inst->mArg1 };
			for (auto operand : operands)
			{
				BeMCOperand checkOperand = *operand;
				while (true)
				{
					auto vregInfo = GetVRegInfo(checkOperand);
					if (vregInfo == NULL)
						break;

					// We may think that range starts should always occur on a "mov <vreg>, <value>", but
					//  we have some cases like un-splatting into a composite, or using an uninitialized local
					//  variable where we want to start the range on ANY use, not just a direct assignment
					if (vregInfo->mAwaitingDbgStart)
					{
						//AllocInst(BeMCInstKind_DbgRangeStart, checkOperand, instIdx + 1);
						//vregInfo->mAwaitingDbgStart = false;
					}
					if (!vregInfo->mRelTo)
						break;
					checkOperand = vregInfo->mRelTo;
				}

				FixOperand(*operand, -1);
				if (operand->mKind == BeMCOperandKind_NativeReg)
				{
					auto nativeReg = GetFullRegister(operand->mReg);
					BF_ASSERT(nativeReg != X64Reg_None);
					if (nativeReg != X64Reg_RSP) // This can happen from allocas
						mUsedRegs.Add(nativeReg);
				}
			}

			if (inst->IsMov())
			{
				bool removeInst = false;

				if (GetFixedOperand(inst->mArg0) == GetFixedOperand(inst->mArg1))
				{
					removeInst = true;
				}

				if ((inst->mArg0.IsNativeReg()) && (inst->mArg1.IsNativeReg()))
				{
					// Removes size-reducing moves such as "mov eax, rax"
					if (GetFullRegister(inst->mArg0.mReg) == inst->mArg1.mReg)
					{
						removeInst = true;
					}
				}

				if (removeInst)
				{
					inst->mKind = BeMCInstKind_EnsureInstructionAt;
					inst->mResult = BeMCOperand();
					inst->mArg0 = BeMCOperand();
					inst->mArg1 = BeMCOperand();
				}
			}

			switch (inst->mKind)
			{
			case BeMCInstKind_PreserveVolatiles:
			{
				int preserveIdx;
				BeMCInst* preserveInst;
				BeMCInst* restoreInst;
				if (inst->mKind == BeMCInstKind_PreserveVolatiles)
				{
					preserveIdx = instEnum.mReadIdx;
					preserveInst = inst;
					restoreInst = mcBlock->mInstructions[FindRestoreVolatiles(mcBlock, instEnum.mReadIdx)];
				}
				else
				{
					preserveIdx = FindPreserveVolatiles(mcBlock, instEnum.mReadIdx);
					preserveInst = mcBlock->mInstructions[preserveIdx];
					restoreInst = inst;
				}
				int insertIdx = instEnum.mWriteIdx;

				if ((inst->mArg0.IsNativeReg()) && (inst->mArg1.IsNativeReg()))
				{
					// If our exception (IE: div target) is set to the desired preserve reg then
					if (inst->mArg0.mReg == GetFullRegister(inst->mArg1.mReg))
						break;
				}

				// Save volatile registers
				for (int liveVRegIdx : *inst->mLiveness)
				{
					if (liveVRegIdx >= mLivenessContext.mNumItems)
						continue;

					auto checkVRegIdx = GetUnderlyingVReg(liveVRegIdx);
					// Already handled the underlying vreg?
					if ((checkVRegIdx != liveVRegIdx) && (mLivenessContext.IsSet(preserveInst->mLiveness, checkVRegIdx)))
						continue;

					auto vregInfo = mVRegInfo[checkVRegIdx];
					if (vregInfo->mReg != X64Reg_None)
					{
						// Do we specify a particular reg, or just all volatiles?
						if ((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg != GetFullRegister(vregInfo->mReg)))
							continue;

						if (!mLivenessContext.IsSet(restoreInst->mLiveness, liveVRegIdx))
						{
							// This vreg doesn't survive until the PreserveRegs -- it's probably used for params or the call addr
							continue;
						}

						if ((inst->mArg0.IsNativeReg()) || (IsVolatileReg(vregInfo->mReg)))
						{
							if (vregInfo->mVolatileVRegSave == -1)
							{
								BF_ASSERT(inst->mKind != BeMCInstKind_RestoreVolatiles); // Should have already been allocated
								auto savedVReg = AllocVirtualReg(vregInfo->mType);
								auto savedVRegInfo = mVRegInfo[savedVReg.mVRegIdx];
								savedVRegInfo->mForceMem = true;
								vregInfo->mVolatileVRegSave = savedVReg.mVRegIdx;
							}

							savedVolatileVRegs.push_back(checkVRegIdx);
							AllocInst(BeMCInstKind_Mov, BeMCOperand::FromVReg(vregInfo->mVolatileVRegSave), GetVReg(checkVRegIdx), insertIdx++);
							if (insertIdx > instEnum.mReadIdx)
								instEnum.Next();
							else
								instEnum.mWriteIdx++;
						}
					}
				}

				bool savingSpecificReg = false;
				if (inst->mArg0)
				{
					if (inst->mArg0.IsNativeReg())
						savingSpecificReg = true;
					else
						BF_ASSERT(inst->mArg0.mKind == BeMCOperandKind_PreserveFlag);
				}

				// Full PreserveRegs case:
				// Check register crossover errors, ie:
				//  Mov RCX, reg0<RDX>
				//  Mov RDX, reg1<RCX>
				if (!savingSpecificReg)
				{
					int preserveIdx = instEnum.mReadIdx;

					bool regsFailed = false;
					for (int pass = 0; true; pass++)
					{
						bool regStomped[X64Reg_COUNT] = { false };
						bool regFailed[X64Reg_COUNT] = { false };
						regsFailed = false;

						bool isFinalPass = pass == 4;
						int instEndIdx = -1;

						for (int instIdx = preserveIdx + 1; instIdx < (int)mcBlock->mInstructions.size(); instIdx++)
						{
							auto inst = mcBlock->mInstructions[instIdx];
							if ((inst->mKind == BeMCInstKind_RestoreVolatiles) ||
								(inst->mKind == BeMCInstKind_Call))
							{
								instEndIdx = instIdx;
								if (inst->mKind == BeMCInstKind_Call)
								{
									BeRMParamsInfo rmInfo;
									GetRMParams(inst->mArg0, rmInfo);

									if (((rmInfo.mRegA != X64Reg_None) && (rmInfo.mRegA != X64Reg_RAX) && (regStomped[rmInfo.mRegA])) ||
										((rmInfo.mRegB != X64Reg_None) && (regStomped[rmInfo.mRegB])))
									{
										BF_ASSERT(pass == 0);

										// Target is stomped! Mov to RAX and then handle it along with the generalized reg setup
										auto callTarget = BeMCOperand::FromReg(X64Reg_RAX);
										AllocInst(BeMCInstKind_Mov, callTarget, inst->mArg0, preserveIdx + 1);
										inst->mArg0 = callTarget;
										regsFailed = true; // So we'll rerun taking into account the new RAX reg
										instEndIdx++;
									}
								}

								break;
							}

							if (inst->mKind == BeMCInstKind_Def)
							{
								// If we have a Def here, it's because of a legalization that introduced a variable in a non-safe position
								SoftFail("Illegal def");
							}

							if (inst->mKind == BeMCInstKind_Mov)
							{
								X64CPURegister regs[2] = { X64Reg_None, X64Reg_None };
								GetUsedRegs(inst->mArg1, regs[0], regs[1]);

								bool isStomped = false;
								for (auto reg : regs)
								{
									if (reg != X64Reg_None)
									{
										if (regStomped[reg])
										{
											regsFailed = true;
											isStomped = true;
											regFailed[reg] = true;
										}
									}
								}

								// Don't swap on the final pass, we need regsFailed to be accurate so this is just a
								//  verification pass
								if ((isStomped) && (!isFinalPass))
								{
									// Bubble up, maybe a reordering will fix our issue
									BF_SWAP(mcBlock->mInstructions[instIdx - 1], mcBlock->mInstructions[instIdx]);
								}

								if (inst->mArg0.IsNativeReg())
								{
									auto reg = GetFullRegister(inst->mArg0.mReg);
									regStomped[(int)reg] = true;
								}
							}
						}

						if (!regsFailed)
							break;

						if (isFinalPass)
						{
							// We've failed to reorder
							int deferredIdx = 0;

							for (int instIdx = preserveIdx + 1; instIdx < instEndIdx; instIdx++)
							{
								auto inst = mcBlock->mInstructions[instIdx];
								if ((inst->mKind == BeMCInstKind_RestoreVolatiles) ||
									(inst->mKind == BeMCInstKind_Call))
									break;

								if (inst->mKind == BeMCInstKind_Mov)
								{
									if (inst->mArg0.IsNativeReg())
									{
										auto reg = GetFullRegister(inst->mArg0.mReg);
										if (regFailed[(int)reg])
										{
											// Convert
											//   Mov <reg>, vreg0
											// To
											//   Push vreg0
											//    ...
											//   Pop <reg>

											auto mcPopReg = inst->mArg0;
											auto popType = GetType(mcPopReg);
											if (!popType->IsFloatOrVector())
												mcPopReg.mReg = GetFullRegister(mcPopReg.mReg);

											auto pushType = GetType(inst->mArg1);
											auto useTypeCode = pushType->IsFloatOrVector() ? pushType->mTypeCode : BeTypeCode_Int64;

											BF_ASSERT(deferredIdx < 4);
											auto mcDeferred = GetCallArgVReg(deferredIdx++, useTypeCode);
											CreateDefineVReg(BeMCOperand::FromVReg(mcDeferred.mVRegIdx), instIdx++);
											instEndIdx++;

											auto popInst = AllocInst(BeMCInstKind_Mov, mcPopReg, mcDeferred, instEndIdx);

											//auto popInst = AllocInst(BeMCInstKind_Pop, mcPopReg, instEndIdx);

											auto arg1 = inst->mArg1;
											FixOperand(arg1);
											if (arg1.IsNativeReg())
											{
												inst->mKind = BeMCInstKind_Mov;
												inst->mArg0 = mcDeferred;
												if (!popType->IsFloatOrVector())
													inst->mArg1 = BeMCOperand::FromReg(GetFullRegister(arg1.mReg));
												else
													inst->mArg1 = BeMCOperand::FromReg(arg1.mReg);
											}
											else
											{
												// Use R11 or XMM5 as our temporary - they are the least likely volatiles to be
												//  allocated, so we may not need to restore them after using them

												X64CPURegister scratchReg;
												if (pushType->mTypeCode == BeTypeCode_Float)
													scratchReg = X64Reg_XMM5_f32;
												else if (pushType->mTypeCode == BeTypeCode_Double)
													scratchReg = X64Reg_XMM5_f64;
												else
													scratchReg = X64Reg_R11;

												int volatileVRegSave = -1;
												for (auto vregIdx : savedVolatileVRegs)
												{
													auto vregInfo = mVRegInfo[vregIdx];
													if (GetFullRegister(vregInfo->mReg) == scratchReg)
													{
														volatileVRegSave = vregInfo->mVolatileVRegSave;
													}
												}

												AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(scratchReg), inst->mArg1, instIdx++);
												instEndIdx++;

												inst->mKind = BeMCInstKind_Mov;
												inst->mArg0 = mcDeferred;
												inst->mArg1 = BeMCOperand::FromReg(scratchReg);

												if (volatileVRegSave != -1)
												{
													AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(scratchReg), BeMCOperand::FromVReg(volatileVRegSave), ++instIdx);
													instEndIdx++;
												}
											}
										}
									}
								}
							}

							break;
						}
					}
				}
			}
			break;

			case BeMCInstKind_RestoreVolatiles:
			{
				bool doRestore = true;
				if ((inst->mArg0.mKind == BeMCOperandKind_PreserveFlag) && ((inst->mArg0.mPreserveFlag & BeMCPreserveFlag_NoRestore) != 0))
				{
					// Don't bother restore registers for NoReturns
					doRestore = false;
				}

				if (doRestore)
				{
					for (auto vregIdx : savedVolatileVRegs)
					{
						auto vregInfo = mVRegInfo[vregIdx];
						int insertIdx = instEnum.mWriteIdx;
						AllocInst(BeMCInstKind_Mov, GetVReg(vregIdx), BeMCOperand::FromVReg(vregInfo->mVolatileVRegSave), insertIdx++);
						if (insertIdx > instEnum.mReadIdx)
							instEnum.Next();
						else
							instEnum.mWriteIdx++;
					}
				}
				savedVolatileVRegs.clear();
			}
			break;
			}
		}
	}
	SetCurrentInst(NULL);

	// Do this at the end so we can properly handle RangeStarts
	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		auto vregInfo = mVRegInfo[vregIdx];
		if (vregInfo->mRelTo)
		{
			FixOperand(vregInfo->mRelTo);
			FixOperand(vregInfo->mRelOffset);
		}
	}
}

BeMCInstForm BeMCContext::GetInstForm(BeMCInst* inst)
{
	if (!inst->mArg0)
		return BeMCInstForm_Unknown;

	auto arg0 = GetFixedOperand(inst->mArg0);
	auto arg1 = GetFixedOperand(inst->mArg1);

	if (!arg1)
	{
		if (arg0.mKind == BeMCOperandKind_Symbol)
			return BeMCInstForm_Symbol;
		if (arg0.mKind == BeMCOperandKind_SymbolAddr)
			return BeMCInstForm_SymbolAddr;

		auto arg0Type = GetType(arg0);
		if (arg0Type == NULL)
			return BeMCInstForm_Unknown;
		if (arg0.IsImmediate())
		{
			if ((arg0.mImmediate >= -0x80000000LL) && (arg0.mImmediate <= 0x7FFFFFFF))
				return BeMCInstForm_IMM32;
		}
		if (arg0.mKind == BeMCOperandKind_NativeReg)
		{
			if (arg0Type->mSize == 4)
				return BeMCInstForm_R32;
			if (arg0Type->mSize == 2)
				return BeMCInstForm_R16;
			if (arg0Type->mSize == 1)
				return BeMCInstForm_R8;
			return BeMCInstForm_R64;
		}
		if (arg0Type->mSize == 4)
			return BeMCInstForm_RM32;
		if (arg0Type->mSize == 2)
			return BeMCInstForm_RM16;
		if (arg0Type->mSize == 1)
			return BeMCInstForm_RM8;
		return BeMCInstForm_RM64;
	}

	auto arg0Type = GetType(arg0);
	auto arg1Type = GetType(arg1);

	if ((arg0Type != NULL) && (arg1Type != NULL) &&
		((arg0Type->IsVector()) || (arg1Type->IsVector())))
	{
		if (arg0.IsNativeReg())
		{
			return BeMCInstForm_XMM128_RM128;
		}
		else
		{
			return BeMCInstForm_FRM128_XMM128;
		}
	}
	else if ((arg0Type != NULL) && (arg1Type != NULL) &&
		((arg0Type->IsFloat()) || (arg1Type->IsFloat())))
	{
		if (arg0.IsNativeReg())
		{
			if (arg0Type->mTypeCode == BeTypeCode_Double)
			{
				if (arg1.IsImmediate())
					return BeMCInstForm_XMM64_IMM;
				switch (arg1Type->mTypeCode)
				{
				case BeTypeCode_Float: return BeMCInstForm_XMM64_FRM32;
				case BeTypeCode_Double: return BeMCInstForm_XMM64_FRM64;
				case BeTypeCode_Int32: return BeMCInstForm_XMM64_RM32;
				case BeTypeCode_Int64: return BeMCInstForm_XMM64_RM64;
				case BeTypeCode_Pointer: return BeMCInstForm_XMM64_RM64;
				default: NotImpl();
				}
			}
			else if (arg0Type->mTypeCode == BeTypeCode_Float)
			{
				if (arg1.IsImmediate())
					return BeMCInstForm_XMM32_IMM;
				switch (arg1Type->mTypeCode)
				{
				case BeTypeCode_Float: return BeMCInstForm_XMM32_FRM32;
				case BeTypeCode_Double: return BeMCInstForm_XMM32_FRM64;
				case BeTypeCode_Int32: return BeMCInstForm_XMM32_RM32;
				case BeTypeCode_Int64: return BeMCInstForm_XMM32_RM64;
				case BeTypeCode_Pointer: return BeMCInstForm_XMM32_RM64;
				default: NotImpl();
				}
			}
			else if (arg1Type->mTypeCode == BeTypeCode_Double)
			{
				if (arg0Type->mSize == 4)
					return BeMCInstForm_R32_F64;
				else
					return BeMCInstForm_R64_F64;
			}
			else if (arg1Type->mTypeCode == BeTypeCode_Float)
			{
				if (arg0Type->mSize == 4)
					return BeMCInstForm_R32_F32;
				else
					return BeMCInstForm_R64_F32;
			}
		}
		else
		{
			if (arg1.IsImmediate())
				return BeMCInstForm_Unknown;

			if (arg0Type->mTypeCode == BeTypeCode_Double)
			{
				//TODO: This used to INCORRECTLY say 'switch (arg0Type->mTypeCode)'
				switch (arg1Type->mTypeCode)
				{
				case BeTypeCode_Float: return BeMCInstForm_FRM32_XMM64;
				case BeTypeCode_Double: return BeMCInstForm_FRM64_XMM64;
				default: NotImpl();
				}
			}
			else if (arg0Type->mTypeCode == BeTypeCode_Float)
			{
				//TODO: This used to INCORRECTLY say 'switch (arg0Type->mTypeCode)'
				switch (arg1Type->mTypeCode)
				{
				case BeTypeCode_Float: return BeMCInstForm_FRM32_XMM32;
				case BeTypeCode_Double: return BeMCInstForm_FRM64_XMM32;
				default: NotImpl();
				}
			}
			else
				NotImpl();
		}
	}

	if ((arg1.IsImmediate()) && (arg0Type != NULL)) // MOV r/m64, imm32
	{
		int64 maxSize = 8;
		if ((arg0Type->IsInt()) && (arg1.IsImmediateInt()))
			maxSize = arg0Type->mSize;

		if ((arg1.mKind == BeMCOperandKind_Immediate_Null) || (maxSize <= 1) ||
			((arg1.mImmediate >= -0x80) && (arg1.mImmediate <= 0x7F)))
		{
			switch (arg0Type->mSize)
			{
			case 8: return BeMCInstForm_RM64_IMM8;
			case 4: return BeMCInstForm_RM32_IMM8;
			case 2: return BeMCInstForm_RM16_IMM8;
			case 1: return BeMCInstForm_RM8_IMM8;
			}
		}
		else if ((maxSize <= 2) ||
			((arg1.mImmediate >= -0x8000) && (arg1.mImmediate <= 0x7FFF)))
		{
			switch (arg0Type->mSize)
			{
			case 8: return BeMCInstForm_RM64_IMM16;
			case 4: return BeMCInstForm_RM32_IMM16;
			case 2: return BeMCInstForm_RM16_IMM16;
			case 1: return BeMCInstForm_RM8_IMM8;
			}
		}
		else if ((maxSize <= 4) ||
			((arg1.mImmediate >= -0x80000000LL) && (arg1.mImmediate <= 0x7FFFFFFF)))
		{
			switch (arg0Type->mSize)
			{
			case 8: return BeMCInstForm_RM64_IMM32;
			case 4: return BeMCInstForm_RM32_IMM32;
			case 2: return BeMCInstForm_RM16_IMM16;
			case 1: return BeMCInstForm_RM8_IMM8;
			}
		}
		else
			return BeMCInstForm_RM64_IMM64;
	}
	if ((arg0Type == NULL) || (arg1Type == NULL) || (arg0Type->mSize != arg1Type->mSize))
		return BeMCInstForm_Unknown;

	if (arg0.mKind == BeMCOperandKind_NativeReg)
	{
		switch (GetType(arg0)->mSize)
		{
		case 8:	return BeMCInstForm_R64_RM64;
		case 4: return BeMCInstForm_R32_RM32;
		case 2: return BeMCInstForm_R16_RM16;
		case 1:	return BeMCInstForm_R8_RM8;
		}
	}
	if (arg1.mKind == BeMCOperandKind_NativeReg)
	{
		switch (GetType(arg1)->mSize)
		{
		case 8: return BeMCInstForm_RM64_R64;
		case 4: return BeMCInstForm_RM32_R32;
		case 2: return BeMCInstForm_RM16_R16;
		case 1: return BeMCInstForm_RM8_R8;
		}
	}

	return BeMCInstForm_Unknown;
}

BeMCInstForm BeMCContext::ToIMM16(BeMCInstForm instForm)
{
	switch (instForm)
	{
	case BeMCInstForm_RM16_IMM8: return BeMCInstForm_RM32_IMM16;
	case BeMCInstForm_RM32_IMM8: return BeMCInstForm_RM32_IMM16;
	case BeMCInstForm_RM64_IMM8: return BeMCInstForm_RM64_IMM16;
	case BeMCInstForm_RM16_IMM16: return BeMCInstForm_RM32_IMM16;
	case BeMCInstForm_RM32_IMM16: return BeMCInstForm_RM32_IMM16;
	case BeMCInstForm_RM64_IMM16: return BeMCInstForm_RM64_IMM16;
	}
	NotImpl();
	return instForm;
}

BeMCInstForm BeMCContext::ToIMM32OrSmaller(BeMCInstForm instForm)
{
	switch (instForm)
	{
	case BeMCInstForm_RM8_IMM8: return BeMCInstForm_RM8_IMM8;
	case BeMCInstForm_RM16_IMM8: return BeMCInstForm_RM16_IMM16;
	case BeMCInstForm_RM32_IMM8: return BeMCInstForm_RM32_IMM32;
	case BeMCInstForm_RM64_IMM8: return BeMCInstForm_RM64_IMM32;
	case BeMCInstForm_RM16_IMM16: return BeMCInstForm_RM16_IMM16;
	case BeMCInstForm_RM32_IMM16: return BeMCInstForm_RM32_IMM32;
	case BeMCInstForm_RM64_IMM16: return BeMCInstForm_RM64_IMM32;
	case BeMCInstForm_RM32_IMM32: return BeMCInstForm_RM32_IMM32;
	case BeMCInstForm_RM64_IMM32: return BeMCInstForm_RM64_IMM32;
	}
	NotImpl();
	return instForm;
}

void BeMCContext::SetCurrentInst(BeMCInst* inst)
{
	if (inst == NULL)
	{
		mCurDbgLoc = NULL;
		mCurVRegsInit = NULL;
		mCurVRegsLive = NULL;
	}
	else
	{
		mCurDbgLoc = inst->mDbgLoc;
		mCurVRegsInit = inst->mVRegsInitialized;
		mCurVRegsLive = inst->mLiveness;
	}
}

int BeMCContext::FindPreserveVolatiles(BeMCBlock* mcBlock, int instIdx)
{
	for (int checkIdx = instIdx - 1; checkIdx >= 0; checkIdx--)
	{
		BeMCInst* checkInst = mcBlock->mInstructions[checkIdx];
		if (checkInst->mKind == BeMCInstKind_PreserveVolatiles)
			return checkIdx;
	}
	return -1;
}

int BeMCContext::FindRestoreVolatiles(BeMCBlock* mcBlock, int instIdx)
{
	for (int checkIdx = instIdx + 1; checkIdx < (int)mcBlock->mInstructions.size(); checkIdx++)
	{
		BeMCInst* checkInst = mcBlock->mInstructions[checkIdx];
		if (checkInst->mKind == BeMCInstKind_RestoreVolatiles)
			return checkIdx;
	}
	return mcBlock->mInstructions.size() - 1; // Ret
}

void BeMCContext::EmitInstPrefix(BeMCInstForm instForm, BeMCInst* inst)
{
	switch (instForm)
	{
	case BeMCInstForm_R16_RM16:
	case BeMCInstForm_R16_RM64_ADDR:
	case BeMCInstForm_RM16_R16:
	case BeMCInstForm_RM16_IMM8:
	case BeMCInstForm_RM16_IMM16:
	case BeMCInstForm_RM64_R16_ADDR:
	case BeMCInstForm_RM16:
	case BeMCInstForm_R16:
		Emit(0x66);
	}

	switch (instForm)
	{
	case BeMCInstForm_R64_RM64:
		EmitREX(inst->mArg0, inst->mArg1, true);
		break;
	case BeMCInstForm_R8_RM8:
	case BeMCInstForm_R16_RM16:
	case BeMCInstForm_R32_RM32:
	case BeMCInstForm_R8_RM64_ADDR:
	case BeMCInstForm_R16_RM64_ADDR:
	case BeMCInstForm_R32_RM64_ADDR:
		EmitREX(inst->mArg0, inst->mArg1, false);
		break;
	case BeMCInstForm_R64_RM64_ADDR:
		EmitREX(inst->mArg0, inst->mArg1, true);
		break;

	case BeMCInstForm_RM8_R8:
	case BeMCInstForm_RM16_R16:
	case BeMCInstForm_RM32_R32:
	case BeMCInstForm_RM64_R8_ADDR:
	case BeMCInstForm_RM64_R16_ADDR:
	case BeMCInstForm_RM64_R32_ADDR:
		EmitREX(inst->mArg1, inst->mArg0, false);
		break;
	case BeMCInstForm_RM64_R64:
	case BeMCInstForm_RM64_R64_ADDR:
		EmitREX(inst->mArg1, inst->mArg0, true);
		break;

	case BeMCInstForm_RM8_IMM8:
	case BeMCInstForm_RM16_IMM8:
	case BeMCInstForm_RM16_IMM16:
	case BeMCInstForm_RM32_IMM8:
	case BeMCInstForm_RM32_IMM16:
	case BeMCInstForm_RM32_IMM32:
		EmitREX(inst->mArg1, inst->mArg0, false);
		break;

	case BeMCInstForm_RM64_IMM8:
	case BeMCInstForm_RM64_IMM16:
	case BeMCInstForm_RM64_IMM32:
	case BeMCInstForm_RM64_IMM64:
		EmitREX(inst->mArg1, inst->mArg0, true);
		break;
	}
}

void BeMCContext::EmitInst(BeMCInstForm instForm, uint16 codeBytes, BeMCInst* inst)
{
	EmitInstPrefix(instForm, inst);
	if (codeBytes <= 0xFF)
		mOut.Write((uint8)codeBytes);
	else
		mOut.Write((int16)codeBytes);
	switch (instForm)
	{
	case BeMCInstForm_R8_RM8:
	case BeMCInstForm_R16_RM16:
	case BeMCInstForm_R32_RM32:
	case BeMCInstForm_R64_RM64:
		EmitModRM(inst->mArg0, inst->mArg1);
		break;
	case BeMCInstForm_R8_RM64_ADDR:
	case BeMCInstForm_R16_RM64_ADDR:
	case BeMCInstForm_R32_RM64_ADDR:
	case BeMCInstForm_R64_RM64_ADDR:
		EmitModRM_Addr(inst->mArg0, inst->mArg1);
		break;
	case BeMCInstForm_RM8_R8:
	case BeMCInstForm_RM16_R16:
	case BeMCInstForm_RM32_R32:
	case BeMCInstForm_RM64_R64:
		EmitModRM(inst->mArg1, inst->mArg0);
		break;
	case BeMCInstForm_RM64_R8_ADDR:
	case BeMCInstForm_RM64_R16_ADDR:
	case BeMCInstForm_RM64_R32_ADDR:
	case BeMCInstForm_RM64_R64_ADDR:
		EmitModRM_Addr(inst->mArg1, inst->mArg0);
		break;
	case BeMCInstForm_RM64_IMM32:
		break;
	default:
		SoftFail("Notimpl EmitInst");
	}
}

void BeMCContext::EmitInst(BeMCInstForm instForm, uint16 codeBytes, uint8 rx, BeMCInst* inst)
{
	EmitInstPrefix(instForm, inst);
	BF_ASSERT(codeBytes != 0);
	if (codeBytes <= 0xFF)
		mOut.Write((uint8)codeBytes);
	else
		mOut.Write((int16)codeBytes);
	switch (instForm)
	{
	case BeMCInstForm_RM8_IMM8:
	case BeMCInstForm_RM16_IMM8:
	case BeMCInstForm_RM32_IMM8:
	case BeMCInstForm_RM64_IMM8:
		EmitModRM(rx, inst->mArg0, -1);
		mOut.Write((int8)inst->mArg1.GetImmediateInt());
		break;
	case BeMCInstForm_RM16_IMM16:
	case BeMCInstForm_RM32_IMM16:
	case BeMCInstForm_RM64_IMM16:
		EmitModRM(rx, inst->mArg0, -2);
		mOut.Write((int16)inst->mArg1.GetImmediateInt());
		break;
	case BeMCInstForm_RM32_IMM32:
	case BeMCInstForm_RM64_IMM32:
		EmitModRM(rx, inst->mArg0, -4);
		mOut.Write((int32)inst->mArg1.GetImmediateInt());
		break;
	case BeMCInstForm_RM64_IMM64:
		EmitModRM(rx, inst->mArg0, -8);
		mOut.Write((int64)inst->mArg1.GetImmediateInt());
		break;
	default:
		SoftFail("Notimpl EmitInst");
	}
}

void BeMCContext::EmitStdInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode_rm_r, uint8 opcode_r_rm, uint8 opcode_rm_imm, uint8 opcode_rm_imm_rx)
{
	switch (instForm)
	{
	case BeMCInstForm_R8_RM8: EmitInst(instForm, opcode_r_rm - 1, inst); break;
	case BeMCInstForm_R16_RM16:
	case BeMCInstForm_R32_RM32:
	case BeMCInstForm_R64_RM64: EmitInst(instForm, opcode_r_rm, inst); break;
	case BeMCInstForm_RM8_R8: EmitInst(instForm, opcode_rm_r - 1, inst); break;
	case BeMCInstForm_RM16_R16:
	case BeMCInstForm_RM32_R32:
	case BeMCInstForm_RM64_R64:	EmitInst(instForm, opcode_rm_r, inst); break;
		// These immediate forms assume an expansion to imm32, OR the register size for 8 and 16 bit registers
	case BeMCInstForm_RM8_IMM8: EmitInst(BeMCInstForm_RM8_IMM8, opcode_rm_imm - 1, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM16_IMM8: EmitInst(BeMCInstForm_RM16_IMM16, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM32_IMM8: EmitInst(BeMCInstForm_RM32_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM64_IMM8: EmitInst(BeMCInstForm_RM64_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM32_IMM16: EmitInst(BeMCInstForm_RM32_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM16_IMM16: EmitInst(BeMCInstForm_RM16_IMM16, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM64_IMM16: EmitInst(BeMCInstForm_RM64_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM32_IMM32: EmitInst(BeMCInstForm_RM32_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM64_IMM32: EmitInst(BeMCInstForm_RM64_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	default:
		Print();
		NotImpl();
	}
}

void BeMCContext::EmitStdInst(BeMCInstForm instForm, BeMCInst * inst, uint8 opcode_rm_r, uint8 opcode_r_rm, uint8 opcode_rm_imm, uint8 opcode_rm_imm_rx, uint8 opcode_rm_imm8, uint8 opcode_rm_imm8_rx)
{
	switch (instForm)
	{
	case BeMCInstForm_R8_RM8: EmitInst(instForm, opcode_r_rm - 1, inst); break;
	case BeMCInstForm_R16_RM16:
	case BeMCInstForm_R32_RM32:
	case BeMCInstForm_R64_RM64: EmitInst(instForm, opcode_r_rm, inst); break;
	case BeMCInstForm_RM8_R8: EmitInst(instForm, opcode_rm_r - 1, inst); break;
	case BeMCInstForm_RM16_R16:
	case BeMCInstForm_RM32_R32:
	case BeMCInstForm_RM64_R64:	EmitInst(instForm, opcode_rm_r, inst); break;

	case BeMCInstForm_RM8_IMM8: EmitInst(BeMCInstForm_RM8_IMM8, opcode_rm_imm - 1, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM16_IMM8: EmitInst(BeMCInstForm_RM16_IMM8, opcode_rm_imm8, opcode_rm_imm8_rx, inst); break;
	case BeMCInstForm_RM32_IMM8: EmitInst(BeMCInstForm_RM32_IMM8, opcode_rm_imm8, opcode_rm_imm8_rx, inst); break;
	case BeMCInstForm_RM64_IMM8: EmitInst(BeMCInstForm_RM64_IMM8, opcode_rm_imm8, opcode_rm_imm8_rx, inst); break;
		// These immediate forms assume an expansion to imm32, OR the register size for 8 and 16 bit registers
	case BeMCInstForm_RM32_IMM16: EmitInst(BeMCInstForm_RM32_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM16_IMM16: EmitInst(BeMCInstForm_RM16_IMM16, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM64_IMM16: EmitInst(BeMCInstForm_RM64_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM32_IMM32: EmitInst(BeMCInstForm_RM32_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	case BeMCInstForm_RM64_IMM32: EmitInst(BeMCInstForm_RM64_IMM32, opcode_rm_imm, opcode_rm_imm_rx, inst); break;
	default:
		NotImpl();
	}
}

bool BeMCContext::EmitStdXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode)
{
	bool is64Bit = false;
	switch (instForm)
	{
	case BeMCInstForm_R64_F32:
	case BeMCInstForm_XMM32_RM64:
		is64Bit = true;
		// Fallthrough
	case BeMCInstForm_R32_F32:
	case BeMCInstForm_XMM32_RM32:
	case BeMCInstForm_XMM32_IMM:
	case BeMCInstForm_XMM32_FRM32:
	case BeMCInstForm_XMM64_FRM32:
		Emit(0xF3); EmitREX(inst->mArg0, inst->mArg1, is64Bit);
		Emit(0x0F); Emit(opcode);
		EmitModRM(inst->mArg0, inst->mArg1);
		return true;

	case BeMCInstForm_R64_F64:
	case BeMCInstForm_XMM64_RM64:
		is64Bit = true;
		// Fallthrough
	case BeMCInstForm_R32_F64:
	case BeMCInstForm_XMM64_RM32:
	case BeMCInstForm_XMM64_IMM:
	case BeMCInstForm_XMM64_FRM64:
	case BeMCInstForm_XMM32_FRM64:
		Emit(0xF2); EmitREX(inst->mArg0, inst->mArg1, is64Bit);
		Emit(0x0F); Emit(opcode);
		EmitModRM(inst->mArg0, inst->mArg1);
		return true;
	case BeMCInstForm_XMM128_RM128:
		{
			BeTypeCode elemType = BeTypeCode_Float;
			auto arg0 = GetFixedOperand(inst->mArg0);
			auto arg1 = GetFixedOperand(inst->mArg1);

			auto arg0Type = GetType(inst->mArg0);
			auto arg1Type = GetType(inst->mArg1);
			if (arg0Type->IsExplicitVectorType())
			{
				auto vecType = (BeVectorType*)arg0Type;
				elemType = vecType->mElementType->mTypeCode;
			}

			if (arg1Type->IsFloat())
			{
				if (elemType == BeTypeCode_Double)
					Emit(0x66);
				EmitREX(arg1, arg1, false);
				Emit(0x0F); Emit(0xC6); // SHUFPS / SHUFPD
				EmitModRM(arg1, arg1);
				Emit(0);
			}

			if (elemType == BeTypeCode_Double)
				Emit(0x66);
			EmitREX(arg0, arg1, is64Bit);
			Emit(0x0F); Emit(opcode);
			EmitModRM(arg0, arg1);
			return true;
		}
		break;
 	case BeMCInstForm_FRM128_XMM128:
// 		{
// 			Emit(0xF2); EmitREX(inst->mArg0, inst->mArg1, is64Bit);
// 			Emit(0x0F); Emit(opcode);
// 			EmitModRM(inst->mArg0, inst->mArg1);
// 		}
		NOP;

 		break;
	}

	return false;
}

bool BeMCContext::EmitStdXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode, uint8 opcode_dest_frm)
{
	bool is64Bit = false;
	switch (instForm)
	{
	case BeMCInstForm_FRM32_XMM32:
		Emit(0xF3); EmitREX(inst->mArg1, inst->mArg0, is64Bit);
		Emit(0x0F); Emit(opcode_dest_frm);
		EmitModRM(inst->mArg1, inst->mArg0);
		return true;
	case BeMCInstForm_FRM64_XMM64:
		Emit(0xF2); EmitREX(inst->mArg1, inst->mArg0, is64Bit);
		Emit(0x0F); Emit(opcode_dest_frm);
		EmitModRM(inst->mArg1, inst->mArg0);
		return true;
	default:
		return EmitStdXMMInst(instForm, inst, opcode);
	}

	return false;
}

bool BeMCContext::EmitPackedXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode)
{
	bool is64Bit = false;
	switch (instForm)
	{
	case BeMCInstForm_R64_F32:
	case BeMCInstForm_XMM32_RM64:
		is64Bit = true;
		// Fallthrough
	case BeMCInstForm_R32_F32:
	case BeMCInstForm_XMM32_RM32:
	case BeMCInstForm_XMM32_IMM:
	case BeMCInstForm_XMM32_FRM32:
	case BeMCInstForm_XMM64_FRM32: // CVTSS2SD
		EmitREX(inst->mArg0, inst->mArg1, is64Bit);
		Emit(0x0F); Emit(opcode);
		EmitModRM(inst->mArg0, inst->mArg1);
		return true;

	case BeMCInstForm_R64_F64:
	case BeMCInstForm_XMM64_RM64:
		is64Bit = true;
		// Fallthrough
	case BeMCInstForm_R32_F64:
	case BeMCInstForm_XMM64_RM32:
	case BeMCInstForm_XMM64_IMM:
	case BeMCInstForm_XMM64_FRM64:
	case BeMCInstForm_XMM32_FRM64: // CVTSD2SS
		EmitREX(inst->mArg0, inst->mArg1, is64Bit);
		Emit(0x0F); Emit(opcode);
		EmitModRM(inst->mArg0, inst->mArg1);
		return true;
	}

	return false;
}

BeMCOperand BeMCContext::IntXMMGetPacked(BeMCOperand arg, BeVectorType* vecType)
{
	auto argType = GetType(arg);
	if (!argType->IsVector())
	{
		BeMCOperand xmm15;
		xmm15.mReg = X64Reg_M128_XMM15;
		xmm15.mKind = BeMCOperandKind_NativeReg;

		if (arg.IsImmediate())
		{
			BeMCOperand immOperand;
			immOperand.mKind = BeMCOperandKind_Immediate_int32x4;
			immOperand.mImmediate = arg.mImmediate;

			Emit(0xF3);
			EmitREX(xmm15, immOperand, false);
			Emit(0x0F); Emit(0x6F); // MOVDQU
			EmitModRM(xmm15, immOperand);
		}
		else
		{
			Emit(0x66);
			EmitREX(xmm15, arg, false);
			Emit(0x0F); Emit(0x6E); // MOVD
			EmitModRM(xmm15, arg);

			if (vecType->mElementType->mTypeCode == BeTypeCode_Int16)
				Emit(0xF2);
			else
				Emit(0x66);
			EmitREX(xmm15, xmm15, false);
			Emit(0x0F);
			switch (vecType->mElementType->mTypeCode)
			{
			case BeTypeCode_Int8:
				Emit(0x38); Emit(0x00); // PSHUFB
				break;
			case BeTypeCode_Int16:
				Emit(0x70); // PSHUFW
				break;
			case BeTypeCode_Int32:
				Emit(0x70); // PSHUFD
				break;
			}
			EmitModRM(xmm15, xmm15);
			Emit(0);
		}

		arg = xmm15;
	}
	return arg;
}

bool BeMCContext::EmitIntXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode)
{
	if (instForm != BeMCInstForm_XMM128_RM128)
		return false;

	auto arg0 = GetFixedOperand(inst->mArg0);
	auto arg1 = GetFixedOperand(inst->mArg1);
	auto arg0Type = GetType(inst->mArg0);
	auto arg1Type = GetType(inst->mArg1);

	if (arg0Type->IsExplicitVectorType())
	{
		auto vecType = (BeVectorType*)arg0Type;
		if ((vecType->mElementType->mTypeCode == BeTypeCode_Int8) ||
			(vecType->mElementType->mTypeCode == BeTypeCode_Int16) ||
			(vecType->mElementType->mTypeCode == BeTypeCode_Int32))
		{
			arg1 = IntXMMGetPacked(arg1, vecType);

			Emit(0x66);
			EmitREX(arg0, arg1, false);
			Emit(0x0F);
			switch (vecType->mElementType->mTypeCode)
			{
			case BeTypeCode_Int8:
				Emit(opcode);
				break;
			case BeTypeCode_Int16:
				Emit(opcode + 1);
				break;
			case BeTypeCode_Int32:
				Emit(opcode + 2);
				break;
			}

			EmitModRM(arg0, arg1);
			return true;
		}
	}
	return false;
}

bool BeMCContext::EmitIntBitwiseXMMInst(BeMCInstForm instForm, BeMCInst* inst, uint8 opcode)
{
	if (instForm != BeMCInstForm_XMM128_RM128)
		return false;

	auto arg0 = GetFixedOperand(inst->mArg0);
	auto arg1 = GetFixedOperand(inst->mArg1);
	auto arg0Type = GetType(inst->mArg0);

	if (arg0Type->IsExplicitVectorType())
	{
		auto vecType = (BeVectorType*)arg0Type;
		if ((vecType->mElementType->mTypeCode == BeTypeCode_Int8) ||
			(vecType->mElementType->mTypeCode == BeTypeCode_Int16) ||
			(vecType->mElementType->mTypeCode == BeTypeCode_Int32))
		{
			arg1 = IntXMMGetPacked(arg1, vecType);

			Emit(0x66);
			EmitREX(arg0, arg1, false);
			Emit(0x0F);
			Emit(opcode); // PXOR
			EmitModRM(arg0, arg1);
			return true;
		}
	}
	return false;
}

void BeMCContext::EmitAggMov(const BeMCOperand& dest, const BeMCOperand& src)
{
	BeRMParamsInfo rmInfo;
	GetRMParams(dest, rmInfo);

	BF_ASSERT(src.mKind == BeMCOperandKind_ConstAgg);
	auto aggConstant = src.mConstant;

	BeConstData constData;
	aggConstant->GetData(constData);

	Array<uint8>& dataVec = constData.mData;

	int memSize = dataVec.size();
	int curOfs = 0;

	bool allowRep = dataVec.size() >= BF_REP_MOV_LIMIT;

	union IntUnion
	{
		int64 mInt64;
		int32 mInt32;
		int16 mInt16;
		int8 mInt8;
	};

	bool isValRegSet = false;
	int64 curVal = 0;

	auto _EmitMovR11 = [&](int64 val)
	{
		int64 prevVal = curVal;
		bool wasValRegSet = isValRegSet;
		isValRegSet = true;
		curVal = val;

		if ((wasValRegSet) && (val == prevVal))
			return;

		if (val == 0)
		{
			// xor r11, r11
			Emit(0x4D); Emit(0x31); Emit(0xDB);
			return;
		}

		if (wasValRegSet)
		{
			if ((val & ~0xFF) == (prevVal & ~0xFF))
			{
				// mov r11b, val8
				Emit(0x41); Emit(0xB3);
				Emit((int8)val);
				return;
			}

			if ((val & ~0xFFFF) == (prevVal & ~0xFFFF))
			{
				// mov r11w, val16
				Emit(0x66); Emit(0x41); Emit(0xBB);
				mOut.Write((int16)val);
				return;
			}
		}

		if ((val >= 0) && (val <= 0x7FFFFFFF))
		{
			// mov r11d, val32
			Emit(0x41); Emit(0xBB);
			mOut.Write((int32)val);
		}
		else if ((val >= -0x80000000LL) && (val <= 0x7FFFFFFF))
		{
			// mov r11, val32 (sign extended)
			Emit(0x49); Emit(0xC7); Emit(0xC3);
			mOut.Write((int32)val);
		}
		else
		{
			// movabs r11, val64
			Emit(0x49); Emit(0xBB);
			mOut.Write((int64)val);
		}
	};

	for (; curOfs <= memSize - 8; )
	{
		if (allowRep)
		{
			uint8 val = dataVec[curOfs];

			int repSize = 0;
			int checkOfs = curOfs;
			while (checkOfs < (int)dataVec.size())
			{
				if (dataVec[checkOfs] != val)
					break;
				checkOfs++;
				repSize++;
			}

			if (repSize >= 16)
			{
				bool regSaved = false;
				if ((rmInfo.mRegA == X64Reg_RAX) ||
					(rmInfo.mRegA == X64Reg_RCX) ||
					(rmInfo.mRegA == X64Reg_RDI))
				{
					BF_ASSERT(rmInfo.mRegB == X64Reg_None);
					// mov R11, regA
					Emit(0x49); Emit(0x89);
					EmitModRM(BeMCOperand::FromReg(rmInfo.mRegA), BeMCOperand::FromReg(X64Reg_R11));
					regSaved = true;
				}

				// lea rdi, <dest+curOfs>
				EmitREX(BeMCOperand::FromReg(X64Reg_RDI), dest, true);
				Emit(0x8D);
				EmitModRMRel(EncodeRegNum(X64Reg_RDI), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + curOfs);

				// mov al, <val>
				Emit(0xB0); Emit(val);

				// mov edx, <repSize>
				Emit(0xB9);
				mOut.Write((int32)repSize);

				// rep stosb
				Emit(0xF3); Emit(0xAA);

				if (regSaved)
				{
					// mov regA, R11
					Emit(0x4C); Emit(0x89);
					EmitModRM(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(rmInfo.mRegA));
				}

				curOfs += repSize;
				continue;
			}
		}

		int64 newValReg = *((int64*)&dataVec[curOfs]);
		_EmitMovR11(newValReg);

		// mov <dest+curOfs>, R11
		EmitREX(BeMCOperand::FromReg(X64Reg_R11), dest, true);
		Emit(0x89);
		EmitModRMRel(EncodeRegNum(X64Reg_R11), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + curOfs);
		curOfs += 8;
	}

	for (; curOfs <= memSize - 4; curOfs += 4)
	{
		int64 newValReg = curVal;
		*((int32*)&newValReg) = *((int32*)&dataVec[curOfs]);
		_EmitMovR11(newValReg);

		// mov <dest+curOfs>, R11d
		EmitREX(BeMCOperand::FromReg(X64Reg_R11D), dest, false);
		Emit(0x89);
		EmitModRMRel(EncodeRegNum(X64Reg_R11D), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + curOfs);
	}

	for (; curOfs <= memSize - 2; curOfs += 2)
	{
		int64 newValReg = curVal;
		*((int16*)&newValReg) = *((int16*)&dataVec[curOfs]);
		_EmitMovR11(newValReg);

		// mov <dest+curOfs>, R11w
		Emit(0x66); EmitREX(BeMCOperand::FromReg(X64Reg_R11W), dest, false);
		Emit(0x89);
		EmitModRMRel(EncodeRegNum(X64Reg_R11W), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + curOfs);
	}

	for (; curOfs <= memSize - 1; curOfs += 1)
	{
		int64 newValReg = curVal;
		*((int8*)&newValReg) = *((int8*)&dataVec[curOfs]);
		_EmitMovR11(newValReg);

		// mov <dest+curOfs>, R11b
		EmitREX(BeMCOperand::FromReg(X64Reg_R11B), dest, false);
		Emit(0x89 - 1);
		EmitModRMRel(EncodeRegNum(X64Reg_R11B), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + curOfs);
	}

	for (auto constVal : constData.mConsts)
	{
		BeMCRelocation reloc;

		// movabs r11, val64
		Emit(0x49); Emit(0xBB);

		reloc.mKind = BeMCRelocationKind_ADDR64;
		reloc.mOffset = mOut.GetPos();

		auto operand = GetOperand(constVal.mConstant);

		reloc.mSymTableIdx = operand.mSymbolIdx;
		mCOFFObject->mTextSect.mRelocs.push_back(reloc);
		mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);

		mOut.Write((int64)0);

		// mov <dest+curOfs>, R11
		EmitREX(BeMCOperand::FromReg(X64Reg_R11), dest, true);
		Emit(0x89);
		EmitModRMRel(EncodeRegNum(X64Reg_R11), rmInfo.mRegA, rmInfo.mRegB, 1, rmInfo.mDisp + constVal.mIdx);
	}
}

void BeMCContext::DoCodeEmission()
{
	BP_ZONE("BeMCContext::DoCodeEmission");

	struct BeMCJump
	{
	public:
		int mCodeOffset;
		int mLabelIdx;
		int mJumpKind; // 0 = rel8, 1 = rel16 (not valid in x64), 2 = rel32
		BeCmpKind mCmpKind;

	public:
		BeMCJump()
		{
			mCmpKind = BeCmpKind_None;
			mCodeOffset = -1;
			mLabelIdx = -1;
			mJumpKind = -1;
		}
	};

	SizedArray<int, 64> labelPositions;
	labelPositions.resize(mCurLabelIdx);
	for (int labelIdx = 0; labelIdx < mCurLabelIdx; labelIdx++)
		labelPositions[labelIdx] = -1;
	SizedArray<BeMCJump, 64> deferredJumps;

	auto& xdata = mCOFFObject->mXDataSect.mData;
	int xdataStartPos = xdata.GetPos();

	int textSectStartPos = mOut.GetPos();
	int lastLabelIdx = -1;

	bool hasPData = mStackSize != 0;
	if (hasPData)
	{
		// xdata must be DWORD aligned
		while ((xdataStartPos & 3) != 0)
		{
			xdata.Write((uint8)0);
			xdataStartPos++;
		}

		// pdata starting loc
		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = mCOFFObject->mPDataSect.mData.GetPos();
		reloc.mSymTableIdx = mCOFFObject->GetSymbol(mBeFunction)->mIdx;
		mCOFFObject->mPDataSect.mRelocs.push_back(reloc);
		mCOFFObject->mPDataSect.mData.Write((int32)0);

		xdata.Write((uint8)1); // version
		xdata.Write((uint8)0); // prolog size (placeholder)
		xdata.Write((uint8)0); // num unwind codes (placeholder)
		if (mUseBP)
		{
			int frameReg = 5;
			int offset = 0; // scaled by 16
			xdata.Write((uint8)(frameReg + (offset << 4))); // frame register
		}
		else
			xdata.Write((uint8)0); // frame register
	}

	struct BeMCDeferredUnwind
	{
		BeMCInst* mUnwindInst;
		int mCodePos;
	};

	struct BeDbgInstPos
	{
		int mPos;
		int mOrigPos;
		BeMCInst* mInst;
	};

	SizedArray<BeDbgInstPos, 64> dbgInstPositions;

	String dbgStr;
	if (mDebugging)
	{
		dbgStr += "DoCodeEmission:\n";
	}

	SizedArray<BeMCDeferredUnwind, 64> deferredUnwinds;
	mCurDbgLoc = NULL;
	bool needsInstructionAtDbgLoc = false;

	bool dbgExtendLifetime = false;

	int hotJumpLen = 5;
	int funcCodePos = 0;

	Array<BeDbgVariable*> deferredGapEnds;

	BeVTrackingList* vregsLive = mLivenessContext.AllocEmptyList();
	BeVTrackingList* vregsInitialized = mVRegInitializedContext.AllocEmptyList();
	for (auto mcBlock : mBlocks)
	{
		for (auto inst : mcBlock->mInstructions)
		{
			mActiveInst = inst;

			if (mDebugging)
			{
				ToString(inst, dbgStr, true, true);
			}

			auto checkInst = inst;

			int newFuncCodePos = mOut.GetPos() - textSectStartPos;
			if (newFuncCodePos != funcCodePos)
			{
				funcCodePos = newFuncCodePos;
				if (dbgExtendLifetime)
				{
					dbgExtendLifetime = false;

					if (mDebugging)
					{
						//
					}
				}
			}

			while (!deferredGapEnds.IsEmpty())
			{
				BeDbgVariable* dbgVar = deferredGapEnds.back();
				deferredGapEnds.pop_back();

				auto& range = dbgVar->mGaps.back();
				range.mLength = funcCodePos - range.mOffset;
				if (range.mLength <= 0)
				{
					dbgVar->mGaps.pop_back();
				}
			}

			if ((inst->mKind == BeMCInstKind_LifetimeEnd) || (inst->mKind == BeMCInstKind_LifetimeExtend))
			{
				if (needsInstructionAtDbgLoc)
				{
					// This 'dbgExtendLifetime' is useful in cases like
					// repeat { int a = 123; } while (false);
					//  Otherwise 'a' wouldn't be viewable
					dbgExtendLifetime = true;
				}
			}

			switch (inst->mKind)
			{
			case BeMCInstKind_Label:
				//Why did we set this FALSE here? Just because we have a label doesn't mean we've emitted any actual instructions....
				// Leaving this out broke a test where we couldn't view a 'this' at the end of a particular method
				//dbgExtendLifetime = false;
				break;
			}

			if ((inst->mDbgLoc != NULL) && (mDbgFunction != NULL))
			{
				if (inst->mDbgLoc != mCurDbgLoc)
				{
					bool allowEmission = true;
					if (inst->mKind == BeMCInstKind_DbgDecl)
					{
						// We need to separate out dbgDecls if we are attempting to extend a previous one,
						//  otherwise wait for a real instruction
						if (dbgExtendLifetime)
						{
							mOut.Write((uint8)0x90);
							funcCodePos++;
							needsInstructionAtDbgLoc = false;
							dbgExtendLifetime = false;
						}
						else
							allowEmission = false;
					}
					else
					{
						mCurDbgLoc = inst->mDbgLoc;
						if ((!mDbgFunction->mEmissions.empty()) && (mDbgFunction->mEmissions.back().mPos == funcCodePos))
						{
							bool hadEmission = false;
							if (needsInstructionAtDbgLoc)
							{
								// We only need an instruction on this LINE, not necessarily the same column or lexical scope
								auto prevDbgLoc = mDbgFunction->mEmissions.back().mDbgLoc;
								if ((prevDbgLoc->mLine != mCurDbgLoc->mLine) ||
									(prevDbgLoc->GetDbgFile() != mCurDbgLoc->GetDbgFile()) ||
									((prevDbgLoc->mColumn != -1) != (mCurDbgLoc->mColumn != -1)))
								{
									// We didn't emit anything so we need to put a NOP in
									mOut.Write((uint8)0x90);
									funcCodePos++;
									needsInstructionAtDbgLoc = false;
									hadEmission = true;
									dbgExtendLifetime = false;
								}
							}

							if (!hadEmission)
							{
								// No code emitted, take back the last codeEmission
								mDbgFunction->mEmissions.pop_back();
							}
						}
					}

					if (allowEmission)
					{
						BeDbgCodeEmission codeEmission;
						codeEmission.mDbgLoc = mCurDbgLoc;
						codeEmission.mPos = funcCodePos;
						mDbgFunction->mEmissions.push_back(codeEmission);
					}
				}
			}

			BeMCJump jump;
			auto instForm = GetInstForm(inst);
			auto arg0 = GetFixedOperand(inst->mArg0);
			auto arg1 = GetFixedOperand(inst->mArg1);
			auto arg0Type = GetType(inst->mArg0);
			auto arg1Type = GetType(inst->mArg1);

			// Generate gaps
			if ((inst->mVRegsInitialized != NULL) && (vregsInitialized != inst->mVRegsInitialized))
			{
				for (int changeIdx = 0; changeIdx < inst->mVRegsInitialized->mNumChanges; changeIdx++)
				{
					int vregInitializedIdx = inst->mVRegsInitialized->GetChange(changeIdx);
					if (vregInitializedIdx >= 0) // Added
					{
						if (vregInitializedIdx >= mVRegInitializedContext.mNumItems)
							continue;
						auto dbgVar = mVRegInfo[vregInitializedIdx]->mDbgVariable;
						if ((dbgVar != NULL) && (!dbgVar->mDbgLifeEnded))
						{
							// Wasn't initialized, now it is
							if (!dbgVar->mGaps.empty())
							{
								auto& range = dbgVar->mGaps.back();

								if (dbgVar->mDeclLifetimeExtend)
								{
									if (mDebugging)
									{
										dbgStr += StrFormat("#### %d Dbg Applied LifetimeExtend %s from %d to %d\n", funcCodePos, dbgVar->mName.c_str(), dbgVar->mDeclEnd, range.mOffset);
									}

									range.mOffset++;
									if (range.mOffset > dbgVar->mDeclEnd)
										dbgVar->mDeclEnd = range.mOffset;
									dbgVar->mDeclLifetimeExtend = false;
								}

								// We have to check for this because it's possible we get multiple adds
								if (range.mLength == -1)
								{
									deferredGapEnds.Add(dbgVar);
								}

								if (mDebugging)
								{
									dbgStr += StrFormat("#### %d Dbg End Gap %s\n", funcCodePos, dbgVar->mName.c_str());
								}
							}
						}
					}
					else // Removed
					{
						vregInitializedIdx = -vregInitializedIdx - 1;
						if (vregInitializedIdx >= mVRegInitializedContext.mNumItems)
							continue;
						auto dbgVar = mVRegInfo[vregInitializedIdx]->mDbgVariable;
						if ((dbgVar != NULL) && (!dbgVar->mDbgLifeEnded))
						{
							bool inGap = false;
							if (!dbgVar->mGaps.empty())
								inGap = dbgVar->mGaps.back().mLength == -1;

							if (!inGap)
							{
								if (mDebugging)
								{
									dbgStr += StrFormat("#### %d Dbg Start Gap %s\n", funcCodePos, dbgVar->mName.c_str());
								}

								if (dbgVar->mDeclStart == -1)
								{
									// Ignore
								}
								else
								{
									// Starting a new gap
									BeDbgVariableRange range;
									range.mOffset = funcCodePos;
									range.mLength = -1;
									dbgVar->mGaps.push_back(range);
								}
							}
						}
					}
				}

				vregsInitialized = inst->mVRegsInitialized;
			}

			// Finish range for variables exiting liveness.  Since liveness is built in end-to-start order,
			//  an "add" means that the NEXT instruction won't have this entry (if the next entry doesn't share
			//  the exact liveness value)
			if ((inst->mLiveness != NULL) && (inst->mLiveness != vregsLive))
			{
				for (int changeIdx = 0; changeIdx < vregsLive->mNumChanges; changeIdx++)
				{
					int vregLiveIdx = vregsLive->GetChange(changeIdx);
					if (vregLiveIdx < 0)
						continue;

					// Check for both the 'exists' flag changing and the 'in scope' flag changing
					int vregIdx = vregLiveIdx % mLivenessContext.mNumItems;
					auto dbgVar = mVRegInfo[vregIdx]->mDbgVariable;
					if ((dbgVar != NULL) && (!dbgVar->mDbgLifeEnded))
					{
						//if (!mLivenessContext.IsSet(inst->mLiveness, vregIdx))
						{
							if (!dbgVar->mGaps.empty())
							{
								auto& lastGap = dbgVar->mGaps.back();
								if (lastGap.mLength == -1)
								{
									// Unused gap
									//dbgVar->mGaps.pop_back();

									// Don't remove this 'unused gap' because the variable may come back alive
									//  This can happen in blocks after NoReturn calls
								}
							}

							if (dbgVar->mDeclStart != -1)
							{
								dbgVar->mDeclEnd = funcCodePos;

								if (mDebugging)
								{
									dbgStr += StrFormat("#### %d Dbg Setting DeclEnd %s\n", funcCodePos, dbgVar->mName.c_str());
								}
							}

							if ((mDebugging) && (dbgExtendLifetime))
							{
								dbgStr += StrFormat("#### %d Dbg Setting LifetimeExtend %s\n", funcCodePos, dbgVar->mName.c_str());
							}
							dbgVar->mDeclLifetimeExtend = dbgExtendLifetime;
							//if (dbgExtendLifetime)
							//dbgVar->mDeclEnd++;
						}
					}
				}

				vregsLive = inst->mLiveness;
			}

			if (mDebugging)
			{
				BeDbgInstPos dbgInstPos = { funcCodePos, funcCodePos, inst };
				dbgInstPositions.push_back(dbgInstPos);
			}

			switch (inst->mKind)
			{
			case BeMCInstKind_DbgDecl:
				{
					auto vregInfo = GetVRegInfo(inst->mArg0);
					auto dbgVar = vregInfo->mDbgVariable;
					dbgVar->mDeclStart = funcCodePos;

					while (vregInfo->IsDirectRelTo())
					{
						if (vregInfo->mRelTo.IsNativeReg())
						{
							break;
						}
						else
							vregInfo = GetVRegInfo(vregInfo->mRelTo);
					}

					BeMCOperand defArg = inst->mArg0;
					if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1) && (mCompositeRetVRegIdx != inst->mArg0.mVRegIdx))
					{
						defArg = BeMCOperand::FromVReg(mCompositeRetVRegIdx);
						if ((inst->mArg0.mKind == BeMCOperandKind_VReg) && (vregInfo->mType->IsNonVectorComposite()))
							defArg.mKind = BeMCOperandKind_VRegLoad;
						vregInfo = mVRegInfo[mCompositeRetVRegIdx];
					}

					if (vregInfo->mReg != X64Reg_None)
					{
						if ((defArg.mKind == BeMCOperandKind_VRegAddr) || (dbgVar->mIsValue))
							dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_Reg;
						else
							dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_Indexed;
						dbgVar->mPrimaryLoc.mReg = vregInfo->mReg;
						dbgVar->mPrimaryLoc.mOfs = 0;
					}
					else if (vregInfo->mRelTo.IsNativeReg())
					{
						if ((defArg.mKind == BeMCOperandKind_VRegAddr) || (dbgVar->mIsValue))
							dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_Reg;
						else
							dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_Indexed;
						dbgVar->mPrimaryLoc.mReg = vregInfo->mRelTo.mReg;
						if (vregInfo->mRelOffset.IsImmediateInt())
						{
							dbgVar->mPrimaryLoc.mOfs = vregInfo->mRelOffset.mImmediate;
						}
						else
							BF_ASSERT(!vregInfo->mRelOffset);
					}
					else if (vregInfo->mRelTo.mKind == BeMCOperandKind_SymbolAddr)
					{
						SoftFail("Not supported SymbolAddr");
						//dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_SymbolAddr;
						//dbgVar->mPrimaryLoc.mOfs = vregInfo->mRelTo.mSymbolIdx;
					}

					if ((dbgVar->mPrimaryLoc.mKind == BeDbgVariableLoc::Kind_None) && (vregInfo->mType->mSize != 0))
					{
						dbgVar->mPrimaryLoc.mKind = BeDbgVariableLoc::Kind_Indexed;
						GetValAddr(BeMCOperand::FromVRegAddr(inst->mArg0.mVRegIdx), dbgVar->mPrimaryLoc.mReg, dbgVar->mPrimaryLoc.mOfs);
					}

					if (!mVRegInitializedContext.IsSet(vregsInitialized, inst->mArg0.mVRegIdx))
					{
						bool inGap = false;
						if (!dbgVar->mGaps.empty())
							inGap = dbgVar->mGaps.back().mLength == -1;

						if (!inGap)
						{
							if (mDebugging)
							{
								dbgStr += StrFormat(" Dbg Starting Gap %s\n", dbgVar->mName.c_str());
							}

							BeDbgVariableRange range;
							range.mOffset = funcCodePos;
							range.mLength = -1;
							dbgVar->mGaps.push_back(range);
						}
					}

					if (vregInfo->mVolatileVRegSave != -1)
					{
						auto savedVRegInfo = mVRegInfo[vregInfo->mVolatileVRegSave];
						dbgVar->mSavedLoc.mKind = BeDbgVariableLoc::Kind_Indexed;
						GetValAddr(BeMCOperand::FromVRegAddr(vregInfo->mVolatileVRegSave), dbgVar->mSavedLoc.mReg, dbgVar->mSavedLoc.mOfs);
					}
				}
				break;
			case BeMCInstKind_LifetimeExtend:
				break;
			case BeMCInstKind_LifetimeStart:
				break;
			case BeMCInstKind_LifetimeEnd:
				break;
			case BeMCInstKind_LifetimeSoftEnd:
				{
					auto vregInfo = GetVRegInfo(inst->mArg0);
					if ((vregInfo != NULL) && (vregInfo->mDbgVariable != NULL))
					{
						auto dbgVar = vregInfo->mDbgVariable;
						if (dbgVar->mDeclStart != -1)
						{
							dbgVar->mDeclEnd = funcCodePos;
							dbgVar->mDeclLifetimeExtend = false;
							dbgVar->mDbgLifeEnded = true;
							BF_ASSERT((uint)dbgVar->mDeclEnd >= (uint)dbgVar->mDeclStart);
						}
					}
				}
				break;
			case BeMCInstKind_ValueScopeSoftEnd:
				break;
			case BeMCInstKind_ValueScopeHardEnd:
				break;
			case BeMCInstKind_MemSet:
				{
					// This doesn't currently attempt to align properly
					int memSize = inst->mArg0.mMemSetInfo.mSize;
					uint8 memValue = inst->mArg0.mMemSetInfo.mValue;
					int curOfs = 0;

					X64CPURegister destReg = X64Reg_R10;
					int destOfs = 0;

					if (inst->mArg1.IsNativeReg())
					{
						destReg = inst->mArg1.mReg;
					}
					else if (inst->mArg1)
					{
						//BF_ASSERT(inst->mArg1.mKind == BeMCOperandKind_VReg);
						GetValAddr(inst->mArg1, destReg, destOfs);
					}

					if (memValue == 0)
					{
						// xor r11, r11
						Emit(0x4D); Emit(0x31); Emit(0xDB);
					}
					else
					{
						// mov r11, memValue*8
						Emit(0x49); Emit(0xBB);
						for (int i = 0; i < 8; i++)
							Emit(memValue);
					}

					for (; curOfs <= memSize - 8; curOfs += 8)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(destReg), true);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					// If there's more that one 'small' write required then we just do another 64-bit move
					//  which overlaps partially into the previous 64-bit move
					if ((memSize > 8) && (curOfs < memSize) && (memSize - curOfs != 4) &&
						(memSize - curOfs != 2) && (memSize - curOfs != 1))
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(destReg), true);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), destReg, X64Reg_None, 1, (memSize - 8) + destOfs);

						curOfs = memSize;
					}

					for (; curOfs <= memSize - 4; curOfs += 4)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11D), BeMCOperand::FromReg(destReg), false);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11D), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					for (; curOfs <= memSize - 2; curOfs += 2)
					{
						Emit(0x66); EmitREX(BeMCOperand::FromReg(X64Reg_R11W), BeMCOperand::FromReg(destReg), false);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11W), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					for (; curOfs <= memSize - 1; curOfs += 1)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11B), BeMCOperand::FromReg(destReg), false); //-V530
						Emit(0x89 - 1);
						EmitModRMRel(EncodeRegNum(X64Reg_R11B), destReg, X64Reg_None, 1, curOfs + destOfs);
					}
				}
				break;
			case BeMCInstKind_MemCpy:
				{
					// This doesn't currently attempt to align properly
					int memSize = inst->mArg0.mMemCpyInfo.mSize;
					int curOfs = 0;

					X64CPURegister destReg = X64Reg_R10;
					X64CPURegister srcReg = X64Reg_R11;
					int destOfs = 0;
					int srcOfs = 0;

					if (inst->mArg1)
					{
						BF_ASSERT(inst->mArg1.mKind == BeMCOperandKind_VRegPair);
						GetValAddr(BeMCOperand::FromEncoded(inst->mArg1.mVRegPair.mVRegIdx0), destReg, destOfs);
						GetValAddr(BeMCOperand::FromEncoded(inst->mArg1.mVRegPair.mVRegIdx1), srcReg, srcOfs);
					}

					if ((srcReg == destReg) && (srcOfs == destOfs))
						break;

					for (; curOfs <= memSize - 8; curOfs += 8)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(srcReg), true);
						Emit(0x8B);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), srcReg, X64Reg_None, 1, curOfs + srcOfs);

						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(destReg), true);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					// If there's more that one 'small' write required then we just do another 64-bit move
					//  which overlaps partially into the previous 64-bit move
					if ((memSize > 8) && (curOfs < memSize) && (memSize - curOfs != 4) &&
						(memSize - curOfs != 2) && (memSize - curOfs != 1))
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(srcReg), true);
						Emit(0x8B);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), srcReg, X64Reg_None, 1, (memSize - 8) + srcOfs);

						EmitREX(BeMCOperand::FromReg(X64Reg_R11), BeMCOperand::FromReg(destReg), true);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11), destReg, X64Reg_None, 1, (memSize - 8) + destOfs);

						curOfs = memSize;
					}

					for (; curOfs <= memSize - 4; curOfs += 4)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11D), BeMCOperand::FromReg(srcReg), false);
						Emit(0x8B);
						EmitModRMRel(EncodeRegNum(X64Reg_R11D), srcReg, X64Reg_None, 1, curOfs + srcOfs);

						EmitREX(BeMCOperand::FromReg(X64Reg_R11D), BeMCOperand::FromReg(destReg), false);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11D), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					for (; curOfs <= memSize - 2; curOfs += 2)
					{
						Emit(0x66); EmitREX(BeMCOperand::FromReg(X64Reg_R11W), BeMCOperand::FromReg(srcReg), false);
						Emit(0x8B);
						EmitModRMRel(EncodeRegNum(X64Reg_R11W), srcReg, X64Reg_None, 1, curOfs + srcOfs);

						Emit(0x66); EmitREX(BeMCOperand::FromReg(X64Reg_R11W), BeMCOperand::FromReg(destReg), false);
						Emit(0x89);
						EmitModRMRel(EncodeRegNum(X64Reg_R11W), destReg, X64Reg_None, 1, curOfs + destOfs);
					}

					for (; curOfs <= memSize - 1; curOfs += 1)
					{
						EmitREX(BeMCOperand::FromReg(X64Reg_R11B), BeMCOperand::FromReg(srcReg), false);
						Emit(0x8B - 1);
						EmitModRMRel(EncodeRegNum(X64Reg_R11B), srcReg, X64Reg_None, 1, curOfs + srcOfs);

						EmitREX(BeMCOperand::FromReg(X64Reg_R11B), BeMCOperand::FromReg(destReg), false);
						Emit(0x89 - 1);
						EmitModRMRel(EncodeRegNum(X64Reg_R11B), destReg, X64Reg_None, 1, curOfs + destOfs);
					}
				}
				break;
			case BeMCInstKind_FastCheckStack:
				{
					// CMP RSP, [RSP]
					BF_ASSERT(inst->mArg0 == BeMCOperand::FromReg(X64Reg_RSP));
					Emit(0x48);
					Emit(0x3B);
					Emit(0x24);
					Emit(0x24);
				}
				break;
			case BeMCInstKind_TLSSetup:
				{
					if (inst->mArg1) // Alt
					{
						BF_ASSERT(inst->mArg0.IsNativeReg());

						// mov arg0, qword ptr gs:[0x58]
						Emit(0x65);
						EmitREX(inst->mArg0, BeMCOperand(), true);
						Emit(0x8B); Emit(0x04 | (EncodeRegNum(inst->mArg0.mReg) << 3)); Emit(0x25);
						mOut.Write((int32)0x58);
					}
					else
					{
						// mov eax, _tls_index
						Emit(0x8B); Emit(0x05);
						auto sym = mCOFFObject->GetSymbolRef("_tls_index");

						BeMCRelocation reloc;
						reloc.mKind = BeMCRelocationKind_REL32;
						reloc.mOffset = mOut.GetPos();
						reloc.mSymTableIdx = sym->mIdx;
						mCOFFObject->mTextSect.mRelocs.push_back(reloc);
						mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);

						mOut.Write((int32)0);

						auto vregInfo = mVRegInfo[mTLSVRegIdx];
						BF_ASSERT(vregInfo->mReg != X64Reg_None);
						auto tlsOperand = BeMCOperand::FromReg(vregInfo->mReg);

						// mov tlsReg, qword ptr gs:[0x58]
						Emit(0x65);
						EmitREX(tlsOperand, BeMCOperand(), true);
						Emit(0x8B); Emit(0x04 | (EncodeRegNum(vregInfo->mReg) << 3)); Emit(0x25);
						mOut.Write((int32)0x58);

						// mov tlsReg, qword ptr [tlsReg + 8*rax]
						EmitREX(tlsOperand, tlsOperand, true);
						Emit(0x8B);
						EmitModRMRel(EncodeRegNum(vregInfo->mReg), vregInfo->mReg, X64Reg_RAX, 8, 0);
					}
				}
				break;
			case BeMCInstKind_PreserveVolatiles:
				{
					for (int vregIdx : *inst->mLiveness)
					{
						if (vregIdx >= mLivenessContext.mNumItems)
							continue;

						auto vregInfo = mVRegInfo[vregIdx];
						if ((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg != vregInfo->mReg))
							continue;
						if (vregInfo->mVolatileVRegSave != -1)
						{
							if (vregInfo->mDbgVariable != NULL)
							{
								BeDbgVariableRange range;
								range.mOffset = funcCodePos;
								range.mLength = -1;
								vregInfo->mDbgVariable->mSavedRanges.push_back(range);
							}
						}
					}
				}
				break;
			case BeMCInstKind_RestoreVolatiles:
				{
					for (int vregIdx : *inst->mLiveness)
					{
						if (vregIdx >= mLivenessContext.mNumItems)
							continue;

						auto vregInfo = mVRegInfo[vregIdx];
						if ((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg != vregInfo->mReg))
							continue;
						if (vregInfo->mVolatileVRegSave != -1)
						{
							if ((vregInfo->mDbgVariable != NULL) && (!vregInfo->mDbgVariable->mSavedRanges.empty()))
							{
								BeDbgVariableRange& range = vregInfo->mDbgVariable->mSavedRanges.back();
								range.mLength = funcCodePos - range.mOffset;

								bool handled = false;
								if (!vregInfo->mDbgVariable->mGaps.empty())
								{
									auto& lastGap = vregInfo->mDbgVariable->mGaps.back();
									if (lastGap.mLength == -1)
									{
										lastGap.mLength = funcCodePos - lastGap.mOffset;
										handled = true;
									}
								}

								if (!handled)
								{
									BeDbgVariableRange gapRange = range;
									// 									gapRange.mOffset++;
									// 									gapRange.mLength--;
									vregInfo->mDbgVariable->mGaps.push_back(gapRange);
								}
							}
						}
					}
				}
				break;

			case BeMCInstKind_None:
				break;

			case BeMCInstKind_Unwind_Alloc:
			case BeMCInstKind_Unwind_PushReg:
			case BeMCInstKind_Unwind_SaveXMM:
			case BeMCInstKind_Unwind_SetBP:
				{
					BeMCDeferredUnwind deferredUnwind = { inst, funcCodePos };
					deferredUnwinds.push_back(deferredUnwind);
				}
				break;

			case BeMCInstKind_Label:
				while (funcCodePos < hotJumpLen)
				{
					Emit(0x90);
					funcCodePos++;
				}
				lastLabelIdx = inst->mArg0.mLabelIdx;
				labelPositions[inst->mArg0.mLabelIdx] = funcCodePos;
				// This ensures we can't jump back into the hot jump area
				break;
			case BeMCInstKind_Nop:
				Emit(0x90);
				break;
			case BeMCInstKind_Unreachable:
				// ud2
				Emit(0x0F); Emit(0x0B);
				break;
			case BeMCInstKind_EnsureInstructionAt:
				needsInstructionAtDbgLoc = true;
				/*if (mDbgFunction != NULL)
				{
					// If this is true, this means that our instruction was the first one encountered at this mDbgPos,
					//  so we emit a NOP so we can step onto this line
					if (mDbgFunction->mEmissions.back().mPos == funcCodePos)
					{
						mOut.Write((uint8)0x90);
					}
				}*/
				break;
			case BeMCInstKind_DbgBreak:
				Emit(0xCC);
				break;
			case BeMCInstKind_MFence:
				Emit(0x0F); Emit(0xAE); Emit(0xF0);
				break;
			case BeMCInstKind_Mov:
				{
					if (inst->mArg1.mKind == BeMCOperandKind_ConstAgg)
					{
						EmitAggMov(inst->mArg0, inst->mArg1);
						break;
					}

					auto vregInfo = GetVRegInfo(inst->mArg1);
					auto arg0Type = GetType(inst->mArg0);
					auto arg1Type = GetType(inst->mArg1);

// 					auto arg1 = inst->mArg1;
// 					while (arg1.IsVReg())
// 					{
// 						auto vregInfo = GetVRegInfo(arg1);
// 						if (vregInfo->IsDirectRelTo())
// 							arg1 = vregInfo->mRelTo;
// 					}

					if (instForm == BeMCInstForm_Unknown)
					{
						if ((inst->mArg0.IsNativeReg()) && (inst->mArg1.IsNativeReg()))
						{
							if ((arg0Type->IsInt()) && (arg0Type->mSize < arg1Type->mSize))
							{
								// Reduce arg1 to arg0 size
								BeMCOperand mcDest;
								BeMCOperand mcSrc = BeMCOperand::FromReg(ResizeRegister(inst->mArg1.mReg, arg0Type));
								BeTypeCode useTypeCode = BeTypeCode_None;
								if (mcSrc.mReg != X64Reg_None)
								{
									// Reduction worked
									mcDest = inst->mArg0;
									useTypeCode = arg0Type->mTypeCode;
								}
								else
								{
									SoftFail("Error in Mov");
								}

								switch (useTypeCode)
								{
								case BeTypeCode_Int8:
									EmitREX(mcDest, mcSrc, false);
									Emit(0x8A); EmitModRM(mcDest, mcSrc);
									break;
								case BeTypeCode_Int16: Emit(0x66); // Fallthrough
								case BeTypeCode_Int32:
								case BeTypeCode_Int64:
									EmitREX(mcDest, mcSrc, arg0Type->mTypeCode == BeTypeCode_Int64);
									Emit(0x8B); EmitModRM(mcDest, mcSrc);
									break;
								default: NotImpl();
								}
								break;
							}
						}

						if (inst->mArg0.IsNativeReg())
						{
							BF_ASSERT(arg0Type->IsIntable());
							BF_ASSERT(arg1Type->IsIntable());
							BF_ASSERT(arg0Type->mSize > arg1Type->mSize);
							// Zero extend 'dest' to 'src', emit movzx
							switch (arg0Type->mTypeCode)
							{
							case BeTypeCode_Int16:
								BF_ASSERT(arg1Type->mTypeCode == BeTypeCode_Int8);
								Emit(0x66);
								EmitREX(inst->mArg0, inst->mArg1, false);
								Emit(0x0F); Emit(0xB6); EmitModRM(inst->mArg0, inst->mArg1);
								break;
							case BeTypeCode_Int32:
								switch (arg1Type->mTypeCode)
								{
								case BeTypeCode_Int8:
								case BeTypeCode_Boolean:
									EmitREX(inst->mArg0, inst->mArg1, false);
									Emit(0x0F); Emit(0xB6); EmitModRM(inst->mArg0, inst->mArg1);
									break;
								case BeTypeCode_Int16:
									EmitREX(inst->mArg0, inst->mArg1, false);
									Emit(0x0F); Emit(0xB7); EmitModRM(inst->mArg0, inst->mArg1);
									break;
								default: NotImpl();
								}
								break;
							case BeTypeCode_Int64:
								switch (arg1Type->mTypeCode)
								{
								case BeTypeCode_Int8:
								case BeTypeCode_Boolean:
									EmitREX(inst->mArg0, inst->mArg1, true);
									Emit(0x0F); Emit(0xB6); EmitModRM(inst->mArg0, inst->mArg1); break;
								case BeTypeCode_Int16:
									EmitREX(inst->mArg0, inst->mArg1, true);
									Emit(0x0F); Emit(0xB7); EmitModRM(inst->mArg0, inst->mArg1); break;
								case BeTypeCode_Int32:
									{
										// We convert from a "mov r64, r32" to a "mov r32, r32", which forces high 32 bits of original r64 to zero
										BeMCOperand mcDest = BeMCOperand::FromReg(ResizeRegister(inst->mArg0.mReg, arg1Type));
										EmitREX(mcDest, inst->mArg1, false);
										Emit(0x8B); EmitModRM(mcDest, inst->mArg1);
									}
									break;
								}
								break;
							default:
								NotImpl();
							}
							break; // from inst switch
						}

						if (arg0Type->mTypeCode == BeTypeCode_Float)
						{
							if (inst->mArg1.IsImmediate())
							{
								// Emit as a int32 mov
								EmitREX(BeMCOperand(), inst->mArg0, false);
								Emit(0xC7);
								EmitModRM(0, inst->mArg0, -4);
								float val = inst->mArg1.mImmF32;
								mOut.Write(*(int32*)&val);
								break;
							}
						}
					}

					if (inst->mArg1.mKind == BeMCOperandKind_CmpResult)
					{
						BF_ASSERT(instForm == BeMCInstForm_R8_RM8);

						// SETcc
						auto& cmpResult = mCmpResults[inst->mArg1.mCmpResultIdx];
						uint8 opCode = GetJumpOpCode(cmpResult.mCmpKind, false) + 0x20;
						EmitREX(BeMCOperand(), inst->mArg0, false);
						Emit(0x0F);
						Emit(opCode);
						EmitModRM(0, inst->mArg0);
						break;
					}

					bool isLEA = inst->mArg1.mKind == BeMCOperandKind_SymbolAddr;
					if ((instForm == BeMCInstForm_R64_RM64) && (inst->mArg1.IsVRegAny()))
					{
						bool isMulti = false;
						if (GetRMForm(inst->mArg1, isMulti) == BeMCRMMode_Direct)
						{
							// LEA
							auto modInst = *inst;
							BF_ASSERT(modInst.mArg1.IsVRegAny());
							modInst.mArg1 = BeMCOperand::ToLoad(modInst.mArg1);
							EmitInst(BeMCInstForm_R64_RM64, 0x8D, &modInst);
							break;
						}
						else
						{
							auto vregInfo1 = GetVRegInfo(inst->mArg1);
							if (vregInfo1->mRelTo.mKind == BeMCOperandKind_SymbolAddr)
								isLEA = true;
						}
					}

					if (inst->mArg1.mKind == BeMCOperandKind_VRegAddr)
					{
						NotImpl();

						/*// LEA
						switch (instForm)
						{
						case BeMCInstForm_R64_RM64: EmitInst(BeMCInstForm_R64_RM64, 0x8D, inst); break;
						default:
							NotImpl();
						}*/
					}
					else if (isLEA)
					{
						// LEA
						switch (instForm)
						{
						case BeMCInstForm_R64_RM64: EmitInst(BeMCInstForm_R64_RM64, 0x8D, inst); break;
						default:
							NotImpl();
						}
					}
					// ??
					/*else if ((vregInfo != NULL) && (vregInfo->mIsExpr))
					{
						// LEA
						switch (instForm)
						{
						case BeMCInstForm_R64_RM64: EmitInst(BeMCInstForm_R64_RM64_ADDR, 0x8D, inst); break;
						default:
							NotImpl();
						}
					}*/
					else if ((inst->mArg0.IsNativeReg()) && (inst->mArg1.IsZero()))
					{
						auto type = GetType(inst->mArg0);

						if (type->mSize == 1)
						{
							// Emit as XOR <arg0>, <arg0>
							EmitREX(inst->mArg0, inst->mArg0, false);
							mOut.Write((uint8)0x30); // r, r/m64
							EmitModRM(inst->mArg0, inst->mArg0);
						}
						else
						{
							// Emit as XOR <arg0>, <arg0>
							EmitREX(inst->mArg0, inst->mArg0, type->mSize == 8);
							mOut.Write((uint8)0x33); // r, r/m64
							EmitModRM(inst->mArg0, inst->mArg0);
						}
					}
					else
					{
						switch (instForm)
						{
						case BeMCInstForm_XMM32_RM32:
							{
								BF_ASSERT(inst->mArg1.IsNativeReg());
								// CVTSI2SS - use zero-extended 64-bit register
								EmitStdXMMInst(BeMCInstForm_XMM32_RM64, inst, 0x2A);
							}
							break;
						case BeMCInstForm_XMM64_RM32:
							{
								BF_ASSERT(inst->mArg1.IsNativeReg());
								// CVTSI2SD - use zero-extended 64-bit register
								EmitStdXMMInst(BeMCInstForm_XMM64_RM64, inst, 0x2A);
							}
							break;
						case BeMCInstForm_XMM64_RM64:
						case BeMCInstForm_XMM32_RM64:
							{
								// uint64->xmm Not implemented
								// We do a signed transform instead

								// CVTSI2SS / CVTSI2SD
								EmitStdXMMInst(instForm, inst, 0x2A);
							}
							break;
						case BeMCInstForm_XMM64_FRM32:
						case BeMCInstForm_XMM32_FRM64:
							{
								// CVTSS2SD / CVTSD2SS
								EmitStdXMMInst(instForm, inst, 0x5A);
							}
							break;
						case BeMCInstForm_R32_F32:
						case BeMCInstForm_R32_F64:
						case BeMCInstForm_R64_F32:
						case BeMCInstForm_R64_F64:
							{
								// CVTTSS2SI
								EmitStdXMMInst(instForm, inst, 0x2C);
							}
							break;
						case BeMCInstForm_XMM128_RM128:
							{
								// MOVUPS
								EmitREX(arg0, arg1, true);
								Emit(0x0F); Emit(0x10);

								if (arg1.IsImmediateInt())
									arg1.mKind = BeMCOperandKind_Immediate_int32x4;

								EmitModRM(arg0, arg1);
							}
							break;
						case BeMCInstForm_FRM128_XMM128:
							{
								if (arg0Type->mSize == 4)
								{
									BeMCOperand imm1;
									imm1.mImmediate = 1;
									imm1.mKind = BeMCOperandKind_Immediate_int32x4;

									BeMCOperand xmm15;
									xmm15.mReg = X64Reg_M128_XMM15;
									xmm15.mKind = BeMCOperandKind_NativeReg;

									// MOVUPS
									EmitREX(xmm15, arg1, true);
									Emit(0x0F); Emit(0x10);
									EmitModRM(xmm15, arg1);

									// ANDPS xmm15, <1, 1, 1, 1>
									EmitREX(xmm15, imm1, true);
									Emit(0x0F); Emit(0x54);
									EmitModRM(xmm15, imm1);

									// PACKUSWB xmm15, xmm15
									Emit(0x66); EmitREX(xmm15, xmm15, true);
									Emit(0x0F); Emit(0x67);
									EmitModRM(xmm15, xmm15);

									// PACKUSWB xmm15, xmm15
									Emit(0x66); EmitREX(xmm15, xmm15, true);
									Emit(0x0F); Emit(0x67);
									EmitModRM(xmm15, xmm15);

									// MOVD <arg0>, xmm15
									Emit(0x66); EmitREX(xmm15, arg0, false);
									Emit(0x0F); Emit(0x7E);
									EmitModRM(xmm15, arg0);
								}
								else
								{
									// MOVUPS
									EmitREX(arg1, arg0, true);
									Emit(0x0F); Emit(0x11);
									EmitModRM(arg1, arg0);
								}
							}
							break;
						default:
							{
								// MOVSS/MOVSD
								if (EmitStdXMMInst(instForm, inst, 0x10, 0x11))
									break;

								if ((inst->mArg0.IsNativeReg()) && (arg1.IsImmediateInt()))
								{
									auto arg0Type = GetType(inst->mArg0);
									if (arg0Type->mTypeCode == BeTypeCode_Int16)
									{
										Emit(0x66);
										EmitREX(BeMCOperand(), inst->mArg0, false);
										Emit(0xB8 + EncodeRegNum(inst->mArg0.mReg));
										mOut.Write((int16)arg1.mImmediate);
										break;
									}
									else if ((arg0Type->mTypeCode == BeTypeCode_Int32) || (arg0Type->mTypeCode == BeTypeCode_Int64))
									{
										// For 64-bit writes, we would like to use 32-bit zero extension but we resort to the full
										//  64-bits if necessary
										auto arg0 = inst->mArg0;
										if (arg0Type->mTypeCode == BeTypeCode_Int64)
										{
											auto resizedReg = ResizeRegister(arg0.mReg, 4);
											if ((resizedReg != X64Reg_None) && (inst->mArg1.mImmediate < 0) && (inst->mArg1.mImmediate >= -0x80000000LL))
											{
												// Do sign-extend R32 mov
												EmitREX(BeMCOperand(), inst->mArg0, true);
												Emit(0xC7);
												EmitModRM(0, inst->mArg0);
												mOut.Write((int32)inst->mArg1.mImmediate);
												break;
											}

											if ((resizedReg == X64Reg_None) || (inst->mArg1.mImmediate < 0) || (inst->mArg1.mImmediate >= 0x100000000LL))
											{
												EmitREX(BeMCOperand(), inst->mArg0, true);
												Emit(0xB8 + EncodeRegNum(inst->mArg0.mReg));
												mOut.Write((int64)inst->mArg1.mImmediate);
												break;
											}
											else
												arg0.mReg = resizedReg;
										}

										EmitREX(BeMCOperand(), arg0, false);
										Emit(0xB8 + EncodeRegNum(arg0.mReg));
										mOut.Write((int32)inst->mArg1.mImmediate);
										break;
									}
								}

								EmitStdInst(instForm, inst, 0x89, 0x8B, 0xC7, 0x0);
							}
						}
					}
				}
				break;
			case BeMCInstKind_MovRaw:
				{
					auto arg0Type = GetType(inst->mArg0);
					auto arg1Type = GetType(inst->mArg1);

					if ((arg0Type->mTypeCode == BeTypeCode_Int64) && (arg1Type->mTypeCode == BeTypeCode_Double))
					{
						Emit(0x66); EmitREX(inst->mArg1, inst->mArg0, true);
						Emit(0x0F); Emit(0x7E);
						EmitModRM(inst->mArg1, inst->mArg0);
					}
					else
					{
						NotImpl();
					}
				}
				break;
			case BeMCInstKind_MovSX:
				{
					auto arg0Type = GetType(inst->mArg0);
					auto arg1Type = GetType(inst->mArg1);
					switch (arg0Type->mTypeCode)
					{
					case BeTypeCode_Int16:
						BF_ASSERT(arg1Type->mTypeCode == BeTypeCode_Int8);
						Emit(0x66);
						EmitREX(inst->mArg0, inst->mArg1, false);
						Emit(0x0F); Emit(0xBE); EmitModRM(inst->mArg0, inst->mArg1);
						break;
					case BeTypeCode_Int32:
						switch (arg1Type->mTypeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(inst->mArg0, inst->mArg1, false);
							Emit(0x0F); Emit(0xBE); EmitModRM(inst->mArg0, inst->mArg1);
							break;
						case BeTypeCode_Int16:
							EmitREX(inst->mArg0, inst->mArg1, false);
							Emit(0x0F); Emit(0xBF); EmitModRM(inst->mArg0, inst->mArg1);
							break;
						case BeTypeCode_Float:
							// CVTSS2SI
							Emit(0xF3); EmitREX(inst->mArg0, inst->mArg1, false);
							Emit(0x0F); Emit(0x2C);
							EmitModRM(inst->mArg0, inst->mArg1);
							break;
						default: NotImpl();
						}
						break;
					case BeTypeCode_Int64:
						switch (arg1Type->mTypeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(inst->mArg0, inst->mArg1, true);
							Emit(0x0F); Emit(0xBE); EmitModRM(inst->mArg0, inst->mArg1); break;
						case BeTypeCode_Int16:
							EmitREX(inst->mArg0, inst->mArg1, true);
							Emit(0x0F); Emit(0xBF); EmitModRM(inst->mArg0, inst->mArg1); break;
						case BeTypeCode_Int32:
							EmitREX(inst->mArg0, inst->mArg1, true);
							Emit(0x63); EmitModRM(inst->mArg0, inst->mArg1); break;
						case BeTypeCode_Float:
							// CVTSS2SI
							Emit(0xF3); EmitREX(inst->mArg0, inst->mArg1, true);
							Emit(0x0F); Emit(0x2C);
							EmitModRM(inst->mArg0, inst->mArg1);
							break;
						default: NotImpl();
						}
						break;

					case BeTypeCode_Float:
					case BeTypeCode_Double:
						{
							if (arg1Type->IsInt())
							{
								// CVTSI2SS / CVTSI2SD
								EmitStdXMMInst(instForm, inst, 0x2A);
							}
							else
								NotImpl();

							/*switch (arg1Type->mTypeCode)
							{
							case BeTypeCode_Double:
								EmitStdXMMInst(BeMCInstForm_XMM32_RM64, inst, 0x5A);
								break;
							default:
								NotImpl();
								break;
							}*/
						}
						break;
					default: NotImpl();
					}
				}
				break;
			case BeMCInstKind_CmpXChg:
				{
					auto arg0Type = GetType(inst->mArg0);

					if (inst->mArg1.IsNativeReg())
					{
						switch (instForm)
						{
						case BeMCInstForm_R8_RM8:
							instForm = BeMCInstForm_RM8_R8;
							break;
						case BeMCInstForm_R16_RM16:
							instForm = BeMCInstForm_RM16_R16;
							break;
						case BeMCInstForm_R32_RM32:
							instForm = BeMCInstForm_RM32_R32;
							break;
						case BeMCInstForm_R64_RM64:
							instForm = BeMCInstForm_RM64_R64;
							break;
						}
					}

					if (!inst->mArg0.IsNativeReg())
						Emit(0xF0); // LOCK

					switch (instForm)
					{
					case BeMCInstForm_RM8_R8:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x0F); Emit(0xB0); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM16_R16:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x66); Emit(0x0F); Emit(0xB0); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM32_R32:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x0F); Emit(0xB1); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM64_R64:
						EmitREX(inst->mArg1, inst->mArg0, true);
						Emit(0x0F); Emit(0xB1); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					default:
						SoftFail("Invalid CmpXChg args");
					}
				}
				break;
			case BeMCInstKind_XAdd:
				{
					auto arg0Type = GetType(inst->mArg0);

					switch (instForm)
					{
					case BeMCInstForm_R8_RM8:
						if (inst->mArg1.IsNativeReg())
							instForm = BeMCInstForm_RM8_R8;
						break;
					case BeMCInstForm_R16_RM16:
						if (inst->mArg1.IsNativeReg())
							instForm = BeMCInstForm_RM16_R16;
						break;
					case BeMCInstForm_R32_RM32:
						if (inst->mArg1.IsNativeReg())
							instForm = BeMCInstForm_RM32_R32;
						break;
					case BeMCInstForm_R64_RM64:
						if (inst->mArg1.IsNativeReg())
							instForm = BeMCInstForm_RM64_R64;
						break;
					}

					if (!inst->mArg0.IsNativeReg())
						Emit(0xF0); // LOCK

					switch (instForm)
					{
					case BeMCInstForm_RM8_R8:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x0F); Emit(0xC0); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM16_R16:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x66); Emit(0x0F); Emit(0xC0); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM32_R32:
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x0F); Emit(0xC1); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					case BeMCInstForm_RM64_R64:
						EmitREX(inst->mArg1, inst->mArg0, true);
						Emit(0x0F); Emit(0xC1); EmitModRM(inst->mArg1, inst->mArg0);
						break;
					default:
						SoftFail("Invalid XAdd args");
					}
				}
				break;
			case BeMCInstKind_XChg:
				{
					EmitStdInst(instForm, inst, 0x87, 0x87, 0x00, 0x0);
				}
				break;
			case BeMCInstKind_Load:
				{
					if (inst->mArg1.IsSymbol())
					{
						BF_ASSERT(inst->mArg0.IsNativeReg());
					}

					switch (instForm)
					{
					case BeMCInstForm_XMM64_FRM64:
					case BeMCInstForm_XMM32_FRM32:
						// MOVSS/MOVSD
						EmitStdXMMInst(instForm, inst, 0x10);
						break;
					case BeMCInstForm_R32_RM32:
						EmitInst(BeMCInstForm_R32_RM64_ADDR, 0x8B, inst);
						break;
					case BeMCInstForm_R64_RM64:
						EmitInst(BeMCInstForm_R64_RM64_ADDR, 0x8B, inst);
						break;
					case BeMCInstForm_XMM32_RM64:
					case BeMCInstForm_XMM64_RM64:
						// MOVSS / MOVSD
						BF_ASSERT(inst->mArg0.IsNativeReg());
						if (instForm == BeMCInstForm_XMM64_RM64)
							Emit(0xF2);
						else
							Emit(0xF3);
						EmitREX(inst->mArg0, inst->mArg1, false);
						Emit(0x0F); Emit(0x10);
						EmitModRM_Addr(inst->mArg0, inst->mArg1);
						break;
					default:
						{
							BF_ASSERT(inst->mArg0.IsNativeReg());
							auto arg0Type = GetType(inst->mArg0);
							auto arg1Type = GetType(inst->mArg1);
							BF_ASSERT(arg1Type->mSize == 8);

							switch (arg0Type->mSize)
							{
							case 1: EmitInst(BeMCInstForm_R8_RM64_ADDR, 0x8B, inst); break;
							case 2: EmitInst(BeMCInstForm_R16_RM64_ADDR, 0x8B, inst); break;
							case 4: EmitInst(BeMCInstForm_R32_RM64_ADDR, 0x8B, inst); break;
							case 8: EmitInst(BeMCInstForm_R64_RM64_ADDR, 0x8B, inst); break;
							}
						}
					}

					/*BF_ASSERT(inst->mArg0.mKind == BeMCOperandKind_NativeReg);
					BF_ASSERT(inst->mArg1.mKind == BeMCOperandKind_NativeReg);

					EmitREX(inst->mArg0, inst->mArg1);
					mOut.Write((uint8)0x8B);
					uint8 modRM = (0x0 << 6) | (EncodeRegNum(inst->mArg1.mReg)) | (EncodeRegNum(inst->mArg0.mReg)); // <arg0>, [<arg1>]
					mOut.Write(modRM);*/
				}
				break;
			case BeMCInstKind_Store:
				{
					switch (instForm)
					{
						/*case BeMCInstForm_R64_RM64:
							{
								// Make sure its really R64, R64
								BF_ASSERT(inst->mArg1.IsNativeReg());
								EmitInst(BeMCInstForm_RM64_R64_ADDR, 0x89, inst);
							}
							break;*/
					case BeMCInstForm_RM64_R64:	EmitInst(BeMCInstForm_RM64_R64_ADDR, 0x89, inst); break;
					case BeMCInstForm_FRM32_XMM32:
					case BeMCInstForm_FRM64_XMM64:
						// MOVSS/MOVSD
						EmitStdXMMInst(instForm, inst, 0x10, 0x11);
						break;
					case BeMCInstForm_R64_F64:
					case BeMCInstForm_R64_F32:
						// MOVSS / MOVSD
						BF_ASSERT(inst->mArg1.IsNativeReg());
						if (instForm == BeMCInstForm_R64_F64)
							Emit(0xF2);
						else
							Emit(0xF3);
						EmitREX(inst->mArg1, inst->mArg0, false);
						Emit(0x0F); Emit(0x11);
						EmitModRM_Addr(inst->mArg1, inst->mArg0);
						break;
					default:
						{
							BF_ASSERT(inst->mArg0.IsNativeReg());
							BF_ASSERT(inst->mArg1.IsNativeReg());
							auto arg0Type = GetType(inst->mArg0);
							auto arg1Type = GetType(inst->mArg1);
							BF_ASSERT(arg0Type->mSize == 8);

							switch (arg1Type->mSize)
							{
							case 1: EmitInst(BeMCInstForm_RM64_R8_ADDR, 0x89, inst); break;
							case 2: EmitInst(BeMCInstForm_RM64_R16_ADDR, 0x89, inst); break;
							case 4: EmitInst(BeMCInstForm_RM64_R32_ADDR, 0x89, inst); break;
							case 8: EmitInst(BeMCInstForm_RM64_R64_ADDR, 0x89, inst); break;
							}
						}
					}
				}
				break;
			case BeMCInstKind_Push:
				{
					auto arg0Type = GetType(inst->mArg0);
					if (inst->mArg1)
					{
						BF_ASSERT(IsXMMReg(inst->mArg0.mReg));
						BF_ASSERT(inst->mArg1.IsImmediate());
						// Is there a performance benefit to properly using MOVAPD vs MOVAPS if we only
						//  use this register for double storage?
						// MOVAPS
						EmitREX(inst->mArg0, BeMCOperand(), false);
						Emit(0x0F); Emit(0x29);
						EmitModRMRel(EncodeRegNum(inst->mArg0.mReg), X64Reg_RSP, X64Reg_None, 1, (int)inst->mArg1.mImmediate);
						break;
					}

					if (arg0Type->IsFloat())
					{
						BF_ASSERT(IsXMMReg(inst->mArg0.mReg));

						// sub rsp, sizeof(arg0)
						Emit(0x48); Emit(0x83); Emit(0xEC); Emit(arg0Type->mSize);

						Emit((arg0Type->mSize == 4) ? 0xF3 : 0xF2);
						EmitREX(inst->mArg0, BeMCOperand(), false);
						Emit(0x0F); Emit(0x11);
						EmitModRMRel(EncodeRegNum(inst->mArg0.mReg), X64Reg_RSP, X64Reg_None, 1, 0);
						break;
					}

					// This is used in combination with register pops, and we can't pop 32-bit registers
					//  so we need to make sure we're always pushing a full 64 bits
					switch (instForm)
					{
					case BeMCInstForm_R8: // Always use full reg (always push 64 bits)
					case BeMCInstForm_R16:
					case BeMCInstForm_R32:
					case BeMCInstForm_R64:
						EmitREX(BeMCOperand(), inst->mArg0, true);
						mOut.Write((uint8)(0x50 + EncodeRegNum(inst->mArg0.mReg)));
						break;
					case BeMCInstForm_RM64:
						EmitREX(BeMCOperand(), inst->mArg0, true);
						Emit(0xFF); EmitModRM(6, inst->mArg0);
						break;
					case BeMCInstForm_IMM32: // Always pad to 64 bits
						{
							Emit(0x68); mOut.Write((int32)inst->mArg0.mImmediate);
							// Emit padded 0
							Emit(0x68); mOut.Write((int32)0);
						}
						break;
					default:
						NotImpl();
					}
				}
				break;
			case BeMCInstKind_Pop:
				{
					auto arg0Type = GetType(inst->mArg0);
					if (inst->mArg1)
					{
						BF_ASSERT(IsXMMReg(inst->mArg0.mReg));
						BF_ASSERT(inst->mArg1.IsImmediate());
						// Is there a performance benefit to properly using MOVAPD vs MOVAPS if we only
						//  use this register for double storage?
						// MOVAPS
						EmitREX(inst->mArg0, BeMCOperand(), false);
						Emit(0x0F); Emit(0x28);
						// Push always uses RSP (required for stack unwinding), but Pop uses RBP when applicable
						//  because it is still set up at that point
						EmitModRMRelStack(EncodeRegNum(inst->mArg0.mReg), (int)inst->mArg1.mImmediate, 1);
						break;
					}

					if (arg0Type->IsFloat())
					{
						BF_ASSERT(IsXMMReg(inst->mArg0.mReg));

						Emit((arg0Type->mSize == 4) ? 0xF3 : 0xF2);
						EmitREX(inst->mArg0, BeMCOperand(), false);
						Emit(0x0F); Emit(0x10);
						EmitModRMRel(EncodeRegNum(inst->mArg0.mReg), X64Reg_RSP, X64Reg_None, 1, 0);

						// add rsp, sizeof(arg0)
						Emit(0x48); Emit(0x83); Emit(0xC4); Emit(arg0Type->mSize);
						break;
					}

					switch (instForm)
					{
					case BeMCInstForm_R64:
						EmitREX(BeMCOperand(), inst->mArg0, true);
						mOut.Write((uint8)(0x58 + EncodeRegNum(inst->mArg0.mReg)));
						break;
					case BeMCInstForm_RM64:
						EmitREX(BeMCOperand(), inst->mArg0, true);
						Emit(0x8F); EmitModRM(0, inst->mArg0);
					default:
						NotImpl();
					}
				}
				break;
			case BeMCInstKind_Neg:
				{
					auto typeCode = GetType(inst->mArg0)->mTypeCode;
					switch (typeCode)
					{
					case BeTypeCode_Int8:
						EmitREX(BeMCOperand(), inst->mArg0, false);
						Emit(0xF6);
						EmitModRM(3, inst->mArg0);
						break;
					case BeTypeCode_Int16: Emit(0x66); // Fallthrough
					case BeTypeCode_Int32:
					case BeTypeCode_Int64:
						EmitREX(BeMCOperand(), inst->mArg0, typeCode == BeTypeCode_Int64);
						Emit(0xF7); EmitModRM(3, inst->mArg0);
						break;
					default:
						NotImpl();
					}
				}
				break;
			case BeMCInstKind_Not:
				{
					auto typeCode = GetType(inst->mArg0)->mTypeCode;
					switch (typeCode)
					{
					case BeTypeCode_Int8:
						EmitREX(BeMCOperand(), inst->mArg0, false);
						Emit(0xF6);
						EmitModRM(2, inst->mArg0);
						break;
					case BeTypeCode_Int16: Emit(0x66); // Fallthrough
					case BeTypeCode_Int32:
					case BeTypeCode_Int64:
						EmitREX(BeMCOperand(), inst->mArg0, typeCode == BeTypeCode_Int64);
						Emit(0xF7); EmitModRM(2, inst->mArg0);
						break;
					default:
						NotImpl();
					}
				}
				break;
			case BeMCInstKind_Add:
				{
					if ((inst->mResult) && (inst->mResult != inst->mArg0) && (!inst->mDisableShortForm))
					{
						BF_ASSERT(!inst->mDisableShortForm);
						BF_ASSERT(inst->mResult.IsNativeReg());
						BF_ASSERT(inst->mArg0.IsNativeReg());
						BF_ASSERT(inst->mArg1.IsImmediate());

						// LEA form
						auto resultType = GetType(inst->mArg0);
						switch (resultType->mTypeCode)
						{
						case BeTypeCode_Int16:
							Emit(0x66);
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(inst->mResult, inst->mArg0, resultType->mTypeCode == BeTypeCode_Int64);
							Emit(0x8D); EmitModRMRel(EncodeRegNum(inst->mResult.mReg), ResizeRegister(inst->mArg0.mReg, 8), X64Reg_None, 1, (int)inst->mArg1.mImmediate);
							break;
						default:
							NotImpl();
						}
					}
					else if ((inst->mArg1.IsImmediateInt()) && (inst->mArg1.mImmediate == 1) && (!inst->mDisableShortForm))
					{
						// Emit as INC <arg0>
						auto typeCode = GetType(inst->mArg0)->mTypeCode;
						switch (typeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(BeMCOperand(), inst->mArg0, false);
							Emit(0xFE); EmitModRM(0, inst->mArg0);
							break;
						case BeTypeCode_Int16: Emit(0x66); // Fallthrough
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(BeMCOperand(), inst->mArg0, typeCode == BeTypeCode_Int64);
							Emit(0xFF); EmitModRM(0, inst->mArg0);
							break;
						default:
							NotImpl();
						}
					}
					else if (((inst->mArg1.IsImmediateInt()) && (inst->mArg1.mImmediate == -1)) && (!inst->mDisableShortForm))
					{
						// Emit as DEC <arg0>
						auto typeCode = GetType(inst->mArg0)->mTypeCode;
						switch (typeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(BeMCOperand(), inst->mArg0, false);
							Emit(0xFE); EmitModRM(1, inst->mArg0);
							break;
						case BeTypeCode_Int16: Emit(0x66); // Fallthrough
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(BeMCOperand(), inst->mArg0, typeCode == BeTypeCode_Int64);
							Emit(0xFF); EmitModRM(1, inst->mArg0);
							break;
						default:
							NotImpl();
						}
					}
					else
					{
						if (((instForm == BeMCInstForm_RM64_IMM16) || (instForm == BeMCInstForm_RM64_IMM32)) &&
							(inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_RAX))
						{
							// Emit as ADD RAX, <imm32>
							EmitREX(inst->mArg0, BeMCOperand(), true);
							mOut.Write((uint8)0x05);
							mOut.Write((int32)inst->mArg1.mImmediate);
							break;
						}

						if (EmitIntXMMInst(instForm, inst, 0xFC)) // PADD?
							break;
						if (EmitStdXMMInst(instForm, inst, 0x58))
							break;
						EmitStdInst(instForm, inst, 0x01, 0x03, 0x81, 0x0, 0x83, 0x0);
					}
				}
				break;
			case BeMCInstKind_Sub:
				{
					if ((inst->mArg1.IsImmediateInt()) && (inst->mArg1.mImmediate == 1) && (!inst->mDisableShortForm))
					{
						// Emit as DEC <arg0>
						auto typeCode = GetType(inst->mArg0)->mTypeCode;
						switch (typeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(BeMCOperand(), inst->mArg0, false);
							Emit(0xFE); EmitModRM(1, inst->mArg0);
							break;
						case BeTypeCode_Int16: Emit(0x66); // Fallthrough
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(BeMCOperand(), inst->mArg0, typeCode == BeTypeCode_Int64);
							Emit(0xFF); EmitModRM(1, inst->mArg0);
							break;
						default:
							NotImpl();
						}
					}
					else
					{
						if (((instForm == BeMCInstForm_RM64_IMM16) || (instForm == BeMCInstForm_RM64_IMM32)) &&
							(inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_RAX))
						{
							// Emit as SUB RAX, <imm32>
							EmitREX(inst->mArg0, BeMCOperand(), true);
							mOut.Write((uint8)0x2D);
							mOut.Write((int32)inst->mArg1.mImmediate);
							break;
						}

						if (EmitIntXMMInst(instForm, inst, 0xF8)) // PSUB?
							break;
						if (EmitStdXMMInst(instForm, inst, 0x5C))
							break;
						EmitStdInst(instForm, inst, 0x29, 0x2B, 0x81, 0x5, 0x83, 0x5);
					}
				}
				break;
			case BeMCInstKind_Mul:
				{
					if (arg0Type->IsIntable())
					{
						bool isValid = true;
						auto typeCode = GetType(inst->mArg1)->mTypeCode;
						switch (typeCode)
						{
						case BeTypeCode_Int8:
							isValid = inst->mArg0 == BeMCOperand::FromReg(X64Reg_AL);
							break;
						case BeTypeCode_Int16:
							isValid = inst->mArg0 == BeMCOperand::FromReg(X64Reg_AX);
							break;
						case BeTypeCode_Int32:
							isValid = inst->mArg0 == BeMCOperand::FromReg(X64Reg_EAX);
							break;
						case BeTypeCode_Int64:
							isValid = inst->mArg0 == BeMCOperand::FromReg(X64Reg_RAX);
							break;
						default:
							isValid = false;
						}
						if (!isValid)
							SoftFail("Invalid mul arguments");

						switch (typeCode)
						{
						case BeTypeCode_Int8:
							EmitREX(BeMCOperand(), inst->mArg1, false);
							Emit(0xF6);
							EmitModRM(4, inst->mArg1);
							break;
						case BeTypeCode_Int16: Emit(0x66); // Fallthrough
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(BeMCOperand(), inst->mArg1, typeCode == BeTypeCode_Int64);
							Emit(0xF7); EmitModRM(4, inst->mArg1);
							break;
						default:
							NotImpl();
						}
						break;
					}
				}
				//Fallthrough
			case BeMCInstKind_IMul:
				{
					if (instForm == BeMCInstForm_XMM128_RM128)
					{
						if (arg0Type->IsExplicitVectorType())
						{
							auto vecType = (BeVectorType*)arg0Type;
							if (vecType->mElementType->mTypeCode == BeTypeCode_Int32)
							{
								Emit(0x66);
								EmitREX(arg0, arg1, false);
								Emit(0x0F);
								if (inst->mKind == BeMCInstKind_IMul)
									Emit(0xD5); // PMULLW
								else
								{
									Emit(0x38); Emit(0x40); // PMULLD
								}
								EmitModRM(arg0, arg1);
								break;
							}
						}
					}

					if (EmitStdXMMInst(instForm, inst, 0x59))
						break;

					BeMCOperand result = inst->mResult;
					if ((!result) && (inst->mArg1.IsImmediate()))
						result = inst->mArg0; // Do as 3-form anyway
					if (result)
					{
						BF_ASSERT(inst->mArg1.IsImmediate());
						if ((inst->mArg0.IsNativeReg()) && (!inst->mDisableShortForm) &&
							((inst->mArg1.mImmediate == 2) || (inst->mArg1.mImmediate == 4) || (inst->mArg1.mImmediate == 8)))
						{
							// LEA form
							auto resultType = GetType(inst->mArg0);
							if (resultType->mTypeCode != BeTypeCode_Int8)
							{
								switch (resultType->mTypeCode)
								{
								case BeTypeCode_Int16:
									Emit(0x66);
								case BeTypeCode_Int32:
								case BeTypeCode_Int64:
									uint8 rex = GetREX(result, inst->mArg0, resultType->mTypeCode == BeTypeCode_Int64);
									if (rex != 0)
									{
										if (rex & 1)
										{
											// Mov REX flag from RM to SiB
											rex = (rex & ~1) | 2;
										}
										Emit(rex);
									}
									Emit(0x8D); EmitModRMRel(EncodeRegNum(result.mReg), X64Reg_None, inst->mArg0.mReg, (int)inst->mArg1.mImmediate, 0);
									break;
								}
								break;
							}
						}

						auto typeCode = GetType(inst->mArg0)->mTypeCode;
						switch (typeCode)
						{
						case BeTypeCode_Int16:
							Emit(0x66);
							// Fall through
						case BeTypeCode_Int32:
						case BeTypeCode_Int64:
							EmitREX(result, inst->mArg0, typeCode == BeTypeCode_Int64);

							if ((inst->mArg1.mImmediate >= -0x80) && (inst->mArg1.mImmediate <= 0x7F))
							{
								Emit(0x6B);
								EmitModRM(result, inst->mArg0, -1);
								mOut.Write((int8)inst->mArg1.mImmediate);
							}
							else
							{
								Emit(0x69);
								EmitModRM(result, inst->mArg0, (typeCode == BeTypeCode_Int16) ? -2 : -4);
								if (typeCode == BeTypeCode_Int16)
									mOut.Write((int16)inst->mArg1.mImmediate);
								else
									mOut.Write((int32)inst->mArg1.mImmediate);
							}
							break;
						default:
							NotImpl();
						}
					}
					else
					{
						if (inst->mArg1.IsImmediate())
						{
							int64 multVal = inst->mArg1.mImmediate;
							if (IsPowerOfTwo(multVal))
							{
								int shiftCount = 0;
								while (multVal > 1)
								{
									shiftCount++;
									multVal >>= 1;
								}

								auto arg0Type = GetType(inst->mArg0);
								switch (arg0Type->mTypeCode)
								{
								case BeTypeCode_Int64:
									EmitREX(inst->mArg0, BeMCOperand(), true);
									// Fall through
								case BeTypeCode_Int32:
									Emit(0xC1); Emit((0x4 << 3) | (3 << 6) | (EncodeRegNum(inst->mArg0.mReg)));
									Emit((uint8)shiftCount);
									break;
								default:
									NotImpl();
								}
								break;
							}
						}

						switch (instForm)
						{
						case BeMCInstForm_R8_RM8:
							BF_ASSERT(inst->mArg0.mReg == X64Reg_AL);
							EmitREX(BeMCOperand(), inst->mArg1, false);
							Emit(0xF6);
							EmitModRM(0x5, inst->mArg1);
							break;
						case BeMCInstForm_R16_RM16:
						case BeMCInstForm_R32_RM32:
						case BeMCInstForm_R64_RM64: EmitInst(instForm, 0xAF0F, inst); break;
						default:
							NotImpl();
						}
					}
				}
				break;
			case BeMCInstKind_Div:
				{
					auto arg0Type = GetType(inst->mArg0);
					switch (arg0Type->mTypeCode)
					{
					case BeTypeCode_Int8:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_AL));
						// XOR ah, ah
						Emit(0x30); Emit(0xE4);
						// DIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_AX), inst->mArg1, false);
						Emit(0xF6); EmitModRM(0x6, inst->mArg1);
						break;
					case BeTypeCode_Int16:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_AX));
						// XOR dx, dx
						Emit(0x66); Emit(0x31); Emit(0xD2);
						// DIV rm
						Emit(0x66);
						EmitREX(BeMCOperand::FromReg(X64Reg_AX), inst->mArg1, false);
						Emit(0xF7); EmitModRM(0x6, inst->mArg1);
						break;
					case BeTypeCode_Int32:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_EAX));
						// XOR edx, edx
						Emit(0x31); Emit(0xD2);
						// DIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_EAX), inst->mArg1, false);
						Emit(0xF7); EmitModRM(0x6, inst->mArg1);
						break;
					case BeTypeCode_Int64:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_RAX));
						// XOR rdx, rdx
						Emit(0x48); Emit(0x31); Emit(0xD2);
						// DIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_RAX), inst->mArg1, true);
						Emit(0xF7); EmitModRM(0x6, inst->mArg1);
						break;
					}
				}
				break;
			case BeMCInstKind_IDiv:
				{
					if (EmitStdXMMInst(instForm, inst, 0x5E))
						break;

					auto arg0Type = GetType(inst->mArg0);
					switch (arg0Type->mTypeCode)
					{
					case BeTypeCode_Int8:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_AL));
						// CBW
						Emit(0x66); Emit(0x98);
						// IDIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_AX), inst->mArg1, false);
						Emit(0xF6); EmitModRM(0x7, inst->mArg1);
						break;
					case BeTypeCode_Int16:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_AX));
						// CWD
						Emit(0x66); Emit(0x99);
						// IDIV rm
						Emit(0x66);
						EmitREX(BeMCOperand::FromReg(X64Reg_AX), inst->mArg1, false);
						Emit(0xF7); EmitModRM(0x7, inst->mArg1);
						break;
					case BeTypeCode_Int32:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_EAX));
						// CDQ
						Emit(0x99);
						// IDIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_EAX), inst->mArg1, false);
						Emit(0xF7); EmitModRM(0x7, inst->mArg1);
						break;
					case BeTypeCode_Int64:
						BF_ASSERT((inst->mArg0.IsNativeReg()) && (inst->mArg0.mReg == X64Reg_RAX));
						// CQO
						Emit(0x48); Emit(0x99);
						// IDIV rm
						EmitREX(BeMCOperand::FromReg(X64Reg_RAX), inst->mArg1, true);
						Emit(0xF7); EmitModRM(0x7, inst->mArg1);
						break;
					}
				}
				break;
			case BeMCInstKind_IRem:
				{
					NotImpl();
					//if (EmitStdXMMInst(instForm, inst, 0x5E))
						//break;
				}
				break;
			case BeMCInstKind_Cmp:
				{
					switch (instForm)
					{
					case BeMCInstForm_XMM32_FRM32:
					case BeMCInstForm_XMM32_IMM:
						// COMISS
						EmitREX(inst->mArg0, inst->mArg1, false);
						Emit(0x0F); Emit(0x2F);
						EmitModRM(inst->mArg0, inst->mArg1);
						break;
					case BeMCInstForm_XMM64_FRM64:
					case BeMCInstForm_XMM64_IMM:
						// COMISD
						Emit(0x66);
						EmitREX(inst->mArg0, inst->mArg1, false);
						Emit(0x0F); Emit(0x2F);
						EmitModRM(inst->mArg0, inst->mArg1);
						break;
					default:
						{
							if (((inst->mArg0.IsNativeReg()) && (inst->mArg1.IsImmediateInt())) && (inst->mArg1.GetImmediateInt() == 0))
							{
								// Test
								auto typeCode = GetType(inst->mArg0)->mTypeCode;
								switch (typeCode)
								{
								case BeTypeCode_Int8:
									EmitREX(inst->mArg0, inst->mArg0, false);
									Emit(0x84);
									EmitModRM(inst->mArg0, inst->mArg0);
									break;
								case BeTypeCode_Int16: Emit(0x66); // Fallthrough
								case BeTypeCode_Int32:
								case BeTypeCode_Int64:
									EmitREX(inst->mArg0, inst->mArg0, typeCode == BeTypeCode_Int64);
									Emit(0x85); EmitModRM(inst->mArg0, inst->mArg0);
									break;
								default:
									NotImpl();
								}

								/*if (arg0Type->mSize == 2)
									Emit(0x66);
								// Emit as TEST <arg0>, <arg0>
								EmitREX(inst->mArg0, inst->mArg0, GetType(inst->mArg0)->mSize == 8);
								mOut.Write((uint8)0x85); // r/m64, r
								EmitModRM(inst->mArg0, inst->mArg0);*/
							}
							else
							{
								EmitStdInst(instForm, inst, 0x39, 0x3B, 0x81, 0x7, 0x83, 0x7);
							}
						}
					}
				}
				break;
			case BeMCInstKind_And:
				{
					if (EmitIntBitwiseXMMInst(instForm, inst, 0xDB)) //PAND
						break;

					BeMCInst modInst = *inst;

					bool isZeroing = false;

					// Use a shorter form if the upper bytes are masked in
					if ((instForm == BeMCInstForm_RM32_IMM32) || (instForm == BeMCInstForm_RM64_IMM32))
					{
						if ((modInst.mArg1.mImmediate & 0xFFFF0000) == 0xFFFF0000)
						{
							if (modInst.mArg0.IsNativeReg())
								modInst.mArg0.mReg = ResizeRegister(modInst.mArg0.mReg, 2);
							instForm = BeMCInstForm_RM16_IMM16;
							if ((modInst.mArg1.mImmediate & 0xFFFF) == 0)
								isZeroing = true;
						}
						else if (instForm == BeMCInstForm_RM32_IMM32)
						{
							if ((modInst.mArg1.mImmediate & 0xFFFFFFFF) == 0)
							{
								isZeroing = true;
							}
						}
					}

					if ((instForm == BeMCInstForm_RM16_IMM16) || (instForm == BeMCInstForm_RM32_IMM16) || (instForm == BeMCInstForm_RM64_IMM16))
					{
						if ((modInst.mArg1.mImmediate & 0xFFFFFF00) == 0xFFFFFF00)
						{
							if (modInst.mArg0.IsNativeReg())
								modInst.mArg0.mReg = ResizeRegister(modInst.mArg0.mReg, 1);
							instForm = BeMCInstForm_RM8_IMM8;
						}
						else if (instForm == BeMCInstForm_RM16_IMM16)
						{
							if ((modInst.mArg1.mImmediate & 0xFFFF) == 0)
							{
								isZeroing = true;
							}
						}
					}

					if (instForm == BeMCInstForm_RM8_IMM8)
					{
						// Are we really just zeroing out the lower byte?
						if (((modInst.mArg1.mImmediate & 0xFF) == 0) && (modInst.mArg0.IsNativeReg()))
						{
							isZeroing = true;
						}
					}

					if (isZeroing)
					{
						int size = 0;
						switch (instForm)
						{
						case BeMCInstForm_RM32_IMM32:
							instForm = BeMCInstForm_RM32_R32;
							break;
						case BeMCInstForm_RM16_IMM16:
							instForm = BeMCInstForm_RM16_R16;
							break;
						case BeMCInstForm_RM8_IMM8:
							instForm = BeMCInstForm_RM8_R8;
							break;
						default:
							NotImpl();
						}

						//XOR arg0, arg0
						modInst.mKind = BeMCInstKind_Xor;
						modInst.mArg1 = modInst.mArg0;
						EmitStdInst(instForm, &modInst, 0x31, 0x33, 0x81, 0x6, 0x83, 0x6);
						break;
					}

					if (modInst.mArg1.IsImmediateFloat())
					{ //ANDPS
						bool is64Bit = (modInst.mArg1.mKind == BeMCOperandKind_Immediate_f64_Packed128);
						EmitREX(inst->mArg0, inst->mArg1, is64Bit);
						Emit(0x0F); Emit(0x54);
						EmitModRM(inst->mArg0, inst->mArg1);
						break;
					}

					EmitStdInst(instForm, &modInst, 0x21, 0x23, 0x81, 0x4, 0x83, 0x4);
				}
				break;
			case BeMCInstKind_Or:
				{
					if (EmitIntBitwiseXMMInst(instForm, inst, 0xEB)) //POR
						break;
					EmitStdInst(instForm, inst, 0x09, 0x0B, 0x81, 0x1, 0x83, 0x1);
				}
				break;
			case BeMCInstKind_Xor:
				{
					if (EmitIntBitwiseXMMInst(instForm, inst, 0xEF)) //PXOR
						break;
					if (EmitPackedXMMInst(instForm, inst, 0x57))
						break;
					EmitStdInst(instForm, inst, 0x31, 0x33, 0x81, 0x6, 0x83, 0x6);
				}
				break;
			case BeMCInstKind_Shl:
			case BeMCInstKind_Shr:
			case BeMCInstKind_Sar:
				{
					if (instForm == Beefy::BeMCInstForm_XMM128_RM128)
					{
						if (arg1.IsImmediate())
						{
							Emit(0x66);
							EmitREX(arg1, arg0, false);
							Emit(0x0F);
							int rx = 0;
							switch (inst->mKind)
							{
							case BeMCInstKind_Shl:
								rx = 6;
								break;
							case BeMCInstKind_Shr:
								rx = 2;
								break;
							case BeMCInstKind_Sar:
								rx = 4;
								break;
							}
							Emit(0x71); // PSLLW / PSRAW / PSRLW
							EmitModRM(rx, arg0);
							Emit((uint8)arg1.mImmediate);
						}
						else
						{
							Emit(0x66);
							EmitREX(arg0, arg1, false);
							Emit(0x0F);
							switch (inst->mKind)
							{
							case BeMCInstKind_Shl:
								Emit(0xF1); // PSLLW
								break;
							case BeMCInstKind_Shr:
								Emit(0xD1); // PSRLW
								break;
							case BeMCInstKind_Sar:
								Emit(0xE1); // PSRAW
								break;
							}

							EmitModRM(arg0, arg1);
						}
						break;
					}

					int rx = 0;
					switch (inst->mKind)
					{
					case BeMCInstKind_Shl:
						rx = 4;
						break;
					case BeMCInstKind_Shr:
						rx = 5;
						break;
					case BeMCInstKind_Sar:
						rx = 7;
						break;
					}

					bool handled = false;
					switch (instForm)
					{
					case BeMCInstForm_RM8_IMM8:
					case BeMCInstForm_RM16_IMM8:
					case BeMCInstForm_RM32_IMM8:
					case BeMCInstForm_RM64_IMM8:
						if (inst->mArg1.mImmediate == 1)
						{
							// Shift by one has a short form
							if (instForm == BeMCInstForm_RM16_IMM8) Emit(0x66);
							EmitREX(inst->mArg1, inst->mArg0, instForm == BeMCInstForm_RM64_IMM8);
							if (instForm == BeMCInstForm_RM8_IMM8)
								Emit(0xD0);
							else
								Emit(0xD1);
							EmitModRM(rx, inst->mArg0);
							handled = true;
						}
						else
						{
							if (instForm == BeMCInstForm_RM16_IMM8) Emit(0x66);
							EmitREX(inst->mArg1, inst->mArg0, instForm == BeMCInstForm_RM64_IMM8);
							if (instForm == BeMCInstForm_RM8_IMM8)
								Emit(0xC0);
							else
								Emit(0xC1);
							EmitModRM(rx, inst->mArg0);
							Emit((uint8)inst->mArg1.mImmediate);
							handled = true;
						}
						break;
					default:
						{
							BF_ASSERT(inst->mArg1.IsNativeReg());
							BF_ASSERT(inst->mArg1.mReg == X64Reg_RCX);
							int destSize = GetType(inst->mArg0)->mSize;

							if (destSize == 2) Emit(0x66);
							EmitREX(BeMCOperand(), inst->mArg0, destSize == 8);
							if (destSize == 1)
								Emit(0xD2);
							else
								Emit(0xD3);
							EmitModRM(rx, inst->mArg0);
							handled = true;
						}
						break;
					}
				}
				break;
			case BeMCInstKind_Test:
				{
					if (instForm == BeMCInstForm_R64_RM64)
					{
						BF_SWAP(inst->mArg0, inst->mArg1);
						instForm = BeMCInstForm_RM64_R64;
					}

					EmitStdInst(instForm, inst, 0x85, 0x00, 0xF7, 0x0);
				}
				break;
			case BeMCInstKind_CondBr:
				{
					if (inst->mArg0.mKind == BeMCOperandKind_Immediate_i64)
					{
						mOut.Write(GetJumpOpCode(inst->mArg1.mCmpKind, false));
						mOut.Write((uint8)inst->mArg0.mImmediate);
					}
					else
					{
						BF_ASSERT(inst->mArg0.mKind == BeMCOperandKind_Label);
						BeMCJump jump;
						jump.mCodeOffset = funcCodePos;
						jump.mLabelIdx = inst->mArg0.mLabelIdx;
						// Speculative make it a short jump
						jump.mJumpKind = 0;
						jump.mCmpKind = inst->mArg1.mCmpKind;
						deferredJumps.push_back(jump);

						mOut.Write(GetJumpOpCode(jump.mCmpKind, false));
						mOut.Write((uint8)0);
					}
				}
				break;
			case BeMCInstKind_Br:
				{
					if (inst->mArg1.mKind == BeMCOperandKind_Immediate_i8)
					{
						if (inst->mArg1.mImmediate == 2) // Fake?
							break;
					}

					if (inst->mArg0.mKind == BeMCOperandKind_Label)
					{
						BeMCJump jump;
						jump.mCodeOffset = funcCodePos;
						jump.mLabelIdx = inst->mArg0.mLabelIdx;
						// Speculatively make it a short jump
						jump.mJumpKind = 0;
						jump.mCmpKind = BeCmpKind_None;
						deferredJumps.push_back(jump);
						mOut.Write((uint8)0xEB);
						mOut.Write((uint8)0);
					}
					else
					{
						auto arg0Type = GetType(inst->mArg0);
						BF_ASSERT(arg0Type->mTypeCode == BeTypeCode_Int64);

						uint8 rex = GetREX(BeMCOperand(), inst->mArg0, true);
						if (rex != 0x40)
							Emit(rex);

						Emit(0xFF);
						EmitModRM(4, inst->mArg0);
					}
				}
				break;
			case BeMCInstKind_Ret:
				mOut.Write((uint8)0xC3);
				break;
			case BeMCInstKind_Call:
				{
					switch (instForm)
					{
					case BeMCInstForm_Symbol:
						{
							// Call [rip+<X>]
							mOut.Write((uint8)0xFF);
							mOut.Write((uint8)0x15);

							BeMCRelocation reloc;
							reloc.mKind = BeMCRelocationKind_REL32;
							reloc.mOffset = mOut.GetPos();
							reloc.mSymTableIdx = inst->mArg0.mSymbolIdx;
							mCOFFObject->mTextSect.mRelocs.push_back(reloc);
							mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);

							mOut.Write((int32)0);
						}
						break;
					case BeMCInstForm_SymbolAddr:
						{
							// Call <X>
							mOut.Write((uint8)0xE8);

							BeMCRelocation reloc;
							reloc.mKind = BeMCRelocationKind_REL32;
							reloc.mOffset = mOut.GetPos();
							reloc.mSymTableIdx = inst->mArg0.mSymbolIdx;
							mCOFFObject->mTextSect.mRelocs.push_back(reloc);
							mTextRelocs.push_back((int)mCOFFObject->mTextSect.mRelocs.size() - 1);

							mOut.Write((int32)0);
						}
						break;
					default:
						{
							EmitREX(BeMCOperand(), inst->mArg0, true);
							mOut.Write((uint8)0xFF);
							EmitModRM(0x2, inst->mArg0);
						}
						break;
					}
				}
				//mOut.Write((uint8)0xC3);
				break;
			default:
				SoftFail("Unhandled instruction in DoCodeEmission", inst->mDbgLoc);
				break;
			}
		}
	}
	mActiveInst = NULL;

	if (mDebugging)
	{
		dbgStr += "\nEMISSIONS:\n";
	}

	// Finish range for all outstanding variables
	for (int vregLiveIdx : *vregsLive)
	{
		if (vregLiveIdx >= mLivenessContext.mNumItems)
			continue;

		int vregIdx = vregLiveIdx % mLivenessContext.mNumItems;
		auto dbgVar = mVRegInfo[vregIdx]->mDbgVariable;
		if (dbgVar != NULL)
		{
			dbgVar->mDeclEnd = mOut.GetPos() - textSectStartPos;
			dbgVar->mDeclLifetimeExtend = false;
			BF_ASSERT(dbgVar->mDeclEnd >= dbgVar->mDeclStart);
			if (!dbgVar->mSavedRanges.empty())
			{
				auto& savedRange = dbgVar->mSavedRanges.back();
				if (savedRange.mLength == -1)
				{
					savedRange.mLength = dbgVar->mDeclEnd - savedRange.mOffset;
					dbgVar->mGaps.push_back(savedRange);
				}
			}
		}
	}

	auto& codeVec = mOut.mData;
	for (int pass = 0; true; pass++)
	{
		bool didWidening = false;

		for (auto& jump : deferredJumps)
		{
			int labelPos = labelPositions[jump.mLabelIdx];
			int offsetRel = jump.mCodeOffset;
			if (jump.mJumpKind == 0)
				offsetRel += 2;
			else if (jump.mJumpKind == 1)
				offsetRel += 4;
			else if (jump.mJumpKind == 2)
			{
				if (jump.mCmpKind == BeCmpKind_None)
					offsetRel += 5;
				else
					offsetRel += 6;
			}
			int offset = labelPos - offsetRel;
			//BF_ASSERT((offset >= -128) && (offset <= 127));

			if ((jump.mJumpKind == 0) &&
				((offset < -0x80) || (offset > 0x7F)))
			{
				// Extend this guy into a rel32
				int adjustFrom = jump.mCodeOffset + 2;
				int adjustBytes = 3;
				if (jump.mCmpKind != BeCmpKind_None)
					adjustBytes++;
				codeVec.Insert(jump.mCodeOffset + 1 + textSectStartPos, (uint8)0xCC, adjustBytes);
				mOut.mPos += adjustBytes;

				if (jump.mCmpKind == BeCmpKind_None)
				{
					codeVec[jump.mCodeOffset + textSectStartPos] = 0xE9;
				}
				else
				{
					codeVec[jump.mCodeOffset + textSectStartPos] = 0x0F;
					codeVec[jump.mCodeOffset + 1 + textSectStartPos] = GetJumpOpCode(jump.mCmpKind, true);
				}

#define CODE_OFFSET_ADJUST(val) if (val >= adjustFrom) val += adjustBytes
				for (auto& labelPosition : labelPositions)
					CODE_OFFSET_ADJUST(labelPosition);
				for (auto& checkJump : deferredJumps)
					CODE_OFFSET_ADJUST(checkJump.mCodeOffset);
				if (mDbgFunction != NULL)
				{
					for (auto& codeEmission : mDbgFunction->mEmissions)
						CODE_OFFSET_ADJUST(codeEmission.mPos);
					for (auto dbgVar : mDbgFunction->mVariables)
					{
						if (dbgVar == NULL)
							continue;
						CODE_OFFSET_ADJUST(dbgVar->mDeclStart);
						CODE_OFFSET_ADJUST(dbgVar->mDeclEnd);
						for (auto& range : dbgVar->mSavedRanges)
							CODE_OFFSET_ADJUST(range.mOffset);
						for (auto& range : dbgVar->mGaps)
							CODE_OFFSET_ADJUST(range.mOffset);
					}
				}
				for (auto& deferredUnwind : deferredUnwinds)
					CODE_OFFSET_ADJUST(deferredUnwind.mCodePos);
				for (int textRelocIdx : mTextRelocs)
				{
					auto& reloc = mCOFFObject->mTextSect.mRelocs[textRelocIdx];
					if (reloc.mOffset - textSectStartPos >= adjustFrom)
						reloc.mOffset += adjustBytes;
				}

				for (auto& dbgInstPos : dbgInstPositions)
					CODE_OFFSET_ADJUST(dbgInstPos.mPos);

#undef CODE_OFFSET_ADJUST

				jump.mJumpKind = 2;
				didWidening = true;
			}

			//TODO: Test extending into a long jump

			if (jump.mJumpKind == 0)
				codeVec[jump.mCodeOffset + 1 + textSectStartPos] = (uint8)offset;
			else if (jump.mCmpKind == BeCmpKind_None)
				*(int32*)(&codeVec[jump.mCodeOffset + 1 + textSectStartPos]) = (uint32)offset;
			else
				*(int32*)(&codeVec[jump.mCodeOffset + 2 + textSectStartPos]) = (uint32)offset;
		}

		if (!didWidening)
			break;
	}

	if (!mSwitchEntries.empty())
	{
		auto thisFuncSym = mCOFFObject->GetSymbol(mBeFunction);
		for (auto& switchEntry : mSwitchEntries)
		{
			auto& sect = mCOFFObject->mRDataSect;
			int32* ofsPtr = (int32*)((uint8*)sect.mData.GetPtr() + switchEntry.mOfs);
			*ofsPtr = labelPositions[switchEntry.mBlock->mLabelIdx];

			BeMCRelocation reloc;
			reloc.mKind = BeMCRelocationKind_ADDR32NB;
			reloc.mOffset = switchEntry.mOfs;
			reloc.mSymTableIdx = thisFuncSym->mIdx;

			sect.mRelocs.push_back(reloc);
		}
	}

	//for (auto& deferredUnwind : deferredUnwinds)

	for (int deferredUnwindIdx = (int)deferredUnwinds.size() - 1; deferredUnwindIdx >= 0; deferredUnwindIdx--)
	{
		auto inst = deferredUnwinds[deferredUnwindIdx].mUnwindInst;
		int codePos = deferredUnwinds[deferredUnwindIdx].mCodePos;

		switch (inst->mKind)
		{
		case BeMCInstKind_Unwind_Alloc:
			{
				xdata.Write((uint8)(codePos));

				int allocSize = (int)inst->mArg0.mImmediate;
				if (allocSize <= 128)
				{
					// UWOP_ALLOC_SMALL
					xdata.Write((uint8)((2) | ((allocSize / 8 - 1) << 4)));
				}
				else if ((allocSize <= 0x7FFF8) && ((allocSize & 7) == 0)) // up to 512k-8 bytes
				{
					// UWOP_ALLOC_LARGE
					xdata.Write((uint8)((1) | ((0) << 4)));
					xdata.Write((uint8)((allocSize / 8) & 0xFF));
					xdata.Write((uint8)((allocSize / 8) >> 8));
				}
				else
				{
					// UWOP_ALLOC_LARGE+
					xdata.Write((uint8)((1) | ((1) << 4)));
					xdata.Write((int32)allocSize);
				}
			}
			break;
		case BeMCInstKind_Unwind_PushReg:
			{
				xdata.Write((uint8)(codePos));

				int regNum = 0;
				switch (inst->mArg0.mReg)
				{
				case X64Reg_RAX: regNum = 0; break;
				case X64Reg_RCX: regNum = 1; break;
				case X64Reg_RDX: regNum = 2; break;
				case X64Reg_RBX: regNum = 3; break;
				case X64Reg_RSP: regNum = 4; break;
				case X64Reg_RBP: regNum = 5; break;
				case X64Reg_RSI: regNum = 6; break;
				case X64Reg_RDI: regNum = 7; break;
				case X64Reg_R8: regNum = 8; break;
				case X64Reg_R9: regNum = 9; break;
				case X64Reg_R10: regNum = 10; break;
				case X64Reg_R11: regNum = 11; break;
				case X64Reg_R12: regNum = 12; break;
				case X64Reg_R13: regNum = 13; break;
				case X64Reg_R14: regNum = 14; break;
				case X64Reg_R15: regNum = 15; break;
				default: NotImpl();
				}
				// UWOP_PUSH_NONVOL
				xdata.Write((uint8)((0) | (regNum << 4)));
			}
			break;
		case BeMCInstKind_Unwind_SaveXMM:
			{
				xdata.Write((uint8)(codePos));
				int regNum = 0;
				switch (inst->mArg0.mReg)
				{
				case X64Reg_M128_XMM0: regNum = 0; break;
				case X64Reg_M128_XMM1: regNum = 1; break;
				case X64Reg_M128_XMM2: regNum = 2; break;
				case X64Reg_M128_XMM3: regNum = 3; break;
				case X64Reg_M128_XMM4: regNum = 4; break;
				case X64Reg_M128_XMM5: regNum = 5; break;
				case X64Reg_M128_XMM6: regNum = 6; break;
				case X64Reg_M128_XMM7: regNum = 7; break;
				case X64Reg_M128_XMM8: regNum = 8; break;
				case X64Reg_M128_XMM9: regNum = 9; break;
				case X64Reg_M128_XMM10: regNum = 10; break;
				case X64Reg_M128_XMM11: regNum = 11; break;
				case X64Reg_M128_XMM12: regNum = 12; break;
				case X64Reg_M128_XMM13: regNum = 13; break;
				case X64Reg_M128_XMM14: regNum = 14; break;
				case X64Reg_M128_XMM15: regNum = 15; break;
				default: NotImpl();
				}
				// UWOP_SAVE_XMM128
				xdata.Write((uint8)((8) | (regNum << 4)));
				xdata.Write((int16)(inst->mArg1.mImmediate / 16));
			}
			break;
		case BeMCInstKind_Unwind_SetBP:
			{
				xdata.Write((uint8)(codePos));
				// UWOP_SET_FPREG
				xdata.Write((uint8)((3) | (0 << 4)));
			}
			break;
		}
	}

	int codeLen = mOut.GetPos() - textSectStartPos;
	int minCodeLen = hotJumpLen;
	int addCodeLen = minCodeLen - codeLen;
	for (int i = 0; i < addCodeLen; i++)
	{
		mOut.Write((uint8)0x90);
	}

	if (mDbgFunction != NULL)
	{
		mDbgFunction->mCodeLen = mOut.GetPos() - textSectStartPos;
	}

	if (hasPData)
	{
		int codeLen = mOut.GetPos() - textSectStartPos;

		// PDATA end addr
		BeMCRelocation reloc;
		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = mCOFFObject->mPDataSect.mData.GetPos();
		reloc.mSymTableIdx = mCOFFObject->GetSymbol(mBeFunction)->mIdx;
		mCOFFObject->mPDataSect.mRelocs.push_back(reloc);
		mCOFFObject->mPDataSect.mData.Write((int32)codeLen);

		// XDATA pos
		reloc.mKind = BeMCRelocationKind_ADDR32NB;
		reloc.mOffset = mCOFFObject->mPDataSect.mData.GetPos();
		reloc.mSymTableIdx = mCOFFObject->mXDataSect.mSymbolIdx;
		mCOFFObject->mPDataSect.mRelocs.push_back(reloc);
		mCOFFObject->mPDataSect.mData.Write((int32)xdataStartPos);

		int numCodes = (xdata.GetPos() - xdataStartPos - 4) / 2;
		if (numCodes > 0)
		{
			xdata.mData[xdataStartPos + 1] = (uint8)(deferredUnwinds.back().mCodePos); // prolog size
			xdata.mData[xdataStartPos + 2] = (uint8)numCodes;
		}
	}

	for (auto& dbgInstPos : dbgInstPositions)
	{
		auto inst = dbgInstPos.mInst;

		dbgStr += StrFormat("%d[%d]", dbgInstPos.mPos, dbgInstPos.mOrigPos);
		if (inst->mDbgLoc != NULL)
			dbgStr += StrFormat("@%d", inst->mDbgLoc->mIdx);

		if (inst->mResult.mKind != BeMCOperandKind_None)
		{
			dbgStr += " ";
			dbgStr += ToString(inst->mResult);
			dbgStr += " = ";
		}

		dbgStr += " ";
		dbgStr += gOpName[(int)inst->mKind];

		if (inst->mArg0.mKind != BeMCOperandKind_None)
		{
			dbgStr += " ";
			dbgStr += ToString(inst->mArg0);
		}

		if (inst->mArg1.mKind != BeMCOperandKind_None)
		{
			dbgStr += ", ";
			dbgStr += ToString(inst->mArg1);
		}

		dbgStr += "\n";
	}

	if ((mDebugging) && (mDbgFunction != NULL))
	{
		dbgStr += "\nDebug Variables:\n";
		for (auto dbgVar : mDbgFunction->mVariables)
		{
			if (dbgVar == NULL)
				continue;

			dbgStr += StrFormat("%s %d to %d", dbgVar->mName.c_str(), dbgVar->mDeclStart, dbgVar->mDeclEnd);
			if (dbgVar->mDeclLifetimeExtend)
				dbgStr += " LifetimeExtend";
			dbgStr += "\n";
			for (auto& gap : dbgVar->mGaps)
			{
				if (gap.mLength == -1)
					dbgStr += StrFormat(" Gap %d to <unterminated>\n", gap.mOffset);
				else
					dbgStr += StrFormat(" Gap %d to %d\n", gap.mOffset, gap.mOffset + gap.mLength);
			}
			for (auto& gap : dbgVar->mSavedRanges)
			{
				if (gap.mLength == -1)
					dbgStr += StrFormat(" SavedRange %d to <unterminated>\n", gap.mOffset);
				else
					dbgStr += StrFormat(" SavedRange %d to %d\n", gap.mOffset, gap.mOffset + gap.mLength);
			}
		}
	}

	if (!dbgStr.empty())
	{
		dbgStr += "\n";
		OutputDebugStr(dbgStr);
	}
}

void BeMCContext::HandleParams()
{
	auto beModule = mBeFunction->mModule;
	int regIdxOfs = 0;
	int paramOfs = 0;
	auto retType = mBeFunction->GetFuncType()->mReturnType;

	X64CPURegister compositeRetReg = X64Reg_None;
	bool flipFirstRegs = false;
	if (mBeFunction->HasStructRet())
	{
		flipFirstRegs = mBeFunction->mCallingConv == BfIRCallingConv_ThisCall;

		//paramOfs = 1;
		/*auto ptrType = (BePointerType*)mBeFunction->mFuncType->mParams[0].mType;
		BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
		retType = ptrType->mElementType;*/
		//retType = mBeFunction->mFuncType->mParams[0].mType;

		//flipFirstRegs = mBeFunction->mCallingConv == BfIRCallingConv_ThisCall;
		/*
		auto beArg = beModule->GetArgument(0);
		compositeRetReg = (mBeFunction->mCallingConv == BfIRCallingConv_ThisCall) ? X64Reg_RDX : X64Reg_RCX;
		mParamsUsedRegs.push_back(compositeRetReg);

		BeMCOperand mcOperand;
		mcOperand.mReg = compositeRetReg;
		mcOperand.mKind = BeMCOperandKind_NativeReg;

		auto ptrType = (BePointerType*)mBeFunction->mFuncType->mParams[0].mType;
		BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
		auto paramVReg = AllocVirtualReg(ptrType->mElementType);
		auto paramVRegInfo = GetVRegInfo(paramVReg);
		CreateDefineVReg(paramVReg);
		paramVRegInfo->mNaturalReg = compositeRetReg;
		AllocInst(BeMCInstKind_Mov, paramVReg, mcOperand);

		mValueToOperand[beArg] = paramVReg;*/
	}
	else if (retType->IsNonVectorComposite())
	{
		compositeRetReg = (mBeFunction->mCallingConv == BfIRCallingConv_ThisCall) ? X64Reg_RDX : X64Reg_RCX;
		auto retVReg = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_Int64));
		auto retVRegInfo = GetVRegInfo(retVReg);
		retVRegInfo->mNaturalReg = compositeRetReg;
		retVRegInfo->mForceReg = true;
		retVRegInfo->mMustExist = true;
		AllocInst(BeMCInstKind_Mov, retVReg, BeMCOperand::FromReg(compositeRetReg));
		mCompositeRetVRegIdx = retVReg.mVRegIdx;
		mParamsUsedRegs.push_back(compositeRetReg);
	}

	for (int paramIdx = 0; paramIdx < (int)mBeFunction->mParams.size() - paramOfs; paramIdx++)
	{
		if (((paramIdx == 0) && (compositeRetReg == X64Reg_RCX)) ||
			((paramIdx == 1) && (compositeRetReg == X64Reg_RDX)))
			regIdxOfs = 1;

		auto funcType = mBeFunction->GetFuncType();
		auto& typeParam = funcType->mParams[paramIdx + paramOfs];
		auto& param = mBeFunction->mParams[paramIdx + paramOfs];
		auto beArg = beModule->GetArgument(paramIdx + paramOfs);

		BeMCOperand mcOperand;
		mcOperand.mReg = X64Reg_None;
		mcOperand.mKind = BeMCOperandKind_NativeReg;

		int regIdx = paramIdx + regIdxOfs;

		if (typeParam.mType->IsFloat())
		{
			switch (regIdx)
			{
			case 0:
				mcOperand.mReg = X64Reg_M128_XMM0; //X64Reg_XMM0_f64;
				break;
			case 1:
				mcOperand.mReg = X64Reg_M128_XMM1; //X64Reg_XMM1_f64;
				break;
			case 2:
				mcOperand.mReg = X64Reg_M128_XMM2; //X64Reg_XMM2_f64;
				break;
			case 3:
				mcOperand.mReg = X64Reg_M128_XMM3; //X64Reg_XMM3_f64;
				break;
			}
		}
		else
		{
			switch (regIdx)
			{
			case 0:
				mcOperand.mReg = !flipFirstRegs ? X64Reg_RCX : X64Reg_RDX;
				break;
			case 1:
				mcOperand.mReg = !flipFirstRegs ? X64Reg_RDX : X64Reg_RCX;
				break;
			case 2:
				mcOperand.mReg = X64Reg_R8;
				break;
			case 3:
				mcOperand.mReg = X64Reg_R9;
				break;
			}
		}

		if (mcOperand.mReg != X64Reg_None)
		{
			mParamsUsedRegs.push_back(mcOperand.mReg);
			mcOperand.mReg = ResizeRegister(mcOperand.mReg, typeParam.mType);
		}

		BeMCOperand paramVReg;
		/*if ((paramIdx == 0) && (mBeFunction->mStructRet))
		{
			auto ptrType = (BePointerType*)typeParam.mType;
			BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
			paramVReg = AllocVirtualReg(ptrType->mElementType);
			paramVReg.mKind = BeMCOperandKind_VRegAddr;
			//paramVReg.mR
		}
		else*/
		paramVReg = AllocVirtualReg(typeParam.mType);
		auto paramVRegInfo = GetVRegInfo(paramVReg);

		if ((mBeFunction->HasStructRet()) && (paramIdx == 0))
		{
			paramVRegInfo->SetRetVal();
		}
		else
			paramVRegInfo->mForceMerge = true;

		CreateDefineVReg(paramVReg);
		if (mcOperand.mReg != X64Reg_None)
		{
			// This indirection allows us to NOT directly use a register for a parameter
			//  if the cost of saving/restoring this volatile reg is too great
			paramVRegInfo->mNaturalReg = mcOperand.mReg;
			AllocInst(BeMCInstKind_Mov, paramVReg, mcOperand);
		}
		else
		{
			paramVRegInfo->mMustExist = true;
			paramVRegInfo->mForceMem = true;
			paramVRegInfo->mFrameOffset = paramIdx * 8 + 8;
			CreateDefineVReg(paramVReg);
		}
		//paramVRegInfo->mDbgVariable = mDbgFunction->mParams[paramIdx];

		mValueToOperand[beArg] = paramVReg;
	}
}

void BeMCContext::ToString(BeMCInst* inst, String& str, bool showVRegFlags, bool showVRegDetails)
{
	if (inst == NULL)
	{
		str += "NULL\n";
		return;
	}

	if (inst->mKind == BeMCInstKind_Label)
	{
		str += ToString(inst->mArg0);
		str += ":";
	}
	else
	{
		str += "  ";

		if (inst->mResult)
		{
			str += ToString(inst->mResult);
			str += " = ";
		}

		str += gOpName[(int)inst->mKind];

		if (inst->mKind == BeMCInstKind_DbgDecl)
		{
			auto vregInfo = GetVRegInfo(inst->mArg0);
			if ((vregInfo != NULL) && (vregInfo->mDbgVariable != NULL))
			{
				if (vregInfo->mDbgVariable->mIsValue)
					str += " <value>";
				else
					str += " <addr>";
			}
		}

		if (inst->mKind == BeMCInstKind_DefPhi)
		{
			str += " ";
			str += ToString(inst->mArg0);
			for (auto& val : inst->mArg0.mPhi->mValues)
			{
				str += ", [";
				str += ToString(BeMCOperand::FromBlock(val.mBlockFrom));
				str += ", ";
				str += ToString(val.mValue);
				str += "]";
			}
		}
		else if (inst->mKind == BeMCInstKind_Load)
		{
			str += " ";
			str += ToString(inst->mArg0);
			str += ", [";
			str += ToString(inst->mArg1);
			str += "]";
		}
		else if (inst->mKind == BeMCInstKind_Store)
		{
			str += " [";
			str += ToString(inst->mArg0);
			str += "], ";
			str += ToString(inst->mArg1);
		}
		else if (inst->mArg0.mKind != BeMCOperandKind_None)
		{
			str += " ";
			str += ToString(inst->mArg0);
			if (inst->mArg1.mKind != BeMCOperandKind_None)
			{
				str += ", ";
				str += ToString(inst->mArg1);
			}
		}
	}

	if (inst->IsDef())
	{
		auto vregInfo = mVRegInfo[inst->mArg0.mVRegIdx];
		if (vregInfo->mRefCount != -1)
			str += StrFormat(" : %d refs", vregInfo->mRefCount);
	}

	/*if (inst->mKind == BeMCInstKind_Def)
	{
		str += " ";
		str += mModule->ToString(GetType(inst->mArg0));
	}*/

	bool hadSemi = false;
	if (inst->mDbgLoc != NULL)
	{
		str += StrFormat("  ; @%d[%d:%d]", inst->mDbgLoc->mIdx, inst->mDbgLoc->mLine + 1, inst->mDbgLoc->mColumn + 1);
		hadSemi = true;
	}

	bool showLiveness = showVRegFlags;
	if ((showLiveness) && (inst->mLiveness != NULL))
	{
		if (!hadSemi)
		{
			str += "  ;";
			hadSemi = true;
		}

		bool isFirstInScope = true;
		str += " live: ";
		int vregIdx = -1;
		for (int nextVRegIdx : *inst->mLiveness)
		{
			if (nextVRegIdx >= mLivenessContext.mNumItems)
			{
				vregIdx = nextVRegIdx;
				int showVRegIdx = vregIdx - mLivenessContext.mNumItems;
				if (!mLivenessContext.IsSet(inst->mLiveness, showVRegIdx))
					str += StrFormat(", %d*", showVRegIdx);
				continue;
			}
			if (vregIdx != -1)
				str += ", ";
			vregIdx = nextVRegIdx;
			str += StrFormat("%d", vregIdx);

			if (showVRegDetails)
			{
				auto vregInfo = mVRegInfo[vregIdx];
				if (vregInfo->mForceReg)
					str += "r";
				if (vregInfo->mForceMem)
					str += "m";
				if (vregInfo->mSpilled)
					str += "s";
			}
		}

		if (inst->mLiveness->mNumChanges > 0)
		{
			str += " | ";
			for (int changeIdx = 0; changeIdx < inst->mLiveness->mNumChanges; changeIdx++)
			{
				if (changeIdx != 0)
					str += ", ";
				int vregIdx = inst->mLiveness->GetChange(changeIdx);
				if (vregIdx >= 0)
					str += StrFormat("+%d", vregIdx);
				else
					str += StrFormat("-%d", -vregIdx - 1);
			}
		}
	}

	bool showInitialized = showVRegFlags;
	if ((showInitialized) && (inst->mVRegsInitialized != NULL))
	{
		if (!hadSemi)
		{
			str += "  ;";
			hadSemi = true;
		}

		bool isFirstUninit = true;
		str += " init: ";
		int vregIdx = -1;
		for (int nextVRegIdx : *inst->mVRegsInitialized)
		{
			if (nextVRegIdx >= mVRegInitializedContext.mNumItems)
			{
				if (isFirstUninit)
					str += " uninit: ";
				else
					str += ", ";
				isFirstUninit = false;
				vregIdx = nextVRegIdx;
				str += StrFormat("%d", vregIdx - mVRegInitializedContext.mNumItems);
			}
			else
			{
				if (vregIdx != -1)
					str += ", ";
				vregIdx = nextVRegIdx;
				str += StrFormat("%d", vregIdx);

				auto vregInfo = mVRegInfo[vregIdx];
				if (vregInfo->mValueScopeRetainedKind == BeMCValueScopeRetainKind_Soft)
					str += "r";
				else if (vregInfo->mValueScopeRetainedKind == BeMCValueScopeRetainKind_Hard)
					str += "rh";
			}
		}

		if (inst->mVRegsInitialized->mNumChanges > 0)
		{
			str += " | ";
			for (int changeIdx = 0; changeIdx < inst->mVRegsInitialized->mNumChanges; changeIdx++)
			{
				if (changeIdx != 0)
					str += ", ";
				int vregIdx = inst->mVRegsInitialized->GetChange(changeIdx);
				if (vregIdx >= 0)
					str += StrFormat("+%d", vregIdx);
				else
					str += StrFormat("-%d", -vregIdx - 1);
			}
		}
	}

	if (inst->mVRegLastUseRecord != NULL)
	{
		str += " lastUse: ";
		auto checkLastUse = inst->mVRegLastUseRecord;
		while (checkLastUse != NULL)
		{
			if (checkLastUse != inst->mVRegLastUseRecord)
				str += ", ";
			str += StrFormat("%d", checkLastUse->mVRegIdx);
			checkLastUse = checkLastUse->mNext;
		}
	}

	str += "\n";
}

String BeMCContext::ToString(bool showVRegFlags, bool showVRegDetails)
{
	String str;
	str += mBeFunction->mName;
	str += "\n";
	str += StrFormat("Stack Size: 0x%X\n", mStackSize);
	str += "Frame Objects:\n";
	for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
	{
		int showVRegIdx = vregIdx;
		auto vregInfo = mVRegInfo[showVRegIdx];
		if ((vregInfo->mIsRetVal) && (mCompositeRetVRegIdx != -1))
		{
			showVRegIdx = mCompositeRetVRegIdx;
			vregInfo = mVRegInfo[showVRegIdx];
		}

		if (vregInfo->mFrameOffset != INT_MIN)
		{
			str += "  ";
			str += ToString(BeMCOperand::FromVReg(vregIdx));
			str += StrFormat(": size=%d, align=%d, at ", vregInfo->mType->mSize, vregInfo->mAlign);

			X64CPURegister reg;
			int offset;
			GetValAddr(BeMCOperand::FromVRegAddr(showVRegIdx), reg, offset);

			str += "[";
			str += X64CPURegisters::GetRegisterName(reg);
			if (offset != 0)
				str += StrFormat(" + 0x%X", offset);
			str += "]";
			str += "\n";
		}
	}
	str += "\n";
	for (int blockIdx = 0; blockIdx < (int)mBlocks.size(); blockIdx++)
	{
		auto mcBlock = mBlocks[blockIdx];
		if (blockIdx > 0)
			str += "\n";
		if (mBlocks.size() > 1)
		{
			str += mcBlock->mName;
			str += ":";
			if (mcBlock->mIsLooped)
				str += "  ; looped";
			str += "  ; preds = ";
			for (int predIdx = 0; predIdx < (int)mcBlock->mPreds.size(); predIdx++)
			{
				if (predIdx != 0)
					str += ", ";
				str += "%";
				str += mcBlock->mPreds[predIdx]->mName;
			}

			///
			str += "  ; succs = ";
			for (int succIdx = 0; succIdx < (int)mcBlock->mSuccs.size(); succIdx++)
			{
				if (succIdx != 0)
					str += ", ";
				str += "%";
				str += mcBlock->mSuccs[succIdx]->mName;
			}
			//

			str += "\n";
		}

		for (auto inst : mcBlock->mInstructions)
			ToString(inst, str, showVRegFlags, showVRegDetails);
	}
	str += "\n";
	return str;
}

void BeMCContext::Print()
{
	OutputDebugStr(ToString(true, false));
}

void BeMCContext::Print(bool showVRegFlags, bool showVRegDetails)
{
	OutputDebugStr(ToString(showVRegFlags, showVRegDetails));
}

BeMCOperand BeMCContext::AllocBinaryOp(BeMCInstKind instKind, const BeMCOperand& lhs, const BeMCOperand& rhs, BeMCBinIdentityKind identityKind, BeMCOverflowCheckKind overflowCheckKind)
{
	if ((lhs.IsImmediate()) && (lhs.mKind == rhs.mKind))
	{
		if (instKind == BeMCInstKind_Add)
		{
			BeMCOperand result;
			result.mKind = lhs.mKind;
			switch (lhs.mKind)
			{
			case BeMCOperandKind_Immediate_i32:
				result.mImmediate = lhs.mImmediate + rhs.mImmediate;
				return result;
			}
		}
	}

	if (identityKind == BeMCBinIdentityKind_Any_IsOne)
	{
		if (((lhs.IsImmediateFloat()) && (lhs.GetImmediateDouble() == 1.0)) ||
			((lhs.IsImmediateInt()) && (lhs.mImmediate == 1)))
			return rhs;
	}

	if (identityKind == BeMCBinIdentityKind_Right_IsOne_Result_Zero)
	{
		if (((rhs.IsImmediateFloat()) && (rhs.GetImmediateDouble() == 1.0)) ||
			((rhs.IsImmediateInt()) && (rhs.mImmediate == 1)))
		{
			BeMCOperand operand = rhs;
			operand.mImmediate = 0;
			return operand;
		}
	}

	if ((identityKind == BeMCBinIdentityKind_Right_IsOne) || (identityKind == BeMCBinIdentityKind_Any_IsOne))
	{
		if (((rhs.IsImmediateFloat()) && (rhs.GetImmediateDouble() == 1.0)) ||
			((rhs.IsImmediateInt()) && (rhs.mImmediate == 1)))
			return lhs;
	}

	if ((identityKind == BeMCBinIdentityKind_Right_IsZero) || (identityKind == BeMCBinIdentityKind_Any_IsZero))
	{
		if (((rhs.IsImmediateFloat()) && (rhs.GetImmediateDouble() == 0.0)) ||
			((rhs.IsImmediateInt()) && (rhs.mImmediate == 0)))
			return lhs;
	}

	if (identityKind == BeMCBinIdentityKind_Any_IsZero)
	{
		if (((lhs.IsImmediateFloat()) && (lhs.GetImmediateDouble() == 0.0)) ||
			((lhs.IsImmediateInt()) && (lhs.mImmediate == 0)))
			return rhs;
	}

	auto result = AllocVirtualReg(GetType(lhs));
	AllocInst(BeMCInstKind_Def, result);

	auto mcInst = AllocInst(instKind, lhs, rhs);
	mcInst->mResult = result;

	if (overflowCheckKind != BeMCOverflowCheckKind_None)
	{
		mcInst->mDisableShortForm = true;
		AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromImmediate(1), BeMCOperand::FromCmpKind((overflowCheckKind == BeMCOverflowCheckKind_B) ? BeCmpKind_NB : BeCmpKind_NO));
		AllocInst(BeMCInstKind_DbgBreak);
	}

	return result;
}

void BeMCContext::Generate(BeFunction* function)
{
	BP_ZONE_F("BeMCContext::Generate %s", function->mName.c_str());

	mBeFunction = function;
	mDbgFunction = mBeFunction->mDbgFunction;
	mModule = function->mModule;

	if (!mModule->mTargetCPU.IsEmpty())
		mModule->mBeIRCodeGen->Fail(StrFormat("Cannot set Target CPU to '%s' for +Og optimization. Considering compiling under a different optimization setting.", mModule->mTargetCPU.c_str()));
	if ((!mModule->mTargetTriple.IsEmpty()) && (!mModule->mTargetTriple.StartsWith("x86_64-pc-windows")))
	{
		mModule->mBeIRCodeGen->Fail(StrFormat("Cannot set Target Triple to '%s' for +Og optimization. Considering compiling under a different optimization setting.", mModule->mTargetTriple.c_str()));
		return;
	}

	//mDbgPreferredRegs[15] = X64Reg_RCX;
	//mDbgPreferredRegs[7] = X64Reg_RCX;
	/*mDbgPreferredRegs[14] = X64Reg_RAX;
	mDbgPreferredRegs[15] = X64Reg_None;
	mDbgPreferredRegs[19] = X64Reg_None;
	mDbgPreferredRegs[31] = X64Reg_R8;
	mDbgPreferredRegs[32] = X64Reg_R8;*/

	//mDbgPreferredRegs[8] = X64Reg_RAX;
	//mDebugging = (function->mName == "?stbi__gif_load_next@6$StbImage@StbImageBeef@bf@@SAPEAEPEAVstbi__context@123@PEAVstbi__gif@123@PEAHHPEAE@Z");
	//		|| (function->mName == "?MethodA@TestProgram@BeefTest@bf@@CAXXZ");
	// 		|| (function->mName == "?Hey@Blurg@bf@@SAXXZ")
	// 		;
			//"?ColorizeCodeString@IDEUtils@IDE@bf@@SAXPEAVString@System@3@W4CodeKind@123@@Z";
		//"?Main@Program@bf@@CAHPEAV?$Array1@PEAVString@System@bf@@@System@2@@Z";

			//"?Hey@Blurg@bf@@SAXXZ";
			//"?get__Value@?$Nullable@ULineAndColumn@EditWidgetContent@widgets@Beefy@bf@@@System@bf@@QEAAULineAndColumn@EditWidgetContent@widgets@Beefy@3@XZ";
			//"?__BfCtor@StructA@bf@@QEAAXXZ";

	if (mDebugging)
	{
		mModule->Print(mBeFunction);
	}

	for (auto beBlock : function->mBlocks)
	{
		int blockIdx = (int)mMCBlockAlloc.size();
		auto mcBlock = mMCBlockAlloc.Alloc();
		mcBlock->mName = beBlock->mName + StrFormat(":%d", blockIdx);
		mcBlock->mBlockIdx = blockIdx;
		mcBlock->mMaxDeclBlockId = blockIdx;

		BeMCOperand mcOperand;
		mcOperand.mKind = BeMCOperandKind_Block;
		mcOperand.mBlock = mcBlock;
		mValueToOperand[beBlock] = mcOperand;
		mBlocks.push_back(mcBlock);
	}

	SizedArray<int, 64> dbgVarsAwaitingEnd;
	BeMDNode* curDbgScope = NULL;

	bool inHeadAlloca = true;

	SizedArray<int, 64> stackSaveVRegs;

	// Scan pass
	mMaxCallParamCount = -1;
	for (int blockIdx = 0; blockIdx < (int)function->mBlocks.size(); blockIdx++)
	{
		auto beBlock = function->mBlocks[blockIdx];
		auto mcBlock = mBlocks[blockIdx];

		for (int instIdx = 0; instIdx < (int)beBlock->mInstructions.size(); instIdx++)
		{
			auto inst = beBlock->mInstructions[instIdx];
			int instType = inst->GetTypeId();

			switch (instType)
			{
			case BeAllocaInst::TypeId:
				{
					auto castedInst = (BeAllocaInst*)inst;
					if ((!inHeadAlloca) || (castedInst->mAlign > 16))
						mUseBP = true;
				}
				break;
			case BeNumericCastInst::TypeId:
			case BeBitCastInst::TypeId:
				break;
			case BeStackSaveInst::TypeId:
				{
					auto stackVReg = AllocVirtualReg(mNativeIntType);
					stackSaveVRegs.push_back(stackVReg.mVRegIdx);
				}
				break;
			case BeCallInst::TypeId:
				{
					auto castedInst = (BeCallInst*)inst;

					if (auto intrin = BeValueDynCast<BeIntrinsic>(castedInst->mFunc))
					{
						// Not a real call
						switch (intrin->mKind)
						{
						case BfIRIntrinsic_VAStart:
							mHasVAStart = true;
							break;
						}
					}
				}
				break;
			case BeMemSetInst::TypeId:
				{
					//mMaxCallParamCount = BF_MAX(mMaxCallParamCount, 4);
				}
				break;
			default:
				inHeadAlloca = false;
				break;
			}
		}
	}

// 	if (mMaxCallParamCount != -1)
// 		mMaxCallParamCount = BF_MAX(mMaxCallParamCount, 4);

	int retCount = 0;
	bool isFirstBlock = true;
	inHeadAlloca = true;

	SizedArray<int, 64> valueScopeStack;

	// Generate pass
	for (int blockIdx = 0; blockIdx < (int)function->mBlocks.size(); blockIdx++)
	{
		auto beBlock = function->mBlocks[blockIdx];
		auto mcBlock = mBlocks[blockIdx];

		mActiveBeBlock = beBlock;
		mActiveBlock = mcBlock;
		if (isFirstBlock)
			HandleParams();

		for (int instIdx = 0; instIdx < (int)beBlock->mInstructions.size(); instIdx++)
		{
			auto inst = beBlock->mInstructions[instIdx];
			BeMCOperand result;
			mCurDbgLoc = inst->mDbgLoc;

			int instType = inst->GetTypeId();

			switch (instType)
			{
			case BeAllocaInst::TypeId:
			case BeNumericCastInst::TypeId:
			case BeBitCastInst::TypeId:
				break;
			default:
				inHeadAlloca = false;
				break;
			}

			switch (instType)
			{
			case BeNopInst::TypeId:
				{
					auto mcInst = AllocInst();
					mcInst->mKind = BeMCInstKind_Nop;
				}
				break;
			case BeUnreachableInst::TypeId:
				{
					auto mcInst = AllocInst();
					mcInst->mKind = BeMCInstKind_Unreachable;

					// 					if (instIdx == beBlock->mInstructions.size() - 1)
					// 					{
					// 						// Fake branch to exit
					// 						mcInst = AllocInst();
					// 						mcInst->mKind = BeMCInstKind_Br;
					// 						mcInst->mArg0 = BeMCOperand::FromBlock(mBlocks.back());
					// 						mcInst->mArg0.mBlock->AddPred(mcBlock);
					// 					}
				}
				break;
			case BeEnsureInstructionAtInst::TypeId:
				{
					auto mcInst = AllocInst();
					mcInst->mKind = BeMCInstKind_EnsureInstructionAt;
				}
				break;
			case BeUndefValueInst::TypeId:
				{
					auto castedInst = (BeUndefValueInst*)inst;
					result = AllocVirtualReg(castedInst->mType);
					CreateDefineVReg(result);
				}
				break;
			case BeExtractValueInst::TypeId:
				{
					auto castedInst = (BeExtractValueInst*)inst;

					BeConstant* constant = BeValueDynCast<BeConstant>(castedInst->mAggVal);
					BeMCOperand mcAgg;

					if (constant == NULL)
					{
						mcAgg = GetOperand(castedInst->mAggVal);
						if (mcAgg.mKind == BeMCOperandKind_ConstAgg)
						{
							constant = mcAgg.mConstant;
						}
					}

					if (constant != NULL)
					{
						result.mImmediate = 0;
						BeType* wantDefaultType = NULL;
						if (constant->mType->IsStruct())
						{
							BeStructType* structType = (BeStructType*)constant->mType;
							auto& member = structType->mMembers[castedInst->mIdx];
							wantDefaultType = member.mType;
						}
						else if (constant->mType->IsSizedArray())
						{
							BeSizedArrayType* arrayType = (BeSizedArrayType*)constant->mType;
							wantDefaultType = arrayType->mElementType;
						}

						if (wantDefaultType != NULL)
						{
							switch (wantDefaultType->mTypeCode)
							{
							case BeTypeCode_Boolean:
							case BeTypeCode_Int8:
								result.mKind = BeMCOperandKind_Immediate_i8;
								break;
							case BeTypeCode_Int16:
								result.mKind = BeMCOperandKind_Immediate_i16;
								break;
							case BeTypeCode_Int32:
								result.mKind = BeMCOperandKind_Immediate_i32;
								break;
							case BeTypeCode_Int64:
								result.mKind = BeMCOperandKind_Immediate_i64;
								break;
							case BeTypeCode_Float:
								result.mKind = BeMCOperandKind_Immediate_f32;
								break;
							case BeTypeCode_Double:
								result.mKind = BeMCOperandKind_Immediate_f64;
								break;
							case BeTypeCode_Pointer:
								result.mKind = BeMCOperandKind_Immediate_Null;
								result.mType = wantDefaultType;
								break;
							case BeTypeCode_Struct:
							case BeTypeCode_SizedArray:
								{
									auto subConst = mAlloc.Alloc<BeConstant>();
									subConst->mType = wantDefaultType;
									result.mConstant = subConst;
									result.mKind = BeMCOperandKind_ConstAgg;
								}
								break;
							default:
								NotImpl();
							}
						}

						break;
					}

					auto aggType = GetType(mcAgg);
					int byteOffset = 0;
					BeType* memberType = NULL;

					if (aggType->IsSizedArray())
					{
						auto sizedArray = (BeSizedArrayType*)aggType;
						memberType = sizedArray->mElementType;
						byteOffset = BF_ALIGN(memberType->mSize, memberType->mAlign) * castedInst->mIdx;
					}
					else
					{
						BF_ASSERT(aggType->IsStruct());
						BeStructType* structType = (BeStructType*)aggType;
						auto& structMember = structType->mMembers[castedInst->mIdx];
						byteOffset = structMember.mByteOffset;
						memberType = structMember.mType;
					}

					if (mcAgg.mKind == BeMCOperandKind_VReg)
						mcAgg.mKind = BeMCOperandKind_VRegAddr;
					else if (mcAgg.mKind == BeMCOperandKind_VRegLoad)
						mcAgg.mKind = BeMCOperandKind_VReg;
					else
						NotImpl();
					auto memberPtrType = mModule->mContext->GetPointerTo(memberType);
					result = AllocRelativeVirtualReg(memberPtrType, mcAgg, BeMCOperand::FromImmediate(byteOffset), 1);
					result.mKind = BeMCOperandKind_VRegLoad;
					CreateDefineVReg(result);
				}
				break;
			case BeInsertValueInst::TypeId:
				{
					auto castedInst = (BeInsertValueInst*)inst;
					auto mcAgg = GetOperand(castedInst->mAggVal);
					auto mcValue = GetOperand(castedInst->mMemberVal);
					auto aggType = GetType(mcAgg);
					BF_ASSERT(aggType->IsStruct());
					BeStructType* structType = (BeStructType*)aggType;
					auto& structMember = structType->mMembers[castedInst->mIdx];

					BF_ASSERT(mcAgg.mKind = BeMCOperandKind_VReg);
					auto mcAggRef = mcAgg;
					mcAggRef.mKind = BeMCOperandKind_VRegAddr;
					auto memberPtrType = mModule->mContext->GetPointerTo(structMember.mType);
					auto mcMemberRef = AllocRelativeVirtualReg(memberPtrType, mcAggRef, BeMCOperand::FromImmediate(structMember.mByteOffset), 1);
					CreateDefineVReg(mcMemberRef);
					mcMemberRef.mKind = BeMCOperandKind_VRegLoad;

					AllocInst(BeMCInstKind_Mov, mcMemberRef, mcValue);
					// Our InsertValue always modifies the source aggregate, it does not make a copy like LLVM's InsertValue would infer.
					//  This is okay because of Beef front end knowledge, but is not general purpose.
					result = mcAgg;
				}
				break;
			case BeNumericCastInst::TypeId:
				{
					auto castedInst = (BeNumericCastInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue);
					auto fromType = GetType(mcValue);

					if (fromType == castedInst->mToType)
					{
						// If it's just a sign change then leave it alone
						result = mcValue;
					}
					else
					{
						auto toType = castedInst->mToType;
						auto toValue = AllocVirtualReg(castedInst->mToType);
						CreateDefineVReg(toValue);
						if ((toType->IsIntable()) && (fromType->IsIntable()) && (toType->mSize < fromType->mSize))
						{
							// For truncating values, no actual instructions are needed, so we can just do a vreg relto ref
							auto vregInfo = mVRegInfo[toValue.mVRegIdx];
							vregInfo->mIsExpr = true;
							vregInfo->mRelTo = mcValue;
						}
						else
						{
							bool doSignExtension = (toType->IsIntable()) && (fromType->IsIntable()) && (toType->mSize > fromType->mSize) && (castedInst->mToSigned) && (castedInst->mValSigned);
							if ((toType->IsFloat()) && (fromType->IsIntable()) && (castedInst->mValSigned))
								doSignExtension = true;

							if (mcValue.IsImmediate())
								doSignExtension = false;

							if (doSignExtension)
							{
								AllocInst(BeMCInstKind_MovSX, toValue, mcValue);
							}
							else
								AllocInst(BeMCInstKind_Mov, toValue, mcValue);
						}
						result = toValue;
					}
				}
				break;
			case BeNegInst::TypeId:
				{
					auto castedInst = (BeNumericCastInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue);

					result = AllocVirtualReg(GetType(mcValue));
					CreateDefineVReg(result);
					AllocInst(BeMCInstKind_Mov, result, mcValue);
					AllocInst(BeMCInstKind_Neg, result);
				}
				break;
			case BeNotInst::TypeId:
				{
					auto castedInst = (BeNumericCastInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue, true);

					// Phi's are easy - just make a new one with the true and false branches swapped
	// 					if (mcValue.mKind == BeMCOperandKind_Phi)
	// 					{
	// 						BeMCPhi* origPhi = mcValue.mPhi;
	// 						BeMCPhi* newPhi = mPhiAlloc.Alloc();
	//
	// 						*newPhi = *origPhi;
	// 						BF_SWAP(newPhi->mBrTrue, newPhi->mBrFalse);
	// 						result.mKind = BeMCOperandKind_Phi;
	// 						result.mPhi = newPhi;
	// 						break;
	// 					}
	//
	// 					if (mcValue.mKind == BeMCOperandKind_CmpResult)
	// 					{
	// 						auto origCmpResult = mCmpResults[mcValue.mCmpResultIdx];
	//
	// 						auto cmpResultIdx = (int)mCmpResults.size();
	// 						BeCmpResult cmpResult;
	// 						cmpResult.mCmpKind = BeModule::InvertCmp(origCmpResult.mCmpKind);
	// 						mCmpResults.push_back(cmpResult);
	// 						result.mKind = BeMCOperandKind_CmpResult;
	// 						result.mCmpResultIdx = cmpResultIdx;
	// 						break;
	// 					}

					if (mcValue.mKind == BeMCOperandKind_NotResult)
					{
						// Double negative! Just unwrap the NotResult.
						result = GetOperand(mcValue.mNotResult->mValue, true);
						break;
					}
					else if ((mcValue.mKind == BeMCOperandKind_Phi) || (mcValue.mKind == BeMCOperandKind_CmpResult))
					{
						auto notResult = mAlloc.Alloc<BeNotResult>();
						notResult->mValue = castedInst->mValue;
						result.mKind = BeMCOperandKind_NotResult;
						result.mNotResult = notResult;
						break;
					}

					// LLVM does a weird thing for Not:  val = (val ^ 0xFF) & 1
					// Which turns a '2' into a '1' which is True to True - non-conformant to C standard?
					// Visual Studio does an actual conditional branch.  For Beef, bools are defined as 1 or 0.
					/*result = AllocVirtualReg(GetType(mcValue));
					CreateDefineVReg(result);
					AllocInst(BeMCInstKind_Mov, result, mcValue);
					BeMCOperand xorVal;
					xorVal.mKind = BeMCOperandKind_Immediate_i8;
					xorVal.mImmediate = 0xFF;
					AllocInst(BeMCInstKind_Xor, result, xorVal);
					BeMCOperand andVal;
					andVal.mKind = BeMCOperandKind_Immediate_i8;
					andVal.mImmediate = 0x1;
					AllocInst(BeMCInstKind_And, result, andVal);*/

					auto type = castedInst->mValue->GetType();

					result = AllocVirtualReg(GetType(mcValue));
					CreateDefineVReg(result);
					AllocInst(BeMCInstKind_Mov, result, mcValue);

					if (type->mTypeCode == BeTypeCode_Boolean)
					{
						BeMCOperand xorVal;
						xorVal.mKind = BeMCOperandKind_Immediate_i8;
						xorVal.mImmediate = 0x1;
						AllocInst(BeMCInstKind_Xor, result, xorVal);
					}
					else
					{
						AllocInst(BeMCInstKind_Not, result);
					}
				}
				break;
			case BeBinaryOpInst::TypeId:
				{
					auto castedInst = (BeBinaryOpInst*)inst;
					auto mcLHS = GetOperand(castedInst->mLHS);
					auto mcRHS = GetOperand(castedInst->mRHS);

					if (castedInst->mOpKind == BeBinaryOpKind_Subtract)
					{
						if (((mcLHS.IsImmediateFloat()) && (mcLHS.GetImmediateDouble() == 0.0)) ||
							((mcLHS.IsImmediateInt()) && (mcLHS.mImmediate == 0)))
						{
							auto castedInst = (BeNumericCastInst*)inst;

							result = AllocVirtualReg(GetType(mcRHS));
							CreateDefineVReg(result);
							AllocInst(BeMCInstKind_Mov, result, mcRHS);
							AllocInst(BeMCInstKind_Neg, result);
							break;
						}
					}

					auto type = GetType(mcLHS);

					switch (castedInst->mOpKind)
					{
					case BeBinaryOpKind_Add: result = AllocBinaryOp(BeMCInstKind_Add, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero,
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? BeMCOverflowCheckKind_O :
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Unsigned) != 0) ? BeMCOverflowCheckKind_B : BeMCOverflowCheckKind_None);
						break;
					case BeBinaryOpKind_Subtract: result = AllocBinaryOp(BeMCInstKind_Sub, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsZero,
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? BeMCOverflowCheckKind_O :
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Unsigned) != 0) ? BeMCOverflowCheckKind_B : BeMCOverflowCheckKind_None);
						break;
					case BeBinaryOpKind_Multiply: result = AllocBinaryOp(((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Unsigned) != 0) ? BeMCInstKind_Mul : BeMCInstKind_IMul, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsOne,
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Signed) != 0) ? BeMCOverflowCheckKind_O :
						((castedInst->mOverflowCheckKind & BfOverflowCheckKind_Unsigned) != 0) ? BeMCOverflowCheckKind_O : BeMCOverflowCheckKind_None);
						break;
					case BeBinaryOpKind_SDivide: result = AllocBinaryOp(BeMCInstKind_IDiv, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsOne); break;
					case BeBinaryOpKind_UDivide: result = AllocBinaryOp(BeMCInstKind_Div, mcLHS, mcRHS, BeMCBinIdentityKind_Right_IsOne); break;
					case BeBinaryOpKind_SModulus: result = AllocBinaryOp(BeMCInstKind_IRem, mcLHS, mcRHS, type->IsFloat() ? BeMCBinIdentityKind_None : BeMCBinIdentityKind_Right_IsOne_Result_Zero); break;
					case BeBinaryOpKind_UModulus: result = AllocBinaryOp(BeMCInstKind_Rem, mcLHS, mcRHS, type->IsFloat() ? BeMCBinIdentityKind_None : BeMCBinIdentityKind_Right_IsOne_Result_Zero); break;
					case BeBinaryOpKind_BitwiseAnd: result = AllocBinaryOp(BeMCInstKind_And, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
					case BeBinaryOpKind_BitwiseOr: result = AllocBinaryOp(BeMCInstKind_Or, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
					case BeBinaryOpKind_ExclusiveOr: result = AllocBinaryOp(BeMCInstKind_Xor, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
					case BeBinaryOpKind_LeftShift: result = AllocBinaryOp(BeMCInstKind_Shl, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
					case BeBinaryOpKind_RightShift: result = AllocBinaryOp(BeMCInstKind_Shr, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
					case BeBinaryOpKind_ARightShift: result = AllocBinaryOp(BeMCInstKind_Sar, mcLHS, mcRHS, BeMCBinIdentityKind_Any_IsZero); break;
					}
				}
				break;
			case BeBitCastInst::TypeId:
				{
					auto castedInst = (BeBitCastInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue);
					if (castedInst->mToType->IsInt())
					{
						BF_ASSERT(castedInst->mToType->mSize == 8);
					}
					else
						BF_ASSERT(castedInst->mToType->IsPointer());
					auto toType = castedInst->mToType;

					if (mcValue.IsImmediate())
					{
						if (mcValue.mImmediate == 0)
						{
							BeMCOperand newImmediate;
							newImmediate.mKind = BeMCOperandKind_Immediate_Null;
							newImmediate.mType = toType;
							result = newImmediate;
						}
						else
						{
							// Non-zero constant.  Weird case, just do an actual MOV
							result = AllocVirtualReg(toType);
							CreateDefineVReg(result);
							auto vregInfo = GetVRegInfo(result);
							AllocInst(BeMCInstKind_Mov, result, mcValue);

							if (mcValue.mKind == BeMCOperandKind_VRegAddr)
							{
								auto srcVRegInfo = GetVRegInfo(mcValue);
								srcVRegInfo->mForceMem = true;
								CheckForce(srcVRegInfo);
							}
						}
					}
					else
					{
						result = AllocVirtualReg(toType);
						CreateDefineVReg(result);
						auto vregInfo = GetVRegInfo(result);
						vregInfo->mRelTo = mcValue;
						vregInfo->mIsExpr = true;

						if (mcValue.mKind == BeMCOperandKind_VRegAddr)
						{
							auto srcVRegInfo = GetVRegInfo(mcValue);
							srcVRegInfo->mForceMem = true;
							CheckForce(srcVRegInfo);
						}
					}
				}
				break;
			case BeCmpInst::TypeId:
				{
					auto castedInst = (BeCmpInst*)inst;
					auto mcLHS = GetOperand(castedInst->mLHS);
					auto mcRHS = GetOperand(castedInst->mRHS);

					auto valType = castedInst->mLHS->GetType();

					auto mcInst = AllocInst(BeMCInstKind_Cmp, mcLHS, mcRHS);

					auto cmpResultIdx = (int)mCmpResults.size();
					BeCmpResult cmpResult;
					cmpResult.mCmpKind = castedInst->mCmpKind;

					if (valType->IsFloat())
					{
						switch (cmpResult.mCmpKind)
						{
						case BeCmpKind_SLT:
							cmpResult.mCmpKind = BeCmpKind_ULT;
							break;
						case BeCmpKind_SLE:
							cmpResult.mCmpKind = BeCmpKind_ULE;
							break;
						case BeCmpKind_SGT:
							cmpResult.mCmpKind = BeCmpKind_UGT;
							break;
						case BeCmpKind_SGE:
							cmpResult.mCmpKind = BeCmpKind_UGE;
							break;
						}
					}

					mCmpResults.push_back(cmpResult);

					result.mKind = BeMCOperandKind_CmpResult;
					result.mCmpResultIdx = cmpResultIdx;

					mcInst->mResult = result;
				}
				break;
			case BeObjectAccessCheckInst::TypeId:
				{
					auto castedInst = (BeObjectAccessCheckInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue);

					auto int8Type = mModule->mContext->GetPrimitiveType(BeTypeCode_Int8);
					auto int8PtrType = mModule->mContext->GetPointerTo(int8Type);

					auto int8PtrVal = AllocVirtualReg(int8PtrType);
					CreateDefineVReg(int8PtrVal);
					auto vregInfo = GetVRegInfo(int8PtrVal);
					vregInfo->mRelTo = mcValue;
					vregInfo->mIsExpr = true;

					int labelNum = mCurLabelIdx++;
					BeMCOperand mcImm;
					mcImm.mKind = BeMCOperandKind_Immediate_i8;
					mcImm.mImmediate = -0x80;

					BeMCOperand int8Val;
					int8Val.mKind = BeMCOperandKind_VRegLoad;
					int8Val.mVRegIdx = int8PtrVal.mVRegIdx;

					AllocInst(BeMCInstKind_Cmp, int8Val, mcImm);
					AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(labelNum), BeMCOperand::FromCmpKind(BeCmpKind_ULT));
					AllocInst(BeMCInstKind_DbgBreak);
					AllocInst(BeMCInstKind_Label, BeMCOperand::FromLabel(labelNum));
				}
				break;
			case BeAllocaInst::TypeId:
				{
					//int homeSize = BF_ALIGN(BF_MAX(mMaxCallParamCount, 4) * 8, 16);
					auto castedInst = (BeAllocaInst*)inst;
					auto mcSize = BeMCOperand::FromImmediate(castedInst->mType->mSize);
					bool isAligned16 = false;
					int align = castedInst->mAlign;
					BeType* allocType = castedInst->mType;
					bool preservedVolatiles = false;
					bool doPtrCast = false;
					if (castedInst->mArraySize != NULL)
					{
						mcSize = BeMCOperand::FromImmediate(castedInst->mType->GetStride());

						auto mcArraySize = GetOperand(castedInst->mArraySize);
						if (mcArraySize.IsImmediate())
						{
							mcSize.mImmediate = mcSize.mImmediate * mcArraySize.mImmediate;
							allocType = mModule->mContext->CreateSizedArrayType(castedInst->mType, mcArraySize.mImmediate);
							doPtrCast = true;
						}
						else
						{
							preservedVolatiles = true;
							AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX));

							inHeadAlloca = false;
							if (mcSize.mImmediate == 1)
							{
								mcSize = mcArraySize;
							}
							else
							{
								auto mcInst = AllocInst(BeMCInstKind_IMul, mcArraySize, mcSize);
								mcInst->mResult = BeMCOperand::FromReg(X64Reg_RAX);
								mcSize = mcInst->mResult;
							}
						}
					}

					// The stack is 16-byte aligned on entry - we have to manually adjust for any alignment greater than that
					if ((inHeadAlloca) && (align <= 16))
					{
						result = AllocVirtualReg(allocType);
						auto vregInfo = mVRegInfo[result.mVRegIdx];
						vregInfo->mAlign = castedInst->mAlign;
						vregInfo->mHasDynLife = true;
						if (castedInst->mForceMem)
							vregInfo->mForceMem = true;
						if (allocType->IsNonVectorComposite())
							vregInfo->mForceMem = true;
						result.mKind = BeMCOperandKind_VRegAddr;

						if (doPtrCast)
						{
							BF_ASSERT(allocType->IsSizedArray());
							auto resultType = mModule->mContext->GetPointerTo(castedInst->mType);

							auto ptrResult = AllocVirtualReg(resultType);
							auto vregInfo = mVRegInfo[ptrResult.mVRegIdx];
							vregInfo->mIsExpr = true;
							vregInfo->mRelTo = result;
							vregInfo->mType = resultType;
							vregInfo->mAlign = resultType->mSize;
							CreateDefineVReg(ptrResult);

							result = ptrResult;
						}
					}
					else
					{
						bool needsChkStk = !castedInst->mNoChkStk;
						bool doFastChkStk = false;
						if (instIdx < (int)beBlock->mInstructions.size() - 1)
						{
							if (auto memSetInstr = BeValueDynCast<BeMemSetInst>(beBlock->mInstructions[instIdx + 1]))
							{
								if (memSetInstr->mAddr == inst)
								{
									// If we're clearing out this memory immediately after allocation then we don't
									//  need to do stack probing - the memset will ensure stack pages are committed
									needsChkStk = false;

									if (mcSize.IsImmediate())
									{
										if (auto sizeConst = BeValueDynCast<BeConstant>(memSetInstr->mSize))
										{
											if (sizeConst->mInt64 < mcSize.mImmediate)
											{
												// We haven't actually cleared out everything so we still need to chkStk
												needsChkStk = true;
											}
										}
									}
								}
							}
						}

						int stackAlign = BF_MAX(align, 16);

						BeMCOperand mcFunc;
						mcFunc.mKind = BeMCOperandKind_SymbolAddr;
						mcFunc.mSymbolIdx = mCOFFObject->GetSymbolRef("__chkstk")->mIdx;

						if (mcSize.IsImmediate())
						{
							// Align to 16 bytes
							mcSize.mImmediate = (mcSize.mImmediate + 0xF) & ~0xF;
						}

						if ((mcSize.IsImmediate()) && (!preservedVolatiles) && (!needsChkStk))
						{
							AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), mcSize);
						}
						else
						{
							if (needsChkStk)
							{
								if ((mcSize.IsImmediate()) && (mcSize.mImmediate < 4096))
								{
									// We can do a fast __chkstk in this case since we have a max of one page to handle
									doFastChkStk = true;
								}
							}

							if (doFastChkStk)
							{
								AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), mcSize);
								AllocInst(BeMCInstKind_FastCheckStack, BeMCOperand::FromReg(X64Reg_RSP));
							}
							else
							{
								if (!preservedVolatiles)
									AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX));

								if (mcSize.IsImmediate())
								{
									AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RAX), mcSize);

									// It's tempting to not do a chkstk when we do an alloc less than 4k, but this isn't valid
									//  because we could break the system by doing three 2k allocs and access the third one first
									//  and BOOM.  We rely on the front-end to tell us when we can omit it.
								}
								else if (!isAligned16)
								{
									AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RAX), mcSize);
									AllocInst(BeMCInstKind_Add, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand::FromImmediate(0xF));
									AllocInst(BeMCInstKind_And, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand::FromImmediate(~0xF));
								}

								if ((needsChkStk) && (!doFastChkStk))
								{
									AllocInst(BeMCInstKind_Call, mcFunc);
								}

								AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromReg(X64Reg_RAX));
								if (doFastChkStk)
								{
									AllocInst(BeMCInstKind_FastCheckStack, BeMCOperand::FromReg(X64Reg_RSP));
								}

								AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX));
							}
						}

						auto resultType = mModule->mContext->GetPointerTo(castedInst->mType);

						auto intType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);
						BeMCOperand ptrValue;
						if (mUseBP)
						{
							ptrValue = AllocVirtualReg(intType);
							auto vregInfo = mVRegInfo[ptrValue.mVRegIdx];
							vregInfo->mIsExpr = true;
							vregInfo->mRelTo = BeMCOperand::FromReg(X64Reg_RSP);
							vregInfo->mRelOffset.mKind = BeMCOperandKind::BeMCOperandKind_Immediate_HomeSize;
							CreateDefineVReg(ptrValue);
						}
						else
						{
							ptrValue = BeMCOperand::FromReg(X64Reg_RSP);
						}

						result = AllocVirtualReg(resultType);
						auto vregInfo = mVRegInfo[result.mVRegIdx];
						vregInfo->mHasDynLife = true;
						CreateDefineVReg(result);

						AllocInst(BeMCInstKind_Mov, result, ptrValue);

						if (stackAlign > 16)
						{
							// We have to align after everything - note that we always have to keep the 'homeSize' space available from RSP for calls,
							//  so the ANDing for alignment must be done here
							AllocInst(BeMCInstKind_And, result, BeMCOperand::FromImmediate(~(stackAlign - 1)));
							AllocInst(BeMCInstKind_Sub, BeMCOperand::FromReg(X64Reg_RSP), BeMCOperand::FromImmediate(stackAlign - 16));
						}

						BF_ASSERT(mUseBP);
					}
				}
				break;
			case BeAliasValueInst::TypeId:
				{
					auto castedInst = (BeAliasValueInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false, true);
					result = AllocVirtualReg(GetType(mcPtr));
					auto vregInfo = mVRegInfo[result.mVRegIdx];
					vregInfo->mIsExpr = true;
					vregInfo->mRelTo = mcPtr;
					vregInfo->mHasDynLife = true;
					CreateDefineVReg(result);
				}
				break;
			case BeLifetimeStartInst::TypeId:
				{
					auto castedInst = (BeLifetimeEndInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false, true, true);
					if (mcPtr)
					{
						auto vregInfo = GetVRegInfo(mcPtr);
						if ((vregInfo != NULL) && (vregInfo->mHasDynLife))
						{
							// This alloca had an assignment (ie: `mov vregX, [RBP+homeSize0]`) so it must be defined at the mov
							// This may indicate incorrectly generated code where we thought an alloca would be in the head but it isn't
						}
						else
							AllocInst(BeMCInstKind_LifetimeStart, mcPtr);
					}
				}
				break;
			case BeLifetimeExtendInst::TypeId:
				{
					auto castedInst = (BeLifetimeEndInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false, true, true);
					if (mcPtr)
						AllocInst(BeMCInstKind_LifetimeExtend, mcPtr);
				}
				break;
			case BeLifetimeEndInst::TypeId:
				{
					auto castedInst = (BeLifetimeEndInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false, true, true);
					if (mcPtr.IsVRegAny())
					{
						AllocInst(BeMCInstKind_LifetimeEnd, mcPtr);
					}
				}
				break;
			case BeLifetimeSoftEndInst::TypeId:
				{
					auto castedInst = (BeLifetimeSoftEndInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false, true, true);
					if (mcPtr.IsVRegAny())
					{
						AllocInst(BeMCInstKind_LifetimeSoftEnd, mcPtr);
					}
				}
				break;
			case BeValueScopeStartInst::TypeId:
				{
					result = BeMCOperand::FromImmediate((int)mVRegInfo.size());
				}
				break;
			case BeValueScopeRetainInst::TypeId:
				{
					auto castedInst = (BeValueScopeRetainInst*)inst;
					auto mcValue = GetOperand(castedInst->mValue, false, true);
					auto vregInfo = GetVRegInfo(mcValue);
					vregInfo->mValueScopeRetainedKind = BeMCValueScopeRetainKind_Soft;
				}
				break;
			case BeValueScopeEndInst::TypeId:
				{
					auto castedInst = (BeValueScopeEndInst*)inst;
					BeMCOperand mcScopeStart = GetOperand(castedInst->mScopeStart, false, true);
					// There are some recordering cases where we have a ValueScopeStart moved after a ValueScopeEnd
					//  Just ignore those. This is just an optimization anyway.
					if (mcScopeStart)
						AllocInst(castedInst->mIsSoft ? BeMCInstKind_ValueScopeSoftEnd : BeMCInstKind_ValueScopeHardEnd, mcScopeStart, BeMCOperand::FromImmediate((int)mVRegInfo.size()));
				}
				break;
			case BeLifetimeFenceInst::TypeId:
				{
					auto castedInst = (BeLifetimeFenceInst*)inst;
					auto mcPtr = GetOperand(castedInst->mPtr, false);
					auto fenceBlock = GetOperand(castedInst->mFenceBlock);

					auto vregInfo = GetVRegInfo(mcPtr);
					vregInfo->mChainLifetimeEnd = true;

					SetAndRestoreValue<BeMCBlock*> prevBlock(mActiveBlock, fenceBlock.mBlock);
					auto lifetimeStart = AllocInst(BeMCInstKind_LifetimeStart, mcPtr);
					lifetimeStart->mDbgLoc = NULL;
				}
				break;
			case BeLoadInst::TypeId:
				{
					auto castedInst = (BeLoadInst*)inst;
					auto mcTarget = GetOperand(castedInst->mTarget, false, false, true);
					result = CreateLoad(mcTarget);
				}
				break;
			case BeStoreInst::TypeId:
				{
					auto castedInst = (BeStoreInst*)inst;
					auto mcVal = GetOperand(castedInst->mVal);
					auto mcPtr = GetOperand(castedInst->mPtr, false, false, true);

					bool handled = false;

					CreateStore(BeMCInstKind_Mov, mcVal, mcPtr);
				}
				break;
			case BeSetCanMergeInst::TypeId:
				{
					auto castedInst = (BeSetCanMergeInst*)inst;
					auto mcVal = GetOperand(castedInst->mVal, false, false, true);
					auto vregInfo = GetVRegInfo(mcVal);
					vregInfo->mForceMerge = true;
				}
				break;
			case BeMemSetInst::TypeId:
				{
					auto castedInst = (BeMemSetInst*)inst;

					if (auto constVal = BeValueDynCast<BeConstant>(castedInst->mVal))
					{
						if (auto constSize = BeValueDynCast<BeConstant>(castedInst->mSize))
						{
							CreateMemSet(GetOperand(castedInst->mAddr), constVal->mUInt8, constSize->mInt64, castedInst->mAlignment);
							break;
						}
					}

					SizedArray<BeValue*, 3> args = { castedInst->mAddr, castedInst->mVal, castedInst->mSize };
					auto mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memset")->mIdx);
					CreateCall(mcFunc, args);
				}
				break;
			case BeFenceInst::TypeId:
				{
					AllocInst(BeMCInstKind_MFence);
				}
				break;
			case BeStackSaveInst::TypeId:
				{
					auto stackVReg = BeMCOperand::FromVReg(stackSaveVRegs.back());
					stackSaveVRegs.pop_back();
					CreateDefineVReg(stackVReg);
					AllocInst(BeMCInstKind_Mov, stackVReg, BeMCOperand::FromReg(X64Reg_RSP));
					result = stackVReg;
				}
				break;
			case BeStackRestoreInst::TypeId:
				{
					auto castedInst = (BeStackRestoreInst*)inst;

					auto mcStackVal = GetOperand(castedInst->mStackVal);
					AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RSP), mcStackVal);
				}
				break;
			case BeGEPInst::TypeId:
				{
					auto castedInst = (BeGEPInst*)inst;

					auto mcVal = GetOperand(castedInst->mPtr);
					auto mcIdx0 = GetOperand(castedInst->mIdx0);

					BePointerType* ptrType = (BePointerType*)GetType(mcVal);
					BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);

					result = mcVal;
					if (castedInst->mIdx1 != NULL)
					{
						// We assume we never do both an idx0 and idx1 at once.  Fix if we change that.
						BF_ASSERT(castedInst->mIdx0);

						auto mcIdx1 = GetOperand(castedInst->mIdx1);
						if (!mcIdx1.IsImmediate())
						{
							// This path is used when we have a const array that gets indexed by a non-const index value
							if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
							{
								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;

								auto elementPtrType = mModule->mContext->GetPointerTo(arrayType->mElementType);

								auto ptrValue = AllocVirtualReg(elementPtrType);
								auto ptrInfo = GetVRegInfo(ptrValue);
								ptrInfo->mIsExpr = true;
								ptrInfo->mRelTo = result;
								CreateDefineVReg(ptrValue);
								result = ptrValue;

								BeMCOperand mcRelOffset;
								int relScale = 1;
								if (mcIdx1.IsImmediate())
								{
									mcRelOffset = BeMCOperand::FromImmediate(mcIdx1.mImmediate * arrayType->mElementType->GetStride());
								}
								else
								{
									mcRelOffset = mcIdx1;
									relScale = arrayType->mElementType->GetStride();
								}

								result = AllocRelativeVirtualReg(elementPtrType, result, mcRelOffset, relScale);
								// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
								CreateDefineVReg(result);
								//TODO: Always correct?
								result.mKind = BeMCOperandKind_VReg;
							}
							else
								SoftFail("Invalid GEP", inst->mDbgLoc);
						}
						else
						{
							BF_ASSERT(mcIdx1.IsImmediate());
							int byteOffset = 0;
							BeType* elementType = NULL;
							if (ptrType->mElementType->mTypeCode == BeTypeCode_Struct)
							{
								BeStructType* structType = (BeStructType*)ptrType->mElementType;
								auto& structMember = structType->mMembers[mcIdx1.mImmediate];
								elementType = structMember.mType;
								byteOffset = structMember.mByteOffset;
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
							{
								auto arrayType = (BeSizedArrayType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = mcIdx1.mImmediate * elementType->GetStride();
							}
							else if (ptrType->mElementType->mTypeCode == BeTypeCode_Vector)
							{
								auto arrayType = (BeVectorType*)ptrType->mElementType;
								elementType = arrayType->mElementType;
								byteOffset = mcIdx1.mImmediate * elementType->GetStride();
							}
							else
							{
								Fail("Invalid gep target");
							}

							auto elementPtrType = mModule->mContext->GetPointerTo(elementType);
							result = AllocRelativeVirtualReg(elementPtrType, result, GetImmediate(byteOffset), 1);
							// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
							CreateDefineVReg(result);
							result.mKind = BeMCOperandKind_VReg;
						}
					}
					else
					{
						// It's temping to do a (IsNonZero) precondition, but if we make a reference to a VReg that is NOT in Addr form,
						//  then this will encode that so we will know we need to do a Load on that value at the Def during legalization

						BeMCOperand mcRelOffset;
						int relScale = 1;
						if (mcIdx0.IsImmediate())
						{
							mcRelOffset = BeMCOperand::FromImmediate(mcIdx0.mImmediate * ptrType->mElementType->GetStride());
						}
						else
						{
							mcRelOffset = mcIdx0;
							relScale = ptrType->mElementType->GetStride();
						}

						result = AllocRelativeVirtualReg(ptrType, result, mcRelOffset, relScale);
						// The def is primary to create a single 'master location' for the GEP vreg to become legalized before use
						CreateDefineVReg(result);
						//TODO: Always correct?
						result.mKind = BeMCOperandKind_VReg;
					}
				}
				break;
			case BeBrInst::TypeId:
				{
					auto castedInst = (BeBrInst*)inst;

					auto mcInst = AllocInst();
					mcInst->mKind = BeMCInstKind_Br;
					mcInst->mArg0 = GetOperand(castedInst->mTargetBlock);
					mcInst->mArg0.mBlock->AddPred(mcBlock);

					if (castedInst->mNoCollapse)
					{
						mcInst->mArg1.mKind = BeMCOperandKind_Immediate_i8;
						mcInst->mArg1.mImmediate = 1;
					}
					else if (castedInst->mIsFake)
					{
						mcInst->mArg1.mKind = BeMCOperandKind_Immediate_i8;
						mcInst->mArg1.mImmediate = 2;
						mcBlock->mHasFakeBr = true;
					}
				}
				break;
			case BeCondBrInst::TypeId:
				{
					auto castedInst = (BeCondBrInst*)inst;
					auto testVal = GetOperand(castedInst->mCond, true);
					auto trueBlock = GetOperand(castedInst->mTrueBlock);
					auto falseBlock = GetOperand(castedInst->mFalseBlock);
					trueBlock.mBlock->AddPred(mcBlock);
					falseBlock.mBlock->AddPred(mcBlock);
					CreateCondBr(mcBlock, testVal, trueBlock, falseBlock);
				}
				break;
			case BePhiInst::TypeId:
				{
					auto castedInst = (BePhiInst*)inst;

					BeMCPhi* mcPhi = mPhiAlloc.Alloc();
					mcPhi->mBlock = mcBlock;
					mcPhi->mIdx = mCurPhiIdx++;

					//if (mDebugging)
					{
						for (auto phiIncoming : castedInst->mIncoming)
						{
							auto blockFrom = GetOperand(phiIncoming->mBlock).mBlock;;
							int insertIdx = blockFrom->mInstructions.size() - 1;
							SetAndRestoreValue<BeMCBlock*> prevActiveBlock(mActiveBlock, blockFrom);
							SetAndRestoreValue<int*> prevInsertIdxPtr(mInsertInstIdxRef, &insertIdx);
							BeMCPhiValue phiVal;
							phiVal.mBlockFrom = blockFrom;
							phiVal.mValue = GetOperand(phiIncoming->mValue, true);
							if (phiVal.mValue.mKind == BeMCOperandKind_VRegAddr)
							{
								auto vregInfo = GetVRegInfo(phiVal.mValue);
								if (!vregInfo->mIsExpr)
								{
									vregInfo->mForceMem = true;
									CheckForce(vregInfo);
								}
							}

							mcPhi->mValues.push_back(phiVal);
						}
					}
					/*else
					{
						for (auto phiIncoming : castedInst->mIncoming)
						{
							BeMCPhiValue phiVal;
							phiVal.mBlockFrom = GetOperand(phiIncoming->mBlock).mBlock;
							phiVal.mValue = GetOperand(phiIncoming->mValue, true);
							mcPhi->mValues.push_back(phiVal);
						}
					}*/

					result.mKind = BeMCOperandKind_Phi;
					result.mPhi = mcPhi;

					// DefPhi is important because when we convert a CondBr of a PHI, because we will need to create jumps to the correct
					//  location when we create it as a value (specifically in the bool case)
					AllocInst(BeMCInstKind_DefPhi, result);
				}
				break;
			case BeSwitchInst::TypeId:
				{
					auto castedInst = (BeSwitchInst*)inst;

					std::stable_sort(castedInst->mCases.begin(), castedInst->mCases.end(), [&](const BeSwitchCase& lhs, const BeSwitchCase& rhs)
						{
							return lhs.mValue->mInt64 < rhs.mValue->mInt64;
						});

					int numVals = castedInst->mCases.size();

					if (numVals > 0)
					{
						int64 loVal = castedInst->mCases.front().mValue->mInt64;
						int64 hiVal = castedInst->mCases.back().mValue->mInt64;

						uint64 avgSpacing = (uint64)(hiVal - loVal) / numVals;
						// Only use a table if we have a lot of values and the values are 'tight' enough
						if ((numVals > 6) && (avgSpacing <= 8))
							CreateTableSwitchSection(castedInst, 0, castedInst->mCases.size());
						else
							CreateBinarySwitchSection(castedInst, 0, castedInst->mCases.size());
					}
					auto mcDefaultBlock = GetOperand(castedInst->mDefaultBlock);
					AllocInst(BeMCInstKind_Br, mcDefaultBlock);
					mcDefaultBlock.mBlock->AddPred(mActiveBlock);
				}
				break;
			case BeRetInst::TypeId:
				{
					auto castedInst = (BeRetInst*)inst;
					if (castedInst->mRetValue != NULL)
					{
						auto retVal = GetOperand(castedInst->mRetValue);
						auto retType = GetType(retVal);
						if (retType->IsNonVectorComposite())
						{
							BF_ASSERT(mCompositeRetVRegIdx != -1);
							BF_ASSERT(retVal.IsVReg());
							auto vregInfo = GetVRegInfo(retVal);

							vregInfo->SetRetVal();
						}
						else if (retType->IsVector())
						{
							X64CPURegister reg = X64Reg_M128_XMM0;
							auto movInst = AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(reg), retVal);
						}
						else
						{
							X64CPURegister reg = X64Reg_RAX;
							if (retType->IsIntable())
							{
								if (retType->mSize == 4)
									reg = X64Reg_EAX;
								else if (retType->mSize == 2)
									reg = X64Reg_AX;
								else if (retType->mSize == 1)
									reg = X64Reg_AL;
							}
							else if (retType->mTypeCode == BeTypeCode_Float)
								reg = X64Reg_XMM0_f32;
							else if (retType->mTypeCode == BeTypeCode_Double)
								reg = X64Reg_XMM0_f64;
							auto movInst = AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(reg), retVal);
						}
					}

					if (mBeFunction->HasStructRet())
					{
						for (int vregIdx = 0; vregIdx < (int)mVRegInfo.size(); vregIdx++)
						{
							auto vregInfo = mVRegInfo[vregIdx];
							if (vregInfo->mIsRetVal)
							{
								AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(X64Reg_RAX), BeMCOperand::FromVReg(vregIdx));
								break;
							}
						}
					}

					auto mcInst = AllocInst();
					mcInst->mKind = BeMCInstKind_Ret;
					retCount++;
				}
				break;
			case BeCallInst::TypeId:
				{
					auto castedInst = (BeCallInst*)inst;
					BeMCOperand mcFunc;
					BeType* returnType = NULL;
					bool isVarArg = false;

					bool useAltArgs = false;
					SizedArray<BeValue*, 6> args;

					if (auto intrin = BeValueDynCast<BeIntrinsic>(castedInst->mFunc))
					{
						switch (intrin->mKind)
						{
						case BfIRIntrinsic_Add:
						case BfIRIntrinsic_And:
						case BfIRIntrinsic_Div:
						case BfIRIntrinsic_Eq:
						case BfIRIntrinsic_Gt:
						case BfIRIntrinsic_GtE:
						case BfIRIntrinsic_Lt:
						case BfIRIntrinsic_LtE:
						case BfIRIntrinsic_Mod:
						case BfIRIntrinsic_Mul:
						case BfIRIntrinsic_Neq:
						case BfIRIntrinsic_Or:
						case BfIRIntrinsic_SAR:
						case BfIRIntrinsic_SHL:
						case BfIRIntrinsic_SHR:
						case BfIRIntrinsic_Sub:
						case BfIRIntrinsic_Xor:
							{
								auto mcLHS = TryToVector(castedInst->mArgs[0].mValue);
								BeMCOperand mcRHS;

								if ((intrin->mKind == BfIRIntrinsic_SAR) ||
									(intrin->mKind == BfIRIntrinsic_SHL) ||
									(intrin->mKind == BfIRIntrinsic_SHR))
								{
									mcRHS = GetOperand(castedInst->mArgs[1].mValue);
									if (!mcRHS.IsImmediateInt())
										mcRHS = BeMCOperand();
								}

								if (!mcRHS)
									mcRHS = TryToVector(castedInst->mArgs[1].mValue);

								switch (intrin->mKind)
								{
								case BfIRIntrinsic_Add:
									result = AllocBinaryOp(BeMCInstKind_Add, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_And:
									result = AllocBinaryOp(BeMCInstKind_And, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_Mul:
									result = AllocBinaryOp(BeMCInstKind_IMul, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_Or:
									result = AllocBinaryOp(BeMCInstKind_Or, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_SAR:
									result = AllocBinaryOp(BeMCInstKind_Sar, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_SHL:
									result = AllocBinaryOp(BeMCInstKind_Shl, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_SHR:
									result = AllocBinaryOp(BeMCInstKind_Shr, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_Sub:
									result = AllocBinaryOp(BeMCInstKind_Sub, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								case BfIRIntrinsic_Xor:
									result = AllocBinaryOp(BeMCInstKind_Xor, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
									break;
								default:
									SoftFail("Unhandled intrinsic");
								}
							}
							break;
// 						case BfIRIntrinsic_Cast:
// 							{
//
// 							}
// 							break;
						case BfIRIntrinsic_Not:
							{
								auto mcLHS = TryToVector(castedInst->mArgs[0].mValue);
								BeMCOperand mcRHS = BeMCOperand::FromImmediate(-1);
								result = AllocBinaryOp(BeMCInstKind_Xor, mcLHS, mcRHS, BeMCBinIdentityKind_None); break;
							}
							break;

						case BfIRIntrinsic_Abs:
							{
								auto mcValue = GetOperand(castedInst->mArgs[0].mValue);

								auto mcType = GetType(mcValue);
								BeMCOperand andValue;

								if (mcType->mSize == 4)
								{
									andValue.mKind = BeMCOperandKind_Immediate_f32_Packed128;
									andValue.mImmediate = 0x7FFFFFFF;
								}
								else
								{
									andValue.mKind = BeMCOperandKind_Immediate_f64_Packed128;
									andValue.mImmediate = 0x7FFFFFFFFFFFFFFFLL;
								}

								result = AllocVirtualReg(GetType(mcValue));
								CreateDefineVReg(result);
								AllocInst(BeMCInstKind_Mov, result, mcValue);
								AllocInst(BeMCInstKind_And, result, andValue);
							}
							break;
						case BfIRIntrinsic_AtomicAdd:
						case BfIRIntrinsic_AtomicSub:
							{
								auto mcPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcVal = GetOperand(castedInst->mArgs[1].mValue);
								auto valType = GetType(mcVal);
								if (!valType->IsFloat())
								{
									auto mcMemKind = GetOperand(castedInst->mArgs[2].mValue);
									if (!mcMemKind.IsImmediateInt())
									{
										SoftFail("Non-constant success ordering on AtomicLoad", castedInst->mDbgLoc);
										break;
									}

									BeMCOperand scratchReg = AllocVirtualReg(valType);
									auto vregInfo = GetVRegInfo(scratchReg);
									vregInfo->mMustExist = true;
									CreateDefineVReg(scratchReg);
									if ((intrin->mKind == BfIRIntrinsic_AtomicSub) && (mcVal.IsImmediate()))
									{
										BeMCOperand mcNeg = mcVal;
										if (mcVal.mKind == BeMCOperandKind_Immediate_f32)
											mcNeg.mImmF32 = -mcNeg.mImmF32;
										else if (mcVal.mKind == BeMCOperandKind_Immediate_f64)
											mcNeg.mImmF64 = -mcNeg.mImmF64;
										else
											mcNeg.mImmediate = -mcNeg.mImmediate;
										AllocInst(BeMCInstKind_Mov, scratchReg, mcNeg);
									}
									else
									{
										AllocInst(BeMCInstKind_Mov, scratchReg, mcVal);
										if (intrin->mKind == BfIRIntrinsic_AtomicSub)
											AllocInst(BeMCInstKind_Neg, scratchReg);
									}
									CreateStore(BeMCInstKind_XAdd, scratchReg, mcPtr);

									if ((mcMemKind.mImmediate & BfIRAtomicOrdering_ReturnModified) != 0)
									{
										if (intrin->mKind == BfIRIntrinsic_AtomicSub)
											AllocInst(BeMCInstKind_Sub, scratchReg, mcVal);
										else
											AllocInst(BeMCInstKind_Add, scratchReg, mcVal);
									}

									result = scratchReg;
									break;
								}
							}
							// Fallthrough
						case BfIRIntrinsic_AtomicAnd:
						case BfIRIntrinsic_AtomicMax:
						case BfIRIntrinsic_AtomicMin:
						case BfIRIntrinsic_AtomicNAnd:
						case BfIRIntrinsic_AtomicOr:
						case BfIRIntrinsic_AtomicUMax:
						case BfIRIntrinsic_AtomicUMin:
						case BfIRIntrinsic_AtomicXor:
							{
								auto mcPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcVal = GetOperand(castedInst->mArgs[1].mValue);
								auto mcMemKind = GetOperand(castedInst->mArgs[2].mValue);
								if (!mcMemKind.IsImmediateInt())
								{
									SoftFail("Non-constant ordering on atomic instruction", castedInst->mDbgLoc);
									break;
								}

								auto valType = GetType(mcVal);
								auto origValType = valType;
								bool isFloat = valType->IsFloat();
								auto mcTarget = BeMCOperand::ToLoad(mcPtr);
								auto useReg = ResizeRegister(X64Reg_RAX, valType->mSize);
								auto origTarget = mcTarget;

								if (isFloat)
								{
									if (valType->mSize == 4)
										valType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int32);
									else
										valType = mModule->mContext->GetPrimitiveType(BeTypeCode_Int64);

									BeMCOperand castedTarget = AllocVirtualReg(valType);
									auto vregInfo = GetVRegInfo(castedTarget);
									vregInfo->mMustExist = true;
									vregInfo->mType = valType;
									vregInfo->mAlign = valType->mAlign;
									vregInfo->mIsExpr = true;
									vregInfo->mRelTo = mcTarget;
									CreateDefineVReg(castedTarget);
									mcTarget = castedTarget;
								}

								int labelIdx = CreateLabel();

								BeMCOperand mcPrev = AllocVirtualReg(valType);
								auto vregInfo = GetVRegInfo(mcPrev);
								vregInfo->mMustExist = true;
								CreateDefineVReg(mcPrev);

								BeMCOperand mcNext = AllocVirtualReg(valType);
								vregInfo = GetVRegInfo(mcNext);
								vregInfo->mMustExist = true;
								vregInfo->mForceMem = isFloat;
								CreateDefineVReg(mcNext);

								AllocInst(BeMCInstKind_Mov, mcPrev, mcTarget);
								AllocInst(BeMCInstKind_Mov, mcNext, mcPrev);

								if (isFloat)
								{
									BeMCOperand mcFNext = AllocVirtualReg(origValType);
									vregInfo = GetVRegInfo(mcFNext);
									vregInfo->mIsExpr = true;
									vregInfo->mRelTo = mcNext;
									CreateDefineVReg(mcFNext);

									if (intrin->mKind == BfIRIntrinsic_AtomicAdd)
										AllocInst(BeMCInstKind_Add, mcFNext, mcVal);
									else
										AllocInst(BeMCInstKind_Sub, mcFNext, mcVal);
								}
								else
								{
									switch (intrin->mKind)
									{
									case BfIRIntrinsic_AtomicAdd:
										AllocInst(BeMCInstKind_Add, mcNext, mcVal);
										break;
									case BfIRIntrinsic_AtomicAnd:
										AllocInst(BeMCInstKind_Or, mcNext, mcVal);
										break;
									case BfIRIntrinsic_AtomicMax:
									case BfIRIntrinsic_AtomicMin:
									case BfIRIntrinsic_AtomicUMax:
									case BfIRIntrinsic_AtomicUMin:
										{
											int cmpLabelIdx = mCurLabelIdx++;
											AllocInst(BeMCInstKind_Cmp, mcNext, mcVal);

											BeCmpKind cmpKind = BeCmpKind_None;
											switch (intrin->mKind)
											{
											case BfIRIntrinsic_AtomicMax:
												cmpKind = BeCmpKind_SGE;
												break;
											case BfIRIntrinsic_AtomicMin:
												cmpKind = BeCmpKind_SLE;
												break;
											case BfIRIntrinsic_AtomicUMax:
												cmpKind = BeCmpKind_UGE;
												break;
											case BfIRIntrinsic_AtomicUMin:
												cmpKind = BeCmpKind_ULE;
												break;
											}

											AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(cmpLabelIdx), BeMCOperand::FromCmpKind(cmpKind));
											AllocInst(BeMCInstKind_Mov, mcNext, mcVal);
											CreateLabel(-1, cmpLabelIdx);
										}
										break;
									case BfIRIntrinsic_AtomicNAnd:
										AllocInst(BeMCInstKind_And, mcNext, mcVal);
										AllocInst(BeMCInstKind_Not, mcNext);
										break;
									case BfIRIntrinsic_AtomicOr:
										AllocInst(BeMCInstKind_Or, mcNext, mcVal);
										break;
									case BfIRIntrinsic_AtomicSub:
										AllocInst(BeMCInstKind_Sub, mcNext, mcVal);
										break;
									case BfIRIntrinsic_AtomicXor:
										AllocInst(BeMCInstKind_Xor, mcNext, mcVal);
										break;
									}
								}

								AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX));
								AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(useReg), mcPrev);
								auto cmpXChgInst = AllocInst(BeMCInstKind_CmpXChg, mcTarget, mcNext);
								AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX));

								AllocInst(BeMCInstKind_CondBr, BeMCOperand::FromLabel(labelIdx), BeMCOperand::FromCmpKind(BeCmpKind_NE));

								if ((mcMemKind.mImmediate & BfIRAtomicOrdering_ReturnModified) != 0)
									result = mcNext;
								else
									result = mcPrev;
							}
							break;
						case BfIRIntrinsic_AtomicCmpStore:
						case BfIRIntrinsic_AtomicCmpStore_Weak:
						case BfIRIntrinsic_AtomicCmpXChg:
							{
								auto mcPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcComparand = GetOperand(castedInst->mArgs[1].mValue);
								auto mcVal = GetOperand(castedInst->mArgs[2].mValue);
								auto mcMemKind = GetOperand(castedInst->mArgs[3].mValue);
								if (!mcMemKind.IsImmediate())
								{
									SoftFail("Non-constant success ordering on atomic operation", castedInst->mDbgLoc);
									break;
								}

								auto valType = GetType(mcVal);
								auto useReg = ResizeRegister(X64Reg_RAX, valType->mSize);

								BeMCOperand scratchReg = AllocVirtualReg(GetType(mcVal));
								auto vregInfo = GetVRegInfo(scratchReg);
								vregInfo->mMustExist = true;
								CreateDefineVReg(scratchReg);
								AllocInst(BeMCInstKind_Mov, scratchReg, mcVal);

								AllocInst(BeMCInstKind_PreserveVolatiles, BeMCOperand::FromReg(X64Reg_RAX));
								AllocInst(BeMCInstKind_Mov, BeMCOperand::FromReg(useReg), mcComparand);
								auto cmpXChgInst = AllocInst(BeMCInstKind_CmpXChg, BeMCOperand::ToLoad(mcPtr), scratchReg);
								if (intrin->mKind == BfIRIntrinsic_AtomicCmpXChg)
								{
									AllocInst(BeMCInstKind_Mov, scratchReg, BeMCOperand::FromReg(useReg));
									result = scratchReg;
								}
								else
								{
									auto cmpResultIdx = (int)mCmpResults.size();
									BeCmpResult cmpResult;
									cmpResult.mCmpKind = BeCmpKind_EQ;
									mCmpResults.push_back(cmpResult);

									result.mKind = BeMCOperandKind_CmpResult;
									result.mCmpResultIdx = cmpResultIdx;

									cmpXChgInst->mResult = result;
								}
								AllocInst(BeMCInstKind_RestoreVolatiles, BeMCOperand::FromReg(X64Reg_RAX));
							}
							break;
						case BfIRIntrinsic_AtomicLoad:
							{
								auto mcTarget = GetOperand(castedInst->mArgs[0].mValue);
								result = CreateLoad(mcTarget);
							}
							break;
						case BfIRIntrinsic_AtomicFence:
							{
								if (castedInst->mArgs.size() == 0)
								{
									// Compiler fence- do nothing.
									break;
								}

								auto mcMemKind = GetOperand(castedInst->mArgs[0].mValue);
								if (!mcMemKind.IsImmediateInt())
								{
									SoftFail("Non-constant success ordering on AtomicFence", castedInst->mDbgLoc);
									break;
								}
								if (mcMemKind.mImmediate == BfIRAtomicOrdering_SeqCst)
									AllocInst(BeMCInstKind_MFence);
							}
							break;
						case BfIRIntrinsic_AtomicStore:
							{
								auto mcPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcVal = GetOperand(castedInst->mArgs[1].mValue);
								auto mcMemKind = GetOperand(castedInst->mArgs[2].mValue);
								if (!mcMemKind.IsImmediateInt())
								{
									SoftFail("Non-constant success ordering on AtomicLoad", castedInst->mDbgLoc);
									break;
								}
								if (mcMemKind.mImmediate == BfIRAtomicOrdering_SeqCst)
								{
									BeMCOperand scratchReg = AllocVirtualReg(GetType(mcVal));
									auto vregInfo = GetVRegInfo(scratchReg);
									vregInfo->mMustExist = true;
									CreateDefineVReg(scratchReg);
									AllocInst(BeMCInstKind_Mov, scratchReg, mcVal);

									CreateStore(BeMCInstKind_XChg, scratchReg, mcPtr);
								}
								else
								{
									CreateStore(BeMCInstKind_Mov, mcVal, mcPtr);
								}
							}
							break;
						case BfIRIntrinsic_AtomicXChg:
							{
								auto mcPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcVal = GetOperand(castedInst->mArgs[1].mValue);
								auto mcMemKind = GetOperand(castedInst->mArgs[2].mValue);
								if (!mcMemKind.IsImmediateInt())
								{
									SoftFail("Non-constant success ordering on AtomicXChg", castedInst->mDbgLoc);
									break;
								}

								BeMCOperand scratchReg = AllocVirtualReg(GetType(mcVal));
								auto vregInfo = GetVRegInfo(scratchReg);
								vregInfo->mMustExist = true;
								CreateDefineVReg(scratchReg);
								AllocInst(BeMCInstKind_Mov, scratchReg, mcVal);

								CreateStore(BeMCInstKind_XChg, scratchReg, mcPtr);
								result = scratchReg;
							}
							break;
						case BfIRIntrinsic_Cast:
							{
								result = AllocVirtualReg(intrin->mReturnType);
								CreateDefineVReg(result);
								auto vregInfo = GetVRegInfo(result);
								vregInfo->mRelTo = GetOperand(castedInst->mArgs[0].mValue);
								vregInfo->mIsExpr = true;
							}
							break;
						case BfIRIntrinsic_DebugTrap:
							{
								AllocInst(BeMCInstKind_DbgBreak);
							}
							break;
						case BfIRIntrinsic_Index:
							{
								auto valPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto idx = GetOperand(castedInst->mArgs[1].mValue);

								auto valType = GetType(valPtr);
								if (!valType->IsPointer())
								{
									SoftFail("Non-pointer index target", castedInst->mDbgLoc);
									break;
								}
								valType = ((BePointerType*)valType)->mElementType;

								if (!valType->IsVector())
								{
									SoftFail("Non-vector index target", castedInst->mDbgLoc);
									break;
								}

								auto vectorType = (BeVectorType*)valType;

								auto elementPtrType = mModule->mContext->GetPointerTo(vectorType->mElementType);

								result = AllocVirtualReg(elementPtrType);
								CreateDefineVReg(result);
								auto vregInfo = GetVRegInfo(result);
								vregInfo->mRelTo = valPtr;
								vregInfo->mRelOffset = idx;
								vregInfo->mRelOffsetScale = vectorType->mElementType->mSize;
								vregInfo->mIsExpr = true;

								result = CreateLoad(result);
							}
							break;
						case BfIRIntrinsic_MemSet:
							{
								if (auto constVal = BeValueDynCast<BeConstant>(castedInst->mArgs[1].mValue))
								{
									if (auto constSize = BeValueDynCast<BeConstant>(castedInst->mArgs[2].mValue))
									{
										if (auto constAlign = BeValueDynCast<BeConstant>(castedInst->mArgs[3].mValue))
										{
											CreateMemSet(GetOperand(castedInst->mArgs[0].mValue), constVal->mUInt8, constSize->mInt64, constAlign->mInt32);
											break;
										}
									}
								}

								mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memset")->mIdx);
								for (int i = 0; i < 3; i++)
									args.Add(castedInst->mArgs[i].mValue);
								useAltArgs = true;
							}
							break;
						case BfIRIntrinsic_MemCpy:
							{
								if (auto constSize = BeValueDynCast<BeConstant>(castedInst->mArgs[2].mValue))
								{
									if (auto constAlign = BeValueDynCast<BeConstant>(castedInst->mArgs[3].mValue))
									{
										CreateMemCpy(GetOperand(castedInst->mArgs[0].mValue), GetOperand(castedInst->mArgs[1].mValue), constSize->mInt64, constAlign->mInt32);
										break;
									}
								}

								mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memcpy")->mIdx);
								//altArgs.insert(altArgs.begin(), castedInst->mArgs.begin(), castedInst->mArgs.begin() + 3);
								for (int i = 0; i < 3; i++)
									args.Add(castedInst->mArgs[i].mValue);
								useAltArgs = true;
							}
							break;
						case BfIRIntrinsic_MemMove:
							{
								mcFunc = BeMCOperand::FromSymbolAddr(mCOFFObject->GetSymbolRef("memmove")->mIdx);
								//altArgs.insert(altArgs.begin(), castedInst->mArgs.begin(), castedInst->mArgs.begin() + 3);
								for (int i = 0; i < 3; i++)
									args.Add(castedInst->mArgs[i].mValue);
								useAltArgs = true;
							}
							break;
						case BfIRIntrinsic_ReturnAddress:
							{
								result = AllocVirtualReg(intrin->mReturnType);
								CreateDefineVReg(result);
 								auto vregInfo = GetVRegInfo(result);
								vregInfo->mFrameOffset = 0;
							}
							break;
						case BfIRIntrinsic_VAArg:
							{
								auto mcListPtr = GetOperand(castedInst->mArgs[0].mValue);
								auto mcDestVoidPtr = GetOperand(castedInst->mArgs[1].mValue);
								auto mcType = GetOperand(castedInst->mArgs[2].mValue);

								BeType* beType = mModule->mBeIRCodeGen->GetBeTypeById((int32)mcType.mImmediate);

								auto mcList = AllocVirtualReg(mModule->mContext->GetPointerTo(mModule->mContext->GetPrimitiveType(BeTypeCode_NullPtr)));
								CreateDefineVReg(mcList);
								auto listVRegInfo = GetVRegInfo(mcList);
								listVRegInfo->mRelTo = mcListPtr;
								listVRegInfo->mIsExpr = true;

								auto mcSrc = AllocVirtualReg(mModule->mContext->GetPointerTo(beType));
								CreateDefineVReg(mcSrc);
								auto srcVRegInfo = GetVRegInfo(mcSrc);
								srcVRegInfo->mRelTo = BeMCOperand::ToLoad(mcList);
								srcVRegInfo->mIsExpr = true;

								auto mcDest = AllocVirtualReg(mModule->mContext->GetPointerTo(beType));
								CreateDefineVReg(mcDest);
								auto destVRegInfo = GetVRegInfo(mcDest);
								destVRegInfo->mRelTo = mcDestVoidPtr;
								destVRegInfo->mIsExpr = true;

								AllocInst(BeMCInstKind_Mov, BeMCOperand::ToLoad(mcDest), BeMCOperand::ToLoad(mcSrc));
								AllocInst(BeMCInstKind_Add, BeMCOperand::ToLoad(mcList), BeMCOperand::FromImmediate(8));
							}
							break;
						case BfIRIntrinsic_VAEnd:
							break;
						case BfIRIntrinsic_VAStart:
							{
								auto mcTarget = GetOperand(castedInst->mArgs[0].mValue);

								auto destVal = AllocVirtualReg(mModule->mContext->GetPointerTo(mModule->mContext->GetPrimitiveType(BeTypeCode_NullPtr)));
								auto destVRegInfo = GetVRegInfo(destVal);
								destVRegInfo->mRelTo = mcTarget;
								destVRegInfo->mIsExpr = true;
								CreateDefineVReg(destVal);

								auto nullPtrType = mModule->mContext->GetPrimitiveType(BeTypeCode_NullPtr);
								auto vaStartVal = AllocVirtualReg(nullPtrType);
								auto vRegInfo = GetVRegInfo(vaStartVal);
								vRegInfo->mMustExist = true;
								vRegInfo->mForceMem = true;
								vRegInfo->mFrameOffset = (int)mBeFunction->mParams.size() * 8 + 8;
								vRegInfo->mRefCount++;
								CreateDefineVReg(vaStartVal);

								AllocInst(BeMCInstKind_Mov, BeMCOperand::ToLoad(destVal), BeMCOperand::FromVRegAddr(vaStartVal.mVRegIdx));
							}
							break;
						default:
							SoftFail(StrFormat("Intrinsic not handled: '%s'", intrin->mName.c_str()), castedInst->mDbgLoc);
							break;
						}
					}
					else
					{
						auto funcPtrType = castedInst->mFunc->GetType();
						if (funcPtrType->IsPointer())
						{
							auto elementType = ((BePointerType*)funcPtrType)->mElementType;
							if (elementType->mTypeCode == BeTypeCode_Function)
							{
								isVarArg = ((BeFunctionType*)elementType)->mIsVarArg;
							}
						}

						returnType = castedInst->GetType();
						mcFunc = GetOperand(castedInst->mFunc);
					}

					if (mcFunc)
					{
						if (!useAltArgs)
						{
							BF_ASSERT(args.IsEmpty());
							for (auto& arg : castedInst->mArgs)
								args.Add(arg.mValue);
						}

						result = CreateCall(mcFunc, args, returnType, castedInst->mCallingConv, castedInst->HasStructRet(), castedInst->mNoReturn, isVarArg);
					}
				}
				break;
			case BeDbgDeclareInst::TypeId:
				{
					auto castedInst = (BeDbgDeclareInst*)inst;
					auto dbgVar = castedInst->mDbgVar;
					auto mcValue = GetOperand(castedInst->mValue, false, false, true);
					auto mcVReg = mcValue;
					auto vregInfo = GetVRegInfo(mcVReg);

					if ((vregInfo != NULL) && (vregInfo->mDbgVariable != NULL))
					{
						auto shadowVReg = AllocVirtualReg(vregInfo->mType);
						CreateDefineVReg(shadowVReg);
						auto shadowVRegInfo = GetVRegInfo(shadowVReg);
						shadowVRegInfo->mIsExpr = true;
						shadowVRegInfo->mValueScopeRetainedKind = BeMCValueScopeRetainKind_Hard;
						shadowVRegInfo->mRelTo = mcVReg;
						shadowVRegInfo->mRelTo.mKind = BeMCOperandKind_VReg;
						shadowVReg.mKind = mcVReg.mKind;
						mcValue = shadowVReg;
						mcVReg = shadowVReg;
						vregInfo = shadowVRegInfo;
						mValueToOperand[dbgVar] = shadowVReg;
					}
					else
					{
						mValueToOperand[dbgVar] = mcVReg;
					}

					dbgVar->mValue = castedInst->mValue;
					dbgVar->mDeclDbgLoc = inst->mDbgLoc;
					dbgVar->mIsValue = castedInst->mIsValue;

					if (!mcVReg.IsVRegAny())
					{
						mcVReg = AllocVirtualReg(mModule->mContext->GetPrimitiveType(BeTypeCode_None));
						vregInfo = GetVRegInfo(mcVReg);

						// Allocate a vreg just to properly track the lifetime of this local constant
						/*auto constType = GetType(mcVReg);
						auto newVReg = AllocVirtualReg(constType);
						vregInfo = GetVRegInfo(newVReg);
						vregInfo->mIsExpr = true;
						vregInfo->mRelTo = mcVReg;
						mcVReg = newVReg;*/
					}

					vregInfo->mDbgVariable = dbgVar;
					vregInfo->mMustExist = true;

					dbgVar->mScope = mCurDbgLoc->mDbgScope;

					dbgVar->mDeclMCBlockId = mcBlock->mBlockIdx;
					AllocInst(BeMCInstKind_DbgDecl, mcVReg);
				}
				break;
			default:
				Fail("Unhandled BeInst type");
				break;
			}

			if (mFailed)
				return;

			if (result.mKind != BeMCOperandKind_None)
				mValueToOperand[inst] = result;
		}

		inHeadAlloca = false;
		isFirstBlock = false;
	}
	mCurDbgLoc = NULL;

	BEMC_ASSERT(valueScopeStack.size() == 0);
	BEMC_ASSERT(retCount == 1);

	bool wantDebug = mDebugging;
	//wantDebug |= function->mName == "?__BfCtor@SpriteBatchRenderer@Repo@bf@@QEAAXTint@@@Z";
	//wantDebug |= function->mName == "?Testos@Fartso@@SAHPEA1@HH@Z";
	//wantDebug |= function->mName == "?GetYoopA@Fartso@@QEAAUYoop@@XZ";
		//"?TestVals@Fartso@@QEAATint@@XZ";
	/*if (function->mName == "?TestVals@Fartso@@QEAATint@@PEA_J@Z")
	{
		//TODO: Temporary, force reg1 to spill
		mVRegInfo[1]->mSpilled = true;
	}*/
	String str;

	if (wantDebug)
	{
		str = "-------- Initial --------\n" + ToString();
		OutputDebugStr(str.c_str());
	} // Hi

	DoTLSSetup();
	DoChainedBlockMerge();
	DoSplitLargeBlocks();
	DetectLoops();
	DoLastUsePass();
	for (int pass = 0; pass < 3; pass++)
	{
		// Shouldn't have more than 2 passes
		BF_ASSERT(pass != 2);
		if (DoInitializedPass())
			break;
	}

	if (wantDebug)
	{
		str = "-------- After DoDefPass --------\n" + ToString();
		OutputDebugStr(str.c_str());
	}

	RefreshRefCounts();
	GenerateLiveness();

	if (wantDebug)
	{
		str = "--------After GenerateLiveness --------\n" + ToString();
		OutputDebugStr(str.c_str());
	}

	DoInstCombinePass();
	RefreshRefCounts();

	if (wantDebug)
	{
		str = "--------After DoInstCombinePass--------\n" + ToString();
		OutputDebugStr(str.c_str());
	}

	DoLoads();

	for (int pass = 0; true; pass++)
	{
		DoRegAssignPass();
		DoSanityChecking();

		if (wantDebug)
		{
			str = "--------After RegAssignPass--------\n" + ToString();
			OutputDebugStr(str.c_str());
		}

		bool regsDone = DoLegalization();
		if (wantDebug)
		{
			str = "--------After Legalization--------\n" + ToString();
			OutputDebugStr(str.c_str());
		}

		if (regsDone)
		{
			if (pass == 0)
			{
				if (wantDebug)
				{
					// The first pass may overestimate mem cost, so run another one
					OutputDebugStr("Processing done on first pass, doing another register assign pass\n");
				}
			}
			else
				break;
		}

		//FOR TESTING
		if (pass == 4)
		{
			NOP;
		}

		if (pass == 16)
		{
			Fail("Register assignment failed!");
		}

		// Reassign regs after legalization
		//DoRegAssignPass();
	}

	DoRegFinalization();
	if (wantDebug)
	{
		str = "--------After RegFinalization--------\n" + ToString();
		OutputDebugStr(str);
	}

	DoBlockCombine();

	if (wantDebug)
	{
		str = "--------After BlockCombine --------\n" + ToString();
		OutputDebugStr(str);
	}

	while (true)
	{
		bool didWork = DoJumpRemovePass();
		if (!didWork)
			break;
		RefreshRefCounts();

		if (wantDebug)
		{
			str = "--------After DoJumpRemovePass --------\n" + ToString();
			OutputDebugStr(str);
		}
	}

	DoFrameObjPass(); // Must be (almost) last

	if (wantDebug)
	{
		str = ToString();
		OutputDebugStr(str.c_str());
	}

	DoCodeEmission();
}