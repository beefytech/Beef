#pragma warning(disable:4996)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#include "DbgTargetHost.h"
#include "DbgExprEvaluator.h"
#include "DebugManager.h"
#include "DebugVisualizers.h"
#include "CPU.h"
#include "X86.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/UTF8.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/FileEnumerator.h"
#include <inttypes.h>
#include <math.h>
#include <limits>
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"
#include "Compiler/BfDemangler.h"
#include "Compiler/BfPrinter.h"
#include <float.h>
#include "BeefySysLib/util/AllocDebug.h"
#include <limits>

USING_NS_BF_DBG;

// conversion logic based on table at http://en.wikipedia.org/wiki/Extended_precision
//CDH TODO put this somewhere more general
double NS_BF_DBG::ConvertFloat80ToDouble(const uint8 fp80[10])
{
	uint16 e = *((uint16*)&fp80[8]);
	uint64 m = *((uint64*)&fp80[0]);
	uint64 bit63 = (uint64)1 << 63;
	uint64 bit62 = (uint64)1 << 62;

	bool isNegative = (e & 0x8000) != 0;
	double s = isNegative ? -1.0 : 1.0;
	e &= 0x7fff;

	if (!e)
	{
		// the high bit and mantissa content will determine whether it's an actual zero, or a denormal or
		// pseudo-denormal number with an effective exponent of -16382.  But since that exponent is so far
		// below anything we can handle in double-precision (even accounting for denormal bit shifts), we're
		// effectively still dealing with zero.
		return s * 0.0;
	}
	else if (e == 0x7fff)
	{
		if (m & bit63)
		{
			if (m & bit62)
			{
				return std::numeric_limits<double>::quiet_NaN();
			}
			else
			{
				if (m == bit63)
					return s * std::numeric_limits<double>::infinity();
				else
					return std::numeric_limits<double>::signaling_NaN();
			}
		}
		else
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
	}
	else
	{
		if (!(m & bit63))
			return std::numeric_limits<double>::quiet_NaN(); // unnormal (we don't handle these since 80387 and later treat them as invalid operands anyway)

		// else is a normalized value
	}

	int useExponent = (int)e - 16383;
	if (useExponent < -1022)
		return s * 0.0; // we could technically support e from -1023 to -1074 as denormals, but don't bother with that for now.
	else if (useExponent > 1023)
		return s * HUGE_VAL;

	useExponent += 1023;

	BF_ASSERT((useExponent > 0) && (useExponent < 0x7ff)); // assume we've filtered for valid exponent range
	BF_ASSERT(m & bit63); // assume we've filtered out values that aren't normalized by now

	uint64 result = 0;
	if (isNegative)
		result |= bit63;
	result |= (uint64)useExponent << 52;
	result |= (m & ~bit63) >> 11;

	return *reinterpret_cast<double*>(&result);
}

addr_target NS_BF_DBG::DecodeTargetDataPtr(const char*& strRef)
{
	addr_target val = (addr_target)stouln(strRef, sizeof(intptr_target) * 2);
	strRef += sizeof(intptr_target) * 2;
	return val;
}

bool DbgTargetHost::ShouldShowStaticMember(DbgType* dbgType, DbgVariable* member)
{
	// If locationData is non-null, that means it was added in addition to the static declaration in the CV type info,
	//  so only add the names from the type definition
	auto flavor = dbgType->mCompileUnit->mDbgModule->mDbgFlavor;
	return ((((dbgType->IsNamespace()) || (flavor != DbgFlavor_MS)) && ((member->mLocationData != NULL) || member->mIsConst)) ||
		((flavor == DbgFlavor_MS) && (member->mLocationData == NULL)));
}

String DbgTargetHost::GetMemberList(DbgType* dbgType, const StringImpl& expr, bool isPtr, bool isStatic, bool forceCast, bool isSplat, bool isReadOnly)
{
	auto dbgModule = dbgType->GetDbgModule();
	dbgType->PopulateType();
	auto language = dbgType->GetLanguage();

	if (!isStatic)
	{
		String retVal;
		bool needsNewline = false;
		bool isBfObject = false;

		if (dbgType->IsBfObjectPtr())
		{
			isBfObject = true;
			dbgType = dbgType->mTypeParam;
		}

		int baseIdx = 0;
		for (auto baseTypeEntry : dbgType->mBaseTypes)
		{
			auto baseType = baseTypeEntry->mBaseType;
			if ((baseType->mSize > 0) || (baseType->mTypeCode != DbgType_Struct) || (strcmp(baseType->mTypeName, "ValueType") != 0))
			{
				String baseTypeStr = baseType->ToStringRaw(language);
				if (baseIdx > 0)
					retVal += "\n";
				if (isSplat)
					retVal += "[base]\t((" + baseTypeStr + ")" + expr + "), nv";
				else if (dbgType->WantsRefThis())
					retVal += "[base]\t((" + baseTypeStr + ")this), nd, na, nv, this=" + expr;
				else
					retVal += "[base]\t((" + baseTypeStr + "*)this), nd, na, nv, this=" + expr;
				if (isReadOnly)
					retVal += ", ne";
			}

			needsNewline = true;

			baseIdx++;
		}

		String thisExpr = expr;
		String castString;
		if (dbgType->IsBfObject())
		{
			auto ptrType = dbgType->GetDbgModule()->GetPointerType(dbgType);
			castString = ptrType->ToStringRaw(language);
		}
		else
			castString = dbgType->ToStringRaw(language);

		bool hadStatics = false;
		for (auto member : dbgType->mMemberList)
		{
			if (member->mMemberOffset < 0)
				continue;

			if (member->mIsStatic)
			{
				if (ShouldShowStaticMember(dbgType, member))
					hadStatics = true;
			}
			else
			{
				bool ignoreMember = false;
				if (member->mName != NULL)
				{
					if ((member->mName[0] == '?') ||
						(member->mName[0] == '$') ||
						(strncmp(member->mName, "_vptr$", 6) == 0))
						ignoreMember = true;
				}

				if (!ignoreMember)
				{
					if (needsNewline)
						retVal += "\n";

					if (member->mName == NULL)
					{
						retVal += GetMemberList(member->mType, expr, isPtr, isStatic, forceCast, isSplat, isReadOnly);
					}
					else
					{
						retVal += String(member->mName);
						if (isSplat)
						{
							retVal += "\t(" + thisExpr + ")." + String(member->mName);
							// We don't want to rely on this being enforced here.  For one, ref types shouldn't get ", ne" added,
							//  and this doesn't solve the issue of attempting to assign via the Immediate window
							/*if (isReadOnly)
								retVal += ", ne";*/
						}
						else
						{
							if (forceCast)
								retVal += "\t((" + castString + ")this)." + String(member->mName);
							else if ((member->mName[0] >= '0') && (member->mName[0] <= '9')) // Numbered tuple member?
								retVal += "\tthis." + String(member->mName);
							else
								retVal += "\t" + String(member->mName);

							retVal += ", this=" + thisExpr;
// 							if (isReadOnly)
// 								retVal += ", ne";
						}
					}
					needsNewline = true;
				}
			}
		}
		if (hadStatics)
		{
			if (needsNewline)
				retVal += "\n";
			retVal += "Static values\t" + castString;
		}
		return retVal;
	}
	else
	{
		if (dbgType->IsBfObjectPtr())
			dbgType = dbgType->mTypeParam;

		String retVal;
		String memberPrefix = expr;

		bool needsNewline = false;
		bool hadStatics = false;
		for (auto member : dbgType->mMemberList)
		{
			if (member->mIsStatic)
			{
				if (ShouldShowStaticMember(dbgType, member))
				{
					if (needsNewline)
						retVal += "\n";
					retVal += String(member->mName) + "\t" + memberPrefix + "." + String(member->mName);
					needsNewline = true;
				}
			}
		}
		return retVal;
	}
	return "";
}

String DbgTargetHost::GetArrayItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData)
{
	DbgEvaluationContext conditionEvaluationContext(this, dbgCompileUnit, debugVis->mCondition);

	String addrs;

	bool checkLeft = true;

	int usedCount = 0;
	while (usedCount < count)
	{
		DbgTypedValue condVal = conditionEvaluationContext.EvaluateInContext(curNode);
		if (!condVal)
			break;
		if (condVal.mBool)
		{
			auto val = curNode;
			if (valueType == NULL)
			{
				String typeAddr = val.mType->ToStringRaw();
				// RPad
				typeAddr.Append(' ', sizeof(addr_target) * 2 - typeAddr.length());
				addrs += typeAddr;
			}

			String addr = EncodeDataPtr(val.mPtr, false);
			addrs += addr;
			usedCount++;
		}

		curNode.mPtr += curNode.mType->mTypeParam->GetStride();
	}
	count = usedCount;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
	}

	return addrs;
}

String DbgTargetHost::GetLinkedListItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, addr_target endNodePtr, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData)
{
	DbgEvaluationContext nextEvaluationContext(this, dbgCompileUnit, debugVis->mNextPointer);
	DbgEvaluationContext valueEvaluationContext(this, dbgCompileUnit, debugVis->mValuePointer);

	String addrs;

	bool checkLeft = true;

	int mapIdx;
	for (mapIdx = 0; mapIdx < count; mapIdx++)
	{
		if (curNode.mPtr == endNodePtr)
			break;
		DbgTypedValue val = valueEvaluationContext.EvaluateInContext(curNode);
		if (!val)
			break;
		if (val.mPtr == 0)
			break;

		if (valueType == NULL)
		{
			String typeAddr = val.mType->ToStringRaw();
			// RPad
			typeAddr.Append(' ', sizeof(addr_target)*2 - typeAddr.length());
			addrs += typeAddr;
		}

		String addr = EncodeDataPtr(val.mPtr, false);
		addrs += addr;

		curNode = nextEvaluationContext.EvaluateInContext(curNode);
	}
	count = mapIdx;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(endNodePtr, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
	}

	return addrs;
}

String DbgTargetHost::GetDictionaryItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgTypedValue dictValue, int bucketIdx, int nodeIdx, int& count, String* outContinuationData)
{
	//DbgEvaluationContext bucketsEvaluationContext(this, dbgModule, debugVis->mBuckets);
	DbgEvaluationContext nextEvaluationContext(this, dbgCompileUnit->mDbgModule, debugVis->mNextPointer);

	DbgTypedValue bucketsPtr = EvaluateInContext(dbgCompileUnit, dictValue, debugVis->mBuckets);
	DbgTypedValue entriesPtr = EvaluateInContext(dbgCompileUnit, dictValue, debugVis->mEntries);
	if ((!bucketsPtr) || (!entriesPtr))
	{
		count = -1;
		return "";
	}
	int entrySize = entriesPtr.mType->mTypeParam->GetStride();
	int bucketIdxSize = bucketsPtr.mType->mTypeParam->GetStride();

	String addrs;

	bool checkLeft = true;

	int encodeCount = 0;
	while (encodeCount < count)
	{
		if (nodeIdx != -1)
		{
			DbgTypedValue entryValue;
			entryValue.mSrcAddress = entriesPtr.mPtr + (nodeIdx * entrySize);
			entryValue.mType = entriesPtr.mType->mTypeParam;

			addrs += EncodeDataPtr(entryValue.mSrcAddress, false);

			DbgTypedValue nextValue = nextEvaluationContext.EvaluateInContext(entryValue);
			if ((!nextValue) || (!nextValue.mType->IsInteger()))
			{
				break;
			}

			nodeIdx = (int)nextValue.GetInt64();
			encodeCount++;
		}
		else
		{
			if (bucketIdxSize == 4)
				nodeIdx = ReadMemory<int>(bucketsPtr.mPtr + bucketIdx * sizeof(int32));
			else
				nodeIdx = (int)ReadMemory<int64>(bucketsPtr.mPtr + bucketIdx * sizeof(int64));
			bucketIdx++;
		}
	}

	count = encodeCount;
	//count = mapIdx;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(dictValue.mType, false) + EncodeDataPtr(dictValue.mSrcAddress, false) +
			EncodeDataPtr((addr_target)bucketIdx, false) + EncodeDataPtr((addr_target)nodeIdx, false);
	}

	return addrs;
}

