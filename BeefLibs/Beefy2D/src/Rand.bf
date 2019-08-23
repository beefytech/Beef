using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Beefy
{
    public class Rand
    {
        static Random sRand = new Random() ~ delete _;

        public static int32 Int()
        {
            return sRand.NextI32();
        }

        public static int32 SInt()
        {
            return sRand.Next(int32.MinValue, int32.MaxValue);
        }

        public static float Float()
        {
            return (float)sRand.NextDouble();
        }

        public static float SFloat()
        {
            return (float)(sRand.NextDouble() * 2) - 1.0f;
        }
    }
}
