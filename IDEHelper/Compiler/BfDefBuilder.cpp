#include "BeefySysLib/util/AllocDebug.h"

#include "BfDefBuilder.h"
#include "BeefySysLib/util/Hash.h"
#include "BfParser.h"
#include "BfResolvePass.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BfUtil.h"

USING_NS_BF;

static void HashNode(HashContext& ctx, BfAstNode* astNode)
{
	// Add a separator marker so two nodes hashed back-to-back won't mix
	uint8 marker = 0;
	ctx.Mixin(&marker, 1);

	if (astNode == NULL)
		return;
	ctx.Mixin(astNode->GetSourceData()->mSrc + astNode->GetSrcStart(), astNode->GetSrcLength());
}

static void HashNode(HashContext& ctx, BfAstNode* astNode, BfAstNode* endNode)
{
	// Add a separator marker so two nodes hashed back-to-back won't mix
	uint8 marker = 0;
	ctx.Mixin(&marker, 1);

	if (endNode == NULL)
	{
		HashNode(ctx, astNode);
		return;
	}
	ctx.Mixin(astNode->GetSourceData()->mSrc + astNode->GetSrcStart(), endNode->GetSrcStart() - astNode->GetSrcStart());
}

static Val128 HashNode(BfAstNode* astNode)
{
	if (astNode == NULL)
		return Val128();
	return Hash128(astNode->GetSourceData()->mSrc + astNode->GetSrcStart(), astNode->GetSrcLength());
}

static Val128 HashNode(BfAstNode* astNode, BfAstNode* endNode)
{
	if (endNode == NULL)
		return HashNode(astNode);
	return Hash128(astNode->GetSourceData()->mSrc + astNode->GetSrcStart(), endNode->GetSrcStart() - astNode->GetSrcStart());
}

static Val128 HashString(const StringImpl& str, const Val128& seed = Val128())
{
	return Hash128(str.c_str(), (int)str.length(), seed);
}

BfDefBuilder::BfDefBuilder(BfSystem* bfSystem)
{
	mPassInstance = NULL;
	mSystem = bfSystem;
	mCurTypeDef = NULL;
	mCurActualTypeDef = NULL;
	mFullRefresh = false;
	mResolvePassData = NULL;
	mCurSource = NULL;

	mFullHashCtx = NULL;
	mSignatureHashCtx = NULL;
}

BfDefBuilder::~BfDefBuilder()
{
	for (auto& usingNamespace : mNamespaceSearch)	
		mSystem->ReleaseAtomComposite(usingNamespace);	
}

void BfDefBuilder::Process(BfPassInstance* passInstance, BfSource* bfSource, bool fullRefresh)
{
	BF_ASSERT((mSystem->mCurSystemLockThreadId == 0) || (mSystem->mCurSystemLockThreadId == BfpThread_GetCurrentId()));

	String fileName;
	BfParser* parser = bfSource->ToParser();
	if (parser != NULL)
		fileName = parser->mFileName;
	BP_ZONE_F("BfDefBuilder::Process %s", fileName.c_str());
	BfLogSys(parser->mSystem, "DefBuilder::Process parser %p\n", parser);

	if (mResolvePassData == NULL)
		mSystem->mNeedsTypesHandledByCompiler = true;

	mPassInstance = passInstance;
	
	bool isAutocomplete = false;
	if ((mResolvePassData != NULL) && (mResolvePassData->mAutoComplete != NULL))
	{		
		mCurSource = bfSource;
		Visit(bfSource->mRootNode);		
		mCurSource = NULL;
		mPassInstance = NULL;
		return;
	}

	mFullRefresh = fullRefresh;
 
	if (bfSource->mPrevRevision != NULL)
	{
		for (auto typeDef : bfSource->mPrevRevision->mTypeDefs)
		{
			BF_ASSERT((typeDef->mDefState >= BfTypeDef::DefState_New) && (typeDef->mDefState < BfTypeDef::DefState_Deleted));
			typeDef->mDefState = BfTypeDef::DefState_AwaitingNewVersion;
		}
	}

	{
		BP_ZONE("RootNode");
		if (bfSource->mRootNode != NULL)
		{
			mCurSource = bfSource;
			Visit(bfSource->mRootNode);
			mCurSource = NULL;
		}
	}
	
	if (bfSource->mPrevRevision != NULL)
	{
		for (auto typeDef : bfSource->mPrevRevision->mTypeDefs)
		{
			if (!typeDef->mIsCombinedPartial)
			{
				if (typeDef->mDefState == BfTypeDef::DefState_AwaitingNewVersion)
				{
					BfLogSys(parser->mSystem, "DefBuilder::Process deleting typeDef %p\n", typeDef);
					typeDef->mName->mAtomUpdateIdx = ++mSystem->mAtomUpdateIdx;
				 	typeDef->mDefState = BfTypeDef::DefState_Deleted;
					mSystem->mTypeMapVersion++;
				}
			}
		}
	}

	mPassInstance = NULL;
}

void BfDefBuilder::Visit(BfIdentifierNode* identifier)
{
	if (mResolvePassData != NULL)	
		mResolvePassData->mExteriorAutocompleteCheckNodes.push_back(identifier);	
}

// We need to be aware of the startNode because the reducer adds specifiers like 'static' and 'public' AFTER the method has
//  already been handled, so we need to ignore that space while determining if we're "inside" this method or not during
//  autocompletion
bool BfDefBuilder::WantsNode(BfAstNode* wholeNode, BfAstNode* startNode, int addLen)
{	
	if ((mResolvePassData == NULL) || (mResolvePassData->mParser->mCursorIdx == -1))
		return true;

	// We need to get all nodes when we get fixits because the cursor could be either before or after fields with
	//  warnings, but still on the correct line
	//if (mResolvePassData->mResolveType == BfResolveType_GetFixits)
		//return true;

	addLen++;
	if ((mResolvePassData->mParser->mCursorIdx >= wholeNode->GetSrcStart()) && (mResolvePassData->mParser->mCursorIdx < wholeNode->GetSrcEnd() + addLen))
	{
		if ((startNode == NULL) || (mResolvePassData->mParser->mCursorIdx >= startNode->GetSrcStart()))
			return true;
	}
	return false; 
}

static int sGenericParamIdx = 0;

void BfDefBuilder::ParseGenericParams(BfGenericParamsDeclaration* genericParamsDecl, BfGenericConstraintsDeclaration* genericConstraints, Array<BfGenericParamDef*>& genericParams, Array<BfExternalConstraintDef>* externConstraintDefs, int outerGenericSize)
{		
	if (genericParamsDecl != NULL)
	{
		int startIdx = (int)genericParams.size();
		for (int genericParamIdx = 0; genericParamIdx < (int)genericParamsDecl->mGenericParams.size(); genericParamIdx++)
		{
			BfIdentifierNode* genericParamNode = genericParamsDecl->mGenericParams[genericParamIdx];
			String name = genericParamNode->ToString();

			for (int checkParamsIdx = startIdx; checkParamsIdx < (int)genericParams.size(); checkParamsIdx++)
			{
				if (genericParams[checkParamsIdx]->mName == name)
				{
					Fail("Duplicate generic param name", genericParamNode);
				}
			}

			auto checkTypeDef = mCurTypeDef;
			while (checkTypeDef != NULL)
			{
				if (&genericParams != &checkTypeDef->mGenericParamDefs)
				{					
					for (int checkParamsIdx = 0; checkParamsIdx < (int)checkTypeDef->mGenericParamDefs.size(); checkParamsIdx++)
					{
						if (checkTypeDef->mGenericParamDefs[checkParamsIdx]->mName == name)
						{							
							mPassInstance->Warn(0, "Generic param name has same name as generic param from outer type", genericParamNode);
						}
					}
				}

				checkTypeDef = checkTypeDef->mOuterType;
			}
			
			auto genericParamDef = new BfGenericParamDef();			
			genericParamDef->mName = name;
			genericParamDef->mNameNodes.Add(genericParamNode);
			genericParamDef->mGenericParamFlags = BfGenericParamFlag_None;			
			genericParams.push_back(genericParamDef);			
		}			
	}

	if (genericConstraints == NULL)
		return;

	for (BfAstNode* genericConstraintNode : genericConstraints->mGenericConstraints)
	{
		auto genericConstraint = BfNodeDynCast<BfGenericConstraint>(genericConstraintNode);
		if (genericConstraint == NULL)
			continue;
		if (genericConstraint->mTypeRef == NULL)
			continue;

		BfIdentifierNode* nameNode = NULL;
		BfGenericParamDef* genericParamDef = NULL;
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(genericConstraint->mTypeRef))
		{
			nameNode = namedTypeRef->mNameNode;
			String findName = nameNode->ToString();
			for (int genericParamIdx = outerGenericSize; genericParamIdx < (int)genericParams.size(); genericParamIdx++)
			{
				auto checkGenericParam = genericParams[genericParamIdx];
				if (checkGenericParam->mName == findName)
					genericParamDef = checkGenericParam;
			}
		}
		
		BfConstraintDef* constraintDef = genericParamDef;

		if (genericParamDef == NULL)
		{
			if (externConstraintDefs == NULL)
			{
				Fail("Cannot find generic parameter in constraint", genericConstraint->mTypeRef);

				if (genericParams.IsEmpty())
					continue;

				genericParamDef = genericParams[0];
				constraintDef = genericParamDef;
			}
			else
			{
				externConstraintDefs->Add(BfExternalConstraintDef());
				BfExternalConstraintDef* externConstraintDef = &externConstraintDefs->back();
				externConstraintDef->mTypeRef = genericConstraint->mTypeRef;
				constraintDef = externConstraintDef;
			}
		}
		
		if (genericParamDef != NULL)
			genericParamDef->mNameNodes.Add(nameNode);

		for (BfAstNode* constraintNode : genericConstraint->mConstraintTypes)
		{
			String name;

			if ((constraintNode->IsA<BfNamedTypeReference>()) || (constraintNode->IsA<BfTokenNode>()))
			{
				name = constraintNode->ToString();
			}
			else if (auto tokenPairNode = BfNodeDynCast<BfTokenPairNode>(constraintNode))
			{
				name = tokenPairNode->mLeft->ToString() + tokenPairNode->mRight->ToString();
			}

			bool hasEquals = (genericConstraint->mColonToken != NULL) && (genericConstraint->mColonToken->mToken == BfToken_AssignEquals);			

			if (!name.empty())
			{
				if ((name == "class") || (name == "struct") || (name == "struct*") || (name == "const") || (name == "var"))
				{
					int prevFlags = constraintDef->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr);
					if (prevFlags != 0)					
					{
						String prevFlagName;
						if (prevFlags & BfGenericParamFlag_Class)
							prevFlagName = "class";
						else if (prevFlags & BfGenericParamFlag_Struct)
							prevFlagName = "struct";
						else //
							prevFlagName = "struct*";

						if (prevFlagName == name)
							Fail(StrFormat("Cannot specify '%s' twice", prevFlagName.c_str()), constraintNode);
						else
							Fail(StrFormat("Cannot specify both '%s' and '%s'", prevFlagName.c_str(), name.c_str()), constraintNode);
						return;
					}

					if (name == "class")
						constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Class);
					else if (name == "struct")
						constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Struct);
					else if (name == "struct*")
						constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_StructPtr);
					else if (name == "const")
						constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Const);
					else //if (name == "var")
						constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Var);
										
					continue;
				}				
				else if (name == "new")
				{
					constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_New);
					continue;
				}				
				else if (name == "delete")
				{
					constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Delete);
					continue;
				}
			}			

			if (auto genericOpConstraint = BfNodeDynCast<BfGenericOperatorConstraint>(constraintNode))
			{
				// Ok
			}
			else
			{
				auto constraintType = BfNodeDynCast<BfTypeReference>(constraintNode);
				if (constraintType == NULL)
				{
					Fail("Invalid constraint", constraintNode);
					return;
				}				
			}

			if (hasEquals)
			{				
				if (constraintDef->mConstraints.IsEmpty())
				{
					constraintDef->mGenericParamFlags = (BfGenericParamFlags)(constraintDef->mGenericParamFlags | BfGenericParamFlag_Equals);
				}
				else
				{
					Fail("Type assignment must be the first constraint", genericConstraint->mColonToken);
				}
			}			
			
			constraintDef->mConstraints.Add(constraintNode);
		}
	}
}

