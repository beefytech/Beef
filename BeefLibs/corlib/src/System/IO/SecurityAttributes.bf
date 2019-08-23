namespace System.IO
{
	[CRepr]
	struct SecurityAttributes
	{
		internal int32 mLength = (int32)sizeof(SecurityAttributes);
        internal uint8* mSecurityDescriptor = null;
        internal int32 mInheritHandle = 0;
	}
}
