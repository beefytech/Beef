using System.Reflection;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace System
{
    public struct Nullable<T> where T : struct
    {
		#region Sync with runtime code
		internal T mValue;
        internal bool mHasValue;
		#endregion

		public this(T value)
        {
            mHasValue = true;
            mValue = value;
        }
        
        public bool HasValue
        {
            get { return mHasValue; }
        }
        
        public T Value
        {
            get
            {
            	Debug.Assert(mHasValue, "Value cannot be retrieved on a null nullable.");
                return mValue;
            }
        }

		public ref T ValueRef
		{
		    get mut
		    {
				Debug.Assert(mHasValue, "Value cannot be retrieved on a null nullable.");
		        return ref mValue;
		    }
		}
        
        /*public override bool Equals(object other)
        {
            if (other == null)
                return mHasValue == false;
            if (!(other is Nullable<T>))
                return false;
            
            return Equals((Nullable<T>)other);
        }
        
        bool Equals(Nullable<T> other)
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
        
        public T GetValueOrDefault(T defaultValue)
        {
            return mHasValue ? mValue : defaultValue;
        }
        
        public override void ToString(String str)
        {
            if (mHasValue)
                mValue.ToString(str);
            else
                str.Clear();
        }
        
        public static implicit operator Nullable<T>(T value)
        {
            return Nullable<T>(value);
        }
        
        public static explicit operator T(Nullable<T> value)
        {
			Debug.Assert(value.mHasValue, "Value cannot be retrieved on a null nullable.");
            return value.mValue;
        }
    }
}
