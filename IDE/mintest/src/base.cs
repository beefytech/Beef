/*using System;
using System.Runtime.InteropServices;

namespace Allegro
{
    public partial class AllegroAPI
    {
        //internal const string AllegroDllName = "allegro_monolith-5.2.dll";

        public static string AllegroDllVersionName(string name)
        {
            var p = (int)Environment.OSVersion.Platform;
            if ((p == 4) || (p == 6) || (p == 128))
                return name; // Linux

            // Windows
            return name + '-' + ALLEGRO_VERSION + '.' + ALLEGRO_SUB_VERSION + ".dll";
        }

        //private const string AllegroDllName = "allegro";
        internal const string AllegroDllName = "allegro_monolith-5.2.dll";


        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate int AppMainDelegate(int argc, string[] argv);


        

        public const int ALLEGRO_VERSION = 5;
        public const int ALLEGRO_SUB_VERSION = 2;
        public const int ALLEGRO_WIP_VERSION = 2;


        /// <summary>
        /// Not sure we need it, but since ALLEGRO_VERSION_STR contains it:
        /// 0 = GIT
        /// 1 = first release
        /// 2... = hotfixes?
        /// 
        /// Note x.y.z (= x.y.z.0) has release number 1, and x.y.z.1 has release
        /// number 2, just to confuse you.
        /// </summary>
        public const int ALLEGRO_RELEASE_NUMBER = 0;

        public const string ALLEGRO_VERSION_STR = "5.2.2 (GIT)";
        public const string ALLEGRO_DATE_STR = "2016";
        public const int ALLEGRO_DATE = 20160731;    /* yyyymmdd */
        public const int ALLEGRO_VERSION_INT =
            ((ALLEGRO_VERSION << 24) | (ALLEGRO_SUB_VERSION << 16) |
            (ALLEGRO_WIP_VERSION << 8) | ALLEGRO_RELEASE_NUMBER);

        //[DllImport(AllegroDllName, CallingConvention = CallingConvention.Cdecl)]
		//[Import(AllegroDllName)]Cdecl)]
        public static extern UInt32 al_get_allegro_version();

        [DllImport(AllegroDllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int al_run_main(int argc,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0, ArraySubType = UnmanagedType.LPStr)]
            string[] argv, AppMainDelegate main);

        /*******************************************/
        /************ Some global stuff ************/
        /*******************************************/

        /// <summary>
        /// ALLEGRO_PI
        /// </summary>
        public const double ALLEGRO_PI = 3.14159265358979323846;

        public static int AL_ID(byte a, byte b, byte c, byte d)
        {
            return (a << 24) | (b << 16) | (c << 8) | d;
        }


        
        public static bool IsLinux
        {
            get
            {
                int p = (int) Environment.OSVersion.Platform;
                return (p == 4) || (p == 6) || (p == 128);
            }
        }
    }
}*/