String DbgTargetHost::GetTreeItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, Array<addr_target>& parentList, DbgType*& valueType, DbgTypedValue& curNode, int count, String* outContinuationData)
{
	DbgEvaluationContext leftEvaluationContext(this, dbgCompileUnit, debugVis->mLeftPointer);
	DbgEvaluationContext rightEvaluationContext(this, dbgCompileUnit, debugVis->mRightPointer);
	DbgEvaluationContext valueEvaluationContext(this, dbgCompileUnit, debugVis->mValuePointer);

	DbgEvaluationContext conditionEvaluationContext(this, dbgCompileUnit, debugVis->mCondition);

	String addrs;

	bool checkLeft = true;

	if ((curNode.mPtr & 2) != 0) // Flag from continuation
	{
		checkLeft = false;
		curNode.mPtr &= (addr_target)~2;
	}

	HashSet<intptr> seenAddrs;

	for (int mapIdx = 0; mapIdx < count; mapIdx++)
	{
		DbgTypedValue readNode;
		while (true)
		{
			bool checkNode = (curNode.mPtr & 1) == 0;

			readNode = curNode;
			readNode.mPtr &= (addr_target)~1;

			if (checkLeft)
			{
				DbgTypedValue leftValue = leftEvaluationContext.EvaluateInContext(readNode);
				bool isEmpty = leftValue.mPtr == 0;
				if ((leftValue) && (conditionEvaluationContext.HasExpression()))
				{
					auto condValue = conditionEvaluationContext.EvaluateInContext(leftValue);
					if (condValue)
						isEmpty = !condValue.mBool;
				}
				if (isEmpty)
				{
					checkLeft = false;
					break; // Handle node
				}

				parentList.push_back(curNode.mPtr);
				curNode = leftValue;
			}
			else if (checkNode)
			{
				break; // Handle node
			}
			else
			{
				DbgTypedValue rightValue = rightEvaluationContext.EvaluateInContext(readNode);
				bool isEmpty = rightValue.mPtr == 0;
				if ((rightValue) && (conditionEvaluationContext.HasExpression()))
				{
					auto condValue = conditionEvaluationContext.EvaluateInContext(rightValue);
					if (condValue)
						isEmpty = !condValue.mBool;
				}
				if (!isEmpty)
				{
					curNode = rightValue;
					checkLeft = true;
				}
				else
				{
					if (parentList.size() == 0)
					{
						// Failed
						break;
					}

					curNode.mPtr = parentList.back();
					parentList.pop_back();
					continue; // Don't check against seenAddrs
				}
			}

			if (!seenAddrs.Add(curNode.mPtr))
			{
				// Failed!
				return "";
			}
		}

		DbgTypedValue val = valueEvaluationContext.EvaluateInContext(readNode);
		if (valueType == NULL)
			valueType = val.mType;

		String addr = EncodeDataPtr(val.mPtr, false);
		addrs += addr;

		curNode.mPtr |= 1; // Node handled
	}

	if (!checkLeft)
		curNode.mPtr |= 2;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) +  EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
		for (auto parent : parentList)
			*outContinuationData += EncodeDataPtr(parent, false);
	}

	return addrs;
}

String DbgTargetHost::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	DbgCompileUnit* dbgCompileUnit = GetCallStackCompileUnit(callStackIdx);;

	if (!IsPaused())
		return "";

	const char* dataPtr = continuationData.c_str();
	DebugVisualizerEntry* debugVis = (DebugVisualizerEntry*)DecodeLocalDataPtr(dataPtr);

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_TreeItems)
	{
		DbgType* valueType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		Array<addr_target> parentList;
		String newContinuationData;
		while (*dataPtr != 0)
			parentList.push_back(DecodeTargetDataPtr(dataPtr));

		String retVal = GetTreeItems(dbgCompileUnit, debugVis, parentList, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		addr_target endNodePtr = DecodeTargetDataPtr(dataPtr);
		DbgType* valueType = (DbgType*) DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetLinkedListItems(dbgCompileUnit, debugVis, endNodePtr, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array)
	{
		DbgType* valueType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetArrayItems(dbgCompileUnit, debugVis, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Dictionary)
	{
		DbgTypedValue dictValue;
		dictValue.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		dictValue.mSrcAddress = DecodeTargetDataPtr(dataPtr);

		int bucketIdx = (int)DecodeTargetDataPtr(dataPtr);
		int nodeIdx = (int)DecodeTargetDataPtr(dataPtr);

		String newContinuationData;
		String retVal = GetDictionaryItems(dbgCompileUnit, debugVis, dictValue, bucketIdx, nodeIdx, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}

	return "";
}

template <typename T>
static String IntTypeToString(T val, const StringImpl& name, DwDisplayInfo* displayInfo, DwFormatInfo& formatInfo)
{
	auto intDisplayType = displayInfo->mIntDisplayType;
	if (formatInfo.mDisplayType == DwDisplayType_Decimal)
		intDisplayType = DwIntDisplayType_Decimal;
	else if (formatInfo.mDisplayType == DwDisplayType_HexUpper)
		intDisplayType = DwIntDisplayType_HexadecimalUpper;
	else if (formatInfo.mDisplayType == DwDisplayType_HexLower)
		intDisplayType = DwIntDisplayType_HexadecimalLower;

	if (intDisplayType == DwIntDisplayType_Binary)
	{
		String binary;
		for (int i = 0; i < sizeof(T) * 8; i++)
		{
			if ((i != 0) && (i % 4 == 0))
				binary = "'" + binary;

			if ((i != 0) && (i % 16 == 0))
				binary = "'" + binary;

			binary = ((val & ((T)1 << i)) ? "1" : "0") + binary;
		}
		return StrFormat("0b'%s\n%s", binary.c_str(), name.c_str());
	}

	if (intDisplayType == DwIntDisplayType_Octal)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0o%%lo\n%s", name.c_str());
		}
		else
			format = StrFormat("0o%%0%do\n%s", sizeof(val) * 2, name.c_str());
		return StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val));
	}

	if (intDisplayType == DwIntDisplayType_HexadecimalUpper)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0x%%l@\n%s", name.c_str());
		}
		else
			format = StrFormat("0x%%0%dX\n%s", sizeof(val) * 2, name.c_str());
		return StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val));
	}

	if (intDisplayType == DwIntDisplayType_HexadecimalLower)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0x%%l@\n%s", name.c_str());
		}
		else
			format = StrFormat("0x%%0%dx\n%s", sizeof(val) * 2, name.c_str());
		return ToLower(StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val)));
	}

	if (std::is_unsigned<T>::value)
	{
		if (sizeof(T) == 8)
		{
			if (val > 0x7FFFFFFFF)
				return StrFormat("%llu\n%s\n:editVal\t%lluUL", val, name.c_str(), val);
			else
				return StrFormat("%llu\n%s", val, name.c_str());
		}
		else
			return StrFormat("%u\n%s", val, name.c_str());
	}
	else
	{
		if (sizeof(T) == 8)
		{
			if ((val > 0x7FFFFFFFF) || (val < -0x80000000LL))
				return StrFormat("%lld\n%s\n:editVal\t%lldL", val, name.c_str(), val);
			else
				return StrFormat("%lld\n%s", val, name.c_str(), val);
		}
		else
			return StrFormat("%d\n%s", val, name.c_str());
	}
}

DwDisplayInfo* DbgTargetHost::GetDisplayInfo(const StringImpl& referenceId)
{
	DwDisplayInfo* displayInfo = &mDebugManager->mDefaultDisplayInfo;
	if (!referenceId.empty())
	{
		if (!mDebugManager->mDisplayInfos.TryGetValue(referenceId, &displayInfo))
		{
			int dollarIdx = referenceId.LastIndexOf('$');
			if ((dollarIdx > 0) && (referenceId[dollarIdx - 1] == ']'))
			{
				// Try getting series displayinfo
				mDebugManager->mDisplayInfos.TryGetValueWith(StringView(referenceId, 0, dollarIdx), &displayInfo);
			}
		}
	}
	return displayInfo;
}

static String WrapWithModifiers(const StringImpl& origName, DbgType* dbgType, DbgLanguage language)
{
	if (language == DbgLanguage_Unknown)
		language = dbgType->GetLanguage();

	String name = origName;
	while (true)
	{
		if (dbgType->mTypeCode == DbgType_Const)
		{
			if (language == DbgLanguage_Beef)
				name = "readonly " + name;
			else
				name = "const " + name;
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Volatile)
		{
			name = "volatile " + name;
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_TypeDef)
		{
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Ref)
		{
			if (language == DbgLanguage_Beef)
				name = "ref " + name;
			else
				name = name + "&";
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Bitfield)
		{
			return dbgType->ToString(language);
		}
		else
			return name;
	}
}

#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

String DbgTargetHost::ReadString(DbgTypeCode charType, intptr addr, bool isLocalAddr, intptr maxLength, DwFormatInfo& formatInfo, bool wantStringView)
{
	int origMaxLength = maxLength;
	if (addr == 0)
		return "";

	BP_ZONE("WinDebugger::ReadString");

	String retVal = "\"";
	bool wasTerminated = false;
	String valString;
	intptr maxShowSize = 255;

	if (maxLength == -1)
		maxLength = formatInfo.mOverrideCount;
	else if (formatInfo.mOverrideCount != -1)
		maxLength = BF_MIN(formatInfo.mOverrideCount, maxLength);
	if (formatInfo.mMaxCount != -1)
		maxLength = BF_MIN(formatInfo.mMaxCount, maxLength);

	if (maxLength == -1)
		maxLength = 8 * 1024 * 1024; // Is 8MB crazy?
	if ((!formatInfo.mRawString) && (!wantStringView))
		maxLength = BF_MIN(maxLength, maxShowSize);

	if (wantStringView)
	{
		// Limit the original string view to 1MB, reevaluate on "More"
		maxLength = BF_MIN(maxLength, 1024 * 1024);
	}

	//EnableMemCache();
	bool readFailed = false;
	intptr strPtr = addr;

	int charLen = 1;
	if ((charType == DbgType_SChar16) || (charType == DbgType_UChar16))
		charLen = 2;
	else if ((charType == DbgType_SChar32) || (charType == DbgType_UChar32))
		charLen = 4;

	bool isUTF8 = formatInfo.mDisplayType == DwDisplayType_Utf8;

	int readSize = BF_MIN(1024, maxLength * charLen);
	uint8 buf[1024];
	uint8* bufPtr = NULL;
	uint8* bufEnd = NULL;
	bool hasHighAscii = false;

	int i;
	for (i = 0; i < maxLength; i++)
	{
		if (bufPtr >= bufEnd)
		{
			while (true)
			{
				if (readSize < charLen)
				{
					readFailed = true;
					break;
				}

				if (ReadMemory(strPtr, readSize, buf, isLocalAddr))
					break;

				readSize /= 2;
			}
			if (readFailed)
				break;

			bufPtr = buf;
			bufEnd = buf + readSize;
		}

		switch (charLen)
		{
		case 1:
			{
				char c = GET_FROM(bufPtr, char);
				if ((c != 0) || (formatInfo.mOverrideCount != -1))
				{
					if ((uint8)c >= 0x80)
						hasHighAscii = true;
					valString.Append(c);
				}
				else
					wasTerminated = true;
			}
			break;
		case 2:
			{
				uint16 c16 = GET_FROM(bufPtr, uint16);
				if ((c16 != 0) || (formatInfo.mOverrideCount != -1))
				{
					char str[8];
					u8_toutf8(str, 8, c16);
					valString += str;
				}
				else
					wasTerminated = true;
			}
			break;
		case 4:
			{
				uint32 c32 = GET_FROM(bufPtr, uint32);
				if ((c32 != 0) || (formatInfo.mOverrideCount != -1))
				{
					char str[8];
					u8_toutf8(str, 8, c32);
					valString += str;
				}
				else
					wasTerminated = true;
			}
			break;
		}

		if ((wasTerminated) && (formatInfo.mOverrideCount != -1))
		{
			valString += '\x00';
			wasTerminated = false;
		}

		if ((wasTerminated) || (readFailed))
		{
			break;
		}
		strPtr += charLen;
	}
	//DisableMemCache();

	if (formatInfo.mOverrideCount != -1)
	{
		if (i == formatInfo.mOverrideCount)
			wasTerminated = true;
	}

	if (strPtr == addr + origMaxLength)
		wasTerminated = true;

	if (valString.length() == formatInfo.mOverrideCount)
		wasTerminated = true;

// 	if (formatInfo.mDisplayType == DwDisplayType_Ascii)
// 	{
// 		// Our encoding for retVal is already assumed to be UTF8, so the special case here actually Ascii
// 		valString = UTF8Encode(ToWString(valString));
// 	}

	if ((formatInfo.mRawString) || (wantStringView))
	{
		if ((formatInfo.mDisplayType == DwDisplayType_Utf8) || (!hasHighAscii))
			return valString;

		String utf8Str;
		for (int i = 0; i < (int)valString.length(); i++)
		{
			char c = valString[i];
			if ((uint8)c >= 0x80)
			{
				utf8Str += (char)(0xC0 | (((uint8)c & 0xFF) >> 6));
				utf8Str += (char)(0x80 | ((uint8)c & 0x3F));
			}
			else
				utf8Str += c;
		}
		return utf8Str;
	}

	if ((readFailed) && (valString.IsEmpty()))
		return "< Failed to read string >";

	retVal += SlashString(valString, true, true, formatInfo.mLanguage == DbgLanguage_Beef);

	// We could go over 'maxShowSize' if we have a lot of slashed chars. An uninitialized string can be filled with '\xcc' chars
	if ((!formatInfo.mRawString) && (!wantStringView) && ((int)retVal.length() > maxShowSize))
	{
		retVal = retVal.Substring(0, maxShowSize);
		wasTerminated = false;
	}

	if (wasTerminated)
		retVal += "\"";
	else
		retVal += "...";

	return retVal;
}

