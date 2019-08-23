#include "BfObjects.h"

USING_NS_BF;

Beefy::String bf::System::Object::GetTypeName()
{
	String* strObj = gBfRtCallbacks.String_Alloc();
	Type* type = GetType();
	gBfRtCallbacks.Type_GetFullName(type, strObj);
	Beefy::String str = strObj->CStr();
	gBfRtCallbacks.Object_Delete(strObj);
	return str;
}

Beefy::String bf::System::Type::GetFullName()
{
	String* strObj = gBfRtCallbacks.String_Alloc();
	gBfRtCallbacks.Type_GetFullName(this, strObj);
	Beefy::String str = strObj->CStr();
	gBfRtCallbacks.Object_Delete(strObj);
	return str;
}
