using System;
using System.Collections;
using System.Text;

namespace Beefy.utils
{
    public abstract class PerfTimer
    {
        [StdCall, CLink]
        extern static void PerfTimer_ZoneStart(char8* name);

        [StdCall, CLink]
        extern static void PerfTimer_ZoneEnd();

        [StdCall, CLink]
        extern static void PerfTimer_Message(char8* theString);

        [StdCall, CLink]
        extern static int32 PerfTimer_IsRecording();

        [StdCall, CLink]
        extern static void PerfTimer_StartRecording();

        [StdCall, CLink]
        extern static void PerfTimer_StopRecording();

        [StdCall, CLink]
        extern static void PerfTimer_DbgPrint();

        static DisposeProxy mZoneEndDisposeProxy ~ delete _;

        public static DisposeProxy ZoneStart(String name)
        {
            if (mZoneEndDisposeProxy == null)
                mZoneEndDisposeProxy = new DisposeProxy(new => ZoneEnd);

            PerfTimer_ZoneStart(name);
            return mZoneEndDisposeProxy;
        }

        public static void ZoneEnd()
        {
            PerfTimer_ZoneEnd();
        }

        public static void Message(String theString)
        {
            PerfTimer_Message(theString);
        }

        public static void Message(String format, params Object[] theParams)
        {
			String outStr = scope String();
			outStr.AppendF(format, params theParams);
            Message(outStr);
        }

        public static bool IsRecording()
        {
            return PerfTimer_IsRecording() != 0;
        }

        public static void StartRecording()
        {
            PerfTimer_StartRecording();
        }

        public static void StopRecording()
        {
            PerfTimer_StopRecording();
        }

        public static void DbgPrint()
        {
            PerfTimer_DbgPrint();
        }
    }
}
