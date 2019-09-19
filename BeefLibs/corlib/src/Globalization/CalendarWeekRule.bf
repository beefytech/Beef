// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Globalization
{
    using System;

    public enum CalendarWeekRule
    {
        FirstDay = 0,           // Week 1 begins on the first day of the year

        FirstFullWeek = 1,      // Week 1 begins on first FirstDayOfWeek not before the first day of the year

        FirstFourDayWeek = 2    // Week 1 begins on first FirstDayOfWeek such that FirstDayOfWeek+3 is not before the first day of the year        
    }
}
