namespace System.IO
{
	[CRepr]
	struct SecurityAttributes
	{
		int32 mLength = (int32)sizeof(SecurityAttributes);
        uint8* mSecurityDescriptor = null;
        int32 mInheritHandle = 0;
	}
}