bool DbgTargetHost::ProcessEvalString(DbgCompileUnit* dbgCompileUnit, DbgTypedValue useTypedValue, String& evalStr, String& displayString, DwFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength)
{
	bool success = true;

	for (int i = 0; i < (int)evalStr.length(); i++)
	{
		char c = evalStr[i];
		char nextC = 0;
		if (i < (int)evalStr.length() - 1)
			nextC = evalStr[i + 1];
		if ((c == '{') && (nextC != '{'))
		{
			// Evaluate

			int endIdx = i;
			for (; endIdx < (int)evalStr.length(); endIdx++)
			{
				//TODO: Do better parsing - this paren could be inside a string, for example
				if (evalStr[endIdx] == '}')
					break;
			}

			DwFormatInfo displayStrFormatInfo = formatInfo;
			displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + (int)displayString.length();
			displayStrFormatInfo.mHidePointers = false;

			if ((limitLength) && (displayStrFormatInfo.mTotalSummaryLength > 255))
			{
				displayString += "...";
			}
			else
			{
				String evalString = evalStr.Substring(i + 1, endIdx - i - 1);
				String errors;
				DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, useTypedValue, evalString, &displayStrFormatInfo, NULL, &errors);
				if (evalResult)
				{
					if (displayStrFormatInfo.mNoEdit)
						formatInfo.mNoEdit = true;

					String result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);

					if ((formatInfo.mRawString) && (limitLength))
					{
						displayString = result;
						return success;
					}

					if (displayStrFormatInfo.mRawString)
					{
						displayString += result;
					}
					else
					{
						int crPos = result.IndexOf('\n');
						if (crPos != -1)
							displayString += result.Substring(0, crPos);
						else
							displayString += result;
					}
				}
				else if (debugVis != NULL)
				{
					success = false;
					displayString += "<DbgVis Failed>";
					DbgVisFailed(debugVis, evalString, errors);
				}
				else
				{
					success = false;
					displayString += "<Eval Failed>";
				}
			}

			i = endIdx;
			continue;
		}
		else if ((c == '{') && (nextC == '{'))
		{
			// Skip next paren
			i++;
		}
		else if ((c == '}') && (nextC == '}'))
		{
			// Skip next paren
			i++;
		}

		displayString += c;
	}

	return success;
}

static bool IsNormalChar(uint32 c)
{
	return (c < 0x80);
}

