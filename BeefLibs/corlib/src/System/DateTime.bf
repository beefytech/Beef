// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics;
using System.Globalization;

namespace System
{
	struct DateTime : IHashable
	{
		 // Number of 100ns ticks per time unit
		private const int64 TicksPerMillisecond = 10000;
		private const int64 TicksPerSecond = TicksPerMillisecond * 1000;
		private const int64 TicksPerMinute = TicksPerSecond * 60;
		private const int64 TicksPerHour = TicksPerMinute * 60;
		private const int64 TicksPerDay = TicksPerHour * 24;

		// Number of milliseconds per time unit
		private const int32 MillisPerSecond = 1000;
		private const int32 MillisPerMinute = MillisPerSecond * 60;
		private const int32 MillisPerHour = MillisPerMinute * 60;
		private const int32 MillisPerDay = MillisPerHour * 24;

		// Number of days in a non-leap year
		private const int32 DaysPerYear = 365;
		// Number of days in 4 years
		private const int32 DaysPer4Years = DaysPerYear * 4 + 1;       // 1461
		// Number of days in 100 years
		private const int32 DaysPer100Years = DaysPer4Years * 25 - 1;  // 36524
		// Number of days in 400 years
		private const int32 DaysPer400Years = DaysPer100Years * 4 + 1; // 146097

		// Number of days from 1/1/0001 to 12/31/1600
		private const int32 DaysTo1601 = DaysPer400Years * 4;          // 584388
		// Number of days from 1/1/0001 to 12/30/1899
		private const int32 DaysTo1899 = DaysPer400Years * 4 + DaysPer100Years * 3 - 367;
		// Number of days from 1/1/0001 to 12/31/1969
		internal const int32 DaysTo1970 = DaysPer400Years * 4 + DaysPer100Years * 3 + DaysPer4Years * 17 + DaysPerYear; // 719,162
		// Number of days from 1/1/0001 to 12/31/9999
		private const int32 DaysTo10000 = DaysPer400Years * 25 - 366;  // 3652059

		internal const int64 MinTicks = 0;
		internal const int64 MaxTicks = DaysTo10000 * TicksPerDay - 1;
		private const int64 MaxMillis = (int64)DaysTo10000 * MillisPerDay;

		private const int64 FileTimeOffset = DaysTo1601 * TicksPerDay;
		private const int64 DoubleDateOffset = DaysTo1899 * TicksPerDay;
		// The minimum OA date is 0100/01/01 (Note it's year 100).
		// The maximum OA date is 9999/12/31
		private const int64 OADateMinAsTicks = (DaysPer100Years - DaysPerYear) * TicksPerDay;
		// All OA dates must be greater than (not >=) OADateMinAsDouble
		private const double OADateMinAsDouble = -657435.0;
		// All OA dates must be less than (not <=) OADateMaxAsDouble
		private const double OADateMaxAsDouble = 2958466.0;

		private const int32 DatePartYear = 0;
		private const int32 DatePartDayOfYear = 1;
		private const int32 DatePartMonth = 2;
		private const int32 DatePartDay = 3;

		private static readonly int32[] DaysToMonth365 = new int32[]{
			0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365} ~ delete _;
		private static readonly int32[] DaysToMonth366 = new int32[]{
			0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366} ~ delete _;

		public static readonly DateTime MinValue = DateTime(MinTicks, DateTimeKind.Unspecified);
		public static readonly DateTime MaxValue = DateTime(MaxTicks, DateTimeKind.Unspecified);

		private const uint64 TicksMask = 0x3FFFFFFFFFFFFFFFUL;
		private const uint64 FlagsMask = 0xC000000000000000;
		private const uint64 LocalMask = 0x8000000000000000;
		private const int64 TicksCeiling = 0x4000000000000000;
		private const uint64 KindUnspecified = 0x0000000000000000UL;
		private const uint64 KindUtc = 0x4000000000000000UL;
		private const uint64 KindLocal = 0x8000000000000000;
		private const uint64 KindLocalAmbiguousDst = 0xC000000000000000;
		private const int32 KindShift = 62;

		private const String TicksField = "ticks";
		private const String DateDataField = "dateData";