BfProtection BfDefBuilder::GetProtection(BfTokenNode* protectionToken)
{		
	if (protectionToken == NULL)
	{
		if (mCurTypeDef->mTypeCode == BfTypeCode_Interface)
			return BfProtection_Public;
		else
			return BfProtection_Private;
	}

	if (protectionToken->GetToken() == BfToken_Public)
		return BfProtection_Public;	
	if (protectionToken->GetToken() == BfToken_Protected)
		return BfProtection_Protected;
	return BfProtection_Private;
}

void BfDefBuilder::Visit(BfConstructorDeclaration* ctorDeclaration)
{
	if (!WantsNode(ctorDeclaration, NULL))
		return;

	Visit((BfMethodDeclaration*)ctorDeclaration);
}

BfMethodDef* BfDefBuilder::CreateMethodDef(BfMethodDeclaration* methodDeclaration, BfMethodDef* outerMethodDef)
{
	BfMethodDef* methodDef;

	if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
	{
		auto operatorDef = new BfOperatorDef();
		operatorDef->mOperatorDeclaration = operatorDecl;
		operatorDef->mIsOperator = true;
		methodDef = operatorDef;		
	}
	else
		methodDef = new BfMethodDef();
	
	methodDef->mDeclaringType = mCurTypeDef;
	methodDef->mMethodDeclaration = methodDeclaration;
	methodDef->mExplicitInterface = methodDeclaration->mExplicitInterface;
	methodDef->mReturnTypeRef = methodDeclaration->mReturnType;	
	methodDef->mProtection = GetProtection(methodDeclaration->mProtectionSpecifier);	
	methodDef->mIsReadOnly = methodDeclaration->mReadOnlySpecifier != NULL;
	methodDef->mIsStatic = methodDeclaration->mStaticSpecifier != NULL;
	methodDef->mIsVirtual = methodDeclaration->mVirtualSpecifier != NULL;	
	methodDef->mIsPartial = methodDeclaration->mPartialSpecifier != NULL;	
	methodDef->mIsNew = methodDeclaration->mNewSpecifier != NULL;
	methodDef->mIsMutating = methodDeclaration->mMutSpecifier != NULL;
	methodDef->mIsExtern = methodDeclaration->mExternSpecifier != NULL;
	methodDef->mBody = methodDeclaration->mBody;

	if (methodDeclaration->mThisToken != NULL)
		methodDef->mMethodType = BfMethodType_Extension;

	HashContext signatureHashCtx;
	HashNode(signatureHashCtx, methodDeclaration, methodDef->mBody);
	//methodDef->mSignatureHash = signatureHashCtx.Finish128();
	if (mSignatureHashCtx != NULL)
		HashNode(*mSignatureHashCtx, methodDeclaration, methodDef->mBody);

	HashContext fullHash;
	HashNode(fullHash, methodDeclaration);
	methodDef->mFullHash = fullHash.Finish128();

	if (methodDeclaration->mVirtualSpecifier != NULL)
	{
		methodDef->mIsOverride = methodDeclaration->mVirtualSpecifier->GetToken() == BfToken_Override;
		methodDef->mIsAbstract = methodDeclaration->mVirtualSpecifier->GetToken() == BfToken_Abstract;

		if (methodDef->mIsOverride)
			mCurTypeDef->mHasOverrideMethods = true;

		if (methodDeclaration->mVirtualSpecifier->GetToken() == BfToken_Concrete)
		{
			methodDef->mIsConcrete = true;
			methodDef->mIsVirtual = false;
		}

		if (mCurTypeDef->mTypeCode == BfTypeCode_Interface)
		{
			if ((methodDef->mIsConcrete) && (!mCurTypeDef->mIsConcrete))
				Fail("Only interfaces declared as 'concrete' can be declare methods as 'concrete'. Consider adding 'concrete' to the interface declaration.", methodDeclaration->mVirtualSpecifier);
			//if (!methodDef->mIsConcrete)
				//Fail(StrFormat("Interfaces methods cannot be declared as '%s'", methodDeclaration->mVirtualSpecifier->ToString().c_str()), methodDeclaration->mVirtualSpecifier);
		}
		else
		{
			if (methodDef->mIsConcrete)
				Fail("Only interfaces methods can be declared as 'concrete'", methodDeclaration->mVirtualSpecifier);
		}

		if (methodDef->mIsAbstract)
		{
			if ((!mCurTypeDef->mIsAbstract) && (mCurTypeDef->mTypeCode != BfTypeCode_Interface))
				Fail("Method is abstract but it is contained in non-abstract class", methodDeclaration);
			if (methodDeclaration->mBody != NULL)
				Fail("Abstract method cannot declare a body", methodDeclaration);
		}
	}
	else
	{
		methodDef->mIsOverride = false;
		methodDef->mIsAbstract = false;
	}		

	if (mCurTypeDef->mTypeCode == BfTypeCode_Interface)
	{
		if ((!methodDef->mIsConcrete) && (!methodDef->mIsStatic) && (!methodDef->mGenericParams.empty()) && (methodDef->mProtection == BfProtection_Public))
			methodDef->mIsVirtual = true;		
	}

	if (auto ctorDeclaration = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
	{
		methodDef->mIsMutating = true;
		methodDef->mMethodType = BfMethodType_Ctor;
		if (methodDef->mIsStatic)
			methodDef->mName = "__BfStaticCtor";
		else
			methodDef->mName = "__BfCtor";
	}
	else if (methodDeclaration->IsA<BfDestructorDeclaration>())
	{		
		methodDef->mMethodType = BfMethodType_Dtor;
		if (methodDef->mIsStatic)
			methodDef->mName = "__BfStaticDtor";
		else
		{
			mCurTypeDef->mDtorDef = methodDef;
			methodDef->mName = "~this";
			if (!methodDef->mIsVirtual)
			{
				methodDef->mIsVirtual = true;
				methodDef->mIsOverride = true;
			}
		}
	}
	else if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
	{
		methodDef->mMethodType = BfMethodType_Operator;
		/*if (propertyDecl->mStaticSpecifier == NULL)
		{
		Fail("Operators must be declared as static", methodDeclaration);
		}*/		

		String declError;
		if (methodDef->mProtection != BfProtection_Public)
		{
			declError = "'public'";
			methodDef->mProtection = BfProtection_Public; // Fix it
		}
		if (operatorDecl->mAssignOp != BfAssignmentOp_None)
		{
			if (methodDef->mIsStatic)
			{
				Fail("Assignment operator must not be declared 'static'", operatorDecl->mStaticSpecifier);
			}
		}
		else
		{
			if (!methodDef->mIsStatic)
			{
				if (!declError.empty())
					declError += " and ";
				declError += "'static'";
				methodDef->mIsStatic = true; // Fix it
			}
		}
		if (!declError.empty())
		{
			Fail(StrFormat("Operator must be declared %s", declError.c_str()), operatorDecl->mOperatorToken);
		}
	}
	else if ((mCurTypeDef->mIsDelegate) || (mCurTypeDef->mIsFunction))
	{
		methodDef->mName = "Invoke";
		methodDef->mMethodType = BfMethodType_Normal;
		methodDef->mProtection = BfProtection_Public;
		methodDef->mIsStatic = mCurTypeDef->mIsFunction;

		auto attributes = mCurTypeDef->mTypeDeclaration->mAttributes;
		while (attributes != NULL)
		{
			if (attributes->mAttributeTypeRef != NULL)
			{
				auto typeRefName = attributes->mAttributeTypeRef->ToString();

				if (typeRefName == "StdCall")
					methodDef->mCallingConvention = BfCallingConvention_Stdcall;
			}
			attributes = attributes->mNextAttribute;
		}		
	}
	else if (methodDeclaration->mMixinSpecifier != NULL)
	{
		if (methodDeclaration->mNameNode != NULL)
			methodDef->mName = methodDeclaration->mNameNode->ToString();
		methodDef->mMethodType = BfMethodType_Mixin;				
	}
	else
	{
		if (methodDeclaration->mNameNode != NULL)
			methodDef->mName = methodDeclaration->mNameNode->ToString();		
		methodDef->mMethodType = BfMethodType_Normal;

		if (methodDeclaration->mThisToken != NULL)
		{
			methodDef->mMethodType = BfMethodType_Extension;
			mCurTypeDef->mHasExtensionMethods = true;
		}
	}

	if (outerMethodDef != NULL)
	{
		for (auto genericParam : outerMethodDef->mGenericParams)
		{
			auto* copiedGenericParamDef = new BfGenericParamDef();
			*copiedGenericParamDef = *genericParam;
			methodDef->mGenericParams.Add(copiedGenericParamDef);
		}
	}

	int outerGenericSize = 0;
	if (outerMethodDef != NULL)
		outerGenericSize = (int)outerMethodDef->mGenericParams.size();

	if ((methodDef->mMethodType == BfMethodType_Normal) ||
		(methodDef->mMethodType == BfMethodType_Operator) ||
		(methodDef->mMethodType == BfMethodType_Mixin) ||
		(methodDef->mMethodType == BfMethodType_Extension))
	{
		ParseGenericParams(methodDeclaration->mGenericParams, methodDeclaration->mGenericConstraintsDeclaration, methodDef->mGenericParams, &methodDef->mExternalConstraints, outerGenericSize);
	}
	else
	{
		if (methodDeclaration->mGenericParams != NULL)
			Fail("Generic parameters are only allowed on normal methods", methodDeclaration->mGenericParams);
		if (methodDeclaration->mGenericConstraintsDeclaration != NULL)
			Fail("Generic constraints are only allowed on normal methods", methodDeclaration->mGenericConstraintsDeclaration);
	}

	bool didDefaultsError = false;
	bool hadParams = false;
	bool hasDefault = false;
	for (int paramIdx = 0; paramIdx < (int)methodDeclaration->mParams.size(); paramIdx++)
	{				
		BfParameterDeclaration* paramDecl = methodDeclaration->mParams[paramIdx];		

		auto paramDef = new BfParameterDef();
		paramDef->mParamDeclaration = paramDecl;
		if (paramDecl->mNameNode != NULL)
			paramDef->mName = paramDecl->mNameNode->ToString();
		paramDef->mTypeRef = paramDecl->mTypeRef;
		paramDef->mMethodGenericParamIdx = mSystem->GetGenericParamIdx(methodDef->mGenericParams, paramDef->mTypeRef);
		if (paramDecl->mModToken == NULL)
			paramDef->mParamKind = BfParamKind_Normal;		
		else //
			paramDef->mParamKind = BfParamKind_Params;

		if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(paramDef->mTypeRef))
		{
			if (dotTypeRef->mDotToken->mToken == BfToken_DotDotDot)
			{
				if (paramIdx == (int)methodDeclaration->mParams.size() - 1)
					paramDef->mParamKind = BfParamKind_VarArgs;
				else
					Fail("Varargs specifier must be the last parameter", methodDef->mParams[paramIdx - 1]->mParamDeclaration);
			}
		}

		if (paramDef->mParamDeclaration != NULL)
		{
			if (paramDef->mParamKind == BfParamKind_Params)
			{
				hadParams = true;
			}
			else if (hadParams)
			{								
				methodDef->mParams[paramIdx - 1]->mParamKind = BfParamKind_Normal; 
				hadParams = false;
				Fail("Params parameter must be the last parameter", methodDef->mParams[paramIdx - 1]->mParamDeclaration);
			}

			if (paramDef->mParamDeclaration->mInitializer != NULL)
				hasDefault = true;
			else if (hasDefault)
			{
				if (!didDefaultsError)
					Fail("Optional parameters must appear after all required parameters", methodDef->mParams[paramIdx - 1]->mParamDeclaration);
				didDefaultsError = true;
			}
		}

		methodDef->mParams.push_back(paramDef);
	}	

	ParseAttributes(methodDeclaration->mAttributes, methodDef);	
	return methodDef;
}

