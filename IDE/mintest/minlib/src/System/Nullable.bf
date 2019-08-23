using System.Reflection;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace System
{
    public struct Nullable<T> where T : struct
    {
		internal T mValue;
        internal bool mHasValue;
		
		public this(T value)
        {
            mHasValue = true;
            mValue = value;
        }

		[Inline]
        public bool HasValue
        {
            get { return mHasValue; }
        }

		[Inline]
        public T Value
        {
            get
            {
                if (!mHasValue)
                {
                    Debug.FatalError("Nullable object must have a value.");
                }
                    
                return mValue;
            }
        }

		[Inline]
		public ref T ValueRef
		{
		    get mut
		    {
		        if (!mHasValue)
		        {
		            Debug.FatalError("Nullable object must have a value.");
		        }
		            
		        return ref mValue;
		    }
		}
        
        /*public override bool Equals(Object other)
        {
            if (other == null)
                return mHasValue == false;
            if (!(other is Nullable<T>))
                return false;
            
            return Equals((Nullable<T>)other);
        }*/
        
        /*bool Equals(Nullable<T> other)
        {
            if (other.mHasValue != mHasValue)
                return false;
            
            if (mHasValue == false)
                return true;
            
            return other.mValue.Equals(mValue);
        }*/
        
        /*public override int GetHashCode()
        {
            if (!mHasValue)
                return 0;
            
            return mValue.GetHashCode();
        }*/
        
        public T GetValueOrDefault()
        {
            return mValue;
        }
        
        public T GetValueOrDefault(T defaultmValue)
        {
            return mHasValue ? mValue : defaultmValue;
        }
        
        public override void ToString(String str)
        {
            if (mHasValue)
                mValue.ToString(str);
            else
                str.Clear();
        }

		//[Inline]
        public static implicit operator Nullable<T>(T value)
        {
            return Nullable<T>(value);
        }

		//[Inline]
        public static explicit operator T(Nullable<T> value)
        {
            return value.mValue;
        }
    }
}
