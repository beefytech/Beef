#include "DbgTargetHost.h"
#include "DbgExprEvaluator.h"
#include "DebugManager.h"
#include "DebugVisualizers.h"
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

DbgEvaluationContext::DbgEvaluationContext(DbgTargetHost* debugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	Init(debugger, dbgModule, expr, formatInfo, contextValue);
}

DbgEvaluationContext::DbgEvaluationContext(DbgTargetHost* debugger, DbgCompileUnit* dbgCompileUnit, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	DbgModule* dbgModule = NULL;
	if (dbgCompileUnit != NULL)
		dbgModule = dbgCompileUnit->mDbgModule;
	Init(debugger, dbgModule, expr, formatInfo, contextValue);
}

void DbgEvaluationContext::Init(DbgTargetHost* debugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	if (expr.empty())
	{
		mParser = NULL;
		mReducer = NULL;
		mPassInstance = NULL;
		mDbgExprEvaluator = NULL;
		mExprNode = NULL;
		return;
	}

	mParser = new BfParser(debugger->mBfSystem);
	mParser->mCompatMode = true;
	mPassInstance = new BfPassInstance(debugger->mBfSystem);
	auto terminatedExpr = expr + ";";
	mParser->SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	mParser->Parse(mPassInstance);

	mReducer = new BfReducer();
	mReducer->mAlloc = mParser->mAlloc;
	mReducer->mSystem = debugger->mBfSystem;
	mReducer->mPassInstance = mPassInstance;
	mReducer->mVisitorPos = BfReducer::BfVisitorPos(mParser->mRootNode);
	mReducer->mVisitorPos.MoveNext();
	mReducer->mCompatMode = mParser->mCompatMode;
	mReducer->mSource = mParser;
	mExprNode = mReducer->CreateExpression(mParser->mRootNode->GetFirst());
	mParser->Close();
	mDbgExprEvaluator = new DbgExprEvaluator(debugger, dbgModule, mPassInstance, -1, -1);

	if ((formatInfo != NULL) && (mExprNode != NULL) && (mExprNode->GetSrcEnd() < (int) expr.length()))
	{
		String formatFlags = expr.Substring(mExprNode->GetSrcEnd());
		String errorString = "Invalid expression";
		if (!debugger->ParseFormatInfo(dbgModule, formatFlags, formatInfo, mPassInstance, NULL, NULL, &errorString, contextValue))
		{
			mPassInstance->FailAt(errorString, mParser->mSourceData, mExprNode->GetSrcEnd(), (int)expr.length() - mExprNode->GetSrcEnd());
			formatFlags = "";
		}
	}

	if (formatInfo != NULL)
	{
		mDbgExprEvaluator->mExplicitThis = formatInfo->mExplicitThis;
		mDbgExprEvaluator->mCallStackIdx = formatInfo->mCallStackIdx;
		mDbgExprEvaluator->mLanguage = formatInfo->mLanguage;
	}
}

bool DbgEvaluationContext::HasExpression()
{
	return mExprNode != NULL;
}

DbgEvaluationContext::~DbgEvaluationContext()
{
	delete mParser;
	delete mReducer;
	delete mDbgExprEvaluator;
	delete mPassInstance;
}

DbgTypedValue DbgEvaluationContext::EvaluateInContext(DbgTypedValue contextTypedValue)
{
	if (mExprNode == NULL)
		return DbgTypedValue();
	mPassInstance->ClearErrors();
	if (contextTypedValue)
	{
		mDbgExprEvaluator->mExplicitThis = contextTypedValue;
		if ((mDbgExprEvaluator->mExplicitThis.mType->IsPointer()) && (mDbgExprEvaluator->mExplicitThis.mType->mTypeParam->WantsRefThis()))
		{
			mDbgExprEvaluator->mExplicitThis.mType = mDbgExprEvaluator->mExplicitThis.mType->mTypeParam;
			mDbgExprEvaluator->mExplicitThis.mSrcAddress = mDbgExprEvaluator->mExplicitThis.mPtr;
			mDbgExprEvaluator->mExplicitThis.mPtr = 0;
		}

		if ((mDbgExprEvaluator->mExplicitThis.mType->IsCompositeType()) && (!mDbgExprEvaluator->mExplicitThis.mType->WantsRefThis()))
		{
			if (mDbgExprEvaluator->mExplicitThis.mSrcAddress != 0)
			{
				mDbgExprEvaluator->mExplicitThis.mType = mDbgExprEvaluator->mDbgModule->GetPointerType(mDbgExprEvaluator->mExplicitThis.mType);
				mDbgExprEvaluator->mExplicitThis.mPtr = mDbgExprEvaluator->mExplicitThis.mSrcAddress;
				mDbgExprEvaluator->mExplicitThis.mSrcAddress = 0;
			}
		}
	}
	if (contextTypedValue.mType != NULL)
		mDbgExprEvaluator->mDbgCompileUnit = contextTypedValue.mType->mCompileUnit;
	DbgTypedValue exprResult;
	auto result = mDbgExprEvaluator->Resolve(mExprNode);
	return result;
}