BfError* BfDefBuilder::Fail(const StringImpl& errorStr, BfAstNode* refNode)
{
	auto error = mPassInstance->Fail(errorStr, refNode);
	if (error != NULL)
		error->mProject = mCurSource->mProject;
	return error;
}

void BfDefBuilder::Visit(BfMethodDeclaration* methodDeclaration)
{
	if (mCurTypeDef == NULL)
	{
		return;
	}

	int addLen = 0;
	if ((methodDeclaration->mNameNode == NULL) && (methodDeclaration->mBody == NULL))
		addLen = 1;	

	mSystem->CheckLockYield();

	bool wantsBody = true;

	BfAstNode* startNode = methodDeclaration->mReturnType;
	if (startNode == NULL)
		startNode = methodDeclaration->mMixinSpecifier;
	if (startNode == NULL)
	{
		if (auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
			startNode = ctorDecl->mThisToken;
		else if (auto ctorDecl = BfNodeDynCast<BfDestructorDeclaration>(methodDeclaration))
			startNode = ctorDecl->mTildeToken;
	}
	if (startNode == NULL)
		startNode = methodDeclaration->mOpenParen;
	if (!WantsNode(methodDeclaration, startNode, addLen))
	{
		if (!WantsNode(methodDeclaration, NULL, addLen))
			return;
		wantsBody = false;
	}

	auto methodDef = CreateMethodDef(methodDeclaration);
	methodDef->mWantsBody = wantsBody;
	if (methodDef->mMethodType == BfMethodType_Operator)	
		mCurTypeDef->mOperators.push_back((BfOperatorDef*)methodDef);	
	mCurTypeDef->mMethods.push_back(methodDef);	

	if (methodDef->mCommutableKind == BfCommutableKind_Forward)
	{				
		auto revMethodDef = CreateMethodDef(methodDeclaration);
		revMethodDef->mWantsBody = wantsBody;
		if (revMethodDef->mMethodType == BfMethodType_Operator)
			mCurTypeDef->mOperators.push_back((BfOperatorDef*)revMethodDef);

		if (revMethodDef->mParams.size() >= 2)
		{
			BF_SWAP(revMethodDef->mParams[0], revMethodDef->mParams[1]);
		}
		revMethodDef->mCommutableKind = BfCommutableKind_Reverse;		
		mCurTypeDef->mMethods.push_back(revMethodDef);		
	}	
}

void BfDefBuilder::ParseAttributes(BfAttributeDirective* attributes, BfMethodDef* methodDef)
{
	while (attributes != NULL)
	{		
		if (attributes->mAttributeTypeRef != NULL)
		{
			auto typeRefName = attributes->mAttributeTypeRef->ToString();

			if (typeRefName == "CLink")
				methodDef->mCLink = true;
			else if (typeRefName == "StdCall")
				methodDef->mCallingConvention = BfCallingConvention_Stdcall;
			else if (typeRefName == "CVarArgs")
				methodDef->mCallingConvention = BfCallingConvention_CVarArgs;
			else if (typeRefName == "Inline")
				methodDef->mAlwaysInline = true;
			else if (typeRefName == "AllowAppend")
				methodDef->mHasAppend = true;
			else if (typeRefName == "Checked")
				methodDef->mCheckedKind = BfCheckedKind_Checked;
			else if (typeRefName == "Unchecked")
				methodDef->mCheckedKind = BfCheckedKind_Unchecked;
			else if (typeRefName == "Export")
			{
				mCurTypeDef->mIsAlwaysInclude = true;
				methodDef->mImportKind = BfImportKind_Export;
			}
			else if (typeRefName == "Import")
			{
				methodDef ->mImportKind = BfImportKind_Import_Unknown;
				if (!attributes->mArguments.IsEmpty())
				{
					if (auto literalExpr = BfNodeDynCast<BfLiteralExpression>(attributes->mArguments[0]))
					{
						if (literalExpr->mValue.mTypeCode == BfTypeCode_CharPtr)
						{
							String filePath = *literalExpr->mValue.mString;
							methodDef->mImportKind = BfMethodDef::GetImportKindFromPath(filePath);							
						}
					}
				}
			}
			else if (typeRefName == "NoReturn")
				methodDef->mNoReturn = true;
			else if (typeRefName == "SkipCall")
				methodDef->mIsSkipCall = true;
			else if (typeRefName == "NoShow")
				methodDef->mIsNoShow = true;
			else if (typeRefName == "NoDiscard")
				methodDef->mIsNoDiscard = true;
			else if (typeRefName == "Commutable")
			{
				if (methodDef->mParams.size() != 2)
				{
					Fail("Commutable attributes can only be applied to methods with two arguments", attributes->mAttributeTypeRef);
				}
				else
				{
					methodDef->mCommutableKind = BfCommutableKind_Forward;
				}
			}			
		}

		attributes = attributes->mNextAttribute;
	}
}

void BfDefBuilder::ParseAttributes(BfAttributeDirective* attributes, BfTypeDef* typeDef)
{
	while (attributes != NULL)
	{		
		if (attributes->mAttributeTypeRef != NULL)
		{
			auto typeRefName = attributes->mAttributeTypeRef->ToString();

			if (typeRefName == "AlwaysInclude")
				typeDef->mIsAlwaysInclude = true;
			else if (typeRefName == "NoDiscard")
				typeDef->mIsNoDiscard = true;
		}

		attributes = attributes->mNextAttribute;
	}
}

void BfDefBuilder::Visit(BfPropertyDeclaration* propertyDeclaration)
{
	int addLen = 0;
	if (propertyDeclaration->mTypeRef == NULL)
		addLen = 1;

	bool wantsBody = true;
	if (!WantsNode(propertyDeclaration, propertyDeclaration->mTypeRef, addLen))
	{
		if (!WantsNode(propertyDeclaration, NULL, addLen))
			return;		
		// The cursor is inside some exterior specifiers, don't process body
		wantsBody = false;		
	}	

	if (propertyDeclaration->mConstSpecifier != NULL)
	{
		Fail("Const properties are not allowed", propertyDeclaration->mConstSpecifier);
	}

	HashNode(*mSignatureHashCtx, propertyDeclaration, propertyDeclaration->mDefinitionBlock);	

	BfPropertyDef* propertyDef = new BfPropertyDef();
	mCurTypeDef->mProperties.push_back(propertyDef);
	propertyDef->mProtection = GetProtection(propertyDeclaration->mProtectionSpecifier);	
	propertyDef->mIdx = (int)mCurTypeDef->mProperties.size() - 1;
	propertyDef->mIsConst = false;
	propertyDef->mIsStatic = propertyDeclaration->mStaticSpecifier != NULL;
	propertyDef->mIsReadOnly = propertyDeclaration->mReadOnlySpecifier != NULL;
	if (propertyDeclaration->mNameNode != NULL)
		propertyDef->mName = propertyDeclaration->mNameNode->ToString();
	else if (propertyDeclaration->IsA<BfIndexerDeclaration>())
	{		
		propertyDef->mName = "[]";
	}
	propertyDef->mTypeRef = propertyDeclaration->mTypeRef;
	propertyDef->mInitializer = NULL;
	propertyDef->mFieldDeclaration = propertyDeclaration;
	propertyDef->mDeclaringType = mCurTypeDef;

	if (auto varType = BfNodeDynCast<BfLetTypeReference>(propertyDef->mTypeRef))		
		propertyDef->mIsReadOnly = true;

	//HashNode(*mSignatureHashCtx, propertyDeclaration, propertyDeclaration->mDefinitionBlock);

	//mCurTypeDef->mSignatureHash = HashNode(propertyDeclaration, propertyDeclaration->mDefinitionBlock, mCurTypeDef->mSignatureHash);	
	if (propertyDeclaration->mDefinitionBlock == NULL) // To differentiate between autocompleting partial property if it transitions to a field
	{
		//mCurTypeDef->mSignatureHash = HashString("nullprop", mCurTypeDef->mSignatureHash);
		mSignatureHashCtx->MixinStr("nullprop");
	}

	auto indexerDeclaration = BfNodeDynCast<BfIndexerDeclaration>(propertyDeclaration);	
	
	bool isAbstract = false;
	if (propertyDeclaration->mVirtualSpecifier != NULL)	
		isAbstract = propertyDeclaration->mVirtualSpecifier->GetToken() == BfToken_Abstract;

	bool needsAutoProperty = mCurTypeDef->HasAutoProperty(propertyDeclaration);
	if (needsAutoProperty)
	{
		BfFieldDef* fieldDef = new BfFieldDef();
		fieldDef->mDeclaringType = mCurTypeDef;
		fieldDef->mFieldDeclaration = propertyDeclaration;
		fieldDef->mProtection = BfProtection_Hidden;
		fieldDef->mIsStatic = propertyDef->mIsStatic;
		fieldDef->mTypeRef = propertyDef->mTypeRef;
		if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(fieldDef->mTypeRef))
			fieldDef->mTypeRef = refTypeRef->mElementType;
		fieldDef->mName = mCurTypeDef->GetAutoPropertyName(propertyDeclaration);		
		fieldDef->mIdx = (int)mCurTypeDef->mFields.size();
		mCurTypeDef->mFields.push_back(fieldDef);

		mCurTypeDef->mSignatureHash = HashString(fieldDef->mName, mCurTypeDef->mSignatureHash);
	}

	for (auto methodDeclaration : propertyDeclaration->mMethods)
	{
		HashNode(*mSignatureHashCtx, methodDeclaration->mAttributes);
		HashNode(*mSignatureHashCtx, methodDeclaration->mProtectionSpecifier);
		HashNode(*mSignatureHashCtx, methodDeclaration->mNameNode);
		HashNode(*mSignatureHashCtx, methodDeclaration->mMutSpecifier);		

		if (!wantsBody)
			continue;
		if (!WantsNode(methodDeclaration))
			continue;

		auto methodDef = new BfMethodDef();
		mCurTypeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = mCurTypeDef;
		methodDef->mMethodDeclaration = methodDeclaration;
		methodDef->mProtection = propertyDef->mProtection;
		methodDef->mWantsBody = (methodDeclaration->mBody != NULL) && (WantsNode(methodDeclaration->mBody));

		if (methodDeclaration->mProtectionSpecifier != NULL)
		{
			BfProtection newProtection = GetProtection(methodDeclaration->mProtectionSpecifier);
			if (newProtection > methodDef->mProtection)
				Fail(StrFormat("the accessibility modifier of the 'get' accessor must be more restrictive than the property or indexer '%s'", propertyDef->mName.c_str()),
					methodDeclaration->mProtectionSpecifier);
			methodDef->mProtection = newProtection;
		}

		methodDef->mIsMutating = methodDeclaration->mMutSpecifier != NULL;
		methodDef->mIsAbstract = isAbstract;
		methodDef->mIsStatic = propertyDef->mIsStatic;
		methodDef->mIsVirtual = propertyDeclaration->mVirtualSpecifier != NULL;
		methodDef->mIsExtern = propertyDeclaration->mExternSpecifier != NULL;
		methodDef->mMethodDeclaration = methodDeclaration;
		methodDef->mExplicitInterface = propertyDeclaration->mExplicitInterface;
		HashContext propHashCtx;
		HashNode(propHashCtx, methodDeclaration->mNameNode);
		HashNode(propHashCtx, methodDeclaration->mAttributes);
		HashNode(propHashCtx, methodDeclaration->mProtectionSpecifier);
		HashNode(propHashCtx, methodDeclaration->mMutSpecifier);
		HashNode(propHashCtx, propertyDeclaration, propertyDeclaration->mDefinitionBlock);
		//methodDef->mSignatureHash = propHashCtx.Finish128();
		methodDef->mFullHash = HashNode(propertyDeclaration);
		if (propertyDeclaration->mVirtualSpecifier != NULL)
			methodDef->mIsOverride = propertyDeclaration->mVirtualSpecifier->GetToken() == BfToken_Override;
		else
			methodDef->mIsOverride = false;
		methodDef->mIsNew = propertyDeclaration->mNewSpecifier != NULL;
		
		if (indexerDeclaration != NULL)
		{
			for (int paramIdx = 0; paramIdx < (int)indexerDeclaration->mParams.size(); paramIdx++)
			{
				auto paramDef = new BfParameterDef();
				BfParameterDeclaration* paramDecl = indexerDeclaration->mParams[paramIdx];
				paramDef->mParamDeclaration = paramDecl;
				paramDef->mName = paramDecl->mNameNode->ToString();
				paramDef->mTypeRef = paramDecl->mTypeRef;
				paramDef->mMethodGenericParamIdx = mSystem->GetGenericParamIdx(methodDef->mGenericParams, paramDef->mTypeRef);
				methodDef->mParams.push_back(paramDef);
			}
		}

		String methodName;		
		if (auto propExprBody = BfNodeDynCast<BfPropertyBodyExpression>(propertyDeclaration->mDefinitionBlock))
			methodName = "get";
		else if ((methodDeclaration != NULL) && (methodDeclaration->mNameNode != NULL))
			methodName = methodDeclaration->mNameNode->ToString();
		
		if (methodName == "get")
		{
			methodDef->mName = "get__";
			if (propertyDeclaration->mNameNode != NULL)
				methodDef->mName += propertyDeclaration->mNameNode->ToString();
			methodDef->mReturnTypeRef = propertyDeclaration->mTypeRef;
			methodDef->mMethodType = BfMethodType_PropertyGetter;
			if (propertyDeclaration->mReadOnlySpecifier != NULL)
				methodDef->mIsReadOnly = true;
			propertyDef->mMethods.Add(methodDef);
		}
		else if (methodName == "set")
		{
			methodDef->mName = "set__";
			if (propertyDeclaration->mNameNode != NULL)
			 	methodDef->mName += propertyDeclaration->mNameNode->ToString();
			methodDef->mMethodType = BfMethodType_PropertySetter;

			if (BfNodeDynCast<BfTokenNode>(methodDeclaration->mBody) != NULL)
				methodDef->mIsMutating = true; // Don't require "set mut;", just "set;"
			
			auto paramDef = new BfParameterDef();		
			paramDef->mName = "value";
			if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(propertyDeclaration->mTypeRef))		
			 	paramDef->mTypeRef = refTypeRef->mElementType;		
			else
			 	paramDef->mTypeRef = propertyDeclaration->mTypeRef;
			methodDef->mParams.push_back(paramDef);					
			propertyDef->mMethods.Add(methodDef);
		}
		else
		{
			// Parse had an error, leave this block as an unnamed method			
		}

		methodDef->mBody = methodDeclaration->mBody;
		ParseAttributes(methodDeclaration->mAttributes, methodDef);
	}
}

