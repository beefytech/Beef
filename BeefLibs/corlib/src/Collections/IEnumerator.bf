using System;

namespace System.Collections
{
    interface IEnumerator<T>
    {
		Result<T> GetNext() mut;
		T Current { get; };
    }

	interface IResettable
	{
		void Reset() mut;
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
