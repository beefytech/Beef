#include "BfDemangler.h"

USING_NS_BF;

static char SafeGetChar(const StringImpl& str, int idx)
{
	if (idx >= (int)str.length())
		return '!';
	return str[idx];
}

DemangleBase::DemangleBase()
{
	mCurIdx = 0;	
	mFailed = false;
	mLanguage = DbgLanguage_Unknown;
	mInArgs = false;
	mBeefFixed = false;
}

bool DemangleBase::Failed()
{
	//BF_DBG_FATAL("DwDemangler::Failed");	
	mFailed = true;
	return false;
}

void DemangleBase::Require(bool result)
{
	//BF_ASSERT(result); 
	if (!result)
	{
		mFailed = true;		
	}
}

//////////////////////////////////////////////////////////////////////////

DwDemangler::DwDemangler()
{	
	mOmitSubstituteAdd = false;
	mIsFirstName = true;
	mTemplateDepth = 0;
	mCaptureTargetType = false;
	mFunctionPopSubstitute = false;
	mRawDemangle = false;	
}

#define RETURN_STR(strVal) do { outName = strVal; return true; } while(false)

bool DwDemangler::DemangleBuiltinType(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'v':
		RETURN_STR("void");	
	case 'w':
		RETURN_STR("wchar_t");
	case 'b':
		RETURN_STR("bool");	
	case 'a':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("int8");
		else
			RETURN_STR("sbyte");
	case 'h':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("uint8");
		else
			RETURN_STR("byte");
	case 's':
		RETURN_STR("short");
	case 't':
		RETURN_STR("ushort");
	case 'i':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("int32");
		else
			RETURN_STR("int");
	case 'j':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("uint32");
		else
			RETURN_STR("uint");
	case 'x':
		RETURN_STR("long");
	case 'l':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("int32");
		else
			RETURN_STR("int");
	case 'm':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("uint32");
		else
			RETURN_STR("uint");
	case 'y':		
		RETURN_STR("ulong");
	case 'c':
		if (mLanguage == DbgLanguage_Beef)
			RETURN_STR("char8");
		else
			RETURN_STR("char");
	case 'f':
		RETURN_STR("float");
	case 'd':
		RETURN_STR("double");
	case 'n':
		RETURN_STR("__int128");
	case 'o':
		RETURN_STR("__uint128");
	case 'e':
		RETURN_STR("__float80");
	case 'g':
		RETURN_STR("__float128");
	case 'z':
		RETURN_STR("...");
	case 'D':
		{
			char nextChar = SafeGetChar(mMangledName, mCurIdx++);
			switch (nextChar)
			{
			case 'i':
				RETURN_STR("char32");
			case 's':
				RETURN_STR("char16");
			case 'a':
				RETURN_STR("auto");
			case 'c':
				RETURN_STR("decltype(auto)");
			case 'n':
				RETURN_STR("std::nullptr_t");
			default:
				RETURN_STR("?");
			}
		}
		break;
	default:
		mCurIdx--;
		break;
	}
	return false;
}

bool DwDemangler::DemangleArrayType(StringImpl& outName)
{
	//TODO:
	/*auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'A':		
		{
			
		}
		break;
	default:
		mCurIdx--;
	}*/
	return false;
}

bool DwDemangler::DemangleClassEnumType(StringImpl& outName)
{
	if (DemangleName(outName))
		return true;
	//TODO: 'Ts', 'Tu', 'Te'
	return false;
}

bool DwDemangler::DemangleFunction(StringImpl& outName)
{	
	bool wantsReturnValue = false;
	//String outName;
	bool hasTemplateArgs;
	Require(DemangleName(outName, &hasTemplateArgs));
	mIsFirstName = false;
	if (hasTemplateArgs)
		wantsReturnValue = true;

	// Pop either function name or template args
	if (mSubstituteList.size() > 0)
		mSubstituteList.pop_back();

	if (mCaptureTargetType)
	{
		int lastAt = (int)outName.LastIndexOf('@');
		if (lastAt != -1)
		{
			DwDemangler subDemangler;
			outName.Remove(0, lastAt + 1);
			outName = subDemangler.Demangle(outName);			
			return true;
		}
		int lastDot = (int)outName.LastIndexOf('.');
		if (lastDot != -1)
			outName.RemoveToEnd(lastDot);
		return true;
	}
	
	// New
	if (mFunctionPopSubstitute)
	{
		BF_ASSERT(mSubstituteList.size() > 0);
		if (mSubstituteList.size() > 0)
			mSubstituteList.pop_back();
		mFunctionPopSubstitute = false;
	}
	
	if (mCurIdx < (int) mMangledName.length())
	{
		//TODO: Needed? Caused a crash. mSubstituteList.pop_back(); // Remove method name

		int atPos = (int)outName.LastIndexOf('@');
		if (atPos != -1)
		{
			int dotPos = (int)outName.LastIndexOf('.', atPos);
			if (dotPos != -1)
			{
				StringT<256> prefix = outName.Substring(dotPos + 1, atPos - dotPos - 1);
				if ((prefix == "get") || (prefix == "set"))
				{
					outName = outName.Substring(0, dotPos + 1) + outName.Substring(atPos + 1) + " " + prefix;
					return true;
				}
			}
		}

		outName += "(";
		bool needsComma = false;

		for (int paramIdx = 0; mCurIdx < (int)mMangledName.length(); paramIdx++)
		{
			if (SafeGetChar(mMangledName, mCurIdx) == 'E')			
				break;			

			if (needsComma)
				outName += ", ";
			
			StringT<256> paramType;
			Require(DemangleType(paramType));
			
			bool atEnd = mCurIdx >= (int) mMangledName.length();
			if ((paramIdx == 0) && (wantsReturnValue))
			{
				outName = paramType + " " + outName;
			}
			else
			{
				if ((paramType == "void") && (paramIdx == 0) && (atEnd))
					break;
				outName += paramType;
				needsComma = true;
			}

			if (mFailed)
				break;
		}

		outName += ")";
	}
	return true;
}