void BfDefBuilder::Visit(BfFieldDeclaration* fieldDeclaration)
{	
	mSystem->CheckLockYield();

	int endingAdd = 1;// Add '1' for autocompletion of 'new' initializer
	
	if (!WantsNode(fieldDeclaration, NULL, endingAdd))
	{
		return;
	}

	// This check is a bit of a hack to determine the difference between a "MemberType mMember" and a proper case entry of "mMember(TupleType)"
	bool isEnumEntryDecl = fieldDeclaration->IsA<BfEnumEntryDeclaration>();

	auto fieldDef = new BfFieldDef();
	mCurTypeDef->mFields.push_back(fieldDef);	
	fieldDef->mFieldDeclaration = fieldDeclaration;
	fieldDef->mDeclaringType = mCurTypeDef;
	if (fieldDeclaration->mNameNode != NULL)
		fieldDef->mName = fieldDeclaration->mNameNode->ToString();
	fieldDef->mProtection = GetProtection(fieldDeclaration->mProtectionSpecifier);	
	if (mCurTypeDef->mIsPartial)
		fieldDef->mProtection = BfProtection_Public;
	else if (isEnumEntryDecl)
		fieldDef->mProtection = BfProtection_Public;	
	fieldDef->mIsReadOnly = fieldDeclaration->mReadOnlySpecifier != NULL;	
	fieldDef->mIsInline = (fieldDeclaration->mReadOnlySpecifier != NULL) && (fieldDeclaration->mReadOnlySpecifier->GetToken() == BfToken_Inline);
	fieldDef->mIsExtern = (fieldDeclaration->mExternSpecifier != NULL);
	fieldDef->mIsConst = (fieldDeclaration->mConstSpecifier != NULL) || (isEnumEntryDecl);
	fieldDef->mIsStatic = (fieldDeclaration->mStaticSpecifier != NULL) || fieldDef->mIsConst;
	fieldDef->mIsVolatile = (fieldDeclaration->mVolatileSpecifier != NULL);
	fieldDef->mTypeRef = fieldDeclaration->mTypeRef;
	if (auto varType = BfNodeDynCast<BfLetTypeReference>(fieldDef->mTypeRef))		
		fieldDef->mIsReadOnly = true;
	
	if ((mCurTypeDef->mTypeCode == BfTypeCode_Enum) && (fieldDef->mTypeRef != NULL) && (!fieldDef->mIsStatic))
	{
		// This check is a bit of a hack to determine the difference between a "MemberType mMember" and a proper case entry of "mMember(TupleType)"
		if (!isEnumEntryDecl)
		{
			Fail("Non-static field declarations are not allowed in enums", fieldDeclaration);
		}
	}

	fieldDef->mIdx = (int)mCurTypeDef->mFields.size() - 1;	
	fieldDef->mInitializer = fieldDeclaration->mInitializer;

	//mCurTypeDef->mSignatureHash = HashNode(fieldDeclaration, mCurTypeDef->mSignatureHash);
	HashNode(*mSignatureHashCtx, fieldDeclaration);
}

void BfDefBuilder::Visit(BfEnumCaseDeclaration* enumCaseDeclaration)
{
	if (!WantsNode(enumCaseDeclaration, enumCaseDeclaration->mCaseToken, 0))
	{
		return;
	}

	for (int entryIdx = 0; entryIdx < (int)enumCaseDeclaration->mEntries.size(); entryIdx++)
	{
		auto caseEntry = enumCaseDeclaration->mEntries[entryIdx];
		Visit(caseEntry);
	}
}

BfFieldDef* BfDefBuilder::AddField(BfTypeDef* typeDef, BfTypeReference* fieldType, const StringImpl& fieldName)
{
	BfFieldDef* fieldDef = new BfFieldDef();	
	fieldDef->mDeclaringType = typeDef;
	fieldDef->mTypeRef = fieldType;
	fieldDef->mName = fieldName;
	fieldDef->mIdx = (int)typeDef->mFields.size();	
	typeDef->mFields.push_back(fieldDef);	
	return fieldDef;
}

BfMethodDef* BfDefBuilder::AddMethod(BfTypeDef* typeDef, BfMethodType methodType, BfProtection protection, bool isStatic, const StringImpl& name)
{
	BF_ASSERT(typeDef->mTypeCode != BfTypeCode_TypeAlias);

	auto methodDef = new BfMethodDef();
	methodDef->mIdx = (int)typeDef->mMethods.size();
	typeDef->mMethods.push_back(methodDef);	
	methodDef->mDeclaringType = typeDef;
	methodDef->mMethodType = methodType;
	if (name.empty())
	{
		if (methodType == BfMethodType_Ctor)
		{
			if (isStatic)
				methodDef->mName = "__BfStaticCtor";
			else
				methodDef->mName = "__BfCtor";
		}
		else if (methodType == BfMethodType_CtorNoBody)
		{
			methodDef->mName = "__BfCtorNoBody";
			methodDef->mNoReflect = true;
		}
		else if (methodType == BfMethodType_CtorClear)
		{
			methodDef->mName = "__BfCtorClear";
			methodDef->mNoReflect = true;
		}
		else if (methodType == BfMethodType_Dtor)
		{
			if (isStatic)
			{
				methodDef->mName = "__BfStaticDtor";
			}
			else
			{
				methodDef->mName = "~this";
				methodDef->mIsVirtual = true;
				methodDef->mIsOverride = true;
				BF_ASSERT(typeDef->mDtorDef == NULL);
				typeDef->mDtorDef = methodDef;
			}
		}
		else
		{
			BF_FATAL("Method name expected");
		}		
	}
	else
		methodDef->mName = name;
	methodDef->mProtection = protection;
	methodDef->mIsStatic = isStatic;
	return methodDef;
}

