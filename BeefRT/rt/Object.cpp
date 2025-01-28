#include "BfObjects.h"

USING_NS_BF;

Beefy::String bf::System::Object::GetTypeName()
{
	String* strObj = BFRTCALLBACKS.String_Alloc();
	Type* type = _GetType();
	BFRTCALLBACKS.Type_GetFullName(type, strObj);
	Beefy::String str(strObj->ToStringView());
	BFRTCALLBACKS.Object_Delete(strObj);
	return str;
}

Beefy::String bf::System::Type::GetFullName()
{
	String* strObj = BFRTCALLBACKS.String_Alloc();
	BFRTCALLBACKS.Type_GetFullName(this, strObj);
	Beefy::String str(strObj->ToStringView());
	BFRTCALLBACKS.Object_Delete(strObj);
	return str;
}

bf::System::Type_NOFLAGS* bf::System::Type::GetTypeData()
{
	if ((BFRTFLAGS & BfRtFlags_ObjectHasDebugFlags) != 0)
		return BFRTCALLBACKS.ClassVData_GetTypeDataPtr((bf::System::ClassVData*)(mClassVData & ~0xFF));
	else
		return BFRTCALLBACKS.ClassVData_GetTypeDataPtr((bf::System::ClassVData*)(mClassVData));
}