bool DwDemangler::DemangleLocalName(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'Z')
	{
		if (SafeGetChar(mMangledName, mCurIdx) == 'L') // Literal (?)
			mCurIdx++;
		DemangleFunction(outName);
		auto endChar = SafeGetChar(mMangledName, mCurIdx++);
		if (endChar != 'E')
			return false;
		endChar = SafeGetChar(mMangledName, mCurIdx++);
		if (endChar == 's')
		{
			//
		}
		else
		{
			mCurIdx--;
			StringT<256> entityName;
			Require(DemangleName(entityName));
			if (mLanguage == DbgLanguage_Beef)
				outName += "." + entityName;
			else
				outName += "::" + entityName;
		}
		endChar = SafeGetChar(mMangledName, mCurIdx++);
		if (endChar == '_')
		{
			//
		}
		else
		{
			mCurIdx--;
		}

		BF_ASSERT(mSubstituteList.size() > 0);
		if (mSubstituteList.size() >= 2)
		{
			mSubstituteList.pop_back();
			mSubstituteList[mSubstituteList.size() - 1] = outName;
		}
		return true;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleType(StringImpl& outName)
{
	StringT<256> cvQualifiers;
	if (DemangleCVQualifiers(cvQualifiers))
	{
		Require(DemangleType(outName));
		outName = cvQualifiers + " " + outName;
		mSubstituteList.push_back(outName);
		return true;
	}

	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{	
	case 'D':
		{
			auto nextChar = SafeGetChar(mMangledName, mCurIdx++);
			if (nextChar == 'p')
			{
				Require(DemangleType(outName));
				return true;
			}
			mCurIdx--;
		}
	case 'P':
		if (DemangleFunctionType(outName))
		{
			mSubstituteList.push_back(outName);
			return true;
		}
		if (DemangleType(outName))
		{
			outName = outName + "*";
			mSubstituteList.push_back(outName);
			return true;
		}
		break;
	case 'R':
		if (DemangleType(outName))
		{
			outName = outName + "&";
			mSubstituteList.push_back(outName);
			return true;
		}
		break;
	case 'O':
		if (DemangleType(outName))
		{
			if (outName[outName.length() - 1] != '&')
				outName = outName + "&&";
			mSubstituteList.push_back(outName);
			return true;
		}
		break;
	case 'U':
		{
			StringT<256> modName;
			Require(DemangleUnqualifiedName(modName));
			Require(DemangleType(outName));

			if (modName[0] == '@')
				outName = outName + " " + modName.Substring(1);
			else
				outName = modName + " " + outName;
			return true;
		}
		break;	
	//TODO: 'C', 'G', 'U'			
	default:
		mCurIdx--;
		break;
	}	
	
	if (DemangleBuiltinType(outName))
		return true;
	if (DemangleFunctionType(outName))
		return true;

	if (DemangleSubstitution(outName))
	{
		StringT<256> templateArgs;
		if (DemangleTemplateArgs(templateArgs))
		{
			outName += templateArgs;
			mSubstituteList.push_back(outName);
		}
		return true;
	}

	if (DemangleClassEnumType(outName))
		return true;
	//TODO: DemangleArrayType
	//TODO: DemanglePointerToMemberType	
	if (DemangleTemplateParam(outName))
		return true;
	//TODO: TemplateTemplateParam
	//TODO: DeclType	

	return false;
}

bool DwDemangler::DemangleFunctionType(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'F')
	{
		StringT<256> returnType;
		Require(DemangleType(returnType));
		outName = returnType + " (*)(";

		for (int paramNum = 0; true; paramNum++)
		{
			if (DemangleEnd())
				break;

			if (paramNum > 0)
				outName += ", ";

			StringT<256> paramType;
			Require(DemangleType(paramType));
			outName += paramType;
		}

		outName += ")";
		return true;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleNestedName(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'N':
		{
			
		}
		break;	
	}
	mCurIdx--;

	return false;
}

bool DwDemangler::DemangleCVQualifiers(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'r':
		RETURN_STR("restrict");
	case 'V':
		RETURN_STR("volatile");
	case 'K':
		RETURN_STR("const");
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleRefQualifier(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'R':
		RETURN_STR("&");
	case 'O':
		RETURN_STR("&&");	
	}
	mCurIdx--;
	return false;
}


bool DwDemangler::DemangleOperatorName(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);	
	StringT<64> opCode;
	opCode += firstChar;	
	opCode += SafeGetChar(mMangledName, mCurIdx++);
	StringT<64> opName;

	if (opCode == "nw")
		opName = " new";
	else if (opCode == "na")
		opName = " new[]";
	else if (opCode == "dl")
		opName = " delete";
	else if (opCode == "da")
		opName = " delete[]";
	else if (opCode == "pl")
		opName = "+";
	else if (opCode == "mi")
		opName = "-";
	else if (opCode == "ml")
		opName = "*";
	else if (opCode == "dv")
		opName = "/";
	else if (opCode == "rm")
		opName = "%";
	else if (opCode == "an")
		opName = "&";
	else if (opCode == "or")
		opName = "|";
	else if (opCode == "eo")
		opName = "^";
	else if (opCode == "aS")
		opName = "=";
	else if (opCode == "pL")
		opName = "+=";
	else if (opCode == "mI")
		opName = "-=";
	else if (opCode == "mL")
		opName = "*=";
	else if (opCode == "dV")
		opName = "/=";
	else if (opCode == "rM")
		opName = "%=";
	else if (opCode == "aN")
		opName = "&=";
	else if (opCode == "oR")
		opName = "|=";
	else if (opCode == "eO")
		opName = "^=";
	else if (opCode == "ls")
		opName = "<<";
	else if (opCode == "rs")
		opName = ">>";
	else if (opCode == "eq")
		opName = "==";
	else if (opCode == "ne")
		opName = "!=";	
	else if (opCode == "lt")
		opName = "<";
	else if (opCode == "gt")
		opName = ">";
	else if (opCode == "ge")
		opName = ">=";
	else if (opCode == "le")
		opName = "<=";
	else if (opCode == "aa")
		opName = "&&";
	else if (opCode == "oo")
		opName = "||";
	else if (opCode == "ad")
		opName = "&";
	else if (opCode == "de")
		opName = "*";
	else if (opCode == "ng")
		opName = "-";
	else if (opCode == "nt")
		opName = "!";
	else if (opCode == "ps")
		opName = "+";
	else if (opCode == "co")
		opName = "~";
	else if (opCode == "pp")
		opName = "++";
	else if (opCode == "mm")
		opName = "--";
	else if (opCode == "cm")
		opName = ",";
	else if (opCode == "pm")
		opName = "->*";
	else if (opCode == "pt")
		opName = "->";
	else if (opCode == "ix")
		opName = "[]";
	else if (opCode == "cl")
		opName = "()";
	else if (opCode == "qu")
		opName = "?";
	else if (opCode == "li")
	{
		opName = "\"\"";
	}
	else if (opCode == "cv")
	{
		StringT<256> typeName;
		Require(DemangleType(typeName));
		opName = " " + typeName;
	}
	else if ((opCode == "C1") || (opCode == "C2") || (opCode == "C3"))
	{
		outName += "this";
	}
	else if ((opCode == "D0") || (opCode == "D1") || (opCode == "D2"))
	{
		outName += "~this";
	}
	else
	{
		mCurIdx -= 2;
		return false;
	}

	if (!opName.empty())
		outName += "operator" + opName;
	//mSubstituteList.push_back(outName);
	
	return true;
}

bool DwDemangler::DemangleSourceName(StringImpl& outName)
{
	auto c = SafeGetChar(mMangledName, mCurIdx);	
	if ((c >= '0') && (c <= '9'))
	{		
		int nameLen = 0;
		while (mCurIdx < (int) mMangledName.length())
		{
			char c = mMangledName[mCurIdx];
			if ((c >= '0') && (c <= '9'))
			{
				nameLen = (int) (c - '0') + (nameLen * 10);
				mCurIdx++;
			}
			else
				break;
		}

		for (int nameIdx = 0; ((nameIdx < nameLen) && (mCurIdx < (int) mMangledName.length())); nameIdx++)
		{
			char c = mMangledName[mCurIdx++];
			outName += c;
		}

		//mSubstituteList.push_back(outName);
		return true;
	}

	return false; 
}

bool DwDemangler::DemangleUnqualifiedName(StringImpl& outName)
{
	if (DemangleOperatorName(outName)) // Also handles ctor/dtor
		return true;
	if (DemangleSourceName(outName))
		return true;	
	return false;
}

