using System.Globalization;

namespace System
{
    public struct TimeSpan : int64, IFormattable
    {
        public const int64 TicksPerMillisecond = 10000;
        private const double MillisecondsPerTick = 1.0 / (double)TicksPerMillisecond;
        
        public const int64 TicksPerSecond = TicksPerMillisecond * 1000;   // 10,000,000
        private const double SecondsPerTick =  1.0 / (double)TicksPerSecond;         // 0.0001
        
        public const int64 TicksPerMinute = TicksPerSecond * 60;         // 600,000,000
        private const double MinutesPerTick = 1.0 / (double)TicksPerMinute; // 1.6666666666667e-9
        
        public const int64 TicksPerHour = TicksPerMinute * 60;        // 36,000,000,000
        private const double HoursPerTick = 1.0 / (double)TicksPerHour; // 2.77777777777777778e-11
        
        public const int64 TicksPerDay = TicksPerHour * 24;          // 864,000,000,000
        private const double DaysPerTick = 1.0 / (double)TicksPerDay; // 1.1574074074074074074e-12
        
        private const int32 MillisPerSecond = 1000;
        private const int32 MillisPerMinute = MillisPerSecond * 60; //     60,000
        private const int32 MillisPerHour = MillisPerMinute * 60;   //  3,600,000
        private const int32 MillisPerDay = MillisPerHour * 24;      // 86,400,000

        protected const int64 MaxSeconds = Int64.MaxValue / TicksPerSecond;
        protected const int64 MinSeconds = Int64.MinValue / TicksPerSecond;

        protected const int64 MaxMilliSeconds = Int64.MaxValue / TicksPerMillisecond;
        protected const int64 MinMilliSeconds = Int64.MinValue / TicksPerMillisecond;
        
        protected const int64 TicksPerTenthSecond = TicksPerMillisecond * 100;

        public static readonly TimeSpan Zero = TimeSpan(0);

		public static readonly TimeSpan MaxValue = TimeSpan(Int64.MaxValue);
		public static readonly TimeSpan MinValue = TimeSpan(Int64.MinValue);

        //public TimeSpan() {
        //    _ticks = 0;
        //}
        
        public this(int64 ticks)
        {
            this = ticks;
        }

		public this(int hours, int minutes, int seconds)
		{
		    this = TimeToTicks(hours, minutes, seconds).Get();
		}

        /*public this(int hours, int minutes, int seconds) {
            _ticks = TimeToTicks(hours, minutes, seconds);
        }*/

        //TODO: This fails too
        /*public this(int days, int hours, int minutes, int seconds)
            : this(days,hours,minutes,seconds,0)
        {
        }*/

        public this(int32 days, int32 hours, int32 minutes, int32 seconds, int32 milliseconds)
        {
            int64 totalMilliSeconds = ((int64)days * 3600 * 24 + (int64)hours * 3600 + (int64)minutes * 60 + seconds) * 1000 + milliseconds;
            /*if (totalMilliSeconds > MaxMilliSeconds || totalMilliSeconds < MinMilliSeconds)
                throw new ArgumentOutOfRangeException(null, Environment.GetResourceString("Overflow_TimeSpanTooLong"));*/
            this = (int64)totalMilliSeconds * TicksPerMillisecond;
        }
        
        public int64 Ticks
        {
            get { return (int64)this; }
        }
        
        public int32 Days
        {
            get { return (int32)((int64)this / TicksPerDay); }
        }
        
        public int32 Hours
        {
            get { return (int32)(((int64)this / TicksPerHour) % 24); }
        }
        
        public int32 Milliseconds
        {
            get { return (int32)(((int64)this / TicksPerMillisecond) % 1000); }
        }
        
        public int32 Minutes
        {
            get { return (int32)(((int64)this / TicksPerMinute) % 60); }
        }
        
        public int32 Seconds
        {
            get { return (int32)(((int64)this / TicksPerSecond) % 60); }
        }
        
        public double TotalDays
        {
            get { return ((double)(int64)this) * DaysPerTick; }
        }
        
        public double TotalHours
        {
            get { return (double)(int64)this * HoursPerTick; }
        }
        
        public double TotalMilliseconds
        {
            get
            {
                double temp = (double)(int64)this * MillisecondsPerTick;
                if (temp > (double)MaxMilliSeconds)
                    return (double)MaxMilliSeconds;
                
                if (temp < (double)MinMilliSeconds)
                    return (double)MinMilliSeconds;
                
                return temp;
            }
        }
        
        public double TotalMinutes
        {
            get { return (double)(int64)this * MinutesPerTick; }
        }
        
        public double TotalSeconds
        {
            get { return (double)(int64)this * SecondsPerTick; }
        }

		protected static Result<int64> TimeToTicks(int hour, int minute, int second) {
		    // totalSeconds is bounded by 2^31 * 2^12 + 2^31 * 2^8 + 2^31,
		    // which is less than 2^44, meaning we won't overflow totalSeconds.
		    int64 totalSeconds = (int64)hour * 3600 + (int64)minute * 60 + (int64)second;
		    if (totalSeconds > MaxSeconds || totalSeconds < MinSeconds)
		        return .Err;
		    return totalSeconds * TicksPerSecond;
		}

		public Result<TimeSpan> Negate()
		{
		    if (Ticks==TimeSpan.MinValue.Ticks)
		        return .Err;
		    return TimeSpan(-(int64)this);
		}

		public override void ToString(String outStr)
		{
		    TimeSpanFormat.[Friend]Format(this, .(), null, outStr);
		}

		public void ToString(String outStr, String format)
		{
		    TimeSpanFormat.[Friend]Format(this, format, null, outStr);
		}

		public void ToString(String outStr, String format, IFormatProvider formatProvider)
		{
		    TimeSpanFormat.[Friend]Format(this, format, formatProvider, outStr);
		}
    }
}