		// The data is stored as an unsigned 64-bit integeter
		//   Bits 01-62: The value of 100-nanosecond ticks where 0 represents 1/1/0001 12:00am, up until the value
		//               12/31/9999 23:59:59.9999999
		//   Bits 63-64: A four-state value that describes the DateTimeKind value of the date time, with a 2nd
		//               value for the rare case where the date time is local, but is in an overlapped daylight
		//               savings time hour and it is in daylight savings time. This allows distinction of these
		//               otherwise ambiguous local times and prevents data loss when round tripping from Local to
		//               UTC time.
		private uint64 dateData;

		internal int64 InternalTicks
		{
			get
			{
				return (int64)(dateData & TicksMask);
			}
		}

		private uint64 InternalKind
		{
			get
			{
				return (dateData & FlagsMask);
			}
		}

		public this()
		{
			dateData = 0;
		}

		public this(int64 ticks)
		{
			if (ticks < MinTicks || ticks > MaxTicks)
				Runtime.FatalError();
			//Contract.EndContractBlock();
			dateData = (uint64)ticks;
		}

		private this(uint64 dateData)
		{
			this.dateData = dateData;
		}

		public this(int64 ticks, DateTimeKind kind)
		{
			if (ticks < MinTicks || ticks > MaxTicks)
			{
				Runtime.FatalError();
			}
			if (kind < DateTimeKind.Unspecified || kind > DateTimeKind.Local)
			{
				Runtime.FatalError();
			}
			//Contract.EndContractBlock();
			this.dateData = ((uint64)ticks | ((uint64)kind << KindShift));
		}

		internal this(int64 ticks, DateTimeKind kind, bool isAmbiguousDst)
		{
			if (ticks < MinTicks || ticks > MaxTicks)
			{
				Runtime.FatalError();
			}
			//Contract.Requires(kind == DateTimeKind.Local, "Internal Constructor is for local times only");
			//Contract.EndContractBlock();
			dateData = ((uint64)ticks | (isAmbiguousDst ? KindLocalAmbiguousDst : KindLocal));
		}

		// Constructs a DateTime from a given year, month, and day. The
		// time-of-day of the resulting DateTime is always midnight.
		//
		public this(int year, int month, int day)
		{
			this.dateData = (uint64)DateToTicks(year, month, day);
		}

		public this(int year, int month, int day, int hour, int minute, int second)
		{
			this.dateData = (uint64)(DateToTicks(year, month, day).Get() + TimeToTicks(hour, minute, second).Get());
		}

		private static Result<int64> DateToTicks(int year, int month, int day)
		{
			if (year >= 1 && year <= 9999 && month >= 1 && month <= 12)
			{
				int32[] days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;
				if (day >= 1 && day <= days[month] - days[month - 1])
				{
					int y = year - 1;
					int n = y * 365 + y / 4 - y / 100 + y / 400 + days[month - 1] + day - 1;
					return n * TicksPerDay;
				}
			}
			//throw new ArgumentOutOfRangeException(null, Environment.GetResourceString("ArgumentOutOfRange_BadYearMonthDay"));
			return .Err;
		}

		// Constructs a DateTime from a given year, month, day, hour,
		// minute, and second.
		//
		public this(int year, int month, int day, int hour, int minute, int second, int millisecond)
		{
			if (millisecond < 0 || millisecond >= MillisPerSecond)
			{
				//throw new ArgumentOutOfRangeException("millisecond", Environment.GetResourceString("ArgumentOutOfRange_Range", 0, MillisPerSecond - 1));
				Runtime.FatalError();
			}
			//Contract.EndContractBlock();
			int64 ticks = DateToTicks(year, month, day).Get() + TimeToTicks(hour, minute, second).Get();
			ticks += millisecond * TicksPerMillisecond;
			if (ticks < MinTicks || ticks > MaxTicks)
				Runtime.FatalError();
				//throw new ArgumentException(Environment.GetResourceString("Arg_DateTimeRange"));
			this.dateData = (uint64)ticks;
		}

		private static Result<int64> TimeToTicks(int hour, int minute, int second)
		{
			//TimeSpan.TimeToTicks is a family access function which does no error checking, so
			//we need to put some error checking out here.
			if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60)
			{
				return (TimeSpan.TimeToTicks(hour, minute, second));
			}
			return .Err;
		}

