using System.Collections;
namespace System.Globalization
{
	enum MonthNameStyles {
	    Regular     = 0x00000000,
	    Genitive    = 0x00000001,
	    LeapYear    = 0x00000002,
	}

	enum DateTimeFormatFlags {
	    None                    = 0x00000000,
	    UseGenitiveMonth        = 0x00000001,
	    UseLeapYearMonth        = 0x00000002,
	    UseSpacesInMonthNames   = 0x00000004, // Has spaces or non-breaking space in the month names.
	    UseHebrewRule           = 0x00000008,   // Format/Parse using the Hebrew calendar rule.
	    UseSpacesInDayNames     = 0x00000010,   // Has spaces or non-breaking space in the day names.
	    UseDigitPrefixInTokens  = 0x00000020,   // Has token starting with numbers.

	    NotInitialized          = -1,
	}

	class DateTimeFormatInfo : IFormatProvider
	{
		//
		// Note, some fields are derived so don't really need to be serialized, but we can't mark as
		// optional because Whidbey was expecting them.  Post-Arrowhead we could fix that
		// once Whidbey serialization is no longer necessary.
		//

		// cache for the invariant culture.
		// invariantInfo is constant irrespective of your current culture.
		private static volatile DateTimeFormatInfo invariantInfo ~ delete _;

		// an index which points to a record in Culture Data Table.
		private CultureData m_cultureData;

		// The culture name used to create this DTFI.
		private String m_name = null;

		// The language name of the culture used to create this DTFI.
		private String m_langName = null;

		// CompareInfo usually used by the parser.
		//private CompareInfo m_compareInfo = null;

		// Culture matches current DTFI. mainly used for string comparisons during parsing.
		private CultureInfo m_cultureInfo = null;

		//
		// Caches for various properties.
		//

		// 

		//NotImpl: Shouldn't be initialized
		String amDesignator     = "AM";
		String pmDesignator     = "PM";
		String dateSeparator    = "/";            // derived from short date (whidbey expects, arrowhead doesn't)
		String generalShortTimePattern = null;     // short date + short time (whidbey expects, arrowhead doesn't)
		String generalLongTimePattern  = null;     // short date + long time (whidbey expects, arrowhead doesn't)
		String timeSeparator    = ":";            // derived from long time (whidbey expects, arrowhead doesn't)
		String monthDayPattern  = null;
		String dateTimeOffsetPattern = null;

		//
		// The following are constant values.
		//
		const String rfc1123Pattern   = "ddd, dd MMM yyyy HH':'mm':'ss 'GMT'";

		// The sortable pattern is based on ISO 8601.
		const String sortableDateTimePattern = "yyyy'-'MM'-'dd'T'HH':'mm':'ss";
		const String universalSortableDateTimePattern = "yyyy'-'MM'-'dd HH':'mm':'ss'Z'";

		//
		// The following are affected by calendar settings.
		//
		Calendar calendar  = null;

		int firstDayOfWeek = -1;
		int calendarWeekRule = -1;

		String fullDateTimePattern  = null;        // long date + long time (whidbey expects, arrowhead doesn't)

		String[] abbreviatedDayNames    = null;

		String[] m_superShortDayNames    = null;

		String[] dayNames                 = null;
		String[] abbreviatedMonthNames    = null;
		String[] monthNames               = null;
		// Cache the genitive month names that we retrieve from the data table.
		String[] genitiveMonthNames       = null;

		// Cache the abbreviated genitive month names that we retrieve from the data table.
		String[] m_genitiveAbbreviatedMonthNames = null;

		// Cache the month names of a leap year that we retrieve from the data table.
		String[] leapYearMonthNames     = null;

		// For our "patterns" arrays we have 2 variables, a string and a string[]
		//
		// The string[] contains the list of patterns, EXCEPT the default may not be included.
		// The string contains the default pattern.
		// When we initially construct our string[], we set the string to string[0]

		// The "default" Date/time patterns 
		String longDatePattern  = "dddd, MMMM d, yyyy";
		String shortDatePattern = "M/d/yyyy";
		String yearMonthPattern = "MMMM yyyy";
		String longTimePattern  = null;
		String shortTimePattern = null;

		// These are Whidbey-serialization compatable arrays (eg: default not included)
		// "all" is a bit of a misnomer since the "default" pattern stored above isn't
		// necessarily a member of the list
		String[] allYearMonthPatterns   = null;   // This was wasn't serialized in Whidbey
		String[] allShortDatePatterns   = null;
		String[] allLongDatePatterns    = null;
		String[] allShortTimePatterns   = null;
		String[] allLongTimePatterns    = null;

