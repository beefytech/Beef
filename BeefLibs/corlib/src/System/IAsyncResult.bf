// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
/*============================================================
**
** Interface: IAsyncResult
**
** Purpose: Interface to encapsulate the results of an async
**          operation
**
===========================================================*/
namespace System {
    
    using System;
    using System.Threading;
    public interface IAsyncResult
    {
        bool 	   IsCompleted { get; }
        WaitEvent  AsyncWaitHandle { get; }
        Object     AsyncState      { get; }
        bool       CompletedSynchronously { get; }
    }
}