bool DbgEvaluationContext::HadError()
{
	return mPassInstance->mFailedIdx != 0;
}

String DbgEvaluationContext::GetErrorStr()
{
	String errorStr = mPassInstance->mErrors[0]->mError;
	if (mExprNode != NULL)
	{
		errorStr += ": ";
		errorStr += mExprNode->ToString();
	}
	return errorStr;
}

bool DbgTargetHost::ParseFormatInfo(DbgModule* dbgModule, const StringImpl& formatInfoStr, DwFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExprString, String* errorString, DbgTypedValue contextTypedValue)
{
	String formatFlags = formatInfoStr;
	if (assignExprOffset != NULL)
		*assignExprOffset = -1;

	while (formatFlags.length() > 0)
	{
		formatFlags = Trim(formatFlags);
		if (formatFlags.IsEmpty())
			break;
		if (formatFlags[0] != ',')
		{
			return false;
		}
		else
		{
			int nextComma = formatFlags.IndexOf(',', 1);
			int quotePos = formatFlags.IndexOf('"', 1);
			if ((quotePos != -1) && (quotePos < nextComma))
			{
				int nextQuotePos = formatFlags.IndexOf('"', quotePos + 1);
				if (nextQuotePos != -1)
					nextComma = formatFlags.IndexOf(',', nextQuotePos + 1);
			}
			if (nextComma == -1)
				nextComma = formatFlags.length();

			String formatCmd = formatFlags.Substring(1, nextComma - 1);
			formatCmd = Trim(formatCmd);
			bool hadError = false;

			if (strncmp(formatCmd.c_str(), "this=", 5) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String thisExpr = formatCmd.Substring(5);
				if (thisExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, thisExpr, formatInfo);
				formatInfo->mExplicitThis = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = thisExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "count=", 6) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(6);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mOverrideCount = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "maxcount=", 9) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(9);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mMaxCount = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "arraysize=", 10) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(10);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "assign=", 7) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String assignExpr = formatCmd.Substring(7);
				if (assignExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, assignExpr, formatInfo);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				if (assignExprOffset != NULL)
				{
					//TODO: Keep track of the offset directly, this is a hack
					*assignExprOffset = (int)formatInfoStr.IndexOf("assign=") + 7;
				}
				if (assignExprString != NULL)
					*assignExprString = dbgEvaluationContext.mExprNode->ToString();
				formatFlags = assignExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "refid=", 6) == 0)
			{
				formatInfo->mReferenceId = formatCmd.Substring(6);
				if ((formatInfo->mReferenceId.mLength >= 2) && (formatInfo->mReferenceId[0] == '\"'))
					formatInfo->mReferenceId = formatInfo->mReferenceId.Substring(1, formatInfo->mReferenceId.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "action=", 7) == 0)
			{
				formatInfo->mAction = formatCmd.Substring(7);
				if ((formatInfo->mAction.mLength >= 2) && (formatInfo->mAction[0] == '\"'))
					formatInfo->mAction = formatInfo->mReferenceId.Substring(1, formatInfo->mReferenceId.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "_=", 2) == 0)
			{
				formatInfo->mSubjectExpr = formatCmd.Substring(2);
				if ((formatInfo->mSubjectExpr.mLength >= 2) && (formatInfo->mSubjectExpr[0] == '\"'))
					formatInfo->mSubjectExpr = formatInfo->mSubjectExpr.Substring(1, formatInfo->mSubjectExpr.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "expectedType=", 13) == 0)
			{
				formatInfo->mExpectedType = formatCmd.Substring(13);
				if ((formatInfo->mExpectedType.mLength >= 2) && (formatInfo->mExpectedType[0] == '\"'))
					formatInfo->mExpectedType = formatInfo->mExpectedType.Substring(1, formatInfo->mExpectedType.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "namespaceSearch=", 16) == 0)
			{
				formatInfo->mNamespaceSearch = formatCmd.Substring(16);
				if ((formatInfo->mNamespaceSearch.mLength >= 2) && (formatInfo->mNamespaceSearch[0] == '\"'))
					formatInfo->mNamespaceSearch = formatInfo->mNamespaceSearch.Substring(1, formatInfo->mNamespaceSearch.length() - 2);
			}
			else if (formatCmd == "d")
			{
				formatInfo->mDisplayType = DwDisplayType_Decimal;
			}
			else if (formatCmd == "x")
			{
				formatInfo->mDisplayType = DwDisplayType_HexLower;
			}
			else if (formatCmd == "X")
			{
				formatInfo->mDisplayType = DwDisplayType_HexUpper;
			}
			else if (formatCmd == "s")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Ascii;
			}
			else if (formatCmd == "s8")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf8;
			}
			else if (formatCmd == "s16")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf16;
			}
			else if (formatCmd == "s32")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf32;
			}
			else if (formatCmd == "nd")
			{
				formatInfo->mIgnoreDerivedClassInfo = true;
			}
			else if (formatCmd == "na")
			{
				formatInfo->mHidePointers = true;
			}
			else if (formatCmd == "nm")
			{
				formatInfo->mNoMembers = true;
			}
			else if (formatCmd == "ne")
			{
				formatInfo->mNoEdit = true;
			}
			else if (formatCmd == "nv")
			{
				formatInfo->mNoVisualizers = true;
			}
			else if (formatCmd == "rawStr")
			{
				formatInfo->mRawString = true;
			}
			else if (((!formatCmd.IsEmpty()) && ((formatCmd[0] >= '0') && (formatCmd[0] <= '9'))) ||
					 (formatCmd.StartsWith("(")))
			{
				String countExpr = formatCmd;
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else
				hadError = true;

			if (hadError)
			{
				if (errorString != NULL)
					*errorString = "Invalid format flags";
				return false;
			}

			formatFlags = formatFlags.Substring(nextComma);
		}
	}
	return true;
}

