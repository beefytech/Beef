using System;
using System.Runtime.InteropServices;

namespace System.Collections
{
    interface IEnumerator
    {
        Object Current { get; }
        bool MoveNext();
        void Reset();
        void Dispose();    
    }
    
    interface IEnumerable
    {
        IEnumerator GetEnumerator();
    }
}

namespace System.Collections.Generic
{
    interface IEnumerator<T>
    {
        Result<T> GetNext() mut;
    }

	interface IRefEnumerator<T> : IEnumerator<T>
	{
        Result<T*> GetNextRef() mut;
	}
    
    concrete interface IEnumerable<T>
    {
        concrete IEnumerator<T> GetEnumerator();
    }
}
