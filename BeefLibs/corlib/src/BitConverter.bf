using System;
using System.Diagnostics.Contracts;
using System.Security;
using System.Diagnostics;

namespace System
{
    public static class BitConverter
    {
#if BIGENDIAN
        public static readonly bool IsLittleEndian /* = false */;
#else
        public static readonly bool IsLittleEndian = true;
#endif

		public static TTo Convert<TFrom, TTo>(TFrom from)
		{
			Debug.Assert(sizeof(TFrom) == sizeof(TTo));
			var from;
			return *(TTo*)(&from);
		}
    }
}
