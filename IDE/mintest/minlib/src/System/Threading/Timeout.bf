// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
//
// <OWNER>[....]</OWNER>

namespace System.Threading {
    using System.Threading;
    using System;
    // A constant used by methods that take a timeout (Object.Wait, Thread.Sleep
    // etc) to indicate that no timeout should occur.
    //
    // <

    public static class Timeout
    {
        //public static readonly TimeSpan InfiniteTimeSpan = TimeSpan(0, 0, 0, 0, Timeout.Infinite);

        public const int32 Infinite = -1;
        internal const uint32 UnsignedInfinite = unchecked((uint32)-1);
    }

}
