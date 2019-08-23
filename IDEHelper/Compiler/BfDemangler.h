#pragma once

#include "BeefySysLib/Common.h"
#include "../DebugCommon.h"

NS_BF_BEGIN

typedef Array<String> SubstituteList;

class DemangleBase
{
public:
	DbgLanguage mLanguage;
	int mCurIdx;
	String mResult;
	bool mFailed;
	String mMangledName;		
	SubstituteList mSubstituteList;
	bool mInArgs;
	bool mBeefFixed;

public:
	DemangleBase();

	bool Failed();
	void Require(bool result);
};

class DwDemangler : public DemangleBase
{
public:	
	SubstituteList mTemplateList;
	bool mIsFirstName;
	int mTemplateDepth;	
	bool mOmitSubstituteAdd;	
	bool mCaptureTargetType;
	bool mFunctionPopSubstitute;
	bool mRawDemangle;	

public:	
	bool DemangleEnd();
	bool DemangleArrayType(StringImpl& outName);
	bool DemangleBuiltinType(StringImpl& outName);
	bool DemangleFunctionType(StringImpl& outName);
	bool DemangleSourceName(StringImpl& outName);
	bool DemangleRefQualifier(StringImpl& outName);	
	bool DemangleType(StringImpl& outName);
	bool DemangleNestedName(StringImpl& outName);
	bool DemangleCVQualifiers(StringImpl& outName);		
	bool DemangleOperatorName(StringImpl& outName);
	bool DemangleExprPriamry(StringImpl& outName);
	bool DemangleTemplateArgPack(StringImpl& outName);
	bool DemangleTemplateArg(StringImpl& outName);
	bool DemangleTemplateArgs(StringImpl& outName);
	bool DemangleUnqualifiedName(StringImpl& outName);
	bool DemangleInternalName(StringImpl& outName);
	bool DemangleSubstitution(StringImpl& outName);
	bool DemangleTemplateParam(StringImpl& outName);
	bool DemangleUnscopedName(StringImpl& outName);
	bool DemangleClassEnumType(StringImpl& outName);
	bool DemangleLocalName(StringImpl& outName);
	bool DemangleName(StringImpl& outName, bool* outHasTemplateArgs = NULL);
	bool DemangleFunction(StringImpl& outName);

public:	
	DwDemangler();

	String Demangle(const StringImpl& mangledName);
};

class MsDemangler : public DemangleBase
{
public:
	int mCurIdx;

public:
	int DemangleNumber();
	bool DemangleString(StringImpl& outName);
	bool DemangleTemplateName(StringImpl& outName, String* primaryName = NULL);
	bool DemangleCV(StringImpl& outName);
	bool DemangleModifiedType(StringImpl& outName, bool isPtr);
	bool DemangleType(StringImpl& outName);
	bool DemangleScopedName(StringImpl& outName, String* primaryName = NULL);
	bool DemangleName(StringImpl& outName);
	static bool IsData(const StringImpl& mangledName);

public:
	MsDemangler();

	String Demangle(const StringImpl& mangledName);
};

class MsDemangleScanner : public DemangleBase
{
public:
	bool mIsData;

public:
	int DemangleNumber();
	bool DemangleString();
	bool DemangleTemplateName();
	bool DemangleCV();
	bool DemangleModifiedType(bool isPtr);
	bool DemangleType();
	bool DemangleScopedName();
	bool DemangleName();
	
public:
	MsDemangleScanner();

	void Process(const StringImpl& mangledName);
};

class BfDemangler
{
public:
	enum Flags
	{
		Flag_None,
		Flag_CaptureTargetType = 1,
		Flag_RawDemangle = 2,
		Flag_BeefFixed = 4
	};

public:
	static String Demangle(const StringImpl& mangledName, DbgLanguage language, Flags flags = Flag_None);
	static bool IsData(const StringImpl& mangledName);
};

NS_BF_END