using System.Reflection;
using System.Collections;
using System.Diagnostics;

namespace System
{
    struct Nullable<T> where T : struct
    {
        T mValue;
        bool mHasValue;
        
        public this(T value)
        {
            mHasValue = true;
            mValue = value;
        }

        public bool HasValue
        {
			[Inline]
            get { return mHasValue; }
        }

        public T Value
        {
			[Inline]
            get
            {
                if (!mHasValue)
                {
                    Debug.FatalError("Value requested for null nullable.");
                }
                    
                return mValue;
            }
        }

		public T ValueOrDefault
		{
			[Inline]
		    get
		    {
		        return mValue;
		    }
		}

        public ref T ValueRef
        {
			[Inline]
            get mut
            {
                if (!mHasValue)
                {
                    Debug.FatalError("Value requested for null nullable.");
                }
                    
                return ref mValue;
            }
        }
        
        public T GetValueOrDefault()
        {
            return mValue;
        }

        public bool TryGetValue(ref T outValue)
        {
            if (!mHasValue)
                return false;
            outValue = mValue;
            return true;
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

        [Inline]
        public static implicit operator Nullable<T>(T value)
        {
			Nullable<T> result;
			result.mHasValue = true;
			result.mValue = value;
			return result;
        }

		[Inline]
		public static Nullable<T> operator implicit <TOther>(TOther value) where T : operator implicit TOther
		{
			Nullable<T> result;
			result.mHasValue = true;
			result.mValue = value;
			return result;
		}

        [Inline]
        public static explicit operator T(Nullable<T> value)
        {
			if (!value.mHasValue)
			    Debug.FatalError("Value requested for null nullable.");
            return value.mValue;
        }

		[Inline]
		public static ref T operator->(ref Nullable<T> value)
		{
			if (!value.mHasValue)
				Debug.FatalError("Value requested for null nullable.");
		    return ref value.mValue;
		}

        [Inline, Commutable]
        public static bool operator==(Nullable<T> lhs, T rhs)
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue == rhs;
        }

        ///

        public static bool operator==<TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T == TOther
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue == rhs;
        }

        public static bool operator==<TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther == T
        {
            if (!rhs.mHasValue) return false;
            return lhs == rhs.mValue;
        }