BfMethodDef* BfDefBuilder::AddDtor(BfTypeDef* typeDef)
{
	auto methodDef = new BfMethodDef();
	typeDef->mMethods.push_back(methodDef);
	methodDef->mDeclaringType = typeDef;
	methodDef->mName = "~this";
	methodDef->mProtection = BfProtection_Public;
	methodDef->mMethodType = BfMethodType_Dtor;
	methodDef->mIsVirtual = true;
	methodDef->mIsOverride = true;
	BF_ASSERT(typeDef->mDtorDef == NULL);
	typeDef->mDtorDef = methodDef;
	return methodDef;
}

void BfDefBuilder::AddDynamicCastMethods(BfTypeDef* typeDef)
{	
	// 
	{
		auto methodDef = new BfMethodDef();
		methodDef->mIdx = (int)typeDef->mMethods.size();
		typeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = typeDef;
		methodDef->mName = BF_METHODNAME_DYNAMICCAST;
		methodDef->mProtection = BfProtection_Protected;
		methodDef->mIsStatic = false;
		methodDef->mMethodType = BfMethodType_Normal;
		methodDef->mIsVirtual = true;
		methodDef->mIsOverride = true;

		auto paramDef = new BfParameterDef();
		paramDef->mName = "id";
		paramDef->mTypeRef = typeDef->mSystem->mDirectInt32TypeRef;
		methodDef->mParams.push_back(paramDef);
		methodDef->mReturnTypeRef = typeDef->mSystem->mDirectObjectTypeRef;
		methodDef->mNoReflect = true;
	}

	// 
	{
		auto methodDef = new BfMethodDef();
		methodDef->mIdx = (int)typeDef->mMethods.size();
		typeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = typeDef;
		methodDef->mName = BF_METHODNAME_DYNAMICCAST_INTERFACE;
		methodDef->mProtection = BfProtection_Protected;
		methodDef->mIsStatic = false;
		methodDef->mMethodType = BfMethodType_Normal;
		methodDef->mIsVirtual = true;
		methodDef->mIsOverride = true;

		auto paramDef = new BfParameterDef();
		paramDef->mName = "id";
		paramDef->mTypeRef = typeDef->mSystem->mDirectInt32TypeRef;
		methodDef->mParams.push_back(paramDef);
		methodDef->mReturnTypeRef = typeDef->mSystem->mDirectObjectTypeRef;
		methodDef->mNoReflect = true;
	}
}

void BfDefBuilder::AddParam(BfMethodDef* methodDef, BfTypeReference* typeRef, const StringImpl& paramName)
{	
	auto paramDef = new BfParameterDef();
	paramDef->mName = paramName;	
	paramDef->mTypeRef = typeRef;
	methodDef->mParams.push_back(paramDef);
}

