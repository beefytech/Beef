using System.Diagnostics;
using System.Collections;
// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Globalization
{
	class CultureData
	{
		private const uint LOCALE_NOUSEROVERRIDE = 0x80000000;   // do not use user overrides

		private const uint LOCALE_SLOCALIZEDDISPLAYNAME = 0x00000002;   // localized name of locale, eg "German (Germany)" in UI language
		private const uint LOCALE_SENGLISHDISPLAYNAME = 0x00000072;   // Display name (language + country usually) in English, eg "German (Germany)"
		private const uint LOCALE_SNATIVEDISPLAYNAME = 0x00000073;   // Display name in native locale language, eg "Deutsch (Deutschland)

		private const uint LOCALE_SLOCALIZEDLANGUAGENAME = 0x0000006f;   // Language Display Name for a language, eg "German" in UI language
		private const uint LOCALE_SENGLISHLANGUAGENAME = 0x00001001;   // English name of language, eg "German"
		private const uint LOCALE_SNATIVELANGUAGENAME = 0x00000004;   // native name of language, eg "Deutsch"

		private const uint LOCALE_SLOCALIZEDCOUNTRYNAME = 0x00000006;   // localized name of country, eg "Germany" in UI language
		private const uint LOCALE_SENGLISHCOUNTRYNAME = 0x00001002;   // English name of country, eg "Germany"
		private const uint LOCALE_SNATIVECOUNTRYNAME = 0x00000008;   // native name of country, eg "Deutschland"

		private const uint LOCALE_STIME = 0x0000001E;   // time separator (derived from LOCALE_STIMEFORMAT, use that instead)
		private const uint LOCALE_STIMEFORMAT = 0x00001003;   // time format string

		const int32 undef = -1;

		// Override flag
		private String sRealName ~ delete _; // Name you passed in (ie: en-US, en, or de-DE_phoneb)
		private String sWindowsName ~ delete _; // Name OS thinks the object is (ie: de-DE_phoneb, or en-US (even if en was passed in))

		// Identity
		private String sName ~ delete _; // locale name (ie: en-us, NO sort info, but could be neutral)
		private String sParent; // Parent name (which may be a custom locale/culture)
		private String sLocalizedDisplayName; // Localized pretty name for this locale
		private String sEnglishDisplayName; // English pretty name for this locale
		private String sNativeDisplayName; // Native pretty name for this locale
		private String sSpecificCulture ~ delete _; // The culture name to be used in CultureInfo.CreateSpecificCulture(), en-US form if neutral, sort name if sort

		// Language
		private String sISO639Language; // ISO 639 Language Name
		private String sLocalizedLanguage; // Localized name for this language
		private String sEnglishLanguage; // English name for this language
		private String sNativeLanguage; // Native name of this language

		// Region
		private String sRegionName; // (RegionInfo)
		//        private int    iCountry=undef           ; // (user can override) ---- code (RegionInfo)
		private int iGeoId = undef; // GeoId
		private String sLocalizedCountry; // localized country name
		private String sEnglishCountry; // english country name (RegionInfo)
		private String sNativeCountry; // native country name
		private String sISO3166CountryName; // ISO 3166 (RegionInfo), ie: US

		// Numbers
		private String sPositiveSign; // (user can override) positive sign
		private String sNegativeSign; // (user can override) negative sign
		private String[] saNativeDigits; // (user can override) native characters for digits 0-9
		// (nfi populates these 5, don't have to be = undef)
		private int32 iDigitSubstitution; // (user can override) Digit substitution 0=context, 1=none/arabic, 2=Native/national (2 seems to be unused)
		private int32 iLeadingZeros; // (user can override) leading zeros 0 = no leading zeros, 1 = leading zeros
		private int32 iDigits; // (user can override) number of fractional digits
		private int32 iNegativeNumber; // (user can override) negative number format
		private int32[] waGrouping; // (user can override) grouping of digits
		private String sDecimalSeparator; // (user can override) decimal separator
		private String sThousandSeparator; // (user can override) thousands separator
		private String sNaN; // Not a Number
		private String sPositiveInfinity; // + Infinity
		private String sNegativeInfinity; // - Infinity

		// Percent
		private int iNegativePercent = undef; // Negative Percent (0-3)
		private int iPositivePercent = undef; // Positive Percent (0-11)
		private String sPercent; // Percent (%) symbol
		private String sPerMille; // PerMille (‰) symbol

		// Currency
		private String sCurrency; // (user can override) local monetary symbol
		private String sIntlMonetarySymbol; // international monetary symbol (RegionInfo)
		private String sEnglishCurrency; // English name for this currency
		private String sNativeCurrency; // Native name for this currency
		// (nfi populates these 4, don't have to be = undef)
		private int iCurrencyDigits; // (user can override) # local monetary fractional digits
		private int iCurrency; // (user can override) positive currency format
		private int iNegativeCurrency; // (user can override) negative currency format
		private int[] waMonetaryGrouping; // (user can override) monetary grouping of digits
		private String sMonetaryDecimal; // (user can override) monetary decimal separator
		private String sMonetaryThousand; // (user can override) monetary thousands separator

		// Misc
		private int32 iMeasure = undef; // (user can override) system of measurement 0=metric, 1=US (RegionInfo)
		private String sListSeparator; // (user can override) list separator
		//        private int    iPaperSize               ; // default paper size (RegionInfo)

		// Time
		private String sAM1159; // (user can override) AM designator
		private String sPM2359; // (user can override) PM designator
		private String sTimeSeparator;
		private volatile String[] saLongTimes; // (user can override) time format
		private volatile String[] saShortTimes; // short time format
		private volatile String[] saDurationFormats; // time duration format

		// Calendar specific data
		private int iFirstDayOfWeek = undef; // (user can override) first day of week (gregorian really)
		private int iFirstWeekOfYear = undef; // (user can override) first week of year (gregorian really)
		private volatile int[] waCalendars; // all available calendar type(s).  The first one is the default calendar

		// Store for specific data about each calendar
		private CalendarData[] calendars; // Store for specific calendar data

		// Text information
		private int iReadingLayout = undef; // Reading layout data
		// 0 - Left to right (eg en-US)
		// 1 - Right to left (eg arabic locales)
		// 2 - Vertical top to bottom with columns to the left and also left to right (ja-JP locales)
		// 3 - Vertical top to bottom with columns proceeding to the right

		private String sTextInfo; // Text info name to use for custom
		private String sCompareInfo; // Compare info name (including sorting key) to use if custom
		private String sScripts; // Typical Scripts for this locale (latn;cyrl; etc)

		// The bools all need to be in one spot
		private bool bUseOverrides; // use user overrides?
		private bool bNeutral; // Flags for the culture (ie: neutral or not right now)

		List<Object> ownedObjects = new .() ~ DeleteContainerAndItems!(_);

		public this()
		{

		}

		void GetNFIValues(NumberFormatInfo nfi)
		{

		}

		public bool IsInvariantCulture
		{
			get
			{
				return true;
			}
		}

		Calendar DefaultCalendar
		{
		    get
		    {
		        /*int defaultCalId = DoGetLocaleInfoInt(LOCALE_ICALENDARTYPE);
		        if (defaultCalId == 0)
		        {
		            defaultCalId = this.CalendarIds[0];
		        }

		        return CultureInfo.GetCalendarInstance(defaultCalId);*/
				//Runtime.NotImplemented();
				// NotImplemented
				return CultureInfo.[Friend]GetCalendarInstance(Calendar.[Friend]CAL_GREGORIAN);
		    }
		}

		StringView CultureName
		{
			get
			{
				switch (this.sName)
				{
				    case "zh-CHS":
				    case "zh-CHT":
				        return this.sName;
				}
				return this.sRealName;
			}
		}

		String[] LongTimes
		{
		    get
		    {
		        if (this.saLongTimes == null
#if !FEATURE_CORECLR
		            || UseUserOverride
#endif
		            )
		        {
		            /*String[] longTimes = DoEnumTimeFormats();
		            if (longTimes == null || longTimes.Length == 0)
		            {
		                this.saLongTimes = Invariant.saLongTimes;
		            }
		            else
		            {
		                this.saLongTimes = longTimes;
		            }*/
					//NotImpl
					saLongTimes = new String[](new .("h:mm:ss tt"));
					ownedObjects.Add(saLongTimes);
					ownedObjects.Add(saLongTimes[0]);
					
		        }
		        return this.saLongTimes;
		    }
		}

		String[] ShortTimes
		{
		    get
		    {
		        if (this.saShortTimes == null
#if !FEATURE_CORECLR
		            || UseUserOverride
#endif
		            )
		        {
		            // Try to get the short times from the OS/culture.dll
		            /*String[] shortTimes = DoEnumShortTimeFormats();

		            if (shortTimes == null || shortTimes.Length == 0)
		            {
		                //
		                // If we couldn't find short times, then compute them from long times
		                // (eg: CORECLR on < Win7 OS & fallback for missing culture.dll)
		                //
		                shortTimes = DeriveShortTimesFromLong();
		            }

		            // Found short times, use them
		            this.saShortTimes = shortTimes;*/
					saShortTimes = new String[](new .("h:mm tt"));
					ownedObjects.Add(saShortTimes);
					ownedObjects.Add(saShortTimes[0]);
		        }
		        return this.saShortTimes;
		    }
		}

		static CultureData GetCultureData(StringView cultureName, bool useUserOverride)
		{
			CultureData culture = CreateCultureData(cultureName, useUserOverride);
			return culture;

		}

		private static CultureData CreateCultureData(StringView cultureName, bool useUserOverride)
		{
		    CultureData culture = new CultureData();
		    //culture.bUseOverrides = useUserOverride;
		    //culture.sRealName = cultureName;

		    // Ask native code if that one's real
		    if (culture.InitCultureData() == false)
		    {
/*#if !FEATURE_CORECLR
		        if (culture.InitCompatibilityCultureData() == false
		         && culture.InitLegacyAlternateSortData() == false)
#endif*/
		        {
		            return null;
		        }
		    }

		    return culture;
		}

		private bool InitCultureData()
		{
			sWindowsName = new String("en-US");
			sRealName = new String("en-US");
			sSpecificCulture = new String("en-US");
			sName = new String("en-US");

			//NotImplemented
			return true;
		}

		////////////////////////////////////////////////////////////////////////////
		//
		// Reescape a Win32 style quote string as a NLS+ style quoted string
		//
		// This is also the escaping style used by custom culture data files
		//
		// NLS+ uses \ to escape the next character, whether in a quoted string or
		// not, so we always have to change \ to \\.
		//
		// NLS+ uses \' to escape a quote inside a quoted string so we have to change
		// '' to \' (if inside a quoted string)
		//
		// We don't build the stringbuilder unless we find something to change
		////////////////////////////////////////////////////////////////////////////
		static void ReescapeWin32String(String inStr)
		{
		    // If we don't have data, then don't try anything
		    if (inStr == null)
		        return;

			var inStr;

			
		    bool inQuote = false;
		    for (int i = 0; i < inStr.Length; i++)
		    {
		        // Look for quote
		        if (inStr[i] == '\'')
		        {
		            // Already in quote?
		            if (inQuote)
		            {
		                // See another single quote.  Is this '' of 'fred''s' or '''', or is it an ending quote?
		                if (i + 1 < inStr.Length && inStr[i + 1] == '\'')
		                {
		                    // Found another ', so we have ''.  Need to add \' instead.
		                    // 1st make sure we have our stringbuilder
		                    
		                    // Append a \' and keep going (so we don't turn off quote mode)
		                    inStr.Insert(i, "\\");
		                    i++;
		                    continue;
		                }

		                // Turning off quote mode, fall through to add it
		                inQuote = false;
		            }
		            else
		            {
		                // Found beginning quote, fall through to add it
		                inQuote = true;
		            }
		        }
		        // Is there a single \ character?
		        else if (inStr[i] == '\\')
		        {
		            // Found a \, need to change it to \\
		            // 1st make sure we have our stringbuilder
		            
		            // Append our \\ to the string & continue
		            inStr.Insert(i, "\\\\");
		            continue;
		        }
		    }
		}

		static void ReescapeWin32Strings(String[] inArray)
		{
		    if (inArray != null)
		    {
		        for (int i = 0; i < inArray.Count; i++)
		        {
		            ReescapeWin32String(inArray[i]);
		        }
		    }
		}

		bool UseUserOverride
		{
		    get
		    {
		        return this.bUseOverrides;
		    }
		}

		bool IsSupplementalCustomCulture
		{
		    get
		    {
		        //return IsCustomCultureId(this.ILANGUAGE);
				return false;
		    }
		}

		static private bool IsOsWin7OrPrior()
		{
		    return Environment.OSVersion.Platform == PlatformID.Win32NT &&
		        Environment.OSVersion.Version < Version(6, 2); // Win7 is 6.1.Build.Revision so we have to check for anything less than 6.2
		}

		CalendarData GetCalendar(int calendarId)
		{
		    Debug.Assert(calendarId > 0 && calendarId <= CalendarData.[Friend]MAX_CALENDARS,
		        "[CultureData.GetCalendar] Expect calendarId to be in a valid range");

		    // arrays are 0 based, calendarIds are 1 based
		    int calendarIndex = calendarId - 1;

		    // Have to have calendars
		    if (calendars == null)
		    {
		        calendars = new CalendarData[CalendarData.[Friend]MAX_CALENDARS];
		    }

		    // we need the following local variable to avoid returning null
		    // when another thread creates a new array of CalendarData (above)
		    // right after we insert the newly created CalendarData (below)
		    CalendarData calendarData = calendars[calendarIndex];
		    // Make sure that calendar has data
		    if (calendarData == null
#if !FEATURE_CORECLR
		        || UseUserOverride
#endif
		        )
		    {
		        //Contract.Assert(this.sWindowsName != null, "[CultureData.GetCalendar] Expected this.sWindowsName to be populated by COMNlsInfo::nativeInitCultureData already");
		        calendarData = new CalendarData(this.sWindowsName, calendarId, this.UseUserOverride);
/*#if !FEATURE_CORECLR
		        //Work around issue where Win7 data for MonthDay contains invalid two sets of data separated by semicolon
		        //even though MonthDay is not enumerated
		        if (IsOsWin7OrPrior() && !IsSupplementalCustomCulture && !IsReplacementCulture)
		        {
		            calendarData.FixupWin7MonthDaySemicolonBug();
		        }
#endif*/
		        calendars[calendarIndex] = calendarData;
		    }

		    return calendarData;
		}

		String[] ShortDates(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saShortDates;
		}

		String[] LongDates(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saLongDates;
		}

		// (user can override) date year/month format.
		String[] YearMonths(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saYearMonths;
		}

		// day names
		String[] DayNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saDayNames;
		}

		// abbreviated day names
		String[] AbbreviatedDayNames(int calendarId)
		{
		    // Get abbreviated day names for this calendar from the OS if necessary
		    return GetCalendar(calendarId).[Friend]saAbbrevDayNames;
		}

		// The super short day names
		String[] SuperShortDayNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saSuperShortDayNames;
		}

		// month names
		String[] MonthNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saMonthNames;
		}

		// Genitive month names
		String[] GenitiveMonthNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saMonthGenitiveNames;
		}

		// month names
		String[] AbbreviatedMonthNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saAbbrevMonthNames;
		}

		// Genitive month names
		String[] AbbreviatedGenitiveMonthNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saAbbrevMonthGenitiveNames;
		}

		// Leap year month names
		// Note: This only applies to Hebrew, and it basically adds a "1" to the 6th month name
		// the non-leap names skip the 7th name in the normal month name array
		String[] LeapYearMonthNames(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]saLeapYearMonthNames;
		}

		// month/day format (single string, no override)
		String MonthDay(int calendarId)
		{
		    return GetCalendar(calendarId).[Friend]sMonthDay;
		}

		void DoGetLocaleInfo(uint lctype, String outStr)
		{
		    DoGetLocaleInfo(this.sWindowsName, lctype, outStr);
		}

		void DoGetLocaleInfo(StringView localeName, uint lctype, String outStr)
		{
			var lctype;

		    // Fix lctype if we don't want overrides
		    if (!UseUserOverride)
		    {
		        lctype |= LOCALE_NOUSEROVERRIDE;
		    }

		    // Ask OS for data
		    /*String result = CultureInfo.nativeGetLocaleInfoEx(localeName, lctype);
		    if (result == null)
		    {
		        // Failed, just use empty string
		        result = String.Empty;
		    }

		    return result;*/
		}

		private static void GetSeparator(StringView format, StringView timeParts, String outStr)
		{
		    /*int index = IndexOfTimePart(format, 0, timeParts);

		    if (index != -1)
		    {
		        // Found a time part, find out when it changes
		        char8 cTimePart = format[index];

		        do
		        {
		            index++;
		        } while (index < format.Length && format[index] == cTimePart);

		        int separatorStart = index;

		        // Now we need to find the end of the separator
		        if (separatorStart < format.Length)
		        {
		            int separatorEnd = IndexOfTimePart(format, separatorStart, timeParts);
		            if (separatorEnd != -1)
		            {
		                // From [separatorStart, count) is our string, except we need to unescape
		                return UnescapeNlsString(format, separatorStart, separatorEnd - 1);
		            }
		        }
		    }

		    return String.Empty;*/
		}

		static private void GetTimeSeparator(StringView format, String outStr)
		{
		    // Time format separator (ie: : in 12:39:00)
		    //
		    // We calculate this from the provided time format
		    //

		    //
		    //  Find the time separator so that we can pretend we know STIME.
		    //
		    GetSeparator(format, "Hhms", outStr);
		}

		String TimeSeparator
		{
		    get
		    {
		        if (sTimeSeparator == null
#if !FEATURE_CORECLR
		            || UseUserOverride
#endif
		            )
		        {
		            String longTimeFormat = scope String();
					DoGetLocaleInfo(LOCALE_STIMEFORMAT, longTimeFormat);
					ReescapeWin32String(longTimeFormat);
		            if (String.IsNullOrEmpty(longTimeFormat))
		            {
		                longTimeFormat = LongTimes[0];
		            }

		            // Compute STIME from time format
		            sTimeSeparator = new String();
					GetTimeSeparator(longTimeFormat, sTimeSeparator);
		        }
		        return sTimeSeparator;
		    }
		}
	}
}