        public static bool operator==<TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T == TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return !lhs.mHasValue && !rhs.mHasValue; // Only both being null results in 'true'
            return lhs.mValue == rhs.mValue;
        }

        ///

        public static bool operator!=<TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T != TOther
        {
            if (!lhs.mHasValue) return true;
            return lhs.mValue != rhs;
        }

        public static bool operator!=<TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther != T
        {
            if (!rhs.mHasValue) return true;
            return lhs != rhs.mValue;
        }

        public static bool operator!=<TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T != TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return !(!lhs.mHasValue && !rhs.mHasValue); // Only both being null results in 'false'
            return lhs.mValue != rhs.mValue;
        }

        ///

        public static bool operator< <TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T < TOther
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue < rhs;
        }

        public static bool operator< <TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther < T
        {
            if (!rhs.mHasValue) return false;
            return lhs < rhs.mValue;
        }

        public static bool operator< <TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T < TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return false;
            return lhs.mValue < rhs.mValue;
        }

        ///

        public static bool operator<=<TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T <= TOther
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue <= rhs;
        }

        public static bool operator<=<TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther <= T
        {
            if (!rhs.mHasValue) return false;
            return lhs <= rhs.mValue;
        }

        public static bool operator<=<TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T <= TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return false;
            return lhs.mValue <= rhs.mValue;
        }

        ///

        public static bool operator><TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T > TOther
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue > rhs;
        }

        public static bool operator><TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther > T
        {
            if (!rhs.mHasValue) return false;
            return lhs > rhs.mValue;
        }

        public static bool operator><TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T > TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return false;
            return lhs.mValue > rhs.mValue;
        }

        ///

        public static bool operator>=<TOther>(Nullable<T> lhs, TOther rhs) where bool : operator T >= TOther
        {
            if (!lhs.mHasValue) return false;
            return lhs.mValue >= rhs;
        }

        public static bool operator>=<TOther>(TOther lhs, Nullable<T> rhs) where bool : operator TOther >= T
        {
            if (!rhs.mHasValue) return false;
            return lhs >= rhs.mValue;
        }

        public static bool operator>=<TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where bool : operator T >= TOther where TOther : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return false;
            return lhs.mValue >= rhs.mValue;
        }

        ///

        public static int operator<=><TOther>(Nullable<T> lhs, TOther rhs) where int : operator T <=> TOther
        {
            return lhs.mValue <=> rhs;
        }

        public static int operator<=><TOther>(TOther lhs, Nullable<T> rhs) where int : operator TOther <=> T
        {
            return lhs <=> rhs.mValue;
        }

        public static int operator<=><TOther>(Nullable<T> lhs, Nullable<TOther> rhs) where int : operator T <=> TOther where TOther : struct
        {
            return lhs.mValue <=> rhs.mValue;
        }

        ///

        public static TResult? operator+<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T + TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue + rhs);
        }
        public static TResult? operator+<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther + T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs + rhs.mValue);
        }
        public static TResult? operator+<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T + TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue + rhs.mValue);
        }

        ///

        public static TResult? operator-<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther - T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs - rhs.mValue);
        }

        public static TResult? operator-<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T - TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue - rhs);
        }

        public static TResult? operator-<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T - TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue - rhs.mValue);
        }

        //

        public static TResult? operator*<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther * T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs * rhs.mValue);
        }

        public static TResult? operator*<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T * TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue * rhs);
        }

        public static TResult? operator*<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T * TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue * rhs.mValue);
        }

        //

        public static TResult? operator/<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther / T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs / rhs.mValue);
        }

        public static TResult? operator/<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T / TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue / rhs);
        }

        public static TResult? operator/<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T / TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue / rhs.mValue);
        }

        //

        public static TResult? operator%<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther % T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs % rhs.mValue);
        }

        public static TResult? operator%<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T % TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue % rhs);
        }

        public static TResult? operator%<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T % TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue % rhs.mValue);
        }

        //

        public static TResult? operator^<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther ^ T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs ^ rhs.mValue);
        }

        public static TResult? operator^<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T ^ TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue ^ rhs);
        }

        public static TResult? operator^<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T ^ TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue ^ rhs.mValue);
        }

        //

        public static TResult? operator&<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther & T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs & rhs.mValue);
        }

        public static TResult? operator&<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T & TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue & rhs);
        }

        public static TResult? operator&<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T & TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue & rhs.mValue);
        }

        //

        public static TResult? operator|<TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther | T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs | rhs.mValue);
        }

        public static TResult? operator|<TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T | TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue | rhs);
        }

        public static TResult? operator|<TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T | TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue | rhs.mValue);
        }

        //

        public static TResult? operator<< <TOther, TResult>(TOther lhs, Nullable<T> rhs) where TResult : operator TOther << T where TResult : struct
        {
            if (!rhs.mHasValue) return null;
            return Nullable<TResult>(lhs << rhs.mValue);
        }

        public static TResult? operator<< <TOther, TResult>(Nullable<T> lhs, TOther rhs) where TResult : operator T << TOther where TResult : struct
        {
            if (!lhs.mHasValue) return null;
            return Nullable<TResult>(lhs.mValue << rhs);
        }

        public static TResult? operator<< <TOther, TResult>(Nullable<T> lhs, Nullable<TOther> rhs) where TOther : struct where TResult : operator T << TOther where TResult : struct
        {
            if ((!lhs.mHasValue) || (!rhs.mHasValue)) return null;
            return Nullable<TResult>(lhs.mValue << rhs.mValue);
        }
    }

    extension Nullable<T> : IHashable where T : IHashable
    {
        public int GetHashCode()
        {
            if (!mHasValue)
                return 0;
            return mValue.GetHashCode();
        }
    }
}
