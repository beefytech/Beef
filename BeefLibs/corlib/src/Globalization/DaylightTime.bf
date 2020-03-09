// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Globalization
{
    using System;
    // This class represents a starting/ending time for a period of daylight saving time.
    public class DaylightTime
    {
        DateTime m_start;
        DateTime m_end;
        TimeSpan m_delta;

        public this()
		{
        }

        public this(DateTime start, DateTime end, TimeSpan delta)
		{
            m_start = start;
            m_end = end;
            m_delta = delta;
        }

		public void Set(DateTime start, DateTime end, TimeSpan delta)
		{
			m_start = start;
			m_end = end;
			m_delta = delta;
		}

        // The start date of a daylight saving period.
        public DateTime Start {
            get {
                return m_start;
            }
        }

        // The end date of a daylight saving period.
        public DateTime End {
            get {
                return m_end;
            }
        }

        // Delta to stardard offset in ticks.
        public TimeSpan Delta {
            get {
                return m_delta;
            }
        }
    
    }

}