		// Cache the era names for this DateTimeFormatInfo instance.
		String[] m_eraNames = null;
		String[] m_abbrevEraNames = null;
		String[] m_abbrevEnglishEraNames = null;

		int[] optionalCalendars = null;

		private const int DEFAULT_ALL_DATETIMES_SIZE = 132;

		// CultureInfo updates this
		bool m_isReadOnly=false;

		// This flag gives hints about if formatting/parsing should perform special code path for things like
		// genitive form or leap year month names.
		DateTimeFormatFlags formatFlags = DateTimeFormatFlags.NotInitialized;
		//internal static bool preferExistingTokens = InitPreferExistingTokens();

		List<Object> ownedObjects = new .() ~ DeleteContainerAndItems!(_);

		public this()
			: this(CultureInfo.InvariantCulture.[Friend]m_cultureData,
					GregorianCalendar.[Friend]GetDefaultInstance())
		{

		}

		public ~this()
		{

		}

		public String AllocString()
		{
			var str = new String();
			ownedObjects.Add(str);
			return str;
		}

		public this(CultureData cultureData, Calendar calendar)
		{
			this.m_cultureData = cultureData;
			this.calendar = calendar;
		}

		public static DateTimeFormatInfo CurrentInfo
		{
			get
			{
				CultureInfo culture = CultureInfo.CurrentCulture;
				if (!culture.[Friend]m_isInherited)
				{
				    DateTimeFormatInfo info = culture.[Friend]dateTimeInfo;
				    if (info != null) {
				        return info;
				    }
				}
				return (DateTimeFormatInfo)culture.GetFormat(typeof(DateTimeFormatInfo));
			}
		}

		public static DateTimeFormatInfo InvariantInfo {
		    get
			{
		        if (invariantInfo == null)
		        {
		            DateTimeFormatInfo info = new DateTimeFormatInfo();
		            info.Calendar.[Friend]SetReadOnlyState(true);
		            info.m_isReadOnly = true;
		            invariantInfo = info;
		        }
		        return (invariantInfo);
		    }
		}

		public Calendar Calendar
		{
			get
			{
				return calendar;
			}
		}

		public StringView AMDesignator
		{
			get
			{
				return amDesignator;
			}
		}

		public StringView PMDesignator
		{
			get
			{
				return pmDesignator;
			}
		}

		public StringView TimeSeparator
		{
			get
			{
				if (timeSeparator == null)
				{
				    timeSeparator = this.m_cultureData.[Friend]TimeSeparator;
				}
				return timeSeparator;
			}
		}

		public StringView DateSeparator
		{
			get
			{
				if (this.dateSeparator == null)
				{
					Runtime.NotImplemented();
				    //this.dateSeparator = this.m_cultureData.DateSeparator(Calendar.ID);
				}
				return dateSeparator;
			}
		}

		public bool HasForceTwoDigitYears
		{
			get
			{
				return false;
			}
		}

		public DateTimeFormatFlags FormatFlags
		{
			get
			{
				if (formatFlags == DateTimeFormatFlags.NotInitialized)
				{
				    // Build the format flags from the data in this DTFI
				    formatFlags = DateTimeFormatFlags.None;
				    /*formatFlags |= (DateTimeFormatFlags)DateTimeFormatInfoScanner.GetFormatFlagGenitiveMonth(
				        MonthNames, internalGetGenitiveMonthNames(false), AbbreviatedMonthNames, internalGetGenitiveMonthNames(true));
				    formatFlags |= (DateTimeFormatFlags)DateTimeFormatInfoScanner.GetFormatFlagUseSpaceInMonthNames(
				        MonthNames, internalGetGenitiveMonthNames(false), AbbreviatedMonthNames, internalGetGenitiveMonthNames(true));
				    formatFlags |= (DateTimeFormatFlags)DateTimeFormatInfoScanner.GetFormatFlagUseSpaceInDayNames(DayNames, AbbreviatedDayNames);
				    formatFlags |= (DateTimeFormatFlags)DateTimeFormatInfoScanner.GetFormatFlagUseHebrewCalendar((int)Calendar.ID);*/                    
				}
				return (formatFlags);
			}
		}