String DbgTargetHost::MaybeQuoteFormatInfoParam(const StringImpl& str)
{
	bool needsQuote = false;
	for (int i = 0; i < (int)str.length(); i++)
	{
		char c = str[i];
		if (c == ',')
			needsQuote = true;
	}
	if (!needsQuote)
		return str;

	String qStr = "\"";
	qStr += str;
	qStr += "\"";
	return qStr;
}

DbgTypedValue DbgTargetHost::EvaluateInContext(DbgCompileUnit* dbgCompileUnit, const DbgTypedValue& contextTypedValue, const StringImpl& subExpr, DwFormatInfo* formatInfo, String* outReferenceId, String* outErrors)
{
	DbgEvaluationContext dbgEvaluationContext(this, dbgCompileUnit->mDbgModule, subExpr, formatInfo, contextTypedValue);
	if (dbgEvaluationContext.mDbgExprEvaluator == NULL)
		return DbgTypedValue();
	dbgEvaluationContext.mDbgExprEvaluator->mDbgCompileUnit = dbgCompileUnit;
	if (formatInfo != NULL)
	{
		dbgEvaluationContext.mDbgExprEvaluator->mLanguage = formatInfo->mLanguage;
		dbgEvaluationContext.mDbgExprEvaluator->mSubjectExpr = formatInfo->mSubjectExpr;
	}
	dbgEvaluationContext.mDbgExprEvaluator->mReferenceId = outReferenceId;
	auto result = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
	if ((formatInfo != NULL) && (dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride != -1))
		formatInfo->mOverrideCount = dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride;
	if (dbgEvaluationContext.mPassInstance->HasFailed())
	{
		if (outErrors != NULL)
		{
			int errIdx = 0;
			for (auto err : dbgEvaluationContext.mPassInstance->mErrors)
			{
				if (errIdx > 0)
					(*outErrors) += "\n";
				(*outErrors) += err->mError;
				errIdx++;
			}
		}
		return DbgTypedValue();
	}
	return result;
}

void DbgTargetHost::DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors)
{
	bool onlyMemError = errors.StartsWith("Failed to read") && !errors.Contains('\n');
	if ((!debugVis->mShowedError) && (!onlyMemError))
	{
		debugVis->mShowedError = true;
		String errStr = StrFormat("DbgVis '%s' failed while evaluating condition '%s'\n", debugVis->mName.c_str(), evalString.c_str());
		String spacedErrors = errors;
		spacedErrors.Insert(0, " ");
		spacedErrors.Replace("\n", "\n ");
		errStr += spacedErrors;
		OutputMessage(errStr);
	}
}

bool DbgTargetHost::EvalCondition(DebugVisualizerEntry* debugVis, DbgCompileUnit* dbgCompileUnit, DbgTypedValue typedVal, DwFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr)
{
	DwFormatInfo displayStrFormatInfo = formatInfo;
	displayStrFormatInfo.mHidePointers = false;
	displayStrFormatInfo.mRawString = false;

	String errors;
	const String conditionStr = mDebugManager->mDebugVisualizers->DoStringReplace(condition, dbgVisWildcardCaptures);
	DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, typedVal, conditionStr, &displayStrFormatInfo, NULL, &errors);
	if ((!evalResult) || (!evalResult.mType->IsBoolean()))
	{
		if (formatInfo.mRawString)
			return false;

		errorStr += "<DbgVis Failed>";
		DbgVisFailed(debugVis, conditionStr, errors);
		return false;
	}

	return evalResult.mBool;
}

DebugVisualizerEntry* DbgTargetHost::FindVisualizerForType(DbgType* dbgType, Array<String>* wildcardCaptures)
{
	auto entry = mDebugManager->mDebugVisualizers->FindEntryForType(dbgType->ToString(DbgLanguage_Unknown, true), dbgType->mCompileUnit->mDbgModule->mDbgFlavor, wildcardCaptures);

	if (entry == NULL)
	{
		dbgType = dbgType->GetPrimaryType();
		dbgType->PopulateType();
		for (auto baseTypeEntry : dbgType->mBaseTypes)
		{
			entry = FindVisualizerForType(baseTypeEntry->mBaseType, wildcardCaptures);
			if (entry != NULL)
				break;
		}
	}

	return entry;
}
