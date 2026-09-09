using System;

namespace FMOD
{
    [CRepr]
    public struct CPU_USAGE
    {
        public float dsp;
        public float stream;
        public float geometry;
        public float update;
        public float convolution1;
        public float convolution2;
    }
}
