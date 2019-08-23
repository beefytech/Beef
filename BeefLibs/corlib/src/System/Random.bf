// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
/*============================================================
**
** Class:  Random
**
**
** Purpose: A random number generator.
**
** 
===========================================================*/
namespace System
{
	using System;
	using System.Runtime;
	using System.Runtime.CompilerServices;
	using System.Globalization;
	using System.Diagnostics.Contracts;
	using System.Diagnostics;
	using System.Threading;

	// This class is thread-safe for random results, but not deterministically thread-safe
	public class Random
	{
      //
      // Private Constants 
      //
		private const int32 MBIG = Int32.MaxValue;
		private const int32 MSEED = 161803398;
		private const int32 MZ = 0;
    
      
      //
      // Member Variables
      //
		private int32 inext;
		private int32 inextp;
		private int32[] SeedArray = new int32[56] ~ delete _;

		private int32 sSeed = (int32)Environment.TickCount;

      //
      // Public Constants
      //
    
      //
      // Native Declarations
      //
    
      //
      // Constructors
      //

		public this() : this(Interlocked.Increment(ref sSeed))
		{
		}

		public this(int seed)
		{
			int32 ii;
			int32 mj, mk;
			int32 useSeed = (int32)seed;
    
        //Initialize our Seed array.
        //This algorithm comes from Numerical Recipes in C (2nd Ed.)
			int32 subtraction = (useSeed == Int32.MinValue) ? Int32.MaxValue : Math.Abs(useSeed);
			mj = MSEED - subtraction;
			SeedArray[55] = mj;
			mk = 1;
			for (int32 i = 1; i < 55; i++)
			{  //Apparently the range [1..55] is special (Knuth) and so we're wasting the 0'th position.
				ii = (21 * i) % 55;
				SeedArray[ii] = mk;
				mk = mj - mk;
				if (mk < 0) mk += MBIG;
				mj = SeedArray[ii];
			}
			for (int32 k = 1; k < 5; k++)
			{
				for (int32 i = 1; i < 56; i++)
				{
					SeedArray[i] -= SeedArray[1 + (i + 30) % 55];
					if (SeedArray[i] < 0) SeedArray[i] += MBIG;
				}
			}
			inext = 0;
			inextp = 21;
			useSeed = 1;
		}
    
      //
      // Package Private Methods
      //
    
      /*====================================Sample====================================
      **Action: Return a new random number [0..1) and reSeed the Seed array.
      **Returns: A double [0..1)
      **Arguments: None
      **Exceptions: None
      ==============================================================================*/
		protected virtual double Sample()
		{
          //Including this division at the end gives us significantly improved
          //random number distribution.
			return (InternalSample() * (1.0 / MBIG));
		}

		private int32 InternalSample()
		{
			int32 retVal;
			int32 locINext = inext;
			int32 locINextp = inextp;

			if (++locINext >= 56) locINext = 1;
			if (++locINextp >= 56) locINextp = 1;

			retVal = SeedArray[locINext] - SeedArray[locINextp];

			if (retVal == MBIG) retVal--;
			if (retVal < 0) retVal += MBIG;

			SeedArray[locINext] = retVal;

			inext = locINext;
			inextp = locINextp;

			return retVal;
		}

      //
      // Public Instance Methods
      // 
    
    
      /*=====================================Next=====================================
      **Returns: An int [0..Int32.MaxValue)
      **Arguments: None
      **Exceptions: None.
      ==============================================================================*/
		public virtual int32 NextI32()
		{
			return InternalSample() & 0x7FFFFFFF;
		}

		public virtual int32 NextS32()
		{
			return InternalSample();
		}

		public virtual int64 NextI64()
		{
			return (((int64)InternalSample() << 32) | InternalSample()) & 0x7FFFFFFF'FFFFFFFFL;
		}

		public virtual int64 NextS64()
		{
			return (((int64)InternalSample() << 32) | InternalSample());
		}

		private double GetSampleForLargeRange()
		{
          // The distribution of double value returned by Sample 
          // is not distributed well enough for a large range.
          // If we use Sample for a range [Int32.MinValue..Int32.MaxValue)
          // We will end up getting even numbers only.
			int32 result = InternalSample();
          // Note we can't use addition here. The distribution will be bad if we do that.
			bool negative = (InternalSample() % 2 == 0) ? true : false;  // decide the sign based on second sample
			if (negative)
			{
				result = -result;
			}
			double d = result;
			d += (Int32.MaxValue - 1); // get a number in range [0 .. 2 * Int32MaxValue - 1)
			d /= 2 * (uint32)Int32.MaxValue - 1;
			return d;
		}


      /*=====================================Next=====================================
      **Returns: An int [minvalue..maxvalue)
      **Arguments: minValue -- the least legal value for the Random number.
      **           maxValue -- One greater than the greatest legal return value.
      **Exceptions: None.
      ==============================================================================*/
		public virtual int32 Next(int32 minValue, int32 maxValue)
		{
			if (minValue > maxValue)
			{
				Runtime.FatalError();
			}
			Contract.EndContractBlock();

			int64 range = (int64)maxValue - minValue;
			if (range <= (int64)Int32.MaxValue)
			{
				return ((int32)(Sample() * range) + minValue);
			}
			else
			{
				return (int32)((int64)(GetSampleForLargeRange() * range) + minValue);
			}
		}

		public virtual int64 Next(int64 minValue, int64 maxValue)
		{
			if (minValue > maxValue)
			{
				Runtime.FatalError();
			}
			Contract.EndContractBlock();

			int64 range = (int64)maxValue - minValue;
			if (range <= (int64)Int32.MaxValue)
			{
				return ((int32)(Sample() * range) + minValue);
			}
			else
			{
				return (int32)((int64)(GetSampleForLargeRange() * range) + minValue);
			}
		}
    
    
      /*=====================================Next=====================================
      **Returns: An int [0..maxValue)
      **Arguments: maxValue -- One more than the greatest legal return value.
      **Exceptions: None.
      ==============================================================================*/
		public virtual int32 Next(int32 maxValue)
		{
			if (maxValue < 0)
			{
				Runtime.FatalError();
			}
			Contract.EndContractBlock();
			return (.)(Sample() * maxValue);
		}

		public virtual int64 Next(int64 maxValue)
		{
			if (maxValue < 0)
			{
				Runtime.FatalError();
			}
			Contract.EndContractBlock();
			return (.)(Sample() * maxValue);
		}
    
    
      /*=====================================Next=====================================
      **Returns: A double [0..1)
      **Arguments: None
      **Exceptions: None
      ==============================================================================*/
		public virtual double NextDouble()
		{
			return Sample();
		}

		public virtual double NextDoubleSigned()
		{
			return (InternalSample() * (2.0 / MBIG)) - 1.0;
		}

    
      /*==================================NextBytes===================================
      **Action:  Fills the byte array with random bytes [0..0x7f].  The entire array is filled.
      **Returns:Void
      **Arugments:  buffer -- the array to be filled.
      **Exceptions: None
      ==============================================================================*/
		public virtual void NextBytes(uint8[] buffer)
		{
			if (buffer == null) Runtime.FatalError();
			Contract.EndContractBlock();
			for (int32 i = 0; i < buffer.Count; i++)
			{
				buffer[i] = (uint8)(InternalSample() % ((int32)UInt8.MaxValue + 1));
			}
		}
	}

	static
	{
		public static Random gRand = new Random() ~ delete _;
	}
}
