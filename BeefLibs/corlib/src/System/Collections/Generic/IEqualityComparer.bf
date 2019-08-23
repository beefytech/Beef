namespace System.Collections.Generic
{
    using System;

    public interface IEqualityComparer</*in*/ T>
    {
        bool Equals(T x, T y);
        int GetHashCode(T obj);                
    }
}