		// Returns the number of days in the month given by the year and
		// month arguments.
		//
		public static Result<int> DaysInMonth(int year, int month)
		{
			if (month < 1 || month > 12)
				return .Err;
				//throw new ArgumentOutOfRangeException("month", Environment.GetResourceString("ArgumentOutOfRange_Month"));
			//Contract.EndContractBlock();
			// IsLeapYear checks the year argument
			int32[] days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;
			return days[month] - days[month - 1];
		}

		public static Result<bool> IsLeapYear(int year)
		{
			if (year < 1 || year > 9999)
			{
				return .Err;
			}
			return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
		}

		// Returns the date part of this DateTime. The resulting value
		// corresponds to this DateTime with the time-of-day part set to
		// zero (midnight).
		//
		public DateTime Date
		{
			get
			{
				int64 ticks = InternalTicks;
				return DateTime((uint64)(ticks - ticks % TicksPerDay) | InternalKind);
			}
		}

		// Returns a given date part of this DateTime. This method is used
		// to compute the year, day-of-year, month, or day part.
		private int32 GetDatePart(int32 part)
		{
			int64 ticks = InternalTicks;
			// n = number of days since 1/1/0001
			int32 n = (int32)(ticks / TicksPerDay);
			// y400 = number of whole 400-year periods since 1/1/0001
			int32 y400 = n / DaysPer400Years;
			// n = day number within 400-year period
			n -= y400 * DaysPer400Years;
			// y100 = number of whole 100-year periods within 400-year period
			int32 y100 = n / DaysPer100Years;
			// Last 100-year period has an extra day, so decrement result if 4
			if (y100 == 4) y100 = 3;
			// n = day number within 100-year period
			n -= y100 * DaysPer100Years;
			// y4 = number of whole 4-year periods within 100-year period
			int32 y4 = n / DaysPer4Years;
			// n = day number within 4-year period
			n -= y4 * DaysPer4Years;
			// y1 = number of whole years within 4-year period
			int32 y1 = n / DaysPerYear;
			// Last year has an extra day, so decrement result if 4
			if (y1 == 4) y1 = 3;
			// If year was requested, compute and return it
			if (part == DatePartYear)
			{
				return y400 * 400 + y100 * 100 + y4 * 4 + y1 + 1;
			}
			// n = day number within year
			n -= y1 * DaysPerYear;
			// If day-of-year was requested, return it
			if (part == DatePartDayOfYear) return n + 1;
			// Leap year calculation looks different from IsLeapYear since y1, y4,
			// and y100 are relative to year 1, not year 0
			bool leapYear = y1 == 3 && (y4 != 24 || y100 == 3);
			int32[] days = leapYear ? DaysToMonth366 : DaysToMonth365;
			// All months have less than 32 days, so n >> 5 is a good conservative
			// estimate for the month
			int32 m = n >> 5 + 1;
			// m = 1-based month number
			while (n >= days[m]) m++;
			// If month was requested, return it
			if (part == DatePartMonth) return m;
			// Return 1-based day-of-month
			return n - days[m - 1] + 1;
		}