String DbgTargetHost::DbgTypedValueToString(const DbgTypedValue& origTypedValue, const StringImpl& expr, DwFormatInfo& formatInfo, DbgExprEvaluator* optEvaluator, bool fullPrecision)
{
	BP_ZONE("WinDebugger::DbgTypedValueToString");

	DbgTypedValue typedValue = origTypedValue;
	auto dbgCompileUnit = typedValue.mType->mCompileUnit;
	auto dbgModule = typedValue.mType->GetDbgModule();
	auto language = origTypedValue.mType->GetLanguage();
	if (language == DbgLanguage_Unknown)
		language = formatInfo.mLanguage;
	formatInfo.mLanguage = language;
	bool isBeef = language == DbgLanguage_Beef;

	char str[32];
	bool readFailed = false;
	bool isCompositeType = false;
	bool isSizedArray = false;
	bool isEnum = false;
	int64 enumVal = 0;
	String result;
	String stringViewData;

	DwDisplayInfo* displayInfo = GetDisplayInfo(formatInfo.mReferenceId);
	bool wantStringView = (displayInfo->mFormatStr == "str") && (formatInfo.mAllowStringView);

	DbgType* origValueType = typedValue.mType;
	bool origHadRef = false;
	DbgType* dwValueType = typedValue.mType->RemoveModifiers(&origHadRef);

	if (dwValueType == NULL)
		dwValueType = dbgModule->GetPrimitiveType(DbgType_Void, language);
	else
		dwValueType = dwValueType->GetPrimaryType();

	if (dwValueType->mTypeCode == DbgType_TypeDef)
	{
		DbgTypedValue realTypedVal = typedValue;
		realTypedVal.mType = dwValueType->mTypeParam;
		return DbgTypedValueToString(realTypedVal, expr, formatInfo, optEvaluator);
	}

	if (formatInfo.mRawString)
	{
		if ((dwValueType->mTypeCode != DbgType_Struct) && (dwValueType->mTypeCode != DbgType_Class) && (dwValueType->mTypeCode != DbgType_Ptr) && (dwValueType->mTypeCode != DbgType_SizedArray))
			return "";
	}

	auto _ShowArraySummary = [&](String& retVal, addr_target ptrVal, int64 arraySize, DbgType* innerType)
	{
		String displayString;
		displayString += "{";
		for (int idx = 0; idx < arraySize; idx++)
		{
			if (formatInfo.mTotalSummaryLength + retVal.length() + displayString.length() > 255)
			{
				displayString += "...";
				break;
			}

			if ((idx != 0) && (!displayString.EndsWith('{')))
				displayString += ", ";

			DwFormatInfo displayStrFormatInfo = formatInfo;
			displayStrFormatInfo.mExpandItemDepth = 1;
			displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + retVal.length() + displayString.length();
			displayStrFormatInfo.mHidePointers = false;
			displayStrFormatInfo.mArrayLength = -1;

			// Why did we have this "na" on here? It made "void*[3]" type things show up as "{,,}"
			//String evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + StrFormat(")[%d], na", idx);
			String evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + StrFormat(")[%lld]", idx);
			DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, typedValue, evalStr, &displayStrFormatInfo);
			String result;
			if (evalResult)
			{
				result = DbgTypedValueToString(evalResult, evalStr, displayStrFormatInfo, NULL);
				int crPos = result.IndexOf('\n');
				if (crPos != -1)
					result.RemoveToEnd(crPos);
			}
			else
				result = "???";

			displayString += result;
		}
		displayString += "}";
		retVal += displayString;
	};

	if (formatInfo.mArrayLength != -1)
	{
		if (formatInfo.mRawString)
			return "";

		if (dwValueType->IsPointer())
		{
			String retVal;
			addr_target ptrVal = (addr_target)typedValue.mPtr;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
			{
				retVal = EncodeDataPtr(ptrVal, true) + " ";
				retVal += dwValueType->mTypeParam->ToString(language);
				retVal += StrFormat("[%lld] ", (int64)formatInfo.mArrayLength);
			}

			_ShowArraySummary(retVal, ptrVal, formatInfo.mArrayLength, dwValueType->mTypeParam);

			String idxStr = "[{0}]";

			DbgType* innerType = dwValueType->mTypeParam;

			retVal += "\n" + dwValueType->ToString(language);

			String evalStr = "*((" + typedValue.mType->ToStringRaw(language) + ")" + EncodeDataPtr(ptrVal, true) + " + {0})";

			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(formatInfo.mArrayLength, 0), 10000) +
				"\t" + idxStr + "\t" + evalStr;
			return retVal;
		}
		else
		{
			DwFormatInfo newFormatInfo = formatInfo;
			newFormatInfo.mArrayLength = -1;

			String retVal = DbgTypedValueToString(typedValue, expr, newFormatInfo, optEvaluator);

			int crPos = (int)retVal.IndexOf('\n');
			if (crPos != -1)
				retVal = "!Array length flag not valid with this type" + retVal.Substring(crPos);
			return retVal;
		}
	}

	switch (dwValueType->mTypeCode)
	{
	case DbgType_Void:
		return "\nvoid";
	case DbgType_Bool:
		{
			if (typedValue.mUInt8 == 0)
				return "false\n" + WrapWithModifiers("bool", origValueType, language);
			else if (typedValue.mUInt8 == 1)
				return "true\n" + WrapWithModifiers("bool", origValueType, language);
			else
				return StrFormat("true (%d)\n", typedValue.mUInt8) + WrapWithModifiers("bool", origValueType, language);
		}
		break;
	case DbgType_UChar:
		if (language != DbgLanguage_Beef)
			return IntTypeToString<uint8>(typedValue.mUInt8, WrapWithModifiers("uint8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_SChar:
		{
			if (typedValue.mInt8 != 0)
			{
				char str[2] = {(char)typedValue.mInt8};
				result = SlashString(str, formatInfo.mDisplayType == DwDisplayType_Utf8, true);

				if (!IsNormalChar(typedValue.mUInt8))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt8);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers("char", origValueType, language);
		}
		break;
	case DbgType_UChar16:
		if (language != DbgLanguage_Beef)
			return IntTypeToString<int16>(typedValue.mUInt8, WrapWithModifiers("uint16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_SChar16:
		{
			if (typedValue.mInt16 != 0)
			{
				u8_toutf8(str, 8, typedValue.mUInt32);
				result = SlashString(str, true, true);
				if (!IsNormalChar(typedValue.mUInt32))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt16);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers(isBeef ? "char16" : "int16_t", origValueType, language);
		}
		break;
	case DbgType_UChar32:
	case DbgType_SChar32:
		{
			if (typedValue.mInt32 != 0)
			{
				u8_toutf8(str, 8, typedValue.mUInt32);
				result = SlashString(str, true, true);
				if (!IsNormalChar(typedValue.mUInt32))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt32);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers(isBeef ? "char32" : "int32_t", origValueType, language);
		}
		break;
	case DbgType_i8:
		return IntTypeToString<int8>(typedValue.mInt8, WrapWithModifiers(isBeef ? "int8" : "int8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u8:
		return IntTypeToString<uint8>(typedValue.mUInt8, WrapWithModifiers(isBeef ? "uint8" : "uint8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i16:
		return IntTypeToString<int16>(typedValue.mInt16, WrapWithModifiers(isBeef ? "int16" : "int16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u16:
		return IntTypeToString<uint16>(typedValue.mUInt16, WrapWithModifiers(isBeef ? "uint16" : "uint16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i32:
		return IntTypeToString<int32>(typedValue.mInt32, WrapWithModifiers(isBeef ? "int32" : "int32_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u32:
		return IntTypeToString<uint32>(typedValue.mUInt32, WrapWithModifiers(isBeef ? "uint32" : "uint32_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i64:
		return IntTypeToString<int64>(typedValue.mInt64, WrapWithModifiers(isBeef ? "int64" : "int64_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u64:
		return IntTypeToString<uint64>(typedValue.mUInt64, WrapWithModifiers(isBeef ? "uint64" : "uint64_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_RegGroup:
		{
			if ((typedValue.mRegNum >= CPUReg_M128_XMMREG_FIRST) && (typedValue.mRegNum <= CPUReg_M128_XMMREG_LAST))
			{
				int callStackIdx = formatInfo.mCallStackIdx;
				FixCallStackIdx(callStackIdx);

				UpdateRegisterUsage(callStackIdx);
				WdStackFrame* wdStackFrame = mCallStack[callStackIdx];
				RegForm regForm = RegForm_Unknown;
				if (typedValue.mRegNum < (int)wdStackFrame->mRegForms.size())
					regForm = wdStackFrame->mRegForms[typedValue.mRegNum];

				int xmmMajor = typedValue.mRegNum - CPUReg_M128_XMMREG_FIRST;
				String headerStr;

				String xmmType = "__m128";

				int xmmCount = 4;
				if ((regForm == RegForm_Double) || (regForm == RegForm_Double2) ||
					(regForm == RegForm_Long) || (regForm == RegForm_Long2) ||
					(regForm == RegForm_ULong) || (regForm == RegForm_ULong2))
					xmmCount = 2;
				//TODO: add byte, short, int, etc...

				if (optEvaluator)
				{
					DwMmDisplayType mmDwMmDisplayType = displayInfo->mMmDisplayType;
					if (mmDwMmDisplayType == DwMmDisplayType_Default)
					{
						if ((regForm == RegForm_Double) || (regForm == RegForm_Double2))
							mmDwMmDisplayType = DwMmDisplayType_Double;
						else if (regForm == RegForm_Int4)
							mmDwMmDisplayType = DwMmDisplayType_Int32;
					}

					if (mmDwMmDisplayType == DwMmDisplayType_Double)
					{
						xmmType = "__m128d";
						xmmCount = 2;
						double xmmRegVals[2];
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_Double);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mDouble;
						}
						headerStr = StrFormat("(%f, %f)", xmmRegVals[0], xmmRegVals[1]);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_UInt8)
					{
						int xmmRegVals[4];
						xmmCount = 16;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < BF_ARRAY_COUNT(xmmRegVals); ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
							xmmRegVals[0] & 0xFF, (xmmRegVals[0] >> 8) & 0xFF, (xmmRegVals[0] >> 16) & 0xFF, (xmmRegVals[0] >> 24) & 0xFF,
							xmmRegVals[1] & 0xFF, (xmmRegVals[1] >> 8) & 0xFF, (xmmRegVals[1] >> 16) & 0xFF, (xmmRegVals[1] >> 24) & 0xFF,
							xmmRegVals[2] & 0xFF, (xmmRegVals[2] >> 8) & 0xFF, (xmmRegVals[2] >> 16) & 0xFF, (xmmRegVals[2] >> 24) & 0xFF,
							xmmRegVals[3] & 0xFF, (xmmRegVals[3] >> 8) & 0xFF, (xmmRegVals[3] >> 16) & 0xFF, (xmmRegVals[3] >> 24) & 0xFF);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int16)
					{
						int xmmRegVals[4];
						xmmCount = 8;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < BF_ARRAY_COUNT(xmmRegVals); ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d, %d, %d, %d, %d)",
							xmmRegVals[0] & 0xFFFF, (xmmRegVals[0] >> 16) & 0xFFFF,
							xmmRegVals[1] & 0xFFFF, (xmmRegVals[1] >> 16) & 0xFFFF,
							xmmRegVals[2] & 0xFFFF, (xmmRegVals[2] >> 16) & 0xFFFF,
							xmmRegVals[3] & 0xFFFF, (xmmRegVals[3] >> 16) & 0xFFFF);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int32)
					{
						int xmmRegVals[4];
						xmmCount = 4;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d)", xmmRegVals[0], xmmRegVals[1], xmmRegVals[2], xmmRegVals[3]);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int64)
					{
						int64 xmmRegVals[2];
						xmmCount = 2;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i64);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt64;
						}
						headerStr = StrFormat("(%lld, %lld)", xmmRegVals[0], xmmRegVals[1]);
					}
					else // float
					{
						float xmmRegVals[4];
						xmmCount = 4;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_Single);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mSingle;
						}
						headerStr = StrFormat("(%f, %f, %f, %f)", xmmRegVals[0], xmmRegVals[1], xmmRegVals[2], xmmRegVals[3]);
					}
				}
				else
				{
					headerStr = StrFormat("XMM%d", xmmMajor);
				}

				result = headerStr + "\n" + xmmType;
				for (int i = 0; i < xmmCount; i++)
				{
					if (xmmCount == 16)
						result += WrapWithModifiers(StrFormat("\n[%d]\t(uint8)($xmm%d_%d >> %d)", i, xmmMajor, i / 4, (i % 4)*8), origValueType, language);
					else if (xmmCount == 8)
						result += WrapWithModifiers(StrFormat("\n[%d]\t(int16)($xmm%d_%d >> %d)", i, xmmMajor, i / 2, (i % 2)*8), origValueType, language);
					else
						result += WrapWithModifiers(StrFormat("\n[%d]\t$xmm%d_%d", i, xmmMajor, i), origValueType, language);
				}

				return result;
			}
			else
			{
				switch (typedValue.mRegNum)
				{
				case CPUReg_CAT_ALLREGS:
				{
					return "ALLREGS\n__allregs\niregs\t$iregs\nflags\t$flags\nfpregs\t$fpregs\nmmregs\t$mmregs\nxmmregs\t$xmmregs";
				}
				break;
				case CPUReg_CAT_IREGS:
				{
#ifdef BF_DBG_32
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = StrFormat("(eax=0x%08x, ebx=0x%08x, ecx=0x%08x, edx=0x%08x, esi=0x%08x, edi=0x%08x, esp=0x%08x, ebp=0x%08x, eip=0x%08x, efl=0x%08x)",
							(uint32)regs->mIntRegs.eax, (uint32)regs->mIntRegs.ebx, (uint32)regs->mIntRegs.ecx, (uint32)regs->mIntRegs.edx,
							(uint32)regs->mIntRegs.esi, (uint32)regs->mIntRegs.edi, (uint32)regs->mIntRegs.esp, (uint32)regs->mIntRegs.ebp,
							(uint32)regs->mIntRegs.eip, (uint32)regs->mIntRegs.efl);
					}
					else
					{
						headerStr = "IREGS";
					}

					return StrFormat("%s\n__iregs\neax\t$eax\nebx\t$ebx\necx\t$ecx\nedx\t$edx\nesi\t$esi\nedi\t$edi\nesp\t$esp\nebp\t$ebp\neip\t$eip", headerStr.c_str());
#else
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = StrFormat("(rax=0x%@, rbx=0x%@, rcx=0x%@, rdx=0x%@, rsi=0x%@, rdi=0x%@, rsp=0x%@, rbp=0x%@, eip=0x%@, r8=0x%@, r9=0x%@, r10=0x%@, r11=0x%@, r12=0x%@, r13=0x%@, r14=0x%@, r15=0x%@, efl=0x%08x)",
							(uint64)regs->mIntRegs.rax, (uint64)regs->mIntRegs.rbx, (uint64)regs->mIntRegs.rcx, (uint64)regs->mIntRegs.rdx,
							(uint64)regs->mIntRegs.rsi, (uint64)regs->mIntRegs.rdi, (uint64)regs->mIntRegs.rsp, (uint64)regs->mIntRegs.rbp,
							(uint64)regs->mIntRegs.rip,
							(uint64)regs->mIntRegs.r8, (uint64)regs->mIntRegs.r9, (uint64)regs->mIntRegs.r10, (uint64)regs->mIntRegs.r11,
							(uint64)regs->mIntRegs.r12, (uint64)regs->mIntRegs.r13, (uint64)regs->mIntRegs.r14, (uint64)regs->mIntRegs.r15,
							(uint32)regs->mIntRegs.efl);
					}
					else
					{
						headerStr = "IREGS";
					}

					return StrFormat("%s\n__iregs\neax\t$eax\nebx\t$ebx\necx\t$ecx\nedx\t$edx\nesi\t$esi\nedi\t$edi\nesp\t$esp\nebp\t$ebp\neip\t$eip\nr8\t$r8\nr9\t$r9\nr10\t$r10\nr11\t$r11\nr12\t$r12\nr13\t$r13\nr14\t$r14\nr15\t$r15", headerStr.c_str());
#endif
				}
				break;
				case CPUReg_CAT_FPREGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = "(";
						for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						{
							if (i)
								headerStr += ", ";
							double val = ConvertFloat80ToDouble(regs->mFpMmRegsArray[i].fp.fp80);
							headerStr += StrFormat("%f", val);
						}
						headerStr += ")";
					}
					else
					{
						headerStr = "FPREGS";
					}

					result = StrFormat("%s\n__fpregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						result += StrFormat("\n[%d]\t$st%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_MMREGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = "(";
						for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						{
							if (i)
								headerStr += ", ";
							uint64 val = regs->mFpMmRegsArray[i].mm;
							headerStr += StrFormat("0x%016llx", val);
						}
						headerStr += ")";
					}
					else
					{
						headerStr = "MMREGS";
					}

					result = StrFormat("%s\n__mmregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						result += StrFormat("\n[%d]\t$mm%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_XMMREGS:
				{
					String headerStr = StrFormat("XMMREGS[%d]", CPURegisters::kNumXmmRegs); // these are too big to put a useful header for the entire category

					result = StrFormat("%s\n__xmmregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumXmmRegs; ++i)
						result += StrFormat("\n[%d]\t$xmm%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_FLAGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
#ifdef BF_DBG_32
#define FLAGVAR(abbr, name) int flag##abbr = ((regs->mIntRegs.efl & ((uint64)1 << CPURegisters::GetFlagBitForRegister(X86Reg_FLAG_##abbr##_##name))) != 0) ? 1 : 0
						FLAGVAR(CF, CARRY);
						FLAGVAR(PF, PARITY);
						FLAGVAR(AF, ADJUST);
						FLAGVAR(ZF, ZERO);
						FLAGVAR(SF, SIGN);
						FLAGVAR(IF, INTERRUPT);
						FLAGVAR(DF, DIRECTION);
						FLAGVAR(OF, OVERFLOW);
#undef FLAGVAR
#else
#define FLAGVAR(abbr, name) int flag##abbr = ((regs->mIntRegs.efl & ((uint64)1 << CPURegisters::GetFlagBitForRegister(X64Reg_FLAG_##abbr##_##name))) != 0) ? 1 : 0
						FLAGVAR(CF, CARRY);
						FLAGVAR(PF, PARITY);
						FLAGVAR(AF, ADJUST);
						FLAGVAR(ZF, ZERO);
						FLAGVAR(SF, SIGN);
						FLAGVAR(IF, INTERRUPT);
						FLAGVAR(DF, DIRECTION);
						FLAGVAR(OF, OVERFLOW);
#undef FLAGVAR
#endif
						headerStr = StrFormat("(CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, IF=%d, DF=%d, OF=%d)",
							flagCF, flagPF, flagAF, flagZF, flagSF, flagIF, flagDF, flagOF);
					}
					else
					{
						headerStr = "FLAGS";
					}

					return StrFormat("%s\n__flags\nCarry (CF)\t$flagcf\nParity (PF)\t$flagpf\nAdjust (AF)\t$flagaf\nZero (ZF)\t$flagzf\nSign (SF)\t$flagsf\nInterrupt (IF)\t$flagif\nDirection (DF)\t$flagdf\nOverflow (OF)\t$flagof",
						headerStr.c_str());
				}
				break;
				default:
					BF_ASSERT(false && "unknown category register");
					return "UNKNOWNCATEGORY\n__unknown\n";
				}
			}
		}
		break;
	case DbgType_Single:
		{
			DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
			if (floatDisplayType == DwFloatDisplayType_Default)
				floatDisplayType = DwFloatDisplayType_Minimal;
			if (floatDisplayType == DwFloatDisplayType_Minimal)
				ExactMinimalFloatToStr(typedValue.mSingle, str);
			else if (floatDisplayType == DwFloatDisplayType_Full)
				sprintf(str, "%1.9g", typedValue.mSingle);
			else if (floatDisplayType == DwFloatDisplayType_HexUpper)
				sprintf(str, "0x%04X", typedValue.mUInt32);
			else //if (floatDisplayType == DwFloatDisplayType_HexLower)
				sprintf(str, "0x%04x", typedValue.mUInt32);
			return StrFormat("%s\n%s", str, WrapWithModifiers("float", origValueType, language).c_str());
		}
	case DbgType_Double:
		{
			DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
			if (floatDisplayType == DwFloatDisplayType_Default)
				floatDisplayType = DwFloatDisplayType_Minimal;
			if (floatDisplayType == DwFloatDisplayType_Minimal)
				ExactMinimalDoubleToStr(typedValue.mDouble, str);
			else if (floatDisplayType == DwFloatDisplayType_Full)
				sprintf(str, "%1.17g", typedValue.mDouble);
			else if (floatDisplayType == DwFloatDisplayType_HexUpper)
				sprintf(str, "0x%08llX", typedValue.mUInt64);
			else //if (floatDisplayType == DwFloatDisplayType_HexLower)
				sprintf(str, "0x%08llx", typedValue.mUInt64);
			return StrFormat("%s\n%s", str, WrapWithModifiers("double", origValueType, language).c_str());
		}
	case DbgType_Subroutine:
		if (typedValue.mCharPtr != NULL)
			return StrFormat("%s\nfunc", typedValue.mCharPtr);
		else
			return "\nfunc";
	case DbgType_RawText:
		return StrFormat("%s\nrawtext", typedValue.mCharPtr);
	case DbgType_Ptr:
	{
		addr_target ptrVal = (addr_target)typedValue.mPtr;
		String retVal;
		DbgType* innerType = dwValueType->mTypeParam;
		if (innerType == NULL)
			return EncodeDataPtr(ptrVal, true) + "\nvoid*";

		bool isChar = false;

		DbgType* unmodInnerType = innerType->RemoveModifiers();
		if (unmodInnerType != NULL)
		{
			if (language == DbgLanguage_Beef)
			{
				if ((unmodInnerType->mTypeCode == DbgType_UChar) ||
					(unmodInnerType->mTypeCode == DbgType_UChar16) ||
					(unmodInnerType->mTypeCode == DbgType_UChar32))
					isChar = true;
			}
			else
			{
				if ((unmodInnerType->mTypeCode == DbgType_SChar) ||
					(unmodInnerType->mTypeCode == DbgType_SChar16) ||
					(unmodInnerType->mTypeCode == DbgType_SChar32))
					isChar = true;
			}
		}

		if ((isChar) && (formatInfo.mArrayLength == -1))
		{
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true);

			int strLen = formatInfo.mOverrideCount;
			if (typedValue.mIsLiteral)
			{
				if (strLen == -1)
					strLen = 0x7FFFFFFF;
				if (typedValue.mDataLen > 0)
					strLen = BF_MIN(strLen, typedValue.mDataLen);
				else
					strLen = BF_MIN(strLen, strlen(typedValue.mCharPtr));
			}

			SetAndRestoreValue<intptr> prevOverrideLen(formatInfo.mOverrideCount, strLen);
			String strResult = ReadString(unmodInnerType->mTypeCode, typedValue.mLocalIntPtr, typedValue.mIsLiteral, strLen, formatInfo, wantStringView);
			if (formatInfo.mRawString)
				return strResult;
			if (!strResult.IsEmpty())
			{
				if (!retVal.IsEmpty())
					retVal += " ";
				if (!wantStringView)
					retVal += strResult;
			}
			retVal += "\n" + origValueType->ToString(language);
			retVal += "\n:stringView";
			if (wantStringView)
			{
				retVal += "\t";
				retVal += SlashString(strResult, false, false, true);
			}
			return retVal;
		}
		else if ((unmodInnerType != NULL) &&
			((unmodInnerType->mTypeCode == DbgType_Class) || (unmodInnerType->mTypeCode == DbgType_Struct) || (unmodInnerType->mTypeCode == DbgType_Union)))
		{
			isCompositeType = true;
		}
		else if ((unmodInnerType != NULL) && (unmodInnerType->mTypeCode == DbgType_SizedArray))
		{
			isSizedArray = true;
		}
		else if (unmodInnerType->mTypeCode == DbgType_Subroutine)
		{
			if (formatInfo.mRawString)
				return "";
			addr_target funcPtr = (addr_target)typedValue.mPtr;
			String retVal;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(funcPtr, true);

			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			String demangledName;
			auto subProgram = mDebugTarget->FindSubProgram(funcPtr);
			if (subProgram != NULL)
			{
				demangledName = subProgram->ToString();
			}
			else if (mDebugTarget->FindSymbolAt(funcPtr, &symbolName, &offset, &dwarf))
			{
				demangledName = BfDemangler::Demangle(symbolName, language);

				if (offset != 0)
					demangledName += StrFormat("+%d", offset);
			}
			else
			{
				auto dbgModule = mDebugTarget->FindDbgModuleForAddress(funcPtr);
				if (dbgModule != NULL)
				{
					demangledName += dbgModule->GetLinkedModule()->mDisplayName + "!";
					demangledName += StrFormat("0x%@", funcPtr);
				}
			}

			if (!demangledName.IsEmpty())
			{
				retVal += " {";
				retVal += demangledName;
				retVal += "}";
			}
			retVal += "\n" + origValueType->ToString(language);

			return retVal;
		}
		else if (unmodInnerType->mTypeCode == DbgType_Void)
		{
			if (formatInfo.mRawString)
				return "";
			addr_target ptr = (addr_target)typedValue.mPtr;
			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			String demangledName;

			retVal += demangledName = StrFormat("0x%@", ptr);

			if (mDebugTarget->FindSymbolAt(ptr, &symbolName, &offset, &dwarf))
			{
				if (offset == 0)
				{
					retVal += " {";
					retVal += BfDemangler::Demangle(symbolName, language);
					retVal += "}";
				}
			}

			retVal += "\n" + origValueType->ToString(language);

			return retVal;
		}
		else
		{
			if (formatInfo.mRawString)
				return "";
			addr_target ptrVal = (addr_target)typedValue.mPtr;
			String retVal;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true);

			if (ptrVal != 0)
			{
				DbgExprEvaluator dbgExprEvaluator(this, dbgModule, NULL, -1, -1);
				DbgTypedValue innerTypedVal = dbgExprEvaluator.ReadTypedValue(NULL, innerType, typedValue.mPtr, DbgAddrType_Target);
				if (innerTypedVal)
				{
					DwFormatInfo defaultFormatInfo;
					defaultFormatInfo.mLanguage = formatInfo.mLanguage;
					defaultFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + 2; // Take into accout the necessary {}'s
					defaultFormatInfo.mExpandItemDepth++;
					String innerStr = DbgTypedValueToString(innerTypedVal, "", defaultFormatInfo, &dbgExprEvaluator);
					int crIdx = innerStr.IndexOf('\n');
					if (crIdx != -1)
					{
						String innerDataStr = innerStr.Substring(0, crIdx);
						if (!innerDataStr.empty())
						{
							if (!retVal.empty())
								retVal += " ";
							retVal += "{" + innerDataStr + "}";
						}
					}
					else
					{
						retVal += "{ ??? }";
					}
				}
			}

			retVal += "\n" + origValueType->ToString(language);
			innerType->PopulateType();

			if ((ptrVal != 0) &&
				((!innerType->mMemberList.IsEmpty()) || (innerType->mSize > 0) || (innerType->mTypeParam != NULL)))
			{
				String ptrDataStr = StrFormat("(%s)", dwValueType->ToStringRaw(language).c_str()) + EncodeDataPtr(typedValue.mPtr, true);
				retVal += "\n*\t";
				// Why did we have this?  It messed up a pointer to sized array
				/*if (language == DbgLanguage_Beef)
					retVal += "this";
				else*/
					retVal += "*this";

				if (!formatInfo.mReferenceId.empty())
					retVal += ", refid=" + MaybeQuoteFormatInfoParam(formatInfo.mReferenceId);

				retVal += ", this=" + ptrDataStr;
			}
			return retVal;
		}

		break;
	}
	case DbgType_Union:
	case DbgType_Class:
	case DbgType_Struct:
		isCompositeType = true;
		break;
	case DbgType_Enum:
		enumVal = typedValue.GetInt64();
		isEnum = true;
		break;
	case DbgType_SizedArray:
		{
			isSizedArray = true;
		}
		break;
	default:
		break;
	}

	if (isSizedArray)
	{
		String retVal;
		addr_target ptrVal = 0;

		DbgType* arrayType = dwValueType;
		DbgType* innerType = dwValueType->mTypeParam;
		if (dwValueType->mTypeCode == DbgType_SizedArray)
		{
			ptrVal = (addr_target)typedValue.mSrcAddress;
		}
		else
		{
			BF_ASSERT(dwValueType->mTypeCode == DbgType_Ptr);
			arrayType = innerType;
			innerType = arrayType->mTypeParam;
			ptrVal = typedValue.mPtr;

			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true) + " ";
		}
		if (ptrVal == 0)
			ptrVal = typedValue.mPtr;

		intptr arraySize = 0;
		intptr innerSize = innerType->GetStride();
		if (innerSize > 0)
			arraySize = arrayType->GetStride() / innerSize;
		else
		{
			// Failure!
		}

		String idxStr = "[{0}]";

		if (innerType->IsChar(language))
		{
			String strVal = ReadString(innerType->mTypeCode, typedValue.mSrcAddress, false, arraySize, formatInfo, false);
			if (formatInfo.mRawString)
				return strVal;
			retVal += strVal;
		}
		else
		{
			if (formatInfo.mRawString)
				return "";

			_ShowArraySummary(retVal, ptrVal, arraySize, innerType);
		}

		retVal += "\n" + origValueType->ToString(language);

		String referenceId = dwValueType->ToString(language);
		String evalStr;

		// Why did we have the "na"? Do we not want to show addresses for all members?
		evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + ")[{0}], refid=" + MaybeQuoteFormatInfoParam(referenceId + ".[]");
		if (typedValue.mIsReadOnly)
			evalStr += ", ne";
		retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(arraySize, 0), 10000) +
			"\t" + idxStr + "\t" + evalStr;
		return retVal;
	}

	dwValueType->PopulateType();

	if (isEnum)
	{
		String retVal;
		int64 bitsLeft = enumVal;
		int valueCount = 0;

		String editVal;

		dwValueType = dwValueType->GetPrimaryType();

		dwValueType->PopulateType();

		while ((bitsLeft != 0) || (valueCount == 0))
		{
			DbgVariable* bestMatch = NULL;

			for (auto member : dwValueType->mMemberList)
			{
				if (member->mConstValue == bitsLeft)
				{
					bestMatch = member;
					break;
				}
			}

			if (bestMatch == NULL)
			{
				for (auto member : dwValueType->mMemberList)
				{
					if ((member->mConstValue != 0) &&
						((member->mConstValue & bitsLeft) == member->mConstValue))
					{
						bestMatch = member;
						break;
					}
				}
			}

			if (bestMatch == NULL)
				break;

			if (valueCount > 0)
			{
				retVal += " | ";
				if (language == DbgLanguage_C)
					editVal += " | ";
			}

			if (language == DbgLanguage_Beef)
				retVal += ".";
			retVal += bestMatch->mName;

			if (language == DbgLanguage_C)
			{
				if (dwValueType->mParent != NULL)
				{
					editVal += dwValueType->mParent->ToString(language);
					editVal += "::";
				}
				editVal += bestMatch->mName;
			}

			valueCount++;
			bitsLeft &= ~bestMatch->mConstValue;
		}

		if ((valueCount == 0) || (bitsLeft != 0))
		{
			if (valueCount > 0)
				retVal += " | ";
			retVal += StrFormat("%lld", bitsLeft);

			if (language == DbgLanguage_C)
			{
				if (valueCount > 0)
					editVal += " | ";
				editVal += StrFormat("%lld", bitsLeft);
			}
		}

		retVal += "\n" + origValueType->ToString();
		if (language == DbgLanguage_C)
		{
			retVal += "\n:editVal\t";
			retVal += editVal;
		}
		retVal += "\n:canEdit";

		return retVal;
	}
	else if (isCompositeType)
	{
		addr_target ptrVal;
		if (dwValueType->IsPointer())
			ptrVal = (addr_target)typedValue.mPtr;
		else
			ptrVal = (addr_target)typedValue.mSrcAddress;
		String retVal;
		if ((!typedValue.mIsLiteral) && (dwValueType->IsPointer()) &&
			((!formatInfo.mHidePointers) || (ptrVal == 0)))
			retVal = EncodeDataPtr(ptrVal, true);

		DbgType* innerType = dwValueType;
		bool wasPtr = false;
		if (innerType->mTypeCode == DbgType_Ptr)
		{
			wasPtr = true;
			innerType = dwValueType->mTypeParam;
			innerType = innerType->RemoveModifiers();
		}

		innerType = innerType->GetPrimaryType();
		addr_target dataPtr = wasPtr ? typedValue.mPtr : typedValue.mSrcAddress;
		DbgType* actualType = NULL;
		bool useActualRawType = false;

		bool isBfObject = innerType->IsBfObject();
		bool hasCPPVTable = false;
		if (!isBfObject)
			hasCPPVTable = innerType->HasCPPVTable();

		int bfObjectFlags = 0;
		addr_target classVDataPtr = 0;
		bool isAppendBfObject = false;
		bool isStackBfObject = false;
		bool isDeletedBfObject = false;
		bool isCompositeWithoutAddress = false;

		if (innerType->IsBfPayloadEnum())
		{
			if (formatInfo.mRawString)
				return "";

			auto tagMember = innerType->mMemberList.mTail;
			int tagIdx = 0;

			if (dataPtr == -1)
			{
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, "(int)" + expr, &formatInfo);
				auto dscValue = dbgEvaluationContext.EvaluateInContext(DbgTypedValue());
				tagIdx = dscValue.mInt32;
			}
			else if (!ReadMemory((intptr)ptrVal + tagMember->mMemberOffset, tagMember->mType->mSize, (void*)&tagIdx))
			{
				return StrFormat("!Failed to read from 0x%@", ptrVal);
			}

			char findStr[16];
			findStr[0] = '_';
			itoa(tagIdx, findStr + 1, 10);
			int len = strlen(findStr);
			findStr[len] = '_';
			len++;

			if (!retVal.empty())
				retVal += " ";

			int startIdx = 0;
			for (auto member : innerType->mMemberList)
			{
				if (strncmp(member->mName, findStr, len) == 0)
				{
					retVal += ".";
					retVal += member->mName + len;

					String tupleExpr;

					DbgTypedValue tupleVal;
					if (dataPtr == -1)
					{
						tupleVal.mSrcAddress = -1;
						tupleVal.mType = member->mType;
						//tupleExpr = "$" + expr + "$u";
						tupleVal.mVariable = typedValue.mVariable;
						tupleExpr = "(" + member->mType->ToStringRaw() + ")(" + expr + ")";
					}
					else
					{
						tupleVal.mType = member->mType;
						tupleVal.mSrcAddress = ptrVal;
					}

					DwFormatInfo displayStrFormatInfo = formatInfo;
					displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + (int)retVal.length();
					displayStrFormatInfo.mExpandItemDepth++;
					displayStrFormatInfo.mHidePointers = false;
					retVal += DbgTypedValueToString(tupleVal, tupleExpr, displayStrFormatInfo, NULL);
					int idx = (int)retVal.IndexOf('\n');
					if (idx != -1)
					{
						if ((idx > 2) && (strncmp(retVal.c_str() + idx - 2, "()", 2) == 0))
						{
							// Take off a terminating "()" on the value, if there is one
							retVal.Remove(idx - 2, 2);
						}

						String typeName = innerType->ToString(DbgLanguage_Unknown, true);
						typeName += " ";
						retVal.Insert(idx + 1, typeName);
					}

					return retVal;
				}
			}
		}

		if (isBfObject)
		{
			classVDataPtr = ReadMemory<addr_target>(ptrVal);
			mDebugTarget->GetCompilerSettings();
			if (mDebugTarget->mBfObjectHasFlags)
			{
				bfObjectFlags = ((int)classVDataPtr) & 0xFF;

				if ((bfObjectFlags & BfObjectFlag_Deleted) != 0)
					isDeletedBfObject = true;
				if ((bfObjectFlags & BfObjectFlag_AppendAlloc) != 0)
					isAppendBfObject = true;
				if ((bfObjectFlags & (BfObjectFlag_StackAlloc | BfObjectFlag_Allocated)) == BfObjectFlag_StackAlloc)
					isStackBfObject = true;

				classVDataPtr &= ~0xFF;
			}
		}

		if (!formatInfo.mIgnoreDerivedClassInfo)
		{
			if (isBfObject)
			{
				dbgModule->ParseSymbolData();

				String symbolName;
				addr_target symOffset;
				if ((mDebugTarget->FindSymbolAt(classVDataPtr, &symbolName, &symOffset)) && (symOffset < 0x100))
				{
					String mangledClassName;

					const char* symEnd = "sBfClassVData";
					int symEndLen = strlen(symEnd);
					if (((int)symbolName.length() > symEndLen) && (strstr(symbolName.c_str(), symEnd) != NULL))
						mangledClassName = symbolName;

					// If we have flags then we may be pointing past the _typeData, actually.  We could fix this by masking out
					//  the flags area, but we need to be sure we are running a build that supports flags
					symEnd = "sBfTypeData";
					symEndLen = strlen(symEnd);
					if (((int) symbolName.length() > symEndLen) && (strstr(symbolName.c_str(), symEnd) != NULL))
						mangledClassName = symbolName;

					if (mangledClassName.length() > 0)
					{
						String className = BfDemangler::Demangle(mangledClassName, innerType->GetLanguage(), BfDemangler::Flag_RawDemangle);

						for (int i = 0; i < className.length() - 3; i++)
						{
							if ((className[i] == 'b') &&
								(className[i + 1] == 'f') &&
								(className[i + 2] == '.'))
							{
								bool matches;
								if (i == 0)
									matches = true;
								else
								{
									char prevC = className[i - 1];
									if ((prevC == ' ') ||
										(prevC == ',') ||
										(prevC == '<'))
									{
										matches = true;
									}
								}
								if (matches)
									className.Remove(i, 3);
							}
						}

						int lastDot = (int)className.LastIndexOf('.');
						if (lastDot > 0)
							className = className.Substring(0, lastDot);

						const char* arrPrefix = "System.Array1<";
						if (strncmp(className.c_str(), arrPrefix, strlen(arrPrefix)) == 0)
						{
							className = className.Substring(strlen(arrPrefix), className.length() - strlen(arrPrefix) - 1);
							className += "[]";
						}

						auto typeEntry = dbgModule->GetLinkedModule()->mTypeMap.Find(className.c_str(), DbgLanguage_BeefUnfixed);
						if (typeEntry != NULL)
						{
							actualType = typeEntry->mValue;
							if (!actualType->IsBfObject())
							{
								if (actualType->mTypeCode == DbgType_Ptr)
								{
									actualType = actualType->mTypeParam;
								}
							}
						}
					}
				}
			}
			else if (hasCPPVTable)
			{
				dbgModule->ParseSymbolData();

				addr_target classVDataPtr = ReadMemory<addr_target>(ptrVal);
				String symbolName;

				addr_target offset = 0;
				if (mDebugTarget->FindSymbolAt(classVDataPtr, &symbolName, &offset, NULL))
				{
					// On GNU, vtable indices can "go negative" for things like RTTI and virtual inheritance, so
					//  we can't rely on an exact vtable address lookup
					if (offset < 0x200)
					{
						DbgLanguage lang = innerType->GetLanguage();
						const char* symStart = (innerType->mCompileUnit->mDbgModule->mDbgFlavor == DbgFlavor_GNU) ? "_ZTV" : "??_7";
						if (strncmp(symbolName.c_str(), symStart, strlen(symStart)) == 0)
						{
							//String mangledClassName = symbolName.Substring(1);
							String className = BfDemangler::Demangle(symbolName, lang);
							int vtableNameIdx = (int)className.IndexOf("::`vftable'");
							if (vtableNameIdx != -1)
								className = className.Substring(0, vtableNameIdx);

							auto typeEntry = dbgModule->mTypeMap.Find(className.c_str(), DbgLanguage_C);
							if (typeEntry != NULL)
							{
								actualType = typeEntry->mValue;

								if ((int)className.IndexOf('<') != -1)
									useActualRawType = true;

								int thisOffset = 0;
								if (!DbgExprEvaluator::TypeIsSubTypeOf(actualType, innerType, &thisOffset))
								{
									// This catches virtual inheritance cases where we can't downcast
									actualType = NULL;
								}
							}
						}
					}
				}
			}
		}

		DbgType* displayType = origValueType;

		String displayString;
		bool wantsCustomExpandedItems = false;
		DebugVisualizerEntry* debugVis = NULL;
		Array<String> dbgVisWildcardCaptures;
		DbgType* dwUseType = (actualType != NULL) ? actualType : innerType;
		//auto ptrDataType = dwValueType;
		//TODO: Changed this from the above to account for COFF types where 'this' is always a fwd reference, does this cause any issues?
		auto ptrDataType = innerType;
		String ptrDataStr;
		if (/*(!innerType->IsBfObject()) &&*/ (!ptrDataType->IsPointer()))
		{
			if ((dataPtr != 0) || (ptrDataType->GetByteCount() > sizeof(addr_target)))
			{
				bool wantsRefThis = ptrDataType->WantsRefThis();
				ptrDataType = ptrDataType->GetDbgModule()->GetPointerType(ptrDataType);
				if (wantsRefThis)
					ptrDataStr += "*";
			}
			else
			{
				// Data is inline - must be int-sized or less
				isCompositeWithoutAddress = true;
				dataPtr = typedValue.mPtr;
			}
		}
		String ptrDataTypeStr = ptrDataType->ToStringRaw();
		ptrDataStr += StrFormat("(%s)", ptrDataTypeStr.c_str()) + EncodeDataPtr(dataPtr, true);

		DbgType* dwUsePtrType = dwUseType;
		String ptrUseDataStr;
		if (!dwUsePtrType->IsPointer())
		{
			bool wantsRefThis = dwUsePtrType->WantsRefThis();
			dwUsePtrType = dwUsePtrType->GetDbgModule()->GetPointerType(dwUsePtrType);
			if (wantsRefThis)
				ptrUseDataStr += "*";
		}
		String ptrUseDataTypeStr = dwUsePtrType->ToStringRaw();
		ptrUseDataStr += StrFormat("(%s)", ptrUseDataTypeStr.c_str()) + EncodeDataPtr(dataPtr, true);

		if ((origTypedValue.mSrcAddress == -1) && (origTypedValue.mVariable != NULL))
		{
			ptrDataStr = origTypedValue.mVariable->mName;
			if (!origTypedValue.mType->RemoveModifiers()->Equals(origTypedValue.mVariable->mType->RemoveModifiers()))
			{
				//ptrDataStr = StrFormat("(%s)%s", origTypedValue.mType->ToString().c_str(), origTypedValue.mVariable->mName);
				ptrDataStr = expr;
			}
			ptrUseDataStr = ptrDataStr;
		}

		bool isNull = wasPtr && (dataPtr == 0);
		bool isBadSrc = !wasPtr && (dataPtr == 0) && (!dwValueType->IsValuelessType());

		if ((ptrVal == 0) && (dwValueType->IsTypedPrimitive()))
		{
			DbgTypedValue rawVal;
			rawVal.mInt64 = origTypedValue.mInt64;
			rawVal.mType = dwValueType->GetRootBaseType();

			ptrDataStr = "(" + dwUseType->ToStringRaw() + ")";
			ptrDataStr += DbgTypedValueToString(rawVal, expr, formatInfo, optEvaluator, fullPrecision);

			int editValIdx = ptrDataStr.IndexOf(":editVal");
			if (editValIdx != -1)
				ptrDataStr.Remove(0, editValIdx + 9);
			int crPos = (int)ptrDataStr.IndexOf('\n');
			if (crPos != -1)
				ptrDataStr.RemoveToEnd(crPos);
			ptrUseDataStr = ptrDataStr;

			if ((origTypedValue.mRegNum != -1) && (!expr.IsEmpty()) && (!formatInfo.mExplicitThis))
			{
				// There's no address, use direct local identifier
				ptrDataStr = expr;
				ptrUseDataStr = expr;
			}

			// This keeps 'function' types from showing null as "<null parent>"
			isBadSrc = false;
		}
		else if ((ptrVal == 0) && (dwValueType->IsCompositeType()))
		{
		}

		DbgTypedValue useTypedValue = typedValue;
		if ((origHadRef) || ((typedValue.mType->HasPointer()) && (!dwUseType->HasPointer())))
		{
			useTypedValue.mSrcAddress = useTypedValue.mPtr;
			useTypedValue.mPtr = 0;

			if (dwUseType->IsTypedPrimitive())
			{
				int byteCount = dwUseType->GetByteCount();
				if (byteCount <= sizeof(intptr))
				{
					ReadMemory(useTypedValue.mSrcAddress, byteCount, &useTypedValue.mPtr);
				}
			}
		}
		useTypedValue.mType = dwUseType;
		if ((!formatInfo.mNoVisualizers) && (!isNull) && (!isBadSrc))
		{
			if (language == DbgLanguage_Beef)
				dwUseType->FixName();
			debugVis = FindVisualizerForType(dwUseType, &dbgVisWildcardCaptures);
		}

		bool hadCustomDisplayString = false;
		if (debugVis != NULL)
		{
			auto& displayStringList = (formatInfo.mRawString || wantStringView) ? debugVis->mStringViews : debugVis->mDisplayStrings;

			for (auto displayEntry : displayStringList)
			{
				if (!displayEntry->mCondition.empty())
				{
					if (!EvalCondition(debugVis, dbgCompileUnit, useTypedValue, formatInfo, displayEntry->mCondition, dbgVisWildcardCaptures, displayString))
						continue;
				}

				hadCustomDisplayString = true;
				String displayStr = mDebugManager->mDebugVisualizers->DoStringReplace(displayEntry->mString, dbgVisWildcardCaptures);
				if (displayString.length() > 0)
					displayString += " ";

				if (wantStringView)
				{
					DwFormatInfo strFormatInfo = formatInfo;
					strFormatInfo.mRawString = true;
					ProcessEvalString(dbgCompileUnit, useTypedValue, displayStr, stringViewData, strFormatInfo, debugVis, true);
				}
				else
					ProcessEvalString(dbgCompileUnit, useTypedValue, displayStr, displayString, formatInfo, debugVis, true);
				if (formatInfo.mRawString)
					return displayString;

				break;
			}

			if ((!debugVis->mExpandItems.empty()) || (debugVis->mCollectionType != DebugVisualizerEntry::CollectionType_None))
			{
				wantsCustomExpandedItems = true;
			}
		}

		if (formatInfo.mRawString)
			return "";

		bool isTuple = (dwUseType->mName != NULL) && (dwUseType->mName[0] == '(') && (language == DbgLanguage_Beef);

		if (isBadSrc)
		{
			displayString += "<null parent>";
		}
		else if ((!isNull) && (!formatInfo.mNoVisualizers) && (!hadCustomDisplayString))
		{
			// Create our own custom display

			String firstRet;
			String bigRet = isTuple ? "(" : "{ ";

			int memberIdx = 0;
			DbgType* summaryType = dwUseType;
			bool summaryDone = false;
			bool truncatedMemberList = false;

			DbgTypedValue summaryTypedValue = useTypedValue;
			String summaryDataStr = ptrDataStr;
			String splatStr;
			if (dataPtr == -1)
				splatStr = expr;

			while (summaryType != NULL)
			{
				summaryType->PopulateType();

				if ((summaryType->IsTypedPrimitive()) &&
					((summaryType->mBaseTypes.IsEmpty()) || (!summaryType->mBaseTypes.front()->mBaseType->IsTypedPrimitive())))
				{
					if (formatInfo.mTotalSummaryLength + (int)displayString.length() > 255)
					{
						truncatedMemberList = true;
						summaryDone = true;
						bigRet += "...";
					}
					else
					{
						DwFormatInfo displayStrFormatInfo = formatInfo;
						displayStrFormatInfo.mExpandItemDepth = 1;
						displayStrFormatInfo.mTotalSummaryLength += (int)displayString.length();
						displayStrFormatInfo.mHidePointers = false;

						DbgType* primType = summaryType->mTypeParam;
						String result;

						if (primType->IsInteger())
							formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

						if ((dataPtr != 0) && (dataPtr != -1))
						{
							String evalString = "(" + primType->ToString() +  ")" + ptrDataStr;
							DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, origTypedValue, evalString, &displayStrFormatInfo);
							if (evalResult)
								result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}
						else
						{
							DbgTypedValue evalResult = origTypedValue;
							evalResult.mType = primType;
							String evalString = "(" + primType->ToString() + ")" + expr;
							result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}

						if (formatInfo.mRawString)
							return result;

						int crPos = result.IndexOf('\n');
						if (crPos != -1)
							result.RemoveToEnd(crPos);

						if (memberIdx == 0)
							firstRet = result;

						bigRet += result;
						memberIdx++;
					}
				}

				for (auto member : summaryType->mMemberList)
				{
					if (!member->mIsStatic)
					{
						if (formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length() > 255)
						{
							truncatedMemberList = true;
							summaryDone = true;
							bigRet += "...";
							break;
						}

						if (member->mName != NULL)
						{
							if (member->mName[0] == '$')
								continue;

							if (!isdigit(*member->mName))
							{
								if (memberIdx != 0)
									bigRet += isTuple ? ", " : " ";

								if ((!isTuple) || (member->mName[0] != '_'))
								{
									bigRet += String(member->mName);
									bigRet += isTuple ? ":" : "=";
								}
							}
							else
							{
								if (memberIdx != 0)
									bigRet += ", ";
							}

							DwFormatInfo displayStrFormatInfo = formatInfo;
							displayStrFormatInfo.mExpandItemDepth = 1;
							displayStrFormatInfo.mHidePointers = false;
							displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length();

							String evalString;
							if (dataPtr != -1)
							{
								if ((member->mName[0] >= '0') && (member->mName[0] <= '9'))
									evalString += "this.";
								evalString += String(member->mName); // +", this=" + summaryDataStr;
							}
							else
							{
								evalString = "(";
								evalString += splatStr;
								evalString += ").";
								evalString += member->mName;
							}
							String referenceId;
							String result;
							if (!member->mType->IsValuelessType())
							{
								DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, summaryTypedValue, evalString, &displayStrFormatInfo, &referenceId);
								if (evalResult)
								{
									displayStrFormatInfo.mReferenceId = referenceId;
									result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
									int crPos = result.IndexOf('\n');
									if (crPos != -1)
										result.RemoveToEnd(crPos);
								}
								else
									result = "???";
							}

							if (member->mType->IsInteger())
								formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

							if (formatInfo.mRawString)
								return result;

							if (memberIdx == 0)
								firstRet = result;

							bigRet += result;
							//formatInfo.mEmbeddedDisplayCount = displayStrFormatInfo.mEmbeddedDisplayCount;
							memberIdx++;
						}
						else
						{
							//TODO: Handle C++ unions?
						}
					}
				}

				if (truncatedMemberList)
					break;

				// Find first base class with members
				DbgType* nextSummaryType = NULL;
				for (auto checkBase : summaryType->mBaseTypes)
				{
					auto checkBaseType = checkBase->mBaseType;
					checkBaseType = checkBaseType->GetPrimaryType();
					checkBaseType->PopulateType();
					if ((checkBaseType->GetByteCount() > 0) || (checkBaseType->IsPrimitiveType()))
					{
						if (!splatStr.empty())
						{
							splatStr = "(" + checkBaseType->ToString() + ")" + splatStr;
						}
						else
						{
							summaryTypedValue.mType = checkBaseType;
						}
						nextSummaryType = checkBaseType;
						break;
					}
				}
				summaryType = nextSummaryType;

				if (summaryType == NULL)
					break;

				// Don't add the Object members
				if ((summaryType->GetBaseType() == NULL) && (summaryType->IsBfObject()))
					break;

				// If we don't have many members then find a base class with some members to show
				if ((memberIdx != 0) && (displayString.length() >= 255))
				{
					truncatedMemberList = true;
					bigRet += "...";
					break;
				}
			}

			bigRet += isTuple ? ")" : " }";

			if (displayString.length() > 0)
				displayString += " ";
			if ((memberIdx == 1) && (!truncatedMemberList) && (firstRet.IndexOf('{') == -1) && (!isTuple))
				displayString += "{ " + firstRet + " }";
			else
				displayString += bigRet;
		}

		DbgType* memberListType = actualType;
		bool memberListForceCast = false;
		if (actualType != NULL)
		{
			String valTypeName = displayType->ToString();
			String actualTypeName = actualType->ToString(DbgLanguage_Unknown, true);
			String actualUseTypeName = actualTypeName;

			if ((int)actualTypeName.IndexOf('^') != -1)
				useActualRawType = true;

			if (useActualRawType)
				actualUseTypeName = actualType->ToStringRaw();

			if (displayString.empty())
			{
				// Nothing to display
			}
			else
			{
				if (!retVal.empty())
					retVal += " ";
				retVal += displayString;
			}

			retVal += "\n" + valTypeName;

			if ((innerType->IsBaseBfObject()) || (innerType->IsInterface()))
			{
				if (actualType != innerType)
				{
					retVal += " {" + actualTypeName + "}";
					memberListForceCast = true;
				}
			}
			else
			{
				if (actualType != innerType)
				{
					retVal += " {" + actualTypeName + "}";
					retVal += "\n";
					if (!wantsCustomExpandedItems)
					{
						retVal += "[" + actualTypeName + "]\t((" + actualUseTypeName;
						if (!actualType->IsBfObject())
							retVal += "*";
						retVal += ")this), nd, na, nv, this=" + ptrDataStr;
						memberListType = innerType;
					}
				}
			}
		}
		else
		{
			if ((formatInfo.mHidePointers) && (formatInfo.mIgnoreDerivedClassInfo))
			{
				displayType = innerType;
				if (displayString.empty())
					retVal += displayType->ToString(DbgLanguage_Unknown, true);
			}

			if (!displayString.empty())
			{
				if (!retVal.empty())
					retVal += " ";
				retVal += displayString;
			}
			else
			{
				if (formatInfo.mRawString)
					return "";
			}

			retVal += "\n" + displayType->ToString(DbgLanguage_Unknown, true);
			memberListType = innerType;
		}

		if ((isBfObject) && (mDebugTarget->mBfObjectHasFlags) && (!formatInfo.mNoVisualizers) && (!formatInfo.mRawString))
		{
			int stackTraceLen = 1;
			addr_target stackTraceAddr = ptrVal + sizeof(addr_target);
			if ((bfObjectFlags & BfObjectFlag_AllocInfo) != 0)
			{
				addr_target objectSize = ReadMemory<addr_target>(ptrVal + sizeof(addr_target));
				addr_target largeAllocInfo = ReadMemory<addr_target>(ptrVal + objectSize);
				stackTraceLen = (largeAllocInfo >> 8) & 0xFFFF;
				stackTraceAddr = ptrVal + objectSize + sizeof(addr_target);
			}
			else if ((bfObjectFlags & BfObjectFlag_AllocInfo_Short) != 0)
			{
				addr_target dbgAllocInfo = ReadMemory<addr_target>(ptrVal + sizeof(addr_target));
				stackTraceLen = dbgAllocInfo & 0xFF;
				stackTraceAddr = ptrVal + (dbgAllocInfo >> 16);
			}

			retVal += StrFormat("\n[AllocStackTrace]\t(System.CallStackList)%s, count=%d, na", EncodeDataPtr(stackTraceAddr, true).c_str(), stackTraceLen);
		}

		retVal += StrFormat("\n:language\t%d", language);

		if (formatInfo.mNoMembers)
		{
			//
		}
		else if (wantsCustomExpandedItems)
		{
			HandleCustomExpandedItems(retVal, dbgCompileUnit, debugVis, dwUseType, dwValueType, ptrUseDataStr, ptrDataStr, useTypedValue, dbgVisWildcardCaptures, formatInfo);
		}
		else if ((!isNull) && (!isBadSrc))
		{
			if (dataPtr == -1)
			{
				//String splatName = ((origTypedValue.mSrcAddress == -1) && (origTypedValue.mVariable != NULL)) ? origTypedValue.mVariable->mName : expr;
				String splatName = expr;
				retVal += "\n" + GetMemberList(memberListType, splatName, wasPtr, false, false, true, origTypedValue.mIsReadOnly);
			}
			else
			{
                retVal += "\n" + GetMemberList(memberListType, ptrDataStr, wasPtr, false, memberListForceCast, isCompositeWithoutAddress, origTypedValue.mIsReadOnly);
			}
		}

		if (formatInfo.mExpandItemDepth > 0)
			return retVal;
		if (isAppendBfObject)
			retVal += "\n:appendAlloc";
		if (isStackBfObject)
			retVal += "\n:stack";
		if (isDeletedBfObject)
			retVal += "\n:deleted";

		if (!formatInfo.mAction.IsEmpty())
		{
			retVal += "\n:action\t";
			retVal += formatInfo.mAction;
		}
		else if ((debugVis != NULL) && (!debugVis->mAction.empty()))
		{
			String rawActionStr = mDebugManager->mDebugVisualizers->DoStringReplace(debugVis->mAction, dbgVisWildcardCaptures);
			String actionStr;
			ProcessEvalString(dbgCompileUnit, useTypedValue, rawActionStr, actionStr, formatInfo, debugVis, true);
			retVal += "\n:action\t" + actionStr;
		}

		if ((!typedValue.mIsLiteral) && (dwValueType->IsPointer()))
		{
			retVal += "\n:editVal\t" + EncodeDataPtr(ptrVal, true);
		}

		if (((debugVis != NULL) && (!debugVis->mStringViews.IsEmpty())) || (wantStringView))
			retVal += "\n:stringView";

		if (wantStringView)
		{
			retVal += "\t";
			retVal += SlashString(stringViewData, false, false, true);
		}

		return retVal;
	}

	return "Unknown Type\n" + origValueType->ToString();
}

void DbgTargetHost::HandleCustomExpandedItems(String& retVal, DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* dwUseType, DbgType* dwValueType, String& ptrUseDataStr, String& ptrDataStr, DbgTypedValue useTypedValue, Array<String>& dbgVisWildcardCaptures, DwFormatInfo& formatInfo)
{
	auto debugVisualizers = mDebugManager->mDebugVisualizers;
	auto dbgModule = dbgCompileUnit->mDbgModule;

	if (formatInfo.mExpandItemDepth > 10) // Avoid crashing on circular ExpandItems
		return;

	auto language = formatInfo.mLanguage;

	bool isReadOnly = false;
	if (useTypedValue.mIsReadOnly)
		isReadOnly = true;

	for (auto entry : debugVis->mExpandItems)
	{
		if (!entry->mCondition.empty())
		{
			String error;
			if (!EvalCondition(debugVis, dbgCompileUnit, useTypedValue, formatInfo, entry->mCondition, dbgVisWildcardCaptures, error))
			{
				if (!error.empty())
					retVal += "\n" + entry->mName + "\t@!<DbgVis Failed>@!";
				continue;
			}
		}
		String replacedStr = debugVisualizers->DoStringReplace(entry->mValue, dbgVisWildcardCaptures);
		retVal += "\n" + entry->mName + "\t" + replacedStr + ", this=(" + ptrUseDataStr + ")";
	}

	String referenceId = dwUseType->ToString();

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_ExpandedItem)
	{
		DbgTypedValue itemValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
		if (itemValue)
		{
			DwFormatInfo itemFormatInfo = formatInfo;
			itemFormatInfo.mExpandItemDepth++;
			String itemRetVal = DbgTypedValueToString(itemValue, "", itemFormatInfo, NULL);

			int crIdx = (int)itemRetVal.IndexOf('\n');
			if (crIdx != -1)
			{
				crIdx = (int)itemRetVal.IndexOf('\n', crIdx + 1);
				if (crIdx != -1)
					retVal += itemRetVal.Substring(crIdx);
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array)
	{
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		Array<int> lowerDimSizes;
		for (auto lowerDim : debugVis->mLowerDimSizes)
		{
			DbgTypedValue lowerDimValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(lowerDim, dbgVisWildcardCaptures), &formatInfo);
			int dimSize = 0;
			if ((lowerDimValue) && (lowerDimValue.mType->IsInteger()))
				dimSize = (int)lowerDimValue.GetInt64();
			dimSize = BF_MAX(dimSize, 1);
			lowerDimSizes.push_back(dimSize);
		}

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			if (!debugVis->mCondition.IsEmpty())
			{
				int size = (int)sizeValue.GetInt64();
				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);

				DbgTypedValue curNode = headPointer;
				Array<addr_target> parentList;
				String continuationData;

				int totalSize = 2;
				auto valueType = headPointer.mType;
				String addrs = GetArrayItems(dbgCompileUnit, debugVis, valueType, headPointer, totalSize, &continuationData);
				String firstAddr;
				String secondAddr;
				bool hasSecondAddr = valueType == NULL;
				if (addrs.length() > 0)
				{
					const char* addrsPtr = addrs.c_str();
					firstAddr = addrs.Substring(0, sizeof(addr_target) * 2);
					if (hasSecondAddr)
						secondAddr = addrs.Substring(sizeof(addr_target) * 2, sizeof(addr_target) * 2);
				}

				String evalStr;
				if (valueType != NULL)
				{
					evalStr = "(" + valueType->ToStringRaw();
					if (!valueType->IsPointer())
						evalStr += "*";
					evalStr += ")0x{1}";
				}
				else
				{
					evalStr += "({1})0x{2}";
				}
				if (!debugVis->mShowElementAddrs)
					evalStr.Insert(0, "*");

				if (addrs.length() > 0)
				{
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, BF_MAX(size, 0), 10000) +
						"\t[{0}]\t" + evalStr + "\t" + firstAddr;

					if (hasSecondAddr)
						retVal += "\t" + secondAddr;

					if (size != 0)
					{
						retVal += "\n:addrs\t" + addrs;
						if (valueType == NULL)
							retVal += "\n:addrsEntrySize\t2";
						if (continuationData.length() > 0)
							retVal += "\n:continuation\t" + continuationData;
					}
				}
			}
			else if (lowerDimSizes.size() == 1)
			{
				int dimSize1 = lowerDimSizes[0];

				String evalStr = "(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
					StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
				evalStr += ", refid=\"" + referenceId + ".[]\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1, 50000) +
					"\t[{0}]\t" + evalStr;
			}
			else if (lowerDimSizes.size() == 2)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];

				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d]*)", headPointer.mType->mTypeParam->ToStringRaw(language).c_str(), dimSize2) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1 / dimSize2, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else if (lowerDimSizes.size() == 3)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];
				int dimSize3 = lowerDimSizes[2];

				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d][%d]*)", headPointer.mType->mTypeParam->ToStringRaw(language).c_str(), dimSize2, dimSize3) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1 / dimSize2 / dimSize3, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else
			{
				String evalStr = "*(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) + " + {0}), this=" + ptrUseDataStr;
				evalStr += ", refid=\"" + referenceId + ".[]${0}\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64(), 50000) +
					"\t[{0}]\t" + evalStr;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_IndexItems)
	{
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			String evalStr = debugVis->mValuePointer + ", this=" + ptrUseDataStr;
			evalStr.Replace("$i", "{0}");

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64(), 50000) +
				"\t[{0}]\t" + evalStr;
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		DbgType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = dbgModule->FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures), dwValueType);
			if (valueType != NULL)
				valueType = valueType->ResolveTypeDef();
		}

		DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);
		if (headPointer)
		{
			DbgTypedValue endPointer;
			if (!debugVis->mEndPointer.empty())
				endPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mEndPointer, dbgVisWildcardCaptures), &formatInfo);
			DbgTypedValue nextPointer = EvaluateInContext(dbgCompileUnit, headPointer, debugVisualizers->DoStringReplace(debugVis->mNextPointer, dbgVisWildcardCaptures), &formatInfo);

			int size = -1;
			if (!debugVis->mSize.empty())
			{
				auto sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
				if (sizeValue)
					size = (int)sizeValue.GetInt64();
			}

			DbgTypedValue curNode = headPointer;
			Array<addr_target> parentList;
			String continuationData;

			int totalSize = 2;
			String addrs = GetLinkedListItems(dbgCompileUnit, debugVis, endPointer.mPtr, valueType, curNode, totalSize, &continuationData);
			String firstAddr;
			String secondAddr;
			bool hasSecondAddr = valueType == NULL;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = addrs.Substring(0, sizeof(addr_target)*2);
				if (hasSecondAddr)
					secondAddr = addrs.Substring(sizeof(addr_target)*2, sizeof(addr_target)*2);
			}

			String evalStr;
			if (valueType != NULL)
			{
				evalStr = "(" + valueType->ToStringRaw();
				if (!valueType->IsPointer())
					evalStr += "*";
				evalStr += ")0x{1}";
			}
			else
			{
				evalStr += "({1})0x{2}";
			}
			if (!debugVis->mShowElementAddrs)
				evalStr.Insert(0, "*");

			if (addrs.length() > 0)
			{
				evalStr += ", refid=\"" + referenceId + ".[]\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
					"\t[{0}]\t" + evalStr + "\t" + firstAddr;

				if (hasSecondAddr)
					retVal += "\t" + secondAddr;

				if (size != 0)
				{
					retVal += "\n:addrs\t" + addrs;
					if (valueType == NULL)
						retVal += "\n:addrsEntrySize\t2";
					if (continuationData.length() > 0)
						retVal += "\n:continuation\t" + continuationData;
				}
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_TreeItems)
	{
		DbgType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = dbgModule->FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures), dwValueType);
			if (valueType != NULL)
				valueType = valueType->ResolveTypeDef();
		}

		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);

		if (sizeValue)
			sizeValue.mType = sizeValue.mType->RemoveModifiers();
		if ((sizeValue) && (headPointer) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			DbgTypedValue curNode = headPointer;
			Array<addr_target> parentList;
			String continuationData;

			int getItemCount = (int)BF_MIN(sizeValue.GetInt64(), 32LL);

			String addrs = GetTreeItems(dbgCompileUnit, debugVis, parentList, valueType, curNode, getItemCount, &continuationData);
			addr_target firstAddr = 0;
			addr_target secondAddr = 0;
			bool hasSecondAddr = valueType == NULL;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = DecodeTargetDataPtr(addrsPtr);
				if (hasSecondAddr)
					secondAddr = DecodeTargetDataPtr(addrsPtr);
			}

			String evalStr;
			if (valueType != NULL)
			{
				evalStr = "*(" + valueType->ToStringRaw();
				if (!valueType->IsPointer())
					evalStr += "*";
				evalStr += ")0x{1}";
			}
			else
			{
				evalStr += "*(_T_{1}*)0x{2}";
			}

			int size = (int)sizeValue.GetInt64();
			if (addrs.length() == 0)
			{
				evalStr = ""; // Failed
			}

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
				"\t[{0}]\t" + evalStr + "\t" + EncodeDataPtr(firstAddr, false);

			if (hasSecondAddr)
				retVal += "\t" + EncodeDataPtr(secondAddr, false);

			if (addrs.length() > 0)
			{
				retVal += "\n:addrs\t" + addrs;
				if (continuationData.length() > 0)
					retVal += "\n:continuation\t" + continuationData;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Dictionary)
	{
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		DbgTypedValue entriesPtrValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mEntries, dbgVisWildcardCaptures), &formatInfo);

		if (sizeValue)
			sizeValue.mType = sizeValue.mType->RemoveModifiers();
		if ((sizeValue) && (entriesPtrValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			String continuationData;

			DbgType* valueType = entriesPtrValue.mType;

			int getItemCount = (int)BF_MIN(sizeValue.GetInt64(), (int64)2);

			DbgType* useTypedValType = useTypedValue.mType;
			addr_target useTypedValPtr = useTypedValue.mPtr;
			addr_target useTypedValAddr = useTypedValue.mSrcAddress;

			String addrs = GetDictionaryItems(dbgCompileUnit, debugVis, useTypedValue, 0, -1, getItemCount, &continuationData);
			addr_target firstAddr = 0;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = DecodeTargetDataPtr(addrsPtr);
			}

			String evalStr = "((" + valueType->ToStringRaw() + ")0x{1}), na";

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, (int)sizeValue.GetInt64(), 10000) +
				"\t[{0}]\t" + evalStr + "\t" + EncodeDataPtr(firstAddr, false);

			if (addrs.length() > 0)
			{
				retVal += "\n:addrs\t" + addrs;
				if (continuationData.length() > 0)
					retVal += "\n:continuation\t" + continuationData;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_CallStackList)
	{
		int size = 0;

		String addrs;
		String firstVal;
		auto ptr = useTypedValue.mPtr;

		for (int i = 0; i < formatInfo.mOverrideCount; i++)
		{
			auto funcAddr = ReadMemory<addr_target>(ptr + i * sizeof(addr_target));
			auto srcFuncAddr = funcAddr;

			addrs += EncodeDataPtr(funcAddr - 1, false);
			if (i == 0)
				firstVal = addrs;
			addrs += EncodeDataPtr((addr_target)0, false);
			size++;

			int inlineIdx = 0;

			auto subProgram = mDebugTarget->FindSubProgram(funcAddr - 1, DbgOnDemandKind_LocalOnly);
			while (subProgram != NULL)
			{
				if (subProgram->mInlineeInfo == NULL)
					break;

				auto prevFuncAddr = subProgram->mBlock.mLowPC;

				subProgram = subProgram->mInlineeInfo->mInlineParent;
				addrs += EncodeDataPtr(subProgram->mBlock.mLowPC + 1, false);
				addrs += EncodeDataPtr(prevFuncAddr, false);
				size++;

				inlineIdx++;
			}
		}

		String evalStr = "(System.CallStackAddr)0x{1}";
		evalStr += ", refid=\"" + referenceId + ".[]\"";
		evalStr += ", ne";

		retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
			"\t[{0}]\t" + evalStr + ", action=ShowCodeAddr {1} {2}\t" + firstVal + "\t" + EncodeDataPtr((addr_target)0, false);

		retVal += "\n:addrs\t" + addrs;
		retVal += "\n:addrsEntrySize\t2";
		return;
	}

	if (formatInfo.mExpandItemDepth == 0)
	{
		//retVal += "\n[Raw View]\tthis, this=" + ptrDataStr + ", nv";
		retVal += "\n[Raw View]\t" + ptrDataStr + ", nv";
	}
}