bool DwDemangler::DemangleEnd()
{	
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'E')
		return true;
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleInternalName(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'L')
	{
		bool result = DemangleSourceName(outName);
		Require(result);
		return result;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleExprPriamry(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'L')
	{
		StringT<256> outType;
		Require(DemangleType(outType));

		StringT<256> value;
		while (true)
		{
			auto c = SafeGetChar(mMangledName, mCurIdx++);
			if (c == 'E')
				break;
			value += c;
		}
		if (outType == "bool")
			outName = (value == "0") ? "false" : "true";
		else
			outName = value;
		return true;
	}
	mCurIdx--;
	return false;
}


bool DwDemangler::DemangleTemplateArg(StringImpl& outName)
{
	if (DemangleType(outName))
		return true;
	if (DemangleExprPriamry(outName))
		return true;
	//TODO: Expression, Simple Expressions, Argument Pack
	return false;
}

bool DwDemangler::DemangleTemplateArgs(StringImpl& outName)
{		
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'I')
	{
		bool recordTemplateArgs = mIsFirstName && (mTemplateDepth == 0);
		if (recordTemplateArgs)
			mTemplateList.Clear();
		mTemplateDepth++;

		bool inParamPack = false;
		bool foundEnd = false;
		bool needsComma = false;
		
		outName = "<";
		for (int argIdx = 0; true; argIdx++)
		{
			if (mFailed)
				return true;

			if (DemangleEnd())
			{
				if (inParamPack)			
				{
					inParamPack = false;				
					continue;
				}
				else
					break;
			}

			StringT<256> templateArg;

			auto firstChar = SafeGetChar(mMangledName, mCurIdx);
			if (firstChar == 'J')
			{
				mCurIdx++;
				inParamPack = true;
				continue;
			}

			Require(DemangleTemplateArg(templateArg));
			if (recordTemplateArgs)
				mTemplateList.push_back(templateArg);
			if (needsComma)
				outName += ", ";
			outName += templateArg;
			needsComma = true;
		}
		outName += ">";

		mTemplateDepth--;
		return true;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleSubstitution(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'S')
	{
		int idx = 0;
		bool hadChars = 0;
		while (true)
		{
			char idxChar = SafeGetChar(mMangledName, mCurIdx++);			
			if (idxChar == 't')
			{
				DemangleUnqualifiedName(outName);
				outName = "std." + outName;
				mSubstituteList.push_back(outName);
				mOmitSubstituteAdd = true;
				return true;
			}
			mOmitSubstituteAdd = true;
			if (idxChar == 'a')
			{
				RETURN_STR("std.allocator");
			}
			if (idxChar == 'b')
			{
				RETURN_STR("std.basic_string");
			}
			if (idxChar == 's')
			{
				RETURN_STR("std.string");
			}
			if (idxChar == 'i')				
			{
				RETURN_STR("std.basic_istream<char, std.char_traits<char>>");
			}
			if (idxChar == 'o')
			{
				RETURN_STR("std.basic_ostream<char, std.char_traits<char>>");
			}
			if (idxChar == 'd')
			{
				RETURN_STR("std.basic_iostream<char, std.char_traits<char>>");
			}

			if (idxChar == '?')
				break;
			if (idxChar == '_')
				break;
			hadChars = true;
			if ((idxChar >= '0') && (idxChar <= '9'))
				idx = (idxChar - '0') + (idx * 36);
			else if ((idxChar >= 'A') && (idxChar <= 'Z'))
				idx = ((idxChar - 'A') + 10) + (idx * 36);
		}

		if (hadChars)
			idx++;
		if ((idx >= 0) && (idx < (int) mSubstituteList.size()))
		{
			outName = mSubstituteList[idx];
			return true;
		}

		outName = "?";
		Failed();
		return true;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleTemplateParam(StringImpl& outName)
{
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	if (firstChar == 'T')
	{
		int idx = 0;
		bool hadChars = 0;
		while (true)
		{
			char idxChar = SafeGetChar(mMangledName, mCurIdx++);			
			if (idxChar == '?')
				break;
			if (idxChar == '_')
				break;
			hadChars = true;
			if ((idxChar >= '0') && (idxChar <= '9'))
				idx = (idxChar - '0') + (idx * 36);
			else if ((idxChar >= 'A') && (idxChar <= 'Z'))
				idx = ((idxChar - 'A') + 10) + (idx * 36);
		}

		if (hadChars)
			idx++;
		if ((idx >= 0) && (idx < (int)mTemplateList.size()))
		{
			outName = mTemplateList[idx];
			mSubstituteList.push_back(outName);
			return true;
		}

		outName = "?";
		Failed();
		return true;
	}
	mCurIdx--;
	return false;
}

bool DwDemangler::DemangleUnscopedName(StringImpl& outName)
{
	if (DemangleUnqualifiedName(outName))
		return true;
	if (DemangleSubstitution(outName))
		return true;	
	return false;
}

bool DwDemangler::DemangleName(StringImpl& outName, bool* outHasTemplateArgs)
{
	if (outHasTemplateArgs != NULL)
		*outHasTemplateArgs = false;
	auto firstChar = SafeGetChar(mMangledName, mCurIdx++);
	switch (firstChar)
	{
	case 'N': // NestedName
		{			
			StringT<64> cvQualifier;
			if (DemangleCVQualifiers(cvQualifier))
				outName += cvQualifier + " ";
			StringT<64> refQualifier;
			if (DemangleRefQualifier(refQualifier))
				outName += refQualifier + " ";			

			int nameCount = 0;
			while ((!DemangleEnd()) && (!mFailed))
			{				
				StringT<128> unqualifiedName;
				mOmitSubstituteAdd = false;
				if ((DemangleUnscopedName(unqualifiedName)) ||
					(DemangleInternalName(unqualifiedName)))
				{
					if (outHasTemplateArgs != NULL)
						*outHasTemplateArgs = false;
					if (nameCount > 0)						
					{
						if (mLanguage == DbgLanguage_Beef)
							outName += ".";
						else
							outName += "::";
					}
					outName += unqualifiedName;
					if (!mOmitSubstituteAdd)
						mSubstituteList.push_back(outName);
					else
						mOmitSubstituteAdd = false;
					nameCount++;
					continue;
				}
				
				StringT<128> templateArgs;
				if (DemangleTemplateArgs(templateArgs))
				{
					if (outHasTemplateArgs != NULL)
						*outHasTemplateArgs = true;
					int arrayDims = 0;
					if (!mRawDemangle)
					{
						if (outName == "__TUPLE")
						{
							outName = "(" + templateArgs.Substring(1, templateArgs.length() - 2) + ")";
							mSubstituteList.push_back(outName);
							continue;
						}
						else if (outName == "Box")
						{
							outName = templateArgs.Substring(1, templateArgs.length() - 2) + "^";
							mSubstituteList.push_back(outName);
							continue;
						}
						if (outName == "System.Array1")
							arrayDims = 1;
						else if (outName == "System.Array2")
							arrayDims = 2;
						else if (outName == "System.Array3")
							arrayDims = 3;
					}
					if (arrayDims > 0)
					{
						outName = templateArgs.Substring(1, templateArgs.length() - 2) + "[";
						for (int i = 0; i < arrayDims - 1; i++)
							outName += ",";
						outName += "]";
					}
					else
					{
						outName += templateArgs;
						mSubstituteList.push_back(outName);
					}
					continue;
				}
				
				Failed();
				break;
			}
			
			return true;
		}
		break;
	}
	mCurIdx--;

	if (DemangleUnscopedName(outName))
	{
		if (!mOmitSubstituteAdd)
			mSubstituteList.push_back(outName);
		else
			mOmitSubstituteAdd = false;

		StringT<128> templateArgs;
		if (DemangleTemplateArgs(templateArgs))
		{
			if (outHasTemplateArgs != NULL)
				*outHasTemplateArgs = true;
			outName += templateArgs;			
		}

		if ((outName.length() > 0) && (outName[outName.length() - 1] == '.'))
		{
			// an incomplete 'std.'
			StringT<128> unqualifiedName;
			if ((DemangleUnscopedName(unqualifiedName)) ||
				(DemangleInternalName(unqualifiedName)))
			{
				outName += unqualifiedName;
			}
			else
			{
				Failed();
			}			
		}

		return true;
	}	

	if (DemangleLocalName(outName))
		return true;

	return false;
}

String DwDemangler::Demangle(const StringImpl& mangledName)
{
	BF_ASSERT(mCurIdx == 0);	

	/*String overrideVal = "_ZZL9DumpStatsP16TCMalloc_PrinteriE3MiB";
	//String overrideVal = "_ZN3Hey4Dude3Bro9TestClass7MethodBEivf";
	//"_ZN3Hey4Dude3Bro9TestClass14DlgTestGenericIiE12TestThinggieEi";
	//"_ZStL19piecewise_construct";
	//"_ZNSbIwSt11char_traitsIwESaIwEED1Ev";
	//"_ZN12_GLOBAL__N_124do_realloc_with_callbackEPvjPFvS0_EPFjPKvE";
	//"_ZNK17TCMalloc_PageMap2ILi19EE3getEj";
	//"_ZN5BeefyL13FromBigEndianEs";
	//"_ZNSt6vectorIZN4BFGC19WriteDebugDumpStateEvE9DebugInfoSaIS1_EED2Ev";
	//"_ZNSt6vectorISsSaISsEE19_M_emplace_back_auxIJRKSsEEEvDpOT_";
	//"_ZSt4moveIRiEONSt16remove_referenceIT_E4typeEOS2_";
	//"_ZL10CStartProcPN6System9Threading6ThreadE";
	//"_ZNSt8_Rb_treeIPN6System9Threading6ThreadESt4pairIKS3_PS3_ESt10_Select1stIS7_ESt4lessIS3_ESaIS7_EE13_Rb_tree_implISB_Lb1EED2Ev";
	//Clang failure: (?) "__ZNSt16allocator_traitsISaISsEE9constructISsJRKSsEEEDTcl12_S_constructfp_fp0_spclsr3stdE7forwardIT0_Efp1_EEERS0_PT_DpOS5_";
		//"__ZNSt4pairIifEC2IKifvEERKS_IT_T0_E";
	//"__ZNSsaSEPKc";
	//"__ZN6System6String6ConcatEU6paramsPNS_6Array1INS_6ObjectEEE";
	//_ZNSt3mapI3HeyfSt4lessIiESaISt4pairIKifEEEixES_S0_S1_S2_S3_S4_S5_S6_
	String overrideDemangled;
	if (mangledName != overrideVal)	
	{
		BfDemangler demangler;
		overrideDemangled = demangler.Demangle(overrideVal);
	}*/
	
	mMangledName = mangledName;	

	String outStr;
	
	if (mangledName.length() < 3)
		return mangledName;

	if (strncmp(mangledName.c_str(), "_Z", 2) == 0)
		mCurIdx = 2;
	else if (strncmp(mangledName.c_str(), "__Z", 3) == 0)
		mCurIdx = 3;
	else
	{
		int dotIdx = (int)mangledName.IndexOf('.');
		if (dotIdx > 1)
			return mangledName.Substring(0, dotIdx) + ":" + Demangle(mangledName.Substring(dotIdx + 1));
		return mangledName;
	}

	/*int ePos = (int) mMangledName.rfind('E');
	if (ePos > 0)
		mMangledName = mMangledName.substr(0, mMangledName.length() - 1);*/

	int endPos = (int)mMangledName.length() - 1;
	while (endPos > 0)
	{
		char c = mMangledName[endPos];
		if (c == '@')
		{
			mMangledName = mMangledName.Substring(0, endPos);
			break;
		}
		bool isNum = (c >= '0') && (c <= '9');
		if (!isNum)
			break;
		endPos--;
	}

	bool mayHaveParams = false;

	char typeChar = SafeGetChar(mangledName, mCurIdx);
	if (typeChar == 'T')
	{
		mCurIdx++;
		char typeChar2 = SafeGetChar(mangledName, mCurIdx++);
		if (typeChar2 == 'S') 
		{
			// typeinfo
		}
		else if (typeChar2 == 'V')
		{
			// vtable
		}
		else
		{
			return mangledName;
		}
	}	
	else if (typeChar == 'S')
	{
		mayHaveParams = false;
	}
	else if (typeChar == 'L')
	{
		mayHaveParams = true;
		mCurIdx++;
	}
	else if (typeChar == 'Z') // Static function-scoped field
		mayHaveParams = false;
	else
		mayHaveParams = true;

	bool wantsReturnValue = false;
	StringT<256> outName;
	if (mayHaveParams)
		Require(DemangleFunction(outName));
	else
		Require(DemangleName(outName));

	if (mFailed)
		return mangledName;

	//OutputDebugStrF("%s\n", outName.c_str());

	return outName;
}

//////////////////////////////////////////////////////////////////////////

MsDemangler::MsDemangler()
{
	mCurIdx = 0;
}

bool MsDemangler::DemangleString(StringImpl& outName)
{	
	while (true)
	{
		char c = SafeGetChar(mMangledName, mCurIdx++);
		if ((c == '!') || (c == '?'))
			return Failed();		
		if (c == '@')
			break;
		outName.Append(c);
	}

	//mSubstituteList.push_back(outName);
	return true;
}

bool MsDemangler::DemangleTemplateName(StringImpl& outName, String* primaryName)
{
	DemangleString(outName);
	if (primaryName != NULL)
		*primaryName = outName;
	mSubstituteList.push_back(outName);

	outName += "<";
	for (int paramIdx = 0; true; paramIdx++)
	{
		String paramType;
		if (!DemangleType(paramType))
			break;		
		if (paramIdx > 0)
			outName += ", ";
		outName += paramType;
	}
	outName += ">";

	return true;
}


bool MsDemangler::DemangleScopedName(StringImpl& outName, String* primaryName)
{			
	for (int nameIdx = 0; true; nameIdx++)
	{
		if (mFailed)
			return false;

		char c = SafeGetChar(mMangledName, mCurIdx++);		
		if (c == '@')
			return true;

		/*{
		if (curScope.length() == 0)			
		return true;			
		if (nameIdx == 0)
		{
		if (primaryName != NULL)
		*primaryName = curScope;
		}
		else
		outName = "::" + outName;
		outName = curScope + outName;
		curScope.clear();
		mSubstituteList.push_back(outName);

		nameIdx++;
		continue;
		}*/

		String namePart;
		if (c == '?')
		{
			c = SafeGetChar(mMangledName, mCurIdx++);
			if (c == '$')
			{				
				SubstituteList oldSubList = mSubstituteList;
				mSubstituteList.Clear();
				DemangleTemplateName(namePart, /*primaryName*/NULL);
				mSubstituteList = oldSubList;				
			}
			else if (c == '?')
			{
				return Failed();
			}
			else
			{
				mCurIdx--;
				int num = DemangleNumber();
				outName = StrFormat("%d", num);
			}

		}
		else if ((c >= '0') && (c <= '9'))
		{			
			int subIdx = c - '0';
			if (subIdx < mSubstituteList.size())
			{
				namePart = mSubstituteList[subIdx];
			}
			else
			{
				return Failed();
			}
		}
		else
		{
			mCurIdx--;			
			Require(DemangleString(namePart));			
		}

		if (nameIdx == 0)
		{			
			//We moved this down so we get the template args for the ctor name
						/*if ((primaryName != NULL) && (primaryName->empty()))
						*primaryName = namePart;*/
			outName = namePart;
			if ((primaryName != NULL) && (primaryName->empty()))
				*primaryName = namePart;			
		}
		else if (mLanguage == DbgLanguage_Beef)
		{
			if ((mBeefFixed) && (namePart == "bf"))
				namePart.Clear();

			if (!namePart.IsEmpty()) 			
				outName = namePart + "." + outName;
		}
		else
		{
			outName = namePart + "::" + outName;
		}

		mSubstituteList.push_back(namePart);
	}
}

bool MsDemangler::DemangleModifiedType(StringImpl& outName, bool isPtr)
{
	String modifier;
	DemangleCV(modifier);	

	char c = SafeGetChar(mMangledName, mCurIdx);
	if (c == 'Y') // Sized array
	{
		String dimStr;

		mCurIdx++;
		int dimCount = DemangleNumber();
		for (int dim = 0; dim < dimCount; dim++)
		{
			int dimSize = DemangleNumber();
			dimStr += StrFormat("[%d]", dimSize);
		}

		DemangleType(outName);
		outName += dimStr;
	}
	else
	{
		DemangleType(outName);
		if (isPtr)
			outName += "*";
	}

	if (!modifier.empty())
		outName = modifier + " " + outName;

	return true;
}

bool MsDemangler::DemangleType(StringImpl& outName)
{
	char c = SafeGetChar(mMangledName, mCurIdx++);
	if ((c == 0) || (c == '@'))
		return false;

	switch (c)
	{	
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		{
			if (mFailed)
				return false;
			int subIdx = c - '0';
			if ((subIdx < 0) || (subIdx >= (int)mSubstituteList.size()))
				return Failed();
			outName = mSubstituteList[subIdx];
			return true;
		}
		break;

	case '$':
		{
			char c = SafeGetChar(mMangledName, mCurIdx++);
			switch (c)
			{
			case '0':
				{
					int64 val = 0;
					bool doNeg = false;
					auto nextChar = SafeGetChar(mMangledName, mCurIdx++);
					if (nextChar == '?')
					{
						doNeg = true;
						nextChar = SafeGetChar(mMangledName, mCurIdx++);
					}

					if ((nextChar >= '0') && (nextChar <= '9'))
					{
						val = (nextChar - '0') + 1;
					}
					else
					{
						while (true)
						{
							if (nextChar == 0)
								return Failed();
							if (nextChar == '@')
								break;
							val = (val * 0x10) + (nextChar - 'A');
							nextChar = SafeGetChar(mMangledName, mCurIdx++);
						}
					}
					if (doNeg)
						val = -val;
					if (mLanguage == DbgLanguage_Beef)
						outName += StrFormat("const %d", val);
					else
						outName += StrFormat("%d", val);
					return true;
				}
				break;
			case 'D':
				{
					int templateParamNum = DemangleNumber();
					return Failed();
				}
				break;
			case 'F':
				{
					int param1 = DemangleNumber();
					int param2 = DemangleNumber();
					return Failed();
				}
				break;
			case 'G':
				{
					int param1 = DemangleNumber();
					int param2 = DemangleNumber();
					int param3 = DemangleNumber();
					return Failed();
				}
				break;
			case 'Q':
				{
					int nonTypeTemplateParam = DemangleNumber();
					return Failed();
				}
				break;
			case '$':
				{
					char c = SafeGetChar(mMangledName, mCurIdx++);
					if (c == 'C')
					{
						String modifier;						
						DemangleCV(modifier);
						DemangleType(outName);
						outName = modifier + " " + outName;
						return true;
					}
					else if (c == 'Q')
					{
						String modifier;						
						DemangleCV(modifier);
						DemangleType(outName);
						outName = modifier + " " + outName;
						return true;
					}
				}
				break;
			}

		}
		break;

	case '_':
		{
			c = SafeGetChar(mMangledName, mCurIdx++);
			switch (c)
			{			
			case 'D':
				outName = "int8";
				return true;
			case 'E':
				outName = "uint8";
				return true;
			case 'F':
				outName = "int16";
				return true;
			case 'G':
				outName = "uint16";
				return true;
			case 'H':
				outName = "int32";
				return true;
			case 'I':
				outName = "uint32";
				return true;
			case 'J':
				outName = "int64";
				return true;
			case 'K':
				outName = "uint64";
				return true;
			case 'L':
				outName = "int128";
				return true;
			case 'M':
				outName = "float";
				return true;
			case 'W':
				outName = "wchar_t";
				return true;
			case 'N':
				outName = "bool";
				return true;
			default:
				return Failed();
				break;
			}
		}
		break;	

	case 'A':
		{
			DemangleModifiedType(outName, false);
			outName += "&";			
		}
		return true;
	case 'B':
		DemangleModifiedType(outName, false);
		outName = "volatile " + outName + "&";
		return true;
	case 'P':		
		DemangleModifiedType(outName, true);	
		return true;
	case 'Q':
		DemangleModifiedType(outName, true);
		outName = "const " + outName;
		return true;
	case 'R':
		DemangleModifiedType(outName, true);
		outName = "volatile " + outName;
		return true;
	case 'S':
		DemangleModifiedType(outName, true);
		outName = "const volatile" + outName;
		return true;
	case 'T':
		// union
		DemangleScopedName(outName);
		return true;
	case 'U':
		// struct
		DemangleScopedName(outName);
		return true;
	case 'V':
		// class
		DemangleScopedName(outName);
		return true;
	case 'W':
		// enum
		c = SafeGetChar(mMangledName, mCurIdx++);
		Require((c >= '0') && (c <= '8'));
		DemangleScopedName(outName);
		return true;
	case 'C':
		outName = "char";
		return true;
	case 'D':
		if (mLanguage == DbgLanguage_Beef)
			outName = "char8";
		else
			outName = "char";
		return true;
	case 'E':
		if (mLanguage == DbgLanguage_Beef)
			outName = "uint8";
		else
			outName = "uchar";
		return true;
	case 'F':
		if (mLanguage == DbgLanguage_Beef)
			outName = "int16";
		else
			outName = "short";
		return true;
	case 'G':
		if (mLanguage == DbgLanguage_Beef)
			outName = "uint16";
		else
			outName = "ushort";
		return true;
	case 'H':
		if (mLanguage == DbgLanguage_Beef)
			outName = "int32";
		else
			outName = "int";
		return true;
	case 'I':
		if (mLanguage == DbgLanguage_Beef)
			outName = "uint32";
		else
			outName = "uint";
		return true;
	case 'J':
		outName = "long";
		return true;
	case 'K':
		outName = "ulong";
		return true;
	case 'M':
		outName = "float";
		return true;
	case 'N':
		outName = "double";
		return true;
	case 'O':
		outName = "long double";
		return true;
	case 'X':
		outName += "void";
		return true;
	case '?':	
		if (mInArgs)
		{
			int num = DemangleNumber();
			//
		}
		else
		{
			DemangleModifiedType(outName, false);
		}
		break;
	default:
		return Failed();
		break;
	}

	return false;
}

int MsDemangler::DemangleNumber()
{	
	char c = SafeGetChar(mMangledName, mCurIdx++);
	bool isSigned = false;
	if (c == '?')
	{		
		isSigned = true;
		c = SafeGetChar(mMangledName, mCurIdx++);		
	}

	if ((c >= '0') && (c <= '9'))
	{
		return c - '0' + 1;
	}

	int val = 0;
	while ((c >= 'A') && (c <= 'P'))
	{
		val = (val * 0x10) + (c - 'A');
		c = SafeGetChar(mMangledName, mCurIdx++);		
	}

	if (c != '@')
		Failed();

	return val;
}

bool MsDemangler::DemangleCV(StringImpl& outName)
{
	String modifier;
	while (true)
	{
		char c = SafeGetChar(mMangledName, mCurIdx++);
		if (c == 'E')
		{
			// __ptr64
		}
		else if (c == 'F')
		{
			// Unaligned
			//BF_ASSERT("Unhandled");
		}
		else if (c == 'I')
		{
			// __restrict
		}
		else
			break;
	}

	mCurIdx--;
	//int modifier = DemangleConst();

	char c = SafeGetChar(mMangledName, mCurIdx++);
	int constVal = 0;
	//BF_ASSERT((c >= 'A') && (c <= 'X'));
	if ((c < 'A') || (c > 'X'))
	{
		return Failed();
	}
	constVal = c - 'A';

	if ((constVal & 3) == 3)
		outName = "const volatile";
	else if ((constVal & 1) != 0)
		outName = "const";
	else if ((constVal & 2) != 0)
		outName = "const";

	/*switch (c)
	{
	case 'A':
	case 'M':
	case 'Q':
	case 'U':
	case 'Y':
	case '2':
	// None
	break;
	case 'B':
	case 'J':
	case 'R':
	case 'V':
	case 'Z':
	case '3':
	modifier = "const";
	break;
	case 'C':
	case 'G':
	case 'K':
	case 'S':
	case 'W':
	case '0':
	modifier = "volatile";
	break;
	case 'D':
	case 'H':
	case 'L':
	modifier = "const volatile";
	break;
	default:
	BF_ASSERT("Unhandled");
	}*/

	return true;
}

bool MsDemangler::DemangleName(StringImpl& outName)
{
	bool appendRetType = false;
	bool hasTemplateArgs = false;

	char c = SafeGetChar(mMangledName, mCurIdx++);
	if (c == '?')
	{		
		c = SafeGetChar(mMangledName, mCurIdx++);
		if (c == '$')
		{
			mCurIdx++;
			c = SafeGetChar(mMangledName, mCurIdx++);
			hasTemplateArgs = true;
		}

		String primaryName;

		const char* funcName = NULL;
		switch (c)
		{
		case '0':
			// Ctor		
			Require(DemangleScopedName(outName, &primaryName));
			if (mLanguage == DbgLanguage_Beef)
				outName += ".this";
			else
				outName += "::" + primaryName;
			break;
		case '1':
			// Dtor
			Require(DemangleScopedName(outName, &primaryName));
			if (mLanguage == DbgLanguage_Beef)
				outName += ".~this";		
			else
				outName += "::~" + primaryName;		
			break;
		case '2': funcName = "operator new"; break;
		case '3': funcName = "operator delete"; break;
		case '4': funcName = "operator="; break;
		case '5': funcName = "operator>>"; break;
		case '6': funcName = "operator<<"; break;
		case '7': funcName = "operator!"; break;
		case '8': funcName = "operator=="; break;
		case '9': funcName = "operator!="; break;
		case 'A': funcName = "operator[]"; break;
		case 'B': 
			funcName = "operator ";
			appendRetType = true;
			break;
		case 'C': funcName = "operator->"; break;
		case 'D': funcName = "operator*"; break;
		case 'E': funcName = "operator++"; break;
		case 'F': funcName = "operator--"; break;
		case 'G': funcName = "operator-"; break;
		case 'H': funcName = "operator+"; break;
		case 'I': funcName = "operator&"; break;
		case 'J': funcName = "operator->*"; break;
		case 'K': funcName = "operator/"; break;
		case 'L': funcName = "operator%"; break;
		case 'M': funcName = "operator<"; break;
		case 'N': funcName = "operator<="; break;
		case 'O': funcName = "operator>"; break;
		case 'P': funcName = "operator>="; break;
		case 'Q': funcName = "operator,"; break;
		case 'R': funcName = "operator()"; break;
		case 'S': funcName = "operator~"; break;
		case 'T': funcName = "operator^"; break;
		case 'U': funcName = "operator|"; break;
		case 'V': funcName = "operator&&"; break;
		case 'W': funcName = "operator||"; break;
		case 'X': funcName = "operator*="; break;
		case 'Y': funcName = "operator+="; break;
		case 'Z': funcName = "operator-="; break;
		case '_':
			{
				c = SafeGetChar(mMangledName, mCurIdx++);
				switch (c)
				{
				case '0': funcName = "operator/="; break;
				case '1': funcName = "operator%="; break;
				case '2': funcName = "operator>>="; break;
				case '3': funcName = "operator<<="; break;
				case '4': funcName = "operator&="; break;
				case '5': funcName = "operator|="; break;
				case '6': funcName = "operator^="; break;
				case '7': funcName = "`vftable'"; break;
				case '8': funcName = "`vbtable'"; break;
				case '9': funcName = "`vcall'"; break;
				case 'A': funcName = "`typeof'"; break;
				case 'B': funcName = "`local static guard'"; break;
					//case 'C': funcName = "`string'"; do_after = 4; break;
				case 'D': funcName = "`vbase destructor'"; break;
				case 'E': funcName = "`vector deleting destructor'"; break;
				case 'F': funcName = "`default constructor closure'"; break;
				case 'G': funcName = "`scalar deleting destructor'"; break;
				case 'H': funcName = "`vector constructor iterator'"; break;
				case 'I': funcName = "`vector destructor iterator'"; break;
				case 'J': funcName = "`vector vbase constructor iterator'"; break;
				case 'K': funcName = "`virtual displacement map'"; break;
				case 'L': funcName = "`eh vector constructor iterator'"; break;
				case 'M': funcName = "`eh vector destructor iterator'"; break;
				case 'N': funcName = "`eh vector vbase constructor iterator'"; break;
				case 'O': funcName = "`copy constructor closure'"; break;
				case 'R':				
					c = SafeGetChar(mMangledName, mCurIdx++);
					switch (c)
					{
					case 0: funcName = "`RTTI Type Descriptor'"; break;
					case 1: funcName = "`RTTI Class Descriptor'"; break;					
					case '2': funcName = "`RTTI Base Class Array'"; break;
					case '3': funcName = "`RTTI Class Hierarchy Descriptor'"; break;
					case '4': funcName = "`RTTI Complete Object Locater"; break;				
					}
					break;
				case 'S': funcName = "`local vftable'"; break;
				case 'T': funcName = "`local vftable constructor closure'"; break;
				case 'U': funcName = "operator new[]"; break;
				case 'V': funcName = "operator delete[]"; break;
				case 'X': funcName = "`placement delete closure'"; break;
				case 'Y': funcName = "`placement delete[] closure'"; break;			
				}
			}
			break;	
		default:
			mCurIdx -= 2;		
			break;
		}

		if (funcName != NULL)
		{
			if (strcmp(funcName, "__BfCtor") == 0)
				funcName = "this";
			else if (strcmp(funcName, "__BfCtorClear") == 0)
				funcName = "this$clear";
			else if (strcmp(funcName, "__BfStaticCtor") == 0)
				funcName = "this";

			if (hasTemplateArgs)
			{
				outName += funcName;
				outName += "<";
				for (int paramIdx = 0; true; paramIdx++)
				{
					String paramType;
					if (!DemangleType(paramType))
						break;
					if (paramType == "void")
						break;

					if (paramIdx > 0)
						outName += ", ";
					outName += paramType;
				}
				outName += ">";
				mSubstituteList.Clear();

				StringT<128> scopeName;
				Require(DemangleScopedName(scopeName, &primaryName));
				if (!scopeName.empty())
				{
					if (mLanguage == DbgLanguage_Beef)
					{						
						scopeName += ".";
					}
					else
						scopeName += "::";
					outName.Insert(0, scopeName);
				}

				/*outName += "(";

				for (int paramIdx = 0; true; paramIdx++)
				{
				String paramType;
				if (!DemangleType(paramType))
				break;
				if (paramType == "void")
				break;

				if (paramIdx > 0)
				outName += ", ";
				outName += paramType;
				}

				outName += ")";*/
			}
			else
			{
				Require(DemangleScopedName(outName, &primaryName));
				if (!primaryName.empty())
				{
					if (mLanguage == DbgLanguage_Beef)
						outName += ".";
					else
						outName += "::";
				}
				outName += funcName;
			}
		}
		else if (outName.empty())
		{
			if (hasTemplateArgs)
			{
				Require(DemangleString(outName));
				primaryName = outName;

				outName += "<";
				for (int paramIdx = 0; true; paramIdx++)
				{
					String paramType;
					if (!DemangleType(paramType))
						break;
					if (paramType == "void")
						break;

					if (paramIdx > 0)
						outName += ", ";
					outName += paramType;
				}
				outName += ">";
				mSubstituteList.Clear();

				String scopeName;
				Require(DemangleScopedName(scopeName, &primaryName));
				if (!scopeName.empty())
				{
					if (mLanguage == DbgLanguage_Beef)
						scopeName += ".";
					else
						scopeName += "::";
					outName.Insert(0, scopeName);
				}
			}
			else
			{
				Require(DemangleScopedName(outName, &primaryName));
			}
		}
	}
	else
	{
		mCurIdx--;
		Require(DemangleScopedName(outName));
	}

	c = SafeGetChar(mMangledName, mCurIdx++);
	if ((c >= '0') && (c <= '9'))
	{
		if ((c == '2') || (c == '3'))
		{
			// Global variable / static member
			String varType;
			Require(DemangleType(varType));		

			c = SafeGetChar(mMangledName, mCurIdx++);
			// 0 = private, 1 = protected, 2 = public
			return true;
		}

		return true;
	}	

	// Method attribute	
	if (c == 0)
		return true;	

	struct
	{
		uint8 mUnused1 : 1;
		uint8 mType : 2; // normal, static, virtual, thunk
		uint8 mAccess : 3; // private, protected, public, non-member
		uint8 mUnused2 : 2;
	} methodData;
	*((uint8*)&methodData) = (c - 'A');

	//int funcTypeVal = c - 'A';
	//int accessType = funcTypeVal >> 3;

	//bool isNonMember = c == 'Y';	
	//bool isVirtual = (c == 'E') || (c == 'M') || (c == 'U');
	//bool hasThis = isVirtual || (c == 'A') || (c == 'I') || (c == 'Q');

	bool isNonMember = methodData.mAccess == 3;
	bool hasThis = !isNonMember && ((methodData.mType == 0) || (methodData.mType == 2));

	// CV qualifier
	if (hasThis)
	{
		String cvQualifier;
		if (!DemangleCV(cvQualifier))
			return false;
		// Ignore
	}

	// Calling convention
	c = SafeGetChar(mMangledName, mCurIdx++);		
	union
	{		
		uint8 mDllExport : 1;
		uint8 mCallingConv : 7; // cdecl, pascal, thiscall, stdcall, fastcall				
	} callType;
	*((uint8*)&callType) = (c - 'A');

	if (callType.mCallingConv > 6)	
		return Failed();

	String retType;
	DemangleType(retType);
	if (appendRetType)
		outName += retType;

	// This was needed for System.ValueType.Equals<_M0>(_M0, ValueType)
	//  Do we need to clear ALWAYS before param list though?
	if (hasTemplateArgs)
		mSubstituteList.Clear();

	mInArgs = true;
	outName += "(";

	for (int paramIdx = 0; true; paramIdx++)
	{
		String paramType;
		if (!DemangleType(paramType))
			break;
		if (paramType == "void")
			break;

		if (paramIdx > 0)
			outName += ", ";
		outName += paramType;
	}

	outName += ")";

	return true;
}

String MsDemangler::Demangle(const StringImpl& mangledName)
{
	StringT<256> outName;

	mMangledName = mangledName;

	char c = mangledName[mCurIdx++];
	BF_ASSERT(c == '?');

	DemangleName(outName);

	if (mFailed)
		return mangledName;
	//return outName;

	//return outName + " : " + mangledName;
	return outName;
}


//////////////////////////////////////////////////////////////////////////

MsDemangleScanner::MsDemangleScanner()
{
	mCurIdx = 0;
	mIsData = false;
}

bool MsDemangleScanner::DemangleString()
{	
	while (true)
	{
		char c = SafeGetChar(mMangledName, mCurIdx++);
		if ((c == '!') || (c == '?'))
			return Failed();		
		if (c == '@')
			break;		
	}

	//mSubstituteList.push_back(outName);
	return true;
}

bool MsDemangleScanner::DemangleTemplateName()
{
	DemangleString();	
		
	for (int paramIdx = 0; true; paramIdx++)
	{		
		if (!DemangleType())
			break;						
	}	

	return true;
}


bool MsDemangleScanner::DemangleScopedName()
{			
	for (int nameIdx = 0; true; nameIdx++)
	{
		if (mFailed)
			return false;

		char c = SafeGetChar(mMangledName, mCurIdx++);		
		if (c == '@')
			return true;

		if (c == '?')
		{
			c = SafeGetChar(mMangledName, mCurIdx++);
			if (c == '$')
			{
				DemangleTemplateName();				
			}
			else if (c == '?')
			{
				return Failed();
			}
			else
			{
				mCurIdx--;
				int num = DemangleNumber();				
			}
			
		}
		else if ((c >= '0') && (c <= '9'))
		{			
			int subIdx = c - '0';
			if (subIdx < mSubstituteList.size())
			{
				//
			}
			else
			{
				return Failed();
			}
		}
		else
		{
			mCurIdx--;			
			Require(DemangleString());
		}				
	}
}

bool MsDemangleScanner::DemangleModifiedType(bool isPtr)
{	
	DemangleCV();	

	char c = SafeGetChar(mMangledName, mCurIdx);
	if (c == 'Y') // Sized array
	{		
		mCurIdx++;
		int dimCount = DemangleNumber();
		for (int dim = 0; dim < dimCount; dim++)
		{
			int dimSize = DemangleNumber();			
		}

		DemangleType();		
	}
	else
	{
		DemangleType();		
	}
	
	return true;
}

bool MsDemangleScanner::DemangleType()
{
	char c = SafeGetChar(mMangledName, mCurIdx++);
	if ((c == 0) || (c == '@'))
		return false;

	switch (c)
	{	
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		{
			if (mFailed)
				return false;
			int subIdx = c - '0';
			if ((subIdx < 0) || (subIdx >= (int)mSubstituteList.size()))
				return Failed();			
			return true;
		}
		break;
	
	case '$':
		{
			char c = SafeGetChar(mMangledName, mCurIdx++);
			switch (c)
			{
			case '0':
				return Failed();
				break;
			case 'D':
				{
					int templateParamNum = DemangleNumber();
					return Failed();
				}
				break;
			case 'F':
				{
					int param1 = DemangleNumber();
					int param2 = DemangleNumber();
					return Failed();
				}
				break;
			case 'G':
				{
					int param1 = DemangleNumber();
					int param2 = DemangleNumber();
					int param3 = DemangleNumber();
					return Failed();
				}
				break;
			case 'Q':
				{
					int nonTypeTemplateParam = DemangleNumber();
					return Failed();
				}
				break;
			case '$':
				{
					char c = SafeGetChar(mMangledName, mCurIdx++);
					if (c == 'C')
					{						
						DemangleCV();
						DemangleType();						
						return true;
					}
					else if (c == 'Q')
					{						
						DemangleCV();
						DemangleType();						
						return true;
					}
				}
				break;
			}
			
		}
		break;
	
	case '_':
		{
			c = SafeGetChar(mMangledName, mCurIdx++);
			switch (c)
			{			
			case 'D':				
				return true;
			case 'E':				
				return true;
			case 'F':				
				return true;
			case 'G':				
				return true;
			case 'H':				
				return true;
			case 'I':				
				return true;
			case 'J':				
				return true;
			case 'K':				
				return true;
			case 'L':				
				return true;
			case 'M':				
				return true;
			case 'W':				
				return true;
			case 'N':				
				return true;
			default:
				return Failed();
				break;
			}
		}
		break;	

	case 'A':
		{
			DemangleModifiedType(false);				
		}
		return true;
	case 'B':
		DemangleModifiedType(false);		
		return true;
	case 'P':		
		DemangleModifiedType(true);	
		return true;
	case 'Q':
		DemangleModifiedType(true);		
		return true;
	case 'R':
		DemangleModifiedType(true);		
		return true;
	case 'S':
		DemangleModifiedType(true);		
		return true;
	case 'T':
		// union
		DemangleScopedName();
		return true;
	case 'U':
		// struct
		DemangleScopedName();
		return true;
	case 'V':
		// class
		DemangleScopedName();
		return true;
	case 'W':
		// enum
		c = SafeGetChar(mMangledName, mCurIdx++);
		Require((c >= '0') && (c <= '8'));
		DemangleScopedName();
		return true;
	
	case 'C':		
		return true;
	case 'D':		
		return true;
	case 'E':		
		return true;
	case 'F':		
		return true;
	case 'G':		
		return true;
	case 'H':		
		return true;
	case 'I':		
		return true;
	case 'J':
		return true;
	case 'K':
		return true;
	case 'M':
		return true;
	case 'N':		
		return true;
	case 'O':		
		return true;
	case 'X':		
		return true;
	case '?':	
		if (mInArgs)
		{
			int num = DemangleNumber();
			//
		}
		else
		{
			DemangleModifiedType(false);
		}
		break;
	default:
		return Failed();
		break;
	}

	return false;
}

int MsDemangleScanner::DemangleNumber()
{	
	char c = SafeGetChar(mMangledName, mCurIdx++);
	bool isSigned = false;
	if (c == '?')
	{		
		isSigned = true;
		c = SafeGetChar(mMangledName, mCurIdx++);		
	}

	if ((c >= '0') && (c <= '9'))
	{
		return c - '0' + 1;
	}

	int val = 0;
	while ((c >= 'A') && (c <= 'P'))
	{
		val = (val * 10) + (c - 'A');
		c = SafeGetChar(mMangledName, mCurIdx++);		
	}

	if (c != '@')
		Failed();

	return val;
}

bool MsDemangleScanner::DemangleCV()
{	
	while (true)
	{
		char c = SafeGetChar(mMangledName, mCurIdx++);
		if (c == 'E')
		{
			// __ptr64
		}
		else if (c == 'F')
		{
			// Unaligned
			//BF_ASSERT("Unhandled");
		}
		else if (c == 'I')
		{
			// __restrict
		}
		else
			break;
	}
	mCurIdx--;
	
	char c = SafeGetChar(mMangledName, mCurIdx++);
	int constVal = 0;	
	if ((c < 'A') || (c > 'X'))
	{
		return Failed();
	}
	constVal = c - 'A';
	return true;
}

bool MsDemangleScanner::DemangleName()
{
	bool appendRetType = false;
	bool hasTemplateArgs = false;

	char c = SafeGetChar(mMangledName, mCurIdx++);
	if (c == '?')
	{		
		// A ?? is always a function
		return true;
	}
	else
	{
		mCurIdx--;
		Require(DemangleScopedName());
	}

	c = SafeGetChar(mMangledName, mCurIdx++);
	if ((c >= '0') && (c <= '9'))
	{
		// Global variable / static member
		mIsData = true;		
		return true;
	}	

	// Not data
	return true;	
}

void MsDemangleScanner::Process(const StringImpl& mangledName)
{	
	mMangledName = mangledName;

	char c = mangledName[mCurIdx++];
	BF_ASSERT(c == '?');

	DemangleName();

	if (mFailed)
	{
		mIsData = false;
	}	
}

//////////////////////////////////////////////////////////////////////////

String BfDemangler::Demangle(const StringImpl& mangledName, DbgLanguage language, Flags flags)
{	
	if (mangledName[0] == '?')
	{
		MsDemangler demangler;
		demangler.mLanguage = language;
		demangler.mBeefFixed = (flags & BfDemangler::Flag_BeefFixed) != 0;
		return demangler.Demangle(mangledName);
	}
	else
	{
		DwDemangler demangler;
		demangler.mLanguage = language;
		demangler.mCaptureTargetType = (flags & BfDemangler::Flag_CaptureTargetType) != 0;
		demangler.mRawDemangle = (flags & BfDemangler::Flag_RawDemangle) != 0;
		demangler.mBeefFixed = (flags & BfDemangler::Flag_BeefFixed) != 0;
		return demangler.Demangle(mangledName);
	}
}

bool BfDemangler::IsData(const StringImpl& mangledName)
{
	if (mangledName[0] == '?')
	{
		MsDemangleScanner demangler;
		demangler.Process(mangledName);
		return demangler.mIsData;
	}
	return false;
}