void BfDefBuilder::Visit(BfTypeDeclaration* typeDeclaration)
{
	BF_ASSERT(typeDeclaration->GetSourceData() == mCurSource->mSourceData);

	if ((typeDeclaration->mTypeNode != NULL) && (typeDeclaration->mNameNode == NULL))
		return;

    /*if (typeDeclaration->mNameNode != NULL)
        OutputDebugStrF("Decl: %s\n", typeDeclaration->mNameNode->ToString().c_str());*/

	bool isAutoCompleteTempType = false;
	if (mResolvePassData != NULL)
	{
		isAutoCompleteTempType = (mResolvePassData->mAutoComplete != NULL);

		int cursorIdx = mResolvePassData->mParser->mCursorIdx;		
		if (typeDeclaration->Contains(cursorIdx, 1, 0))
		{
			// Within bounds
		}
		else if (cursorIdx != -1)
			return;
	}

	int curLine = 0;
	int curColumn = 0;

	auto bfParser = mCurSource->ToParser();
	if (bfParser != NULL)
	{
		int srcStart = typeDeclaration->GetSrcStart();
		auto* jumpEntry = bfParser->mJumpTable + (srcStart / PARSER_JUMPTABLE_DIVIDE);
		if (jumpEntry->mCharIdx > srcStart)
			jumpEntry--;
		curLine = jumpEntry->mLineNum;
		int curSrcPos = jumpEntry->mCharIdx;
		curColumn = 0;
		while (curSrcPos < srcStart)
		{
			if (bfParser->mSrc[curSrcPos] == '\n')
			{
				curLine++;
				curColumn = 0;
			}
			else
				curColumn++;
			curSrcPos++;
		}
	}

	auto outerTypeDef = mCurTypeDef;
	auto actualOuterTypeDef = mCurActualTypeDef;

	while ((outerTypeDef != NULL) && (outerTypeDef->IsGlobalsContainer()))
		outerTypeDef = outerTypeDef->mOuterType;
	while ((actualOuterTypeDef != NULL) && (actualOuterTypeDef->IsGlobalsContainer()))
		actualOuterTypeDef = actualOuterTypeDef->mOuterType;

	SetAndRestoreValue<BfTypeDef*> prevTypeDef(mCurTypeDef, new BfTypeDef());
	SetAndRestoreValue<BfTypeDef*> prevActualTypeDef(mCurActualTypeDef, mCurTypeDef);

	mCurTypeDef->mSystem = mSystem;
	mCurTypeDef->mProject = mCurSource->mProject;
	mCurTypeDef->mNamespace = mNamespace;
	mSystem->AddNamespaceUsage(mCurTypeDef->mNamespace, mCurTypeDef->mProject);
	if (typeDeclaration->mTypeNode == NULL)
	{		
		mCurTypeDef->mIsPartial = true;
		mCurTypeDef->mIsExplicitPartial = true;
	}
	if (typeDeclaration->mNameNode == NULL)
	{
		// Global
		mCurTypeDef->mName = mSystem->mGlobalsAtom;
		mCurTypeDef->mName->Ref();
		BF_ASSERT(mCurTypeDef->mSystem != NULL);
	}
	else
	{
		mCurTypeDef->mName = mSystem->GetAtom(typeDeclaration->mNameNode->ToString());
		if (mCurTypeDef->mName->mIsSystemType)
		{
			Fail(StrFormat("Type name '%s' is reserved", typeDeclaration->mNameNode->ToString().c_str()), typeDeclaration->mNameNode);
		}
	}
	
	BfLogSys(mCurSource->mSystem, "DefBuilder %p %p TypeDecl:%s\n", mCurTypeDef, mCurSource, mCurTypeDef->mName->ToString().mPtr);

	mCurTypeDef->mProtection = (outerTypeDef == NULL) ? BfProtection_Public : BfProtection_Private;	
	if (typeDeclaration->mProtectionSpecifier != NULL)
	{
		if ((outerTypeDef == NULL) && (typeDeclaration->mProtectionSpecifier->GetToken() != BfToken_Public))
		{
			//CS1527
			Fail("Elements defined in a namespace cannot be explicitly declared as private or protected", typeDeclaration->mProtectionSpecifier);
		}
		else
		{
			mCurTypeDef->mProtection = GetProtection(typeDeclaration->mProtectionSpecifier);
		}
	}
	if (typeDeclaration->mAttributes != NULL)
		ParseAttributes(typeDeclaration->mAttributes, mCurTypeDef);

	for (auto& baseClass : typeDeclaration->mBaseClasses)
		mCurTypeDef->mBaseTypes.push_back(baseClass);

	HashContext fullHashCtx;
	HashContext signatureHashCtx;

	SetAndRestoreValue<HashContext*> prevFullHashCtx(mFullHashCtx, &fullHashCtx);
	SetAndRestoreValue<HashContext*> prevSignatureHashCtx(mSignatureHashCtx, &signatureHashCtx);
	
	if (bfParser != NULL)
	{
		mFullHashCtx->MixinStr(bfParser->mFileName);
		mFullHashCtx->Mixin(bfParser->mParserData->mMD5Hash);
	}
	HashNode(*mSignatureHashCtx, typeDeclaration->mTypeNode);
	for (auto& baseClassNode : typeDeclaration->mBaseClasses)
		HashNode(*mSignatureHashCtx, baseClassNode);
	HashNode(*mSignatureHashCtx, typeDeclaration->mAttributes);
	HashNode(*mSignatureHashCtx, typeDeclaration->mAbstractSpecifier);
	HashNode(*mSignatureHashCtx, typeDeclaration->mSealedSpecifier);	
	HashNode(*mSignatureHashCtx, typeDeclaration->mProtectionSpecifier);
	HashNode(*mSignatureHashCtx, typeDeclaration->mPartialSpecifier);
	HashNode(*mSignatureHashCtx, typeDeclaration->mNameNode);
	HashNode(*mSignatureHashCtx, typeDeclaration->mGenericParams);
	HashNode(*mSignatureHashCtx, typeDeclaration->mGenericConstraintsDeclaration);
	
	HashNode(*mFullHashCtx, typeDeclaration);
	
	// Allow internal preprocessor flags to change a full hash change
	if (bfParser != NULL)
	{
		// Note- we insert strings from these HashSets in bucket-order, which is stable
		//  for the same insertion order, but is not truly order independent. Shouldn't matter.
		fullHashCtx.Mixin(bfParser->mParserData->mDefines_Def.size());
		for (auto& defStr : bfParser->mParserData->mDefines_Def)
			fullHashCtx.MixinStr(defStr);
		fullHashCtx.Mixin(bfParser->mParserData->mDefines_NoDef.size());
		for (auto& defStr : bfParser->mParserData->mDefines_NoDef)
			fullHashCtx.MixinStr(defStr);
	}

	// We need to hash the position of the declaration in the file so we can rebuild the debug info with revised line numbers
	int hashPos[2] = { curLine, curColumn };
	mFullHashCtx->Mixin(hashPos);

	if (auto typeAliasDeclaration = BfNodeDynCast<BfTypeAliasDeclaration>(typeDeclaration))
	{
		HashNode(*mSignatureHashCtx, typeAliasDeclaration->mAliasToType);
	}

	//TODO:
	//int randomCrap = rand();
	//HASH128_MIXIN(mCurTypeDef->mFullHash, randomCrap);

	// To cause a type rebuild when we change pragma settings or #if resolves	
	//mCurTypeDef->mFullHash = Hash128(&typeDeclaration->mParser->mStateHash, sizeof(Val128), mCurTypeDef->mFullHash);

	//BfParser* bfParser = typeDeclaration->mParser;

	// Hash in ignored warnings that occur before our type declaration
	std::set<int> ignoredWarningSet;
	auto warningItr = bfParser->mParserData->mWarningEnabledChanges.begin();
	while (warningItr != bfParser->mParserData->mWarningEnabledChanges.end())
	{
		int srcIdx = warningItr->mKey;
		if (srcIdx >= typeDeclaration->GetSrcStart())
			break;

		auto& warningEntry = warningItr->mValue;
		if (!warningEntry.mEnable)
		{
			ignoredWarningSet.insert(warningEntry.mWarningNumber);
		}
		else
		{
			auto setItr = ignoredWarningSet.find(warningEntry.mWarningNumber);
			if (setItr != ignoredWarningSet.end())
				ignoredWarningSet.erase(setItr);
		}
		++warningItr;
	}
	for (auto ignoredWarning : ignoredWarningSet)
	{
		mFullHashCtx->Mixin(ignoredWarning);
	}

	// Hash in relative mPreprocessorIgnoredSectionStarts positions that occur within the type declaration
	auto ignoredSectionItr = bfParser->mPreprocessorIgnoredSectionStarts.upper_bound(typeDeclaration->GetSrcStart());
	while (ignoredSectionItr != bfParser->mPreprocessorIgnoredSectionStarts.end())
	{
		int srcIdx = *ignoredSectionItr;
		if (srcIdx >= typeDeclaration->GetSrcEnd())
			break;
		int relSrcIdx = srcIdx - typeDeclaration->GetSrcStart();
		mFullHashCtx->Mixin(relSrcIdx);
		++ignoredSectionItr;
	}

	SizedArray<BfAtom*, 6> expandedName;
	if (mCurTypeDef->mName != mSystem->mGlobalsAtom)
		expandedName.push_back(mCurTypeDef->mName);
	// Expanded name should container outer namespace parts
	if (outerTypeDef != NULL)
	{
		mCurTypeDef->mNestDepth = outerTypeDef->mNestDepth + 1;
		mCurTypeDef->mNamespaceSearch = outerTypeDef->mNamespaceSearch;
		mCurTypeDef->mStaticSearch = outerTypeDef->mStaticSearch;

		for (auto outerGenericParamDef : outerTypeDef->mGenericParamDefs)
		{
			BfGenericParamDef* copiedGenericParamDef = new BfGenericParamDef();
			*copiedGenericParamDef = *outerGenericParamDef;
			mCurTypeDef->mGenericParamDefs.Add(copiedGenericParamDef);
		}		

		BfTypeDef* parentType = outerTypeDef;
		while (parentType != NULL)
		{
			expandedName.Insert(0, parentType->mNameEx);
			parentType = parentType->mOuterType;
		}
	}
	else
	{
		BF_ASSERT(mCurTypeDef->mNamespaceSearch.size() == 0);
		mCurTypeDef->mNamespaceSearch = mNamespaceSearch;
		mCurTypeDef->mStaticSearch = mStaticSearch;
	}
	
	// We need to mix the namespace search into the signature hash because it can change how type references are resolved
	for (auto& usingName : mCurTypeDef->mNamespaceSearch)
	{		
		mSystem->RefAtomComposite(usingName);
		mSignatureHashCtx->MixinStr(usingName.ToString());
	}
	for (auto& usingName : mCurTypeDef->mStaticSearch)
	{		
		HashNode(*mSignatureHashCtx, usingName);
	}
	
	if ((typeDeclaration->mPartialSpecifier != NULL) && (!isAutoCompleteTempType))
	{
		mCurTypeDef->mIsExplicitPartial = true;
		mCurTypeDef->mIsPartial = true;
	}
	
	bool isExtension = false;
	if ((typeDeclaration->mTypeNode != NULL) && (typeDeclaration->mTypeNode->GetToken() == BfToken_Extension))
	{
		mCurTypeDef->mIsPartial = true;
		isExtension = true;
	}

	BfAtomComposite fullName;
	if (!expandedName.IsEmpty())
		fullName.Set(mCurTypeDef->mNamespace.mParts, mCurTypeDef->mNamespace.mSize, &expandedName[0], (int)expandedName.size());
	else
		fullName = mCurTypeDef->mNamespace;
	
	String fullNameStr = fullName.ToString();
	mCurTypeDef->mHash = 0xBEEF123; // Salt the hash
	for (char c : fullNameStr)
		mCurTypeDef->mHash = ((mCurTypeDef->mHash ^ c) << 4) - mCurTypeDef->mHash;
	mCurTypeDef->mFullName = fullName;	

	BfTypeDef* prevRevisionTypeDef = NULL;
	BfTypeDef* otherDefinitionTypeDef = NULL;
	
	int numGenericParams = 0;
	if (typeDeclaration->mGenericParams != NULL)
		numGenericParams = (int)typeDeclaration->mGenericParams->mGenericParams.size();
	if (outerTypeDef != NULL)
		numGenericParams += (int)outerTypeDef->mGenericParamDefs.size();
	
	mCurTypeDef->mSource = mCurSource;
	mCurTypeDef->mSource->mRefCount++;
	mCurTypeDef->mTypeDeclaration = typeDeclaration;
	mCurTypeDef->mIsAbstract = (typeDeclaration->mAbstractSpecifier != NULL) && (typeDeclaration->mAbstractSpecifier->GetToken() == BfToken_Abstract);
	mCurTypeDef->mIsConcrete = (typeDeclaration->mAbstractSpecifier != NULL) && (typeDeclaration->mAbstractSpecifier->GetToken() == BfToken_Concrete);
	mCurTypeDef->mIsStatic = typeDeclaration->mStaticSpecifier != NULL;
	mCurTypeDef->mIsDelegate = false;
	mCurTypeDef->mIsFunction = false;

	BfToken typeToken = BfToken_None;
	if (typeDeclaration->mTypeNode != NULL)
		typeToken = typeDeclaration->mTypeNode->GetToken();

	if (typeDeclaration->mTypeNode == NULL)
	{
		// Globals
		mCurTypeDef->mTypeCode = BfTypeCode_Struct;
	}
	else if (typeToken == BfToken_Class)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_Object;
	}
	else if (typeToken == BfToken_Delegate)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_Object;
		mCurTypeDef->mIsDelegate = true;
	}
	else if (typeToken == BfToken_Function)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_Struct;
		mCurTypeDef->mIsFunction = true;
	}
	else if (typeToken == BfToken_Interface)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_Interface;
	}
	else if (typeToken == BfToken_Enum)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_Enum;
	}
	else if (typeToken == BfToken_TypeAlias)
	{
		mCurTypeDef->mTypeCode = BfTypeCode_TypeAlias;
	}
	else if (typeToken == BfToken_Struct)
		mCurTypeDef->mTypeCode = BfTypeCode_Struct;
	else if (typeToken == BfToken_Extension)
		mCurTypeDef->mTypeCode = BfTypeCode_Extension;
	else
		BF_FATAL("Unknown type token");

	if (!isAutoCompleteTempType)
	{
		BfTypeDef* prevDef = NULL;
		
		auto itr = mSystem->mTypeDefs.TryGet(fullName);		
		while (itr)
		{
			BfTypeDef* checkTypeDef = *itr;
		
			if (checkTypeDef->mDefState == BfTypeDef::DefState_Deleted)
			{
				itr.MoveToNextHashMatch();
				continue;
			}

			if ((checkTypeDef->NameEquals(mCurTypeDef)) &&				
				(checkTypeDef->mGenericParamDefs.size() == numGenericParams) &&
				(mCurTypeDef->mProject == checkTypeDef->mProject))
			{
				if (checkTypeDef->mIsCombinedPartial)
				{
					// Ignore					
				}
				else
				{
					if (checkTypeDef->mDefState == BfTypeDef::DefState_AwaitingNewVersion)
					{
						// We don't allow "new revision" semantics if the 'isExtension' state changes, or
						//  if the outer type did not use "new revision" semantics (for isExtension change on itself or other outer type)
						
						bool isCompatible = (isExtension == (checkTypeDef->mTypeCode == BfTypeCode_Extension)) &&
							(checkTypeDef->mTypeCode == mCurTypeDef->mTypeCode) &&
							(checkTypeDef->mIsDelegate == mCurTypeDef->mIsDelegate) &&
							(checkTypeDef->mIsFunction == mCurTypeDef->mIsFunction) &&
							(checkTypeDef->mOuterType == actualOuterTypeDef);
						
						if (isCompatible)
						{
							if (prevRevisionTypeDef == NULL)
							{
								prevRevisionTypeDef = checkTypeDef;
								prevDef = checkTypeDef;
							}
						}
					}
					else
					{
						//otherDefinitionTypeDef = prevRevisionTypeDef;
					}
				}
			}
			itr.MoveToNextHashMatch();
		}

		bool doInsertNew = true;
		if (prevRevisionTypeDef != NULL)
		{
			mCurTypeDef->mIsNextRevision = true;			
			bfParser->mTypeDefs.Add(prevRevisionTypeDef);

			if (prevRevisionTypeDef->mDefState == BfTypeDef::DefState_AwaitingNewVersion)
			{
				if (prevRevisionTypeDef->mNextRevision != NULL)
					delete prevRevisionTypeDef->mNextRevision;
				prevRevisionTypeDef->mNextRevision = mCurTypeDef;
				BF_ASSERT(mCurTypeDef->mSystem != NULL);
				mCurActualTypeDef = prevRevisionTypeDef;
				doInsertNew = false;
			}
			else
			{
				if (prevRevisionTypeDef->mNextRevision != NULL)
					prevRevisionTypeDef = prevRevisionTypeDef->mNextRevision;

				prevRevisionTypeDef = NULL;
			}
		}
		else
		{
			mSystem->TrackName(mCurTypeDef);
			bfParser->mTypeDefs.Add(mCurTypeDef);			
		}

		if (doInsertNew)
		{
			mSystem->mTypeDefs.Add(mCurTypeDef);
			mSystem->mTypeMapVersion++;
		}
	}
	else
	{
		mCurTypeDef->mIsNextRevision = true; // We don't track name
		mResolvePassData->mAutoCompleteTempTypes.push_back(mCurTypeDef);
	}

	// Insert name into there
	mCurTypeDef->mOuterType = actualOuterTypeDef;
	//dottedName = mCurTypeDef->mName;
	if ((outerTypeDef != NULL) && (!isAutoCompleteTempType))
	{
		outerTypeDef->mNestedTypes.push_back(mCurActualTypeDef);
	}

	BfLogSysM("Creating TypeDef %p Hash:%d from TypeDecl: %p Source: %p ResolvePass: %d AutoComplete:%d\n", mCurTypeDef, mSystem->mTypeDefs.GetHash(mCurTypeDef), typeDeclaration, 
		typeDeclaration->GetSourceData(), mResolvePassData != NULL, isAutoCompleteTempType);				

	int outerGenericSize = 0;
	if (mCurTypeDef->mOuterType != NULL)
		outerGenericSize = (int)mCurTypeDef->mOuterType->mGenericParamDefs.size();
	ParseGenericParams(typeDeclaration->mGenericParams, typeDeclaration->mGenericConstraintsDeclaration, mCurTypeDef->mGenericParamDefs, NULL, outerGenericSize);
	
	BF_ASSERT(mCurTypeDef->mNameEx == NULL);
	
	if (mCurTypeDef->mGenericParamDefs.size() != 0)
	{
		mCurTypeDef->mNameEx = mSystem->GetAtom(StrFormat("%s`%d", mCurTypeDef->mName->mString.mPtr, numGenericParams));		
	}
	else
	{
		mCurTypeDef->mNameEx = mCurTypeDef->mName;
		mCurTypeDef->mNameEx->mRefCount++;
	}
	if (!fullName.IsEmpty())
	{
		mCurTypeDef->mFullNameEx = fullName;
		mCurTypeDef->mFullNameEx.mParts[mCurTypeDef->mFullNameEx.mSize - 1] = mCurTypeDef->mNameEx;
	}
	
	if (auto defineBlock = BfNodeDynCast<BfBlock>(typeDeclaration->mDefineNode))
	{
		for (auto& member : *defineBlock)
		{
			VisitChildNoRef(member);
		}
	}
	else if (auto defineTokenNode = BfNodeDynCast<BfTokenNode>(typeDeclaration->mDefineNode))
	{
		if (defineTokenNode->GetToken() == BfToken_Semicolon)
		{
			mCurTypeDef->mIsOpaque = true;
		}
	}

	FinishTypeDef(mCurTypeDef->mTypeCode == BfTypeCode_Enum);

	// Map methods into the correct index from previous revision
	if (prevRevisionTypeDef != NULL)
	{
		BF_ASSERT(mCurTypeDef->mTypeCode == prevRevisionTypeDef->mTypeCode);

		if ((mCurTypeDef->mFullHash == prevRevisionTypeDef->mFullHash) && (!mFullRefresh))
		{
			BfLogSys(bfParser->mSystem, "DefBuilder deleting typeDef with no changes %p\n", prevRevisionTypeDef);
			prevRevisionTypeDef->mDefState = BfTypeDef::DefState_Defined;			
			BF_ASSERT(prevRevisionTypeDef->mNextRevision == mCurTypeDef);
			prevRevisionTypeDef->mNextRevision = NULL;
			delete mCurTypeDef;
			mCurTypeDef = NULL;
		}
		else if (mCurTypeDef->mSignatureHash != prevRevisionTypeDef->mSignatureHash)
			prevRevisionTypeDef->mDefState = BfTypeDef::DefState_Signature_Changed;
		else if (mCurTypeDef->mInlineHash != prevRevisionTypeDef->mInlineHash)
			prevRevisionTypeDef->mDefState = BfTypeDef::DefState_InlinedInternals_Changed;
		else
			prevRevisionTypeDef->mDefState = BfTypeDef::DefState_Internals_Changed;		
	}
	
	// There's a new type with this name...
	if ((prevRevisionTypeDef == NULL) && (!isAutoCompleteTempType))
	{
		mCurTypeDef->mName->mAtomUpdateIdx = ++mSystem->mAtomUpdateIdx;
	}	
}

