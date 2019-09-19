
namespace System
{
    class Poof
    {
        struct Whoop
        {

		}

        public struct Inner
        {
            this()
            {
                
			}
            
            Result<void> Test()
            {
                return .Ok;
			}
		}

        struct Other<T>
        {
            Result<void> Pook()
            {
            	return .Ok;
			}

		}

        public static T GetDefault<T>()
        {
            return default(T);
		}
	}
}

static
{
    static int32 sCounterVal = 123;
}
