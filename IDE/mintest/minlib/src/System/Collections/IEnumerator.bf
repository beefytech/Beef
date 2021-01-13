using System;

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

namespace System.Collections
{
    interface IEnumerator<T>
    {
        Result<T> GetNext() mut;
    }

	interface IRefEnumerator<T> : IEnumerator<T>
	{
        Result<T*> GetNextRef() mut;
	}
    
    interface IEnumerable<T>
    {
        concrete IEnumerator<T> GetEnumerator();
    }
}
