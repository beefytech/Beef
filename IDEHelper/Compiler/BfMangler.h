#pragma once

#include "BfSystem.h"
#include "BeefySysLib/util/SLIList.h"
#include "BeefySysLib/util/SizedArray.h"

NS_BF_BEGIN

class BfType;
class BfFieldInstance;
class BfCustomAttributes;

class BfMangler
{
public:
	enum MangleKind
	{
		MangleKind_GNU,
		MangleKind_Microsoft_32,
		MangleKind_Microsoft_64
	};

	struct NameSubstitute
	{
	public:
		enum Kind
		{
			Kind_None,
			Kind_NamespaceAtom,
			Kind_TypeDefNameAtom,
			Kind_TypeInstName,
			Kind_TypeGenericArgs,
			Kind_MethodName,
			Kind_Prefix,
			Kind_PrimitivePrefix,
			Kind_GenericParam			
		};

	public:
		union
		{
			int mExtendsIdx;
			int mExtendsTypeId;
			BfTypeCode mExtendsTypeCode;
		};
		Kind mKind;
		union
		{
			void* mParam;

			BfAtom* mAtom;
			BfTypeDef* mTypeDef;
			BfType* mType;
			BfTypeInstance* mTypeInst;
			BfMethodInstance* mMethodInstance;
			const char* mPrefix;
		};		

	public:
		NameSubstitute(Kind kind, void* param)
		{
			mExtendsIdx = -1;
			mKind = kind;
			mParam = param;
		}

		NameSubstitute(Kind kind, void* param, int extendsId)
		{
			mExtendsIdx = extendsId;
			mKind = kind;
			mParam = param;
		}

		NameSubstitute()
		{
			mExtendsIdx = -1;
			mKind = Kind_None;
			mParam = NULL;
		}
	};

	static void Mangle(StringImpl& outStr, MangleKind mangleKind, BfType* type, BfModule* module = NULL);
	static void Mangle(StringImpl& outStr, MangleKind mangleKind, BfMethodInstance* methodRef);
	static void Mangle(StringImpl& outStr, MangleKind mangleKind, BfFieldInstance* fieldInstance);
	static void MangleMethodName(StringImpl& outStr, MangleKind mangleKind, BfTypeInstance* type, const StringImpl& methodName);
	static void MangleStaticFieldName(StringImpl& outStr, MangleKind mangleKind, BfTypeInstance* owner, const StringImpl& fieldName, BfType* fieldType = NULL);
	static void HandleCustomAttributes(BfCustomAttributes* customAttributes, BfIRConstHolder* constHolder, BfModule* module, StringImpl& name, bool& isCMangle, bool& isCPPMangle);
	static void HandleParamCustomAttributes(BfAttributeDirective* attributes, bool isReturn, bool& isConst);
};

class BfGNUMangler : public BfMangler
{
public:				
	class MangleContext
	{
	public:
		BfModule* mModule;
		bool mCCompat;
		bool mCPPMangle;
		bool mInArgs;
		bool mPrefixObjectPointer;
		llvm::SmallVector<NameSubstitute, 32> mSubstituteList;

	public:
		MangleContext()
		{
			mModule = NULL;
			mCCompat = false;
			mCPPMangle = false;
			mInArgs = false;
			mPrefixObjectPointer = false;
		}
	};
	
	static int ParseSubIdx(StringImpl& name, int strIdx);
	static void AddSubIdx(StringImpl& name, int strIdx);
	static void AddSizedString(StringImpl& name, const StringImpl& addStr);
	static BfTypeCode GetPrimTypeAt(MangleContext& mangleContext, StringImpl& name, int strIdx);
	static void AddPrefix(MangleContext& mangleContext, StringImpl& name, int startIdx, const char* prefix);	
	static void FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub, int& curMatchIdx, bool& matchFailed);
	static void FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInst, int& curMatchIdx, bool& matchFailed);
	static void FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub);	

public:
	static void MangleTypeInst(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInst, BfTypeInstance* postfixTypeInst = NULL, bool* isEndOpen = NULL);

	static void Mangle(MangleContext& mangleContext, StringImpl& name, BfType* type, BfType* postfixType = NULL);
	static String Mangle(BfType* type, BfModule* module = NULL);
	static String Mangle(BfMethodInstance* methodRef);	
	static String MangleMethodName(BfTypeInstance* type, const StringImpl& methodName);
	static String Mangle(BfFieldInstance* methodRef);
	static String MangleStaticFieldName(BfTypeInstance* type, const StringImpl& fieldName);
};

class BfMSMangler : public BfMangler
{
public:
	class MangleContext
	{
	public:
		bool mIsSafeMangle;		

		BfModule* mModule;
		bool mCCompat;
		bool mCPPMangle;
		bool mIs64Bit;		
		SizedArray<NameSubstitute, 10> mSubstituteList;
		SizedArray<BfType*, 10> mSubstituteTypeList;

		bool mWantsGroupStart;
		bool mInArgs;
		bool mInRet;

	public:
		MangleContext()
		{
			mIsSafeMangle = false;			
			mModule = NULL;
			mCCompat = false;
			mIs64Bit = false;
			mCPPMangle = false;
			mWantsGroupStart = false;
			mInArgs = false;
			mInRet = false;
		}

		BfModule* GetUnreifiedModule();		
	};

	static void AddGenericArgs(MangleContext& mangleContext, StringImpl& name, const SizedArrayImpl<BfType*>& genericArgs, int numOuterGenericParams = 0);
	
	static void AddStr(MangleContext& mangleContext, StringImpl& name, const StringImpl& str);
	static void Mangle(MangleContext& mangleContext, StringImpl& name, BfType* type, bool useTypeList = false, bool isConst = false);
	static void Mangle(MangleContext& mangleContext, StringImpl& name, BfTypeInstance* typeInst, bool isAlreadyStarted, bool isOuterType = false);
	static void MangleConst(MangleContext& mangleContext, StringImpl& name, int64 val);

	void AddPrefix(MangleContext & mangleContext, StringImpl& name, int startIdx, const char * prefix);

	static void AddSubIdx(StringImpl& name, int strIdx);	
	static void AddTypeStart(MangleContext & mangleContext, StringImpl& name, BfType* type);	
	static bool FindOrCreateNameSub(MangleContext& mangleContext, StringImpl& name, const NameSubstitute& newNameSub);

public:
	static void Mangle(StringImpl& outStr, bool is64Bit, BfType* type, BfModule* module = NULL);
	static void Mangle(StringImpl& outStr, bool is64Bit, BfMethodInstance* methodRef);
	static void Mangle(StringImpl& outStr, bool is64Bit, BfFieldInstance* fieldInstance);
	static void MangleMethodName(StringImpl& outStr, bool is64Bit, BfTypeInstance* type, const StringImpl& methodName);
	static void MangleStaticFieldName(StringImpl& outStr, bool is64Bit, BfTypeInstance* type, const StringImpl& fieldName, BfType* fieldType = NULL);
};

// A "safe mangle" is used for reconnecting deleted types to their previous id. We make sure we never force a 
//  PopulateType with this kind of mangle, as referenced types may be deleted and marked as undefined
class BfSafeMangler : public BfMSMangler
{
public:
	static String Mangle(BfType* type, BfModule* module = NULL);
};

NS_BF_END