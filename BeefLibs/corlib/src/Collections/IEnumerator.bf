using System;

namespace System.Collections
{
    interface IEnumerator<T>
    {
		Result<T> GetNext() mut;
    }

	interface IResettable
	{
		void Reset() mut;
	}
    
	interface IRefEnumerator<T>
	{
		Result<T> GetNextRef() mut;
	}

    concrete interface IEnumerable<T>
    {
        concrete IEnumerator<T> GetEnumerator();
    }

	concrete interface IRefEnumerable<T>
	{
	    concrete IRefEnumerator<T> GetEnumerator();
	}
}
