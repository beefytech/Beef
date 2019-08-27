using System;

namespace System.Collections.Generic
{
    interface IEnumerator<T>
    {
		Result<T> GetNext() mut;
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
