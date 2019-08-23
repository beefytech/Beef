// ==++== b
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
/*============================================================
**
** Class:  Stopwatch
**
** Purpose: Implementation for Stopwatch class.
**
** Date:  Nov 27, 2002
**
===========================================================*/

namespace System.Diagnostics
{
    using System;

    public class Stopwatch
    {
        private const int64 TicksPerMillisecond = 1000;
        private const int64 TicksPerSecond = TicksPerMillisecond * 1000;
        
        private int64 elapsed;
        private int64 startTimeStamp;
        private bool isRunning;

        public this()
        {
		}
        
        public void Start()
        {
            // Calling start on a running Stopwatch is a no-op.
            if (!isRunning)
            {
                startTimeStamp = GetTimestamp();
                isRunning = true;
            }
        }
        
        public static Stopwatch StartNew()
        {
            Stopwatch s = new Stopwatch();
            s.Start();
            return s;
        }
        
        public void Stop()
        {
            // Calling stop on a stopped Stopwatch is a no-op.
            if (isRunning)
            {
                int64 endTimeStamp = GetTimestamp();
                int64 elapsedThisPeriod = endTimeStamp - startTimeStamp;
                elapsed += elapsedThisPeriod;
                isRunning = false;
                
                if (elapsed < 0)
                {
                    // When measuring small time periods the StopWatch.Elapsed* 
                    // properties can return negative values.  This is due to 
                    // bugs in the basic input/output system (BIOS) or the hardware
                    // abstraction layer (HAL) on machines with variable-speed CPUs
                    // (e.g. Intel SpeedStep).
                    elapsed = 0;
                }
            }
        }
        
        public void Reset()
        {
            elapsed = 0;
            isRunning = false;
            startTimeStamp = 0;
        }

        // Convenience method for replacing {sw.Reset(); sw.Start();} with a single sw.Restart()
        public void Restart()
        {
            elapsed = 0;
            startTimeStamp = GetTimestamp();
            isRunning = true;
        }
        
        public bool IsRunning
        {
            get { return isRunning; }
        }
        
        public TimeSpan Elapsed
        {
            get { return TimeSpan(GetElapsedDateTimeTicks()); }
        }
        
        public int64 ElapsedMilliseconds
        {
            get { return GetRawElapsedTicks() / TicksPerMillisecond; }
        }
        
        public int64 ElapsedMicroseconds
        {
            get { return GetRawElapsedTicks(); }
        }

        public static int64 GetTimestamp()
        {
            return Internal.GetTickCountMicro();
        }

        // Get the elapsed ticks.        
        private int64 GetRawElapsedTicks()
        {
            int64 timeElapsed = elapsed;
            
            if (isRunning)
            {
                // If the StopWatch is running, add elapsed time since
                // the Stopwatch is started last time. 
                int64 currentTimeStamp = GetTimestamp();
                int64 elapsedUntilNow = currentTimeStamp - startTimeStamp;
                timeElapsed += elapsedUntilNow;
            }
            return timeElapsed;
        }   

        // Get the elapsed ticks.        
        private int64 GetElapsedDateTimeTicks()
        {
            return GetRawElapsedTicks() * 10;
        }
    
    }
}
