// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Globalization
{

    using System;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;

// 


    //
    // List of calendar data
    // Note the we cache overrides.
    // Note that localized names (resource names) aren't available from here.
    //
    //  NOTE: Calendars depend on the locale name that creates it.  Only a few
    //            properties are available without locales using CalendarData.GetCalendar(int)

    // StructLayout is needed here otherwise compiler can re-arrange the fields.
    // We have to keep this in-[....] with the definition in calendardata.h
    //
    // WARNING WARNING WARNING
    //
    // WARNING: Anything changed here also needs to be updated on the native side (object.h see type CalendarDataBaseObject)
    // WARNING: The type loader will rearrange class member offsets so the mscorwks!CalendarDataBaseObject
    // WARNING: must be manually structured to match the true loaded class layout
    //
    internal class CalendarData
    {
        // Max calendars
        internal const int MAX_CALENDARS = 23;

        // Identity
        internal String     sNativeName              ~ delete _; // Calendar Name for the locale

        // Formats
        internal String[]   saShortDates             ~ DeleteContainerAndItems!(_); // Short Data format, default first
        internal String[]   saYearMonths             ~ DeleteContainerAndItems!(_); // Year/Month Data format, default first
        internal String[]   saLongDates              ~ DeleteContainerAndItems!(_); // Long Data format, default first
        internal String     sMonthDay                ~ delete _; // Month/Day format

        // Calendar Parts Names
        internal String[]   saEraNames               ~ DeleteContainerAndItems!(_); // Names of Eras
        internal String[]   saAbbrevEraNames         ~ DeleteContainerAndItems!(_); // Abbreviated Era Names
        internal String[]   saAbbrevEnglishEraNames  ~ DeleteContainerAndItems!(_); // Abbreviated Era Names in English
        internal String[]   saDayNames               ~ DeleteContainerAndItems!(_); // Day Names, null to use locale data, starts on Sunday
        internal String[]   saAbbrevDayNames         ~ DeleteContainerAndItems!(_); // Abbrev Day Names, null to use locale data, starts on Sunday
        internal String[]   saSuperShortDayNames     ~ DeleteContainerAndItems!(_); // Super short Day of week names
        internal String[]   saMonthNames             ~ DeleteContainerAndItems!(_); // Month Names (13)
        internal String[]   saAbbrevMonthNames       ~ DeleteContainerAndItems!(_); // Abbrev Month Names (13)
        internal String[]   saMonthGenitiveNames     ~ DeleteContainerAndItems!(_); // Genitive Month Names (13)
        internal String[]   saAbbrevMonthGenitiveNames~ DeleteContainerAndItems!(_); // Genitive Abbrev Month Names (13)
        internal String[]   saLeapYearMonthNames     ~ DeleteContainerAndItems!(_); // Multiple strings for the month names in a leap year.

        // Integers at end to make marshaller happier
        internal int        iTwoDigitYearMax=2029    ; // Max 2 digit year (for Y2K bug data entry)
        internal int        iCurrentEra=0            ;  // current era # (usually 1)

        // Use overrides?
        internal bool       bUseUserOverrides        ; // True if we want user overrides.

        // Static invariant for the invariant locale
        internal static CalendarData Invariant ~ delete _;

        // Private constructor
        private this() {}

        // Invariant constructor
        static this()
        {

            // Set our default/gregorian US calendar data
            // Calendar IDs are 1-based, arrays are 0 based.
            CalendarData invariant = new CalendarData();

            invariant.SetupDefaults();

            // Calendar was built, go ahead and assign it...            
            Invariant = invariant;
        }

		void SetupDefaults()
		{
			// Set default data for calendar
			// Note that we don't load resources since this IS NOT supposed to change (by definition)
			sNativeName           = new String("Gregorian Calendar");  // Calendar Name

			// Year
			iTwoDigitYearMax      = 2029; // Max 2 digit year (for Y2K bug data entry)
			iCurrentEra           = 1; // Current era #

			// Formats
			saShortDates          = AllocStrings("MM/dd/yyyy", "yyyy-MM-dd");          // short date format
			saLongDates           = AllocStrings("dddd, dd MMMM yyyy");                 // long date format
			saYearMonths          = AllocStrings("yyyy MMMM");                         // year month format
			sMonthDay             = new String("MMMM dd");                                            // Month day pattern

			// Calendar Parts Names
			saEraNames            = AllocStrings("A.D.");     // Era names
			saAbbrevEraNames      = AllocStrings("AD");      // Abbreviated Era names
			saAbbrevEnglishEraNames=AllocStrings("AD");     // Abbreviated era names in English
			saDayNames            = AllocStrings("Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday");// day names
			saAbbrevDayNames      = AllocStrings("Sun",    "Mon",    "Tue",     "Wed",       "Thu",      "Fri",    "Sat");     // abbreviated day names
			saSuperShortDayNames  = AllocStrings("Su",     "Mo",     "Tu",      "We",        "Th",       "Fr",     "Sa");      // The super short day names
			saMonthNames          = AllocStrings("January", "February", "March", "April", "May", "June", 
			                                                "July", "August", "September", "October", "November", "December", String.Empty); // month names
			saAbbrevMonthNames    = AllocStrings("Jan", "Feb", "Mar", "Apr", "May", "Jun",
			                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", String.Empty); // abbreviated month names
			saMonthGenitiveNames  = AllocStrings(params saMonthNames);              // Genitive month names (same as month names for invariant)
			saAbbrevMonthGenitiveNames=AllocStrings(params saAbbrevMonthNames);    // Abbreviated genitive month names (same as abbrev month names for invariant)
			saLeapYearMonthNames  = AllocStrings(params saMonthNames);              // leap year month names are unused in Gregorian English (invariant)

			bUseUserOverrides     = false;
		}

		static String[] AllocStrings(params String[] strs)
		{
			String[] newStrs = new String[strs.Count];
			for (var str in strs)
				newStrs[@str] = new String(str);
			return newStrs;
		}

        //
        // Get a bunch of data for a calendar
        //
        internal this(String localeName, int calendarId, bool bUseUserOverrides)
        {
			String[] Clone(String[] strs)
			{
				var newStrs = new String[strs.Count];
				for (var str in strs)
					newStrs[@str] = new String(str);
				return newStrs;
			}

            // Call nativeGetCalendarData to populate the data
            this.bUseUserOverrides = bUseUserOverrides;
            if (!nativeGetCalendarData(this, localeName, calendarId))
            {
                //Contract.Assert(false, "[CalendarData] nativeGetCalendarData call isn't expected to fail for calendar " + calendarId + " locale " +localeName);
				Debug.FatalError("[CalendarData] nativeGetCalendarData call isn't expected to fail for calendar");
                // Something failed, try invariant for missing parts
                // This is really not good, but we don't want the callers to crash.
#unwarn
                if (this.sNativeName == null)   this.sNativeName  = String.Empty;           // Calendar Name for the locale.
                
                // Formats
                if (this.saShortDates == null)  this.saShortDates = Clone(Invariant.saShortDates); // Short Data format, default first
                if (this.saYearMonths == null)  this.saYearMonths = Clone(Invariant.saYearMonths); // Year/Month Data format, default first
                if (this.saLongDates == null)   this.saLongDates  = Clone(Invariant.saLongDates);  // Long Data format, default first
                if (this.sMonthDay == null)     this.sMonthDay    = Invariant.sMonthDay;    // Month/Day format
                
                // Calendar Parts Names
                if (this.saEraNames == null)              this.saEraNames              = Invariant.saEraNames;              // Names of Eras
                if (this.saAbbrevEraNames == null)        this.saAbbrevEraNames        = Invariant.saAbbrevEraNames;        // Abbreviated Era Names
                if (this.saAbbrevEnglishEraNames == null) this.saAbbrevEnglishEraNames = Invariant.saAbbrevEnglishEraNames; // Abbreviated Era Names in English
                if (this.saDayNames == null)              this.saDayNames              = Invariant.saDayNames;              // Day Names, null to use locale data, starts on Sunday
                if (this.saAbbrevDayNames == null)        this.saAbbrevDayNames        = Invariant.saAbbrevDayNames;        // Abbrev Day Names, null to use locale data, starts on Sunday
                if (this.saSuperShortDayNames == null)    this.saSuperShortDayNames    = Invariant.saSuperShortDayNames;    // Super short Day of week names
                if (this.saMonthNames == null)            this.saMonthNames            = Invariant.saMonthNames;            // Month Names (13)
                if (this.saAbbrevMonthNames == null)      this.saAbbrevMonthNames      = Invariant.saAbbrevMonthNames;      // Abbrev Month Names (13)
                // Genitive and Leap names can follow the fallback below
            }

            // Clean up the escaping of the formats
            
			CultureData.ReescapeWin32Strings(this.saShortDates);
            CultureData.ReescapeWin32Strings(this.saLongDates);
            CultureData.ReescapeWin32Strings(this.saYearMonths);
            CultureData.ReescapeWin32String(this.sMonthDay);

            if ((CalendarId)calendarId == CalendarId.TAIWAN)
            {
                // for Geo----al reasons, the ----ese native name should only be returned when 
                // for ----ese SKU
                if (CultureInfo.IsTaiwanSku)
                {
                    // We got the month/day names from the OS (same as gregorian), but the native name is wrong
                    this.sNativeName = "\x4e2d\x83ef\x6c11\x570b\x66c6";
                }
                else
                {
                    this.sNativeName = String.Empty;
                }
            }

            // Check for null genitive names (in case unmanaged side skips it for non-gregorian calendars, etc)
            if (this.saMonthGenitiveNames == null || String.IsNullOrEmpty(this.saMonthGenitiveNames[0]))
                this.saMonthGenitiveNames = this.saMonthNames;              // Genitive month names (same as month names for invariant)
            if (this.saAbbrevMonthGenitiveNames == null || String.IsNullOrEmpty(this.saAbbrevMonthGenitiveNames[0]))
                this.saAbbrevMonthGenitiveNames = this.saAbbrevMonthNames;    // Abbreviated genitive month names (same as abbrev month names for invariant)
            if (this.saLeapYearMonthNames == null || String.IsNullOrEmpty(this.saLeapYearMonthNames[0]))
                this.saLeapYearMonthNames = this.saMonthNames;

            InitializeEraNames(localeName, calendarId);

            InitializeAbbreviatedEraNames(localeName, calendarId);

            // Abbreviated English Era Names are only used for the Japanese calendar.
            if (calendarId == (int)CalendarId.JAPAN)
            {
                //this.saAbbrevEnglishEraNames = JapaneseCalendar.EnglishEraNames();
				Runtime.NotImplemented();
            }
            else
            {
                // For all others just use the an empty string (doesn't matter we'll never ask for it for other calendars)
                this.saAbbrevEnglishEraNames = new String[] { "" };
            }

            // Japanese is the only thing with > 1 era.  Its current era # is how many ever 
            // eras are in the array.  (And the others all have 1 string in the array)
            this.iCurrentEra = this.saEraNames.Count;
        }

        private void InitializeEraNames(String localeName, int calendarId)
        {
            // Note that the saEraNames only include "A.D."  We don't have localized names for other calendars available from windows
            switch ((CalendarId)calendarId)
            {
                // For Localized Gregorian we really expect the data from the OS.
            case CalendarId.GREGORIAN:
                // Fallback for CoreCLR < Win7 or culture.dll missing            
                if (this.saEraNames == null || this.saEraNames.Count == 0 || String.IsNullOrEmpty(this.saEraNames[0]))
                {
					DeleteContainerAndItems!(this.saEraNames);
                    this.saEraNames = AllocStrings("A.D.");
                }
                break;

                // The rest of the calendars have constant data, so we'll just use that
            case CalendarId.GREGORIAN_US:
            case CalendarId.JULIAN:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("A.D.");
                break;
            case CalendarId.HEBREW:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("C.E.");
                break;
            case CalendarId.HIJRI:
            case CalendarId.UMALQURA:
				DeleteContainerAndItems!(this.saEraNames);
                if (localeName == "dv-MV")
                {
                    // Special case for Divehi
                    this.saEraNames = AllocStrings("\x0780\x07a8\x0796\x07b0\x0783\x07a9");
                }
                else
                {
                    this.saEraNames = AllocStrings("\x0628\x0639\x062F \x0627\x0644\x0647\x062C\x0631\x0629");
                }
                break;
            case CalendarId.GREGORIAN_ARABIC:
            case CalendarId.GREGORIAN_XLIT_ENGLISH:
            case CalendarId.GREGORIAN_XLIT_FRENCH:
                // These are all the same:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("\x0645");
                break;

            case CalendarId.GREGORIAN_ME_FRENCH:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("ap. J.-C.");
                break;
                
            case CalendarId.TAIWAN:
                // for Geo----al reasons, the ----ese native name should only be returned when 
                // for ----ese SKU
				DeleteContainerAndItems!(this.saEraNames);
                if (CultureInfo.IsTaiwanSku)
                {
                    // 
                    this.saEraNames = AllocStrings("\x4e2d\x83ef\x6c11\x570b");
                }
                else
                {
                    this.saEraNames = AllocStrings(String.Empty);
                }
                break;

            case CalendarId.KOREA:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("\xb2e8\xae30");
                break;
                
            case CalendarId.THAI:
				DeleteContainerAndItems!(this.saEraNames);
                this.saEraNames = AllocStrings("\x0e1e\x002e\x0e28\x002e");
                break;
                
            case CalendarId.JAPAN:
            case CalendarId.JAPANESELUNISOLAR:
                //this.saEraNames = JapaneseCalendar.EraNames();
				Runtime.NotImplemented();
                //break;

            case CalendarId.PERSIAN:
                if (this.saEraNames == null || this.saEraNames.Count == 0 || String.IsNullOrEmpty(this.saEraNames[0]))
                {
					DeleteContainerAndItems!(this.saEraNames);
                    this.saEraNames = AllocStrings("\x0647\x002e\x0634");
                }
                break;

            default:
                // Most calendars are just "A.D."
                this.saEraNames = Invariant.saEraNames;
                break;
            }
        }

        private void InitializeAbbreviatedEraNames(StringView localeName, int calendarId)
        {
            // Note that the saAbbrevEraNames only include "AD"  We don't have localized names for other calendars available from windows
            switch ((CalendarId)calendarId)
            {
                // For Localized Gregorian we really expect the data from the OS.
            case CalendarId.GREGORIAN:
                // Fallback for CoreCLR < Win7 or culture.dll missing            
                if (this.saAbbrevEraNames == null || this.saAbbrevEraNames.Count == 0 || String.IsNullOrEmpty(this.saAbbrevEraNames[0]))
                {
					DeleteContainerAndItems!(this.saAbbrevEraNames);
                    this.saAbbrevEraNames = AllocStrings("AD");
                }
            
                // The rest of the calendars have constant data, so we'll just use that
            case CalendarId.GREGORIAN_US:
            case CalendarId.JULIAN:
				DeleteContainerAndItems!(this.saAbbrevEraNames);
                this.saAbbrevEraNames = AllocStrings("AD");
                break;                    
            case CalendarId.JAPAN:
            case CalendarId.JAPANESELUNISOLAR:
                //this.saAbbrevEraNames = JapaneseCalendar.AbbrevEraNames();
				Runtime.NotImplemented();
            case CalendarId.HIJRI:
            case CalendarId.UMALQURA:
				DeleteContainerAndItems!(this.saAbbrevEraNames);
                if (localeName == "dv-MV")
                {
                    // Special case for Divehi
                    this.saAbbrevEraNames = AllocStrings("\x0780\x002e");
                }
                else
                {
                    this.saAbbrevEraNames = AllocStrings("\x0647\x0640");
                }
                break;
            case CalendarId.TAIWAN:
                // Get era name and abbreviate it
				DeleteContainerAndItems!(this.saAbbrevEraNames);
                this.saAbbrevEraNames = new String[1];
                if (this.saEraNames[0].Length == 4)
                {
                    this.saAbbrevEraNames[0] = new String(this.saEraNames[0], 2, 2);
                }
                else
                {
                    this.saAbbrevEraNames[0] = new String(this.saEraNames[0]);
                }                        
                break;

            case CalendarId.PERSIAN:
                if (this.saAbbrevEraNames == null || this.saAbbrevEraNames.Count == 0 || String.IsNullOrEmpty(this.saAbbrevEraNames[0]))
                {
					DeleteContainerAndItems!(this.saAbbrevEraNames);
                    this.saAbbrevEraNames = AllocStrings(params this.saEraNames);
                }
                break;

            default:
                // Most calendars just use the full name
                this.saAbbrevEraNames = AllocStrings(params this.saEraNames);
                break;
            }
        }

        internal static CalendarData GetCalendarData(int calendarId)
        {
            /*//
            // Get a calendar.
            // Unfortunately we depend on the locale in the OS, so we need a locale
            // no matter what.  So just get the appropriate calendar from the 
            // appropriate locale here
            //

            // Get a culture name
            // 
            String culture = CalendarIdToCultureName(calendarId);
           
            // Return our calendar
            return CultureInfo.GetCultureInfo(culture).m_cultureData.GetCalendar(calendarId);*/
			Runtime.FatalError();
        }

        //
        // Helper methods
        //
        private static String CalendarIdToCultureName(int calendarId)
        {
            switch (calendarId)
            {
                case Calendar.CAL_GREGORIAN_US: 
                    return "fa-IR";             // "fa-IR" Iran
                    
                case Calendar.CAL_JAPAN:
                    return "ja-JP";             // "ja-JP" Japan
                
                case Calendar.CAL_TAIWAN:
                    return "zh-TW";             // zh-TW Taiwan
                
                case Calendar.CAL_KOREA:            
                    return "ko-KR";             // "ko-KR" Korea
                    
                case Calendar.CAL_HIJRI:
                case Calendar.CAL_GREGORIAN_ARABIC:
                case Calendar.CAL_UMALQURA:
                    return "ar-SA";             // "ar-SA" Saudi Arabia

                case Calendar.CAL_THAI:
                    return "th-TH";             // "th-TH" Thailand
                    
                case Calendar.CAL_HEBREW:
                    return "he-IL";             // "he-IL" Israel
                    
                case Calendar.CAL_GREGORIAN_ME_FRENCH:
                    return "ar-DZ";             // "ar-DZ" Algeria
                
                case Calendar.CAL_GREGORIAN_XLIT_ENGLISH:
                case Calendar.CAL_GREGORIAN_XLIT_FRENCH:
                    return "ar-IQ";             // "ar-IQ"; Iraq
                
                default:
                    // Default to gregorian en-US
                    break;
            }

            return "en-US";
        }

        /*internal void FixupWin7MonthDaySemicolonBug()
        {
            int unescapedCharacterIndex = FindUnescapedCharacter(sMonthDay, ';');
            if (unescapedCharacterIndex > 0)
            {
                sMonthDay = sMonthDay.Substring(0, unescapedCharacterIndex);
            }
        }*/

        private static int FindUnescapedCharacter(String s, char8 charToFind)
        {
            bool inComment = false;
            int length = s.Length;
            for (int i = 0; i < length; i++)
            {
                char8 c = s[i];

                switch (c)
                {
                    case '\'':
                        inComment = !inComment;
                        break;
                    case '\\':
                        i++; // escape sequence -- skip next character
                        break;
                    default:
                        if (!inComment && charToFind == c)
                        {
                            return i;
                        }
                        break;
                }
            }
            return -1;
        }

        
        
        internal static int nativeGetTwoDigitYearMax(int calID)
		{
			Runtime.NotImplemented();
		}
        
        private static bool nativeGetCalendarData(CalendarData data, String localeName, int calendar)
		{
			data.SetupDefaults();
			//NotImplemented();
			return true;
		}

        internal static int nativeGetCalendars(String localeName, bool useUserOverride, int[] calendars)
		{
			Runtime.NotImplemented();
		}

    }
 }