void BfDefBuilder::FinishTypeDef(bool wantsToString)
{
	auto bfSource = mCurTypeDef->mSource;
	bool isAlias = mCurTypeDef->mTypeCode == BfTypeCode_TypeAlias;
	bool hasCtor = false;	
	bool needsDefaultCtor = (mCurTypeDef->mTypeCode != BfTypeCode_Interface) && (!mCurTypeDef->mIsStatic) && (!isAlias);
	bool hasDefaultCtor = false;
	bool hasStaticCtor = false;
	bool hasDtor = false;
	bool hasStaticDtor = false;
	bool hasMarkMethod = false;
	bool hasStaticMarkMethod = false;
	bool hasDynamicCastMethod = false;
	bool hasToStringMethod = false;		
	bool needsEqualsMethod = ((mCurTypeDef->mTypeCode == BfTypeCode_Struct) || (mCurTypeDef->mTypeCode == BfTypeCode_Enum)) && (!mCurTypeDef->mIsStatic);
	bool hasEqualsMethod = false;
	
	bool needsStaticInit = false;
	for (int methodIdx = 0; methodIdx < (int)mCurTypeDef->mMethods.size(); methodIdx++)
	{
		auto method = mCurTypeDef->mMethods[methodIdx];

		if (method->mMethodType == BfMethodType_Ctor)
		{
			if (method->mIsStatic)
			{
				if (hasStaticCtor)
				{
					Fail("Only one static constructor is allowed", method->mMethodDeclaration);
					method->mIsStatic = false;
				}

				if (method->mParams.size() != 0)
				{
					Fail("Static constructor cannot declare parameters", method->mMethodDeclaration);
					method->mIsStatic = false;
				}

				if (method->mName == BF_METHODNAME_MARKMEMBERS_STATIC)
					hasStaticMarkMethod = true;

				hasStaticCtor = true;
			}
			else			
			{
				hasCtor = true;
				if (method->mParams.size() == 0)
					hasDefaultCtor = true;

				auto ctorDeclaration = (BfConstructorDeclaration*)method->mMethodDeclaration;
				if (method->mHasAppend)
				{										
					mCurTypeDef->mHasAppendCtor = true;

					auto methodDef = new BfMethodDef();
					mCurTypeDef->mMethods.Insert(methodIdx + 1, methodDef);
					methodDef->mDeclaringType = mCurTypeDef;
					methodDef->mName = BF_METHODNAME_CALCAPPEND;
					methodDef->mProtection = BfProtection_Public;					
					methodDef->mMethodType = BfMethodType_CtorCalcAppend;
					methodDef->mIsMutating = method->mIsMutating;
					
					methodDef->mMethodDeclaration = method->mMethodDeclaration;
					methodDef->mReturnTypeRef = mSystem->mDirectIntTypeRef;
					methodDef->mIsStatic = true;
					methodDef->mBody = method->mBody;

					for (auto param : method->mParams)
					{
						BfParameterDef* newParam = new BfParameterDef();
						newParam->mName = param->mName;
						newParam->mTypeRef = param->mTypeRef;
						newParam->mMethodGenericParamIdx = param->mMethodGenericParamIdx;
						methodDef->mParams.push_back(newParam);
					}

					// Insert a 'appendIdx'					
					BfParameterDef* newParam = new BfParameterDef();
					newParam->mName = "appendIdx";
					newParam->mTypeRef = mSystem->mDirectRefIntTypeRef;
					newParam->mParamKind = BfParamKind_AppendIdx;
					method->mParams.Insert(0, newParam);					
				}
			}
		}
		else if (method->mMethodType == BfMethodType_Dtor)
		{
			if (method->mIsStatic)
			{
				if (hasStaticDtor)
				{
					Fail("Only one static constructor is allowed", method->mMethodDeclaration);
					method->mIsStatic = false;
				}
				
				hasStaticDtor = true;
			}
			else
			{
				if (hasDtor)
				{
					Fail("Only one destructor is allowed", method->mMethodDeclaration);
					method->mIsStatic = false;
				}
				hasDtor = true;
			}

			if (method->mParams.size() != 0)			
				Fail("Destructors cannot declare parameters", method->GetMethodDeclaration()->mParams[0]);
		}
		else if (method->mMethodType == BfMethodType_Normal)
		{
			if (method->mIsStatic)
			{
				if (method->mName == BF_METHODNAME_MARKMEMBERS_STATIC)
					hasStaticMarkMethod = true;
			}
			else
			{
				if (method->mName == BF_METHODNAME_MARKMEMBERS)
					hasMarkMethod = true;
				if (method->mName == BF_METHODNAME_DYNAMICCAST)
					hasDynamicCastMethod = true;
				if (method->mName == BF_METHODNAME_TO_STRING)
					hasToStringMethod = true;
			}
		}
		else if ((method->mMethodType == BfMethodType_Operator) &&
			(method->mIsStatic) &&			
			(method->mParams.size() == 2))
		{
			if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(method->mMethodDeclaration))
			{
				if (operatorDecl->mBinOp == BfBinaryOp_Equality)
				{ 
					// This is a conservative check.  It's okay to add a system-defined equals method even if we don't need it.
					if ((method->mParams[0]->mTypeRef->ToString() == mCurTypeDef->mName->ToString()) &&
						(method->mParams[1]->mTypeRef->ToString() == mCurTypeDef->mName->ToString()))
					{
						hasEqualsMethod = true;
					}
				}
			}			
		}

		if ((method->mImportKind == BfImportKind_Import_Dynamic) || (method->mImportKind == BfImportKind_Import_Unknown))
			needsStaticInit = true;
	}
	
	if (mCurTypeDef->IsExtension())
		needsDefaultCtor = false;

	bool needsDtor = false;
	bool needsStaticDtor = false;
	bool hasStaticField = false;
	bool hasNonStaticField = false;
	bool hasThreadStatics = false;
	for (auto field : mCurTypeDef->mFields)
	{
		if (field->mIsStatic)
		{
			// Resolve-only compiler wants to visit const initializers in a static ctor method, but we don't 
			//  want to create it for the actual binary
			if ((!field->mIsConst) || (mSystem->mIsResolveOnly))
			{
				hasStaticField = true;
				if (field->mFieldDeclaration != NULL)
				{
					if (field->mFieldDeclaration->mInitializer != NULL)
					{						
						needsStaticInit = true;
					}
					if (field->mFieldDeclaration->mFieldDtor != NULL)
						needsStaticDtor = true;
				}
			}

			if (field->mFieldDeclaration != NULL)
			{
				auto attributes = field->mFieldDeclaration->mAttributes;
				while (attributes != NULL)
				{
					if (attributes->mAttributeTypeRef != NULL)
					{
						auto typeRefName = attributes->mAttributeTypeRef->ToString();

						if (typeRefName == "ThreadStatic")
							hasThreadStatics = true;
					}

					attributes = attributes->mNextAttribute;
				}
			}
		}
		else 
		{
			hasNonStaticField = true;
			if (field->mInitializer != NULL)
				needsDefaultCtor = true;
			if ((field->mFieldDeclaration != NULL) && (field->mFieldDeclaration->mFieldDtor != NULL))
				needsDtor = true;
		}
	}
	
	if ((mCurTypeDef->mTypeCode == BfTypeCode_Object) && (!mCurTypeDef->mIsStatic))
	{				
		auto methodDef = AddMethod(mCurTypeDef, BfMethodType_CtorClear, BfProtection_Private, false, "");
		methodDef->mIsMutating = true;
	}

	if ((needsDtor) && (!hasDtor))
	{		
		auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Dtor, BfProtection_Public, false, "");
		BF_ASSERT(mCurTypeDef->mDtorDef == methodDef);
	}

	if ((needsStaticDtor) && (!hasStaticDtor))
	{		
		auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Dtor, BfProtection_Public, true, "");
	}

	if ((needsStaticInit) && (!hasStaticCtor))
	{				
		auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Ctor, BfProtection_Public, true, "");
	}
		
	bool makeCtorPrivate = hasCtor;