		// Returns the day-of-month part of this DateTime. The returned
		// value is an integer between 1 and 31.
		//
		public int32 Day
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 1);
				//Contract.Ensures(Contract.Result<int>() <= 31);
				return GetDatePart(DatePartDay);
			}
		}

		// Returns the day-of-week part of this DateTime. The returned value
		// is an integer between 0 and 6, where 0 indicates Sunday, 1 indicates
		// Monday, 2 indicates Tuesday, 3 indicates Wednesday, 4 indicates
		// Thursday, 5 indicates Friday, and 6 indicates Saturday.
		//
		public DayOfWeek DayOfWeek
		{
			get
			{
				//Contract.Ensures(Contract.Result<DayOfWeek>() >= DayOfWeek.Sunday);
				//Contract.Ensures(Contract.Result<DayOfWeek>() <= DayOfWeek.Saturday);
				return (DayOfWeek)((InternalTicks / TicksPerDay + 1) % 7);
			}
		}

		// Returns the day-of-year part of this DateTime. The returned value
		// is an integer between 1 and 366.
		//
		public int32 DayOfYear
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 1);
				//Contract.Ensures(Contract.Result<int>() <= 366);  // leap year
				return GetDatePart(DatePartDayOfYear);
			}
		}

		// Returns the hash code for this DateTime.
		//
		public int GetHashCode()
		{
			return (int)InternalTicks;
		}

		// Returns the hour part of this DateTime. The returned value is an
		// integer between 0 and 23.
		//
		public int Hour
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 0);
				//Contract.Ensures(Contract.Result<int>() < 24);
				return (int32)((InternalTicks / TicksPerHour) % 24);
			}
		}

		internal bool IsAmbiguousDaylightSavingTime()
		{
			return (InternalKind == KindLocalAmbiguousDst);
		}


		public DateTimeKind Kind
		{
			get
			{
				switch (InternalKind) {
				case KindUnspecified:
					return DateTimeKind.Unspecified;
				case KindUtc:
					return DateTimeKind.Utc;
				default:
					return DateTimeKind.Local;
				}
			}
		}

		// Returns the millisecond part of this DateTime. The returned value
		// is an integer between 0 and 999.
		//
		public int Millisecond
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 0);
				//Contract.Ensures(Contract.Result<int>() < 1000);
				return (int)((InternalTicks / TicksPerMillisecond) % 1000);
			}
		}

		// Returns the minute part of this DateTime. The returned value is
		// an integer between 0 and 59.
		//
		public int Minute
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 0);
				//Contract.Ensures(Contract.Result<int>() < 60);
				return (int)((InternalTicks / TicksPerMinute) % 60);
			}
		}

		/// Returns the second part of this DateTime. The returned value is
		/// an integer between 0 and 59.
		//
		public int Second
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 0);
				//Contract.Ensures(Contract.Result<int>() < 60);
				return (int)((InternalTicks / TicksPerSecond) % 60);
			}
		}

		// Returns the month part of this DateTime. The returned value is an
		// integer between 1 and 12.
		//
		public int Month
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 1);
				return GetDatePart(DatePartMonth);
			}
		}

		// Returns a DateTime representing the current date and time. The
		// resolution of the returned value depends on the system timer. For
		// Windows NT 3.5 and later the timer resolution is approximately 10ms,
		// for Windows NT 3.1 it is approximately 16ms, and for Windows 95 and 98
		// it is approximately 55ms.
		//
		public static DateTime Now
		{
			get
			{
				//Contract.Ensures(Contract.Result<DateTime>().Kind == DateTimeKind.Local);

				DateTime utc = UtcNow;
				bool isAmbiguousLocalDst = false;
				int64 offset = TimeZoneInfo.GetDateTimeNowUtcOffsetFromUtc(utc, out isAmbiguousLocalDst).Ticks;
				int64 tick = utc.Ticks + offset;
				if (tick > DateTime.MaxTicks)
				{
					return DateTime(DateTime.MaxTicks, DateTimeKind.Local);
				}
				if (tick < DateTime.MinTicks)
				{
					return DateTime(DateTime.MinTicks, DateTimeKind.Local);
				}
				return DateTime(tick, DateTimeKind.Local, isAmbiguousLocalDst);
			}
		}

		public static DateTime UtcNow
		{
			get
			{
				//Contract.Ensures(Contract.Result<DateTime>().Kind == DateTimeKind.Utc);
				// following code is tuned for speed. Don't change it without running benchmark.
				int64 ticks = 0;
				ticks = (int64)Platform.BfpSystem_GetTimeStamp();
				return DateTime(((uint64)(ticks + FileTimeOffset)) | KindUtc);
			}
		}

		/*static int64 GetSystemTimeAsFileTime()
		{
			ThrowUnimplemented();
		}*/

		public int64 Ticks
		{
			get
			{
				return InternalTicks;
			}
		}

		// Returns the time-of-day part of this DateTime. The returned value
		// is a TimeSpan that indicates the time elapsed since midnight.
		//
		public TimeSpan TimeOfDay
		{
			get
			{
				return TimeSpan(InternalTicks % TicksPerDay);
			}
		}

		// Returns a DateTime representing the current date. The date part
		// of the returned value is the current date, and the time-of-day part of
		// the returned value is zero (midnight).
		//
		public static DateTime Today
		{
			get
			{
				return DateTime.Now.Date;
			}
		}

		// Returns the year part of this DateTime. The returned value is an
		// integer between 1 and 9999.
		//
		public int32 Year
		{
			get
			{
				//Contract.Ensures(Contract.Result<int>() >= 1 && Contract.Result<int>() <= 9999);
				return GetDatePart(DatePartYear);
			}
		}

		// Checks whether a given year is a leap year. This method returns true if
		// year is a leap year, or false if not.
		//
		public static bool IsLeapYear(int32 year)
		{
			if (year < 1 || year > 9999)
			{
				Runtime.FatalError();
			}
			//Contract.EndContractBlock();
			return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
		}

		public static DateTime SpecifyKind(DateTime value, DateTimeKind kind)
		{
			return DateTime(value.InternalTicks, kind);
		}

		// Creates a DateTime from a Windows filetime. A Windows filetime is
		// a long representing the date and time as the number of
		// 100-nanosecond intervals that have elapsed since 1/1/1601 12:00am.
		//
		public static DateTime FromFileTime(int64 fileTime)
		{
			return FromFileTimeUtc(fileTime).ToLocalTime();
		}

		public static DateTime FromFileTimeUtc(int64 fileTime)
		{
			int64 universalTicks = fileTime + FileTimeOffset;
			return DateTime(universalTicks, .Utc);
		}

		public int64 ToFileTime()
		{
			return ToUniversalTime().ToFileTimeUtc();
		}

		public int64 ToFileTimeUtc()
		{
			// Treats the input as universal if it is not specified
			int64 ticks = ((InternalKind & LocalMask) != 0UL) ? ToUniversalTime().InternalTicks : this.InternalTicks;
			ticks -= FileTimeOffset;
			if (ticks < 0)
			{
				Runtime.FatalError();
			}
			return ticks;
		}

		public TimeSpan Subtract(DateTime value)
		{
			return TimeSpan(InternalTicks - value.InternalTicks);
		}

		public static Result<DateTime> FromBinaryRaw(int64 dateData)
		{
			int64 ticks = dateData & (int64)TicksMask;
			if ((ticks < MinTicks) || (ticks > MaxTicks))
				return .Err;
			return DateTime((uint64)dateData);
		}

		public int64 ToBinaryRaw()
		{
			return (int64)dateData;
		}

		public Result<DateTime> AddTicks(int64 value)
		{
			int64 ticks = InternalTicks;
			if (value > MaxTicks - ticks)
			{
				//throw new ArgumentOutOfRangeException("value", Environment.GetResourceString("ArgumentOutOfRange_DateArithmetic"));
				return .Err;
			}
			if (value < MinTicks - ticks)
			{
				//throw new ArgumentOutOfRangeException("value", Environment.GetResourceString("ArgumentOutOfRange_DateArithmetic"));
				return .Err;
			}
			return DateTime((uint64)(ticks + value) | InternalKind);
		}

		private Result<DateTime> Add(double value, int scale)
		{
			int64 millis = (int64)(value * scale + (value >= 0 ? 0.5 : -0.5));
			if (millis <= -MaxMillis || millis >= MaxMillis)
				return .Err;
				//throw new ArgumentOutOfRangeException("value", Environment.GetResourceString("ArgumentOutOfRange_AddValue"));
			return AddTicks(millis * TicksPerMillisecond);
		}

		// Returns the DateTime resulting from adding the given number of
		// years to this DateTime. The result is computed by incrementing
		// (or decrementing) the year part of this DateTime by value
		// years. If the month and day of this DateTime is 2/29, and if the
		// resulting year is not a leap year, the month and day of the resulting
		// DateTime becomes 2/28. Otherwise, the month, day, and time-of-day
		// parts of the result are the same as those of this DateTime.
		//
		public Result<DateTime> AddYears(int value)
		{
			if (value < -10000 || value > 10000) return .Err;
			return AddMonths(value * 12);
		}

		/// Returns the DateTime resulting from adding a fractional number of
		/// days to this DateTime. The result is computed by rounding the
		/// fractional number of days given by value to the nearest
		/// millisecond, and adding that interval to this DateTime. The
		/// value argument is permitted to be negative.
		//
		public DateTime AddDays(double value)
		{
			return Add(value, MillisPerDay);
		}

		// Returns the DateTime resulting from adding a fractional number of
		// hours to this DateTime. The result is computed by rounding the
		// fractional number of hours given by value to the nearest
		// millisecond, and adding that interval to this DateTime. The
		// value argument is permitted to be negative.
		//
		public DateTime AddHours(double value)
		{
			return Add(value, MillisPerHour);
		}

		// Returns the DateTime resulting from the given number of
		// milliseconds to this DateTime. The result is computed by rounding
		// the number of milliseconds given by value to the nearest integer,
		// and adding that interval to this DateTime. The value
		// argument is permitted to be negative.
		//
		public DateTime AddMilliseconds(double value)
		{
			return Add(value, 1);
		}

		// Returns the DateTime resulting from adding a fractional number of
		// minutes to this DateTime. The result is computed by rounding the
		// fractional number of minutes given by value to the nearest
		// millisecond, and adding that interval to this DateTime. The
		// value argument is permitted to be negative.
		//
		public DateTime AddMinutes(double value)
		{
			return Add(value, MillisPerMinute);
		}

		// Returns the DateTime resulting from adding the given number of
		// months to this DateTime. The result is computed by incrementing
		// (or decrementing) the year and month parts of this DateTime by
		// months months, and, if required, adjusting the day part of the
		// resulting date downwards to the last day of the resulting month in the
		// resulting year. The time-of-day part of the result is the same as the
		// time-of-day part of this DateTime.
		//
		// In more precise terms, considering this DateTime to be of the
		// form y / m / d + t, where y is the
		// year, m is the month, d is the day, and t is the
		// time-of-day, the result is y1 / m1 / d1 + t,
		// where y1 and m1 are computed by adding months months
		// to y and m, and d1 is the largest value less than
		// or equal to d that denotes a valid day in month m1 of year
		// y1.
		//
		public Result<DateTime> AddMonths(int months)
		{
			if (months < -120000 || months > 120000)
				return .Err;
				//throw new ArgumentOutOfRangeException("months", Environment.GetResourceString("ArgumentOutOfRange_DateTimeBadMonths"));

			int y = GetDatePart(DatePartYear);
			int m = GetDatePart(DatePartMonth);
			int d = GetDatePart(DatePartDay);
			int i = m - 1 + months;
			if (i >= 0)
			{
				m = i % 12 + 1;
				y = y + i / 12;
			}
			else
			{
				m = 12 + (i + 1) % 12;
				y = y + (i - 11) / 12;
			}
			if (y < 1 || y > 9999)
			{
				//throw new ArgumentOutOfRangeException("months", Environment.GetResourceString("ArgumentOutOfRange_DateArithmetic"));
				return .Err;
			}
			int days = DaysInMonth(y, m);
			if (d > days) d = days;
			return DateTime((uint64)(Try!(DateToTicks(y, m, d)) + InternalTicks % TicksPerDay) | InternalKind);
		}

		// Returns the DateTime resulting from adding a fractional number of
		// seconds to this DateTime. The result is computed by rounding the
		// fractional number of seconds given by value to the nearest
		// millisecond, and adding that interval to this DateTime. The
		// value argument is permitted to be negative.
		//
		public DateTime AddSeconds(double value)
		{
			return Add(value, MillisPerSecond);
		}

		public static int operator <=>(DateTime lhs, DateTime rhs)
		{
			return lhs.InternalTicks <=> rhs.InternalTicks;
		}

		public static Result<DateTime> operator +(DateTime d, TimeSpan t)
		{
			int64 ticks = d.InternalTicks;
			int64 valueTicks = (int64)t;
			if (valueTicks > MaxTicks - ticks || valueTicks < MinTicks - ticks)
			{
				return .Err;
			}
			return DateTime((uint64)(ticks + valueTicks) | d.InternalKind);
		}

		public Result<DateTime> Subtract(TimeSpan t)
		{
			int64 ticks = InternalTicks;
			int64 valueTicks = (int64)t;
			if (ticks - MinTicks < valueTicks || ticks - MaxTicks > valueTicks)
			{
				return .Err;
			}
			return DateTime((uint64)(ticks + valueTicks) | InternalKind);
		}

		public static DateTime operator -(DateTime d, TimeSpan t)
		{
			int64 ticks = d.InternalTicks;
			int64 valueTicks = (int64)t;
			Runtime.Assert((ticks - MinTicks >= valueTicks && ticks - MaxTicks <= valueTicks));
			return DateTime((uint64)(ticks + valueTicks) | d.InternalKind);
		}

		public static TimeSpan operator -(DateTime lhs, DateTime rhs)
		{
			return TimeSpan(lhs.InternalTicks - rhs.InternalTicks);
		}

		public DateTime ToLocalTime()
		{
			return ToLocalTime(false);
		}

		internal DateTime ToLocalTime(bool throwOnOverflow)
		{
			if (Kind == DateTimeKind.Local)
			{
				return this;
			}
#unwarn
			bool isDaylightSavings = false;
			bool isAmbiguousLocalDst = false;
			//int64 offset = 0;
			//ThrowUnimplemented();
			int64 offset = TimeZoneInfo.GetUtcOffsetFromUtc(this, TimeZoneInfo.Local, out isDaylightSavings, out isAmbiguousLocalDst).Ticks;
#unwarn
			int64 tick = Ticks + offset;
			if (tick > DateTime.MaxTicks)
			{
				if (throwOnOverflow)
					Runtime.FatalError();
				else
					return DateTime(DateTime.MaxTicks, DateTimeKind.Local);
			}
			if (tick < DateTime.MinTicks)
			{
				if (throwOnOverflow)
					Runtime.FatalError();
				else
					return DateTime(DateTime.MinTicks, DateTimeKind.Local);
			}
			return DateTime(tick, DateTimeKind.Local, isAmbiguousLocalDst);
		}


		public void ToLongDateString(String outString)
		{
			DateTimeFormat.Format(this, "D", DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToLongTimeString(String outString)
		{
			DateTimeFormat.Format(this, "T", DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToShortDateString(String outString)
		{
			DateTimeFormat.Format(this, "d", DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToShortTimeString(String outString)
		{
			DateTimeFormat.Format(this, "t", DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToString(String outString)
		{
			DateTimeFormat.Format(this, .(), DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToString(String outString, String format)
		{
			DateTimeFormat.Format(this, format, DateTimeFormatInfo.CurrentInfo, outString);
		}

		public void ToString(String outString, IFormatProvider provider)
		{
			DateTimeFormat.Format(this, .(), DateTimeFormatInfo.GetInstance(provider), outString);
		}

		public void ToString(String outString, String format, IFormatProvider provider)
		{
			DateTimeFormat.Format(this, format, DateTimeFormatInfo.GetInstance(provider), outString);
		}

		public DateTime ToUniversalTime()
		{
			return TimeZoneInfo.ConvertTimeToUtc(this, TimeZoneInfoOptions.NoThrowOnInvalidTime);
		}


		/*public static Boolean TryParse(String s, out DateTime result) {
			return DateTimeParse.TryParse(s, DateTimeFormatInfo.CurrentInfo, DateTimeStyles.None, out result);
		}

		public static Boolean TryParse(String s, IFormatProvider provider, DateTimeStyles styles, out DateTime result) {
			DateTimeFormatInfo.ValidateStyles(styles, "styles");
			return DateTimeParse.TryParse(s, DateTimeFormatInfo.GetInstance(provider), styles, out result);
		}    
			
		public static Boolean TryParseExact(String s, String format, IFormatProvider provider, DateTimeStyles style, out DateTime result) {
			DateTimeFormatInfo.ValidateStyles(style, "style");
			return DateTimeParse.TryParseExact(s, format, DateTimeFormatInfo.GetInstance(provider), style, out result);
		}    

		public static Boolean TryParseExact(String s, String[] formats, IFormatProvider provider, DateTimeStyles style, out DateTime result) {
			DateTimeFormatInfo.ValidateStyles(style, "style");
			return DateTimeParse.TryParseExactMultiple(s, formats, DateTimeFormatInfo.GetInstance(provider), style, out result);
		}*/

		internal static Result<DateTime> TryCreate(int year, int month, int day, int hour, int minute, int second, int millisecond)
		{
			if (year < 1 || year > 9999 || month < 1 || month > 12)
			{
				return .Err;
			}
			int32[] days = Try!(IsLeapYear(year)) ? DaysToMonth366 : DaysToMonth365;
			if (day < 1 || day > days[month] - days[month - 1])
			{
				return .Err;
			}
			if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60 || second < 0 || second >= 60)
			{
				return .Err;
			}
			if (millisecond < 0 || millisecond >= MillisPerSecond)
			{
				return .Err;
			}
			int64 ticks = Try!(DateToTicks(year, month, day)) + Try!(TimeToTicks(hour, minute, second));

			ticks += millisecond * TicksPerMillisecond;
			if (ticks < MinTicks || ticks > MaxTicks)
			{
				return .Err;
			}
			return DateTime(ticks, DateTimeKind.Unspecified);
		}
	}
}