		public StringView ShortDatePattern
		{
			get
			{
				// Initialize our short date pattern from the 1st array value if not set
				if (this.shortDatePattern == null)
				{
				    // Initialize our data
				    this.shortDatePattern = this.UnclonedShortDatePatterns[0];
				}

				return this.shortDatePattern;
			}
		}

		public StringView LongDatePattern
		{
			get
			{
				// Initialize our long date pattern from the 1st array value if not set
				if (this.longDatePattern == null)
				{
				    // Initialize our data
				    this.longDatePattern = this.UnclonedLongDatePatterns[0];
				}

				return this.longDatePattern;
			}
		}

		public StringView ShortTimePattern
		{
			get
			{
				// Initialize our short time pattern from the 1st array value if not set
				if (this.shortTimePattern == null)
				{
				    // Initialize our data
				    this.shortTimePattern = this.UnclonedShortTimePatterns[0];
				}
				return this.shortTimePattern;
			}
		}

		public StringView FullDateTimePattern
		{
			get
			{
				if (fullDateTimePattern == null)
					fullDateTimePattern = AllocString()..Append("dddd, MMMM d, yyyy h:mm:ss tt");
				return fullDateTimePattern;
			}
		}

		public StringView DateTimeOffsetPattern
		{
			get
			{
				if (dateTimeOffsetPattern == null)
					dateTimeOffsetPattern = AllocString()..Append("M/d/yyyy h:mm:ss tt zzz");
				return dateTimeOffsetPattern;
			}
		}

		public StringView GeneralShortTimePattern
		{
			get {
			    if (generalShortTimePattern == null) {
			        generalShortTimePattern = AllocString()..AppendF("{0} {1}", ShortDatePattern, ShortTimePattern);
			    }
			    return (generalShortTimePattern);
			}
		}

		public StringView GeneralLongTimePattern
		{
			get
			{
				if (generalLongTimePattern == null) {
				    generalLongTimePattern = AllocString()..AppendF("{0} {1}", ShortDatePattern, LongTimePattern);
				}
				return (generalLongTimePattern);
			}
		}

		public StringView MonthDayPattern
		{
			get 
			{
			    if (this.monthDayPattern == null) 
			    {
			        this.monthDayPattern = this.m_cultureData.[Friend]MonthDay(Calendar.ID);
			    }
			    //Contract.Assert(this.monthDayPattern != null, "DateTimeFormatInfo.MonthDayPattern, monthDayPattern != null");
			    return (this.monthDayPattern);
			}

			set {
			    /*if (IsReadOnly)
			        throw new InvalidOperationException(Environment.GetResourceString("InvalidOperation_ReadOnly"));
			    if (value == null) {
			        throw new ArgumentNullException("value",
			            Environment.GetResourceString("ArgumentNull_String"));
			    }
			    Contract.EndContractBlock();

			    this.monthDayPattern = value;*/
				Runtime.NotImplemented();
			}
		}

		public StringView RFC1123Pattern
		{
			get
			{
				return (rfc1123Pattern);
			}
		}

		public StringView SortableDateTimePattern
		{
			get
			{
				return sortableDateTimePattern;
			}
		}

		public StringView LongTimePattern
		{
			get
			{
				if (this.longTimePattern == null)
				{
				    // Initialize our data
				    this.longTimePattern = this.UnclonedLongTimePatterns[0];
				}

				return this.longTimePattern;  
			}
		}

		public StringView UniversalSortableDateTimePattern
		{
			get
			{
				return universalSortableDateTimePattern;
			}
		}

		public StringView YearMonthPattern
		{
			get
			{
				if (this.yearMonthPattern == null)
				{
				    // Initialize our data
				    this.yearMonthPattern = this.UnclonedYearMonthPatterns[0];
				}
				return this.yearMonthPattern;
			}
		}

		public void GetAbbreviatedDayName(DayOfWeek dayofweek, String outStr)
		{
			outStr.Append(CalendarData.[Friend]Invariant.[Friend]saAbbrevDayNames[(int)dayofweek]);
		}

		public void GetDayName(DayOfWeek dayofweek, String outStr)
		{
			outStr.Append(CalendarData.[Friend]Invariant.[Friend]saDayNames[(int)dayofweek]);
		}

		public void GetAbbreviatedMonthName(int month, String outStr)
		{
			outStr.Append(CalendarData.[Friend]Invariant.[Friend]saAbbrevMonthNames[month - 1]);
		}