// 	if ((!mCurTypeDef->IsExtension()) && (mCurTypeDef->mMethods.empty()))
// 	{
// 		// This is a bit of a hack to ensure we actually generate debug info in the module
// 		needsDefaultCtor = true;
// 		makeCtorPrivate = true;
// 	}

	if (mCurTypeDef->mTypeCode == BfTypeCode_TypeAlias)
		needsDefaultCtor = false;

	if ((needsDefaultCtor) && (!hasDefaultCtor))
	{
		BfProtection prot = hasCtor ? BfProtection_Hidden : BfProtection_Public;
		if (mCurTypeDef->mName == mSystem->mEmptyAtom)
			prot = BfProtection_Hidden;

		// Create default constructor.  If it's the only constructor then make it public,
		//  otherwise make it private so we can still internally use it but the user can't		
		auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Ctor, prot, false, "");
		methodDef->mIsMutating = true;
	}

	
	bool isAutocomplete = false;
	if ((mResolvePassData != NULL) && (mResolvePassData->mAutoComplete != NULL))
		isAutocomplete = true;
		
	//TODO: Don't do this for the autocomplete pass	
	if ((!hasDynamicCastMethod) && (mCurTypeDef->mTypeCode != BfTypeCode_Interface) && (mCurTypeDef->mTypeCode != BfTypeCode_Extension) &&
		(!mCurTypeDef->mIsStatic) && (!isAutocomplete) && (!isAlias))
	{		
		AddDynamicCastMethods(mCurTypeDef);
	}	

	bool isPayloadEnum = false;
	if (mCurTypeDef->mTypeCode == BfTypeCode_Enum)
	{
		for (auto fieldDef : mCurTypeDef->mFields)
		{
			if (auto enumEntryDecl = BfNodeDynCast<BfEnumEntryDeclaration>(fieldDef->mFieldDeclaration))
			{
				if (enumEntryDecl->mTypeRef != NULL)
					isPayloadEnum = true;
			}
		}
	}

	if (isPayloadEnum)
		hasNonStaticField = true;

	if (mCurTypeDef->mTypeCode != BfTypeCode_Interface)
	{
		if ((hasStaticField) && (!hasStaticMarkMethod))
		{						
			auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Normal, BfProtection_Protected, true, BF_METHODNAME_MARKMEMBERS_STATIC);
			methodDef->mNoReflect = true;			
		}

		if (hasThreadStatics)
		{
			auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Normal, BfProtection_Protected, true, BF_METHODNAME_FIND_TLS_MEMBERS);
			methodDef->mNoReflect = true;
		}

		if ((hasNonStaticField) && (!hasMarkMethod))
		{				
			auto methodDef = AddMethod(mCurTypeDef, BfMethodType_Normal, BfProtection_Protected, false, BF_METHODNAME_MARKMEMBERS);
			methodDef->mIsVirtual = true;
			methodDef->mIsOverride = true;
			methodDef->mNoReflect = true;			
			methodDef->mCallingConvention = BfCallingConvention_Cdecl;
			mCurTypeDef->mHasOverrideMethods = true;
		}
	}
	
	if (hasToStringMethod)
		wantsToString = false;

	if (mCurTypeDef->mIsFunction)
	{
		wantsToString = false;
		needsEqualsMethod = false;
	}
	
	if ((mCurTypeDef->mTypeCode == BfTypeCode_Enum) && (!isPayloadEnum))
	{		
		auto methodDef = new BfMethodDef();
		mCurTypeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = mCurTypeDef;
		methodDef->mName = BF_METHODNAME_ENUM_HASFLAG;
		methodDef->mReturnTypeRef = mSystem->mDirectBoolTypeRef;
		methodDef->mProtection = BfProtection_Public;
		AddParam(methodDef, mSystem->mDirectSelfTypeRef, "checkEnum");

		// Underlying
		{
			auto methodDef = new BfMethodDef();
			mCurTypeDef->mMethods.push_back(methodDef);
			methodDef->mDeclaringType = mCurTypeDef;			
			methodDef->mName = BF_METHODNAME_ENUM_GETUNDERLYING;
			methodDef->mReturnTypeRef = mSystem->mDirectSelfBaseTypeRef;
			methodDef->mMethodType = BfMethodType_PropertyGetter;
			methodDef->mProtection = BfProtection_Public;

			auto propDef = new BfPropertyDef();
			mCurTypeDef->mProperties.Add(propDef);
			propDef->mTypeRef = mSystem->mDirectSelfBaseTypeRef;
			propDef->mDeclaringType = mCurTypeDef;
			propDef->mName = "Underlying";
			propDef->mMethods.Add(methodDef);
			propDef->mProtection = BfProtection_Public;
		}

		// UnderlyingRef
		{
			auto methodDef = new BfMethodDef();
			mCurTypeDef->mMethods.push_back(methodDef);
			methodDef->mDeclaringType = mCurTypeDef;
			methodDef->mIsMutating = true;
			methodDef->mName = BF_METHODNAME_ENUM_GETUNDERLYINGREF;
			methodDef->mReturnTypeRef = mSystem->mDirectRefSelfBaseTypeRef;
			methodDef->mMethodType = BfMethodType_PropertyGetter;
			methodDef->mProtection = BfProtection_Public;

			auto propDef = new BfPropertyDef();
			mCurTypeDef->mProperties.Add(propDef);
			propDef->mTypeRef = mSystem->mDirectRefSelfBaseTypeRef;
			propDef->mDeclaringType = mCurTypeDef;
			propDef->mName = "UnderlyingRef";
			propDef->mMethods.Add(methodDef);
			propDef->mProtection = BfProtection_Public;
		}
	}

	if (wantsToString)
	{
		auto methodDef = new BfMethodDef();
		mCurTypeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = mCurTypeDef;
		methodDef->mName = BF_METHODNAME_TO_STRING;
		methodDef->mReturnTypeRef = mSystem->mDirectVoidTypeRef;
		methodDef->mProtection = BfProtection_Public;
		methodDef->mIsOverride = true;
		methodDef->mIsVirtual = true;
		AddParam(methodDef, mSystem->mDirectStringTypeRef, "outStr");
		mCurTypeDef->mHasOverrideMethods = true;
	}
	
	if ((needsEqualsMethod) && (!hasEqualsMethod))
	{		
		auto methodDef = new BfMethodDef();
		mCurTypeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = mCurTypeDef;
		methodDef->mName = BF_METHODNAME_DEFAULT_EQUALS;
		methodDef->mReturnTypeRef = mSystem->mDirectBoolTypeRef;
		methodDef->mProtection = BfProtection_Private;
		methodDef->mIsStatic = true;
		AddParam(methodDef, mSystem->mDirectSelfTypeRef, "lhs");
		AddParam(methodDef, mSystem->mDirectSelfTypeRef, "rhs");
	}

	if (needsEqualsMethod)
	{
		auto methodDef = new BfMethodDef();
		mCurTypeDef->mMethods.push_back(methodDef);
		methodDef->mDeclaringType = mCurTypeDef;
		methodDef->mName = BF_METHODNAME_DEFAULT_STRICT_EQUALS;
		methodDef->mReturnTypeRef = mSystem->mDirectBoolTypeRef;
		methodDef->mProtection = BfProtection_Private;
		methodDef->mIsStatic = true;
		AddParam(methodDef, mSystem->mDirectSelfTypeRef, "lhs");
		AddParam(methodDef, mSystem->mDirectSelfTypeRef, "rhs");
	}

	HashContext inlineHashCtx;

	if (mCurSource != NULL)
	{
		auto bfParser = mCurSource->ToParser();
		if (bfParser != NULL)
			inlineHashCtx.MixinStr(bfParser->mFileName);
	}

	//for (auto methodDef : mCurTypeDef->mMethods)
	for (int methodIdx = 0; methodIdx < (int)mCurTypeDef->mMethods.size(); methodIdx++)
	{
		// Add in name so the hand-created methods can be compared by hash
		auto methodDef = mCurTypeDef->mMethods[methodIdx];
		methodDef->mIdx = methodIdx;		
		methodDef->mFullHash = HashString(methodDef->mName, methodDef->mFullHash);		
		if (mSignatureHashCtx != NULL)
			mSignatureHashCtx->MixinStr(methodDef->mName);
		
		if ((methodDef->mAlwaysInline) || 
			(methodDef->mHasAppend) ||
			(methodDef->mMethodType == BfMethodType_Mixin))
			inlineHashCtx.Mixin(methodDef->mFullHash);

		if (mFullRefresh)
			methodDef->mCodeChanged = true;
	}
	
	mCurTypeDef->mInlineHash = inlineHashCtx.Finish128();
	if (mSignatureHashCtx != NULL)
	{
		mCurTypeDef->mSignatureHash = mSignatureHashCtx->Finish128();

		// We need to hash the signature in here because the preprocessor settings
		//  may change so the text is the same but the signature changes 
		//  so fullHash needs to change too
		mFullHashCtx->Mixin(mCurTypeDef->mSignatureHash);
	}

	if (mFullHashCtx != NULL)
		mCurTypeDef->mFullHash = mFullHashCtx->Finish128();
}

void BfDefBuilder::Visit(BfUsingDirective* usingDirective)
{
	if (usingDirective->mNamespace == NULL)
	{
		BF_ASSERT(mPassInstance->HasFailed());
		return;
	}

	if (mResolvePassData != NULL)
		mResolvePassData->mExteriorAutocompleteCheckNodes.push_back(usingDirective);

	String usingString = usingDirective->mNamespace->ToString();	
	BfAtomComposite usingComposite;
	mSystem->ParseAtomComposite(usingString, usingComposite, true);	
		
	if (!mNamespaceSearch.Contains(usingComposite))
		mNamespaceSearch.Insert(0, usingComposite);
	else
		mSystem->ReleaseAtomComposite(usingComposite);	
}

void BfDefBuilder::Visit(BfUsingStaticDirective* usingDirective)
{
	if (mResolvePassData != NULL)
		mResolvePassData->mExteriorAutocompleteCheckNodes.push_back(usingDirective);
	
	if (usingDirective->mTypeRef != NULL)
		mStaticSearch.Add(usingDirective->mTypeRef);
}

void BfDefBuilder::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	BfAtomComposite prevNamespace = mNamespace;
	int prevNamespaceSearchCount = (int)mNamespaceSearch.size();
	
	if (namespaceDeclaration->mNameNode == NULL)
		return;

	String namespaceLeft = namespaceDeclaration->mNameNode->ToString();
	while (true)
	{
		int dotIdx = (int)namespaceLeft.IndexOf('.');
		if (dotIdx == -1)
		{
			BfAtom* namespaceAtom = mSystem->GetAtom(namespaceLeft);
			mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);
									
			if (!mNamespaceSearch.Contains(mNamespace))
			{
				mSystem->RefAtomComposite(mNamespace);
				mNamespaceSearch.Insert(0, mNamespace);							
			}
			mSystem->ReleaseAtom(namespaceAtom);
			break;
		}

		BfAtom* namespaceAtom = mSystem->GetAtom(namespaceLeft.Substring(0, dotIdx));
		mNamespace.Set(mNamespace.mParts, mNamespace.mSize, &namespaceAtom, 1);		
		namespaceLeft = namespaceLeft.Substring(dotIdx + 1);
				
		if (!mNamespaceSearch.Contains(mNamespace))
		{
			mSystem->RefAtomComposite(mNamespace);
			mNamespaceSearch.Insert(0, mNamespace);
		}
		mSystem->ReleaseAtom(namespaceAtom);		
	}
	
	VisitChild(namespaceDeclaration->mBlock);
	while ((int)mNamespaceSearch.size() > prevNamespaceSearchCount)		
	{
		BfAtomComposite& atomComposite = mNamespaceSearch[0];
		mSystem->ReleaseAtomComposite(atomComposite);
		mNamespaceSearch.RemoveAt(0);
	}
	mNamespace = prevNamespace;
}

void BfDefBuilder::Visit(BfBlock* block)
{
	VisitMembers(block);
}

void BfDefBuilder::Visit(BfRootNode* rootNode)
{
	VisitMembers(rootNode);
}
