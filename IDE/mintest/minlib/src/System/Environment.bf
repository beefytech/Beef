namespace System
{
    class Environment
    {
#if !PLATFORM_UNIX
       	public static readonly String NewLine = "\r\n";
#else
		static readonly string NewLine = new string("\n");
#endif // !PLATFORM_UNIX

        public static String GetResourceString(String key) 
        {
            return key;
            //return GetResourceFromDefault(key);
        }

        public static String GetResourceString(String key, params Object[] values) 
        {
            return key;
            //return GetResourceFromDefault(key);
        }

        public static String GetRuntimeResourceString(String key, String defaultValue = null) 
        {
            if (defaultValue != null)
                return defaultValue;
            return key;
            //return GetResourceFromDefault(key);
        }
	}
}