		public void GetMonthName(int month, String outStr)
		{
			outStr.Append(CalendarData.[Friend]Invariant.[Friend]saMonthNames[month - 1]);
		}

		public void GetEraName(int era, String outStr)
		{
			outStr.Append(CalendarData.[Friend]Invariant.[Friend]saEraNames[era]);
		}

		void internalGetMonthName(int month, MonthNameStyles style, bool abbreviated, String outStr)
		{
			GetMonthName(month, outStr);
		}

		public  Object GetFormat(Type formatType)
		{
		    return (formatType == typeof(DateTimeFormatInfo)? this: null);
		}

		public static DateTimeFormatInfo GetInstance(IFormatProvider provider)
		{
		    // Fast case for a regular CultureInfo
		    DateTimeFormatInfo info;
		    CultureInfo cultureProvider = provider as CultureInfo;
		    if (cultureProvider != null && !cultureProvider.[Friend]m_isInherited)
		    {
		        return cultureProvider.DateTimeFormat;
		    }
		    // Fast case for a DTFI;
		    info = provider as DateTimeFormatInfo;
		    if (info != null) {
		        return info;
		    }
		    // Wasn't cultureInfo or DTFI, do it the slower way
		    if (provider != null) {
		        info = provider.GetFormat(typeof(DateTimeFormatInfo)) as DateTimeFormatInfo;
		        if (info != null) {
		            return info;
		        }
		    }
		    // Couldn't get anything, just use currentInfo as fallback
		    return CurrentInfo;
		}

		private String[] UnclonedYearMonthPatterns
		{
		    get
		    {
		        if (this.allYearMonthPatterns == null)
		        {
		            this.allYearMonthPatterns = this.m_cultureData.[Friend]YearMonths(this.Calendar.ID);
		        }

		        return this.allYearMonthPatterns;
		    }
		}

		private String [] UnclonedShortDatePatterns
		{
		    get
		    {
		        if (allShortDatePatterns == null)
		        {
		            this.allShortDatePatterns = this.m_cultureData.[Friend]ShortDates(this.Calendar.ID);
		        }

		        return this.allShortDatePatterns;
		    }
		}

		private String[] UnclonedLongDatePatterns
		{
		    get
		    {
		        if (allLongDatePatterns == null)
		        {
		            this.allLongDatePatterns = this.m_cultureData.[Friend]LongDates(this.Calendar.ID);
		        }

		        return this.allLongDatePatterns;
		    }
		}

		private String[] UnclonedShortTimePatterns
		{
		    get
		    {
		        if (this.allShortTimePatterns == null)
		        {
		            this.allShortTimePatterns = this.m_cultureData.[Friend]ShortTimes;
		        }
		        
		        return this.allShortTimePatterns;
		    }
		}

		private String[] UnclonedLongTimePatterns
		{
		    get
		    {
		        if (this.allLongTimePatterns == null)
		        {
		            this.allLongTimePatterns = this.m_cultureData.[Friend]LongTimes;
		        }
		        
		        return this.allLongTimePatterns;
		    }
		}

		private String m_fullTimeSpanPositivePattern ~ delete _;
		String FullTimeSpanPositivePattern 
		{
		    get
		    {
		        if (m_fullTimeSpanPositivePattern == null)
		        {
		            CultureData cultureDataWithoutUserOverrides;
		            if (m_cultureData.[Friend]UseUserOverride)
		                cultureDataWithoutUserOverrides = CultureData.[Friend]GetCultureData(m_cultureData.[Friend]CultureName, false);
		            else
		                cultureDataWithoutUserOverrides = m_cultureData;
		            StringView decimalSeparator = scope NumberFormatInfo(cultureDataWithoutUserOverrides).NumberDecimalSeparator;
		            
		            m_fullTimeSpanPositivePattern = AllocString()..Append("d':'h':'mm':'ss'");
					m_fullTimeSpanPositivePattern.Append(decimalSeparator);
					m_fullTimeSpanPositivePattern.Append("'FFFFFFF");
		        }
		        return m_fullTimeSpanPositivePattern;
		    }
		}

		private String m_fullTimeSpanNegativePattern ~ delete _;
		String FullTimeSpanNegativePattern 
		{
		    get
		    {
		        if (m_fullTimeSpanNegativePattern == null)
		            m_fullTimeSpanNegativePattern = AllocString()..Append("'-'", FullTimeSpanPositivePattern);
		        return m_fullTimeSpanNegativePattern;
		    }
		}
	}
}
