namespace System
{
	interface IServiceProvider
	{
        // Interface does not need to be marked with the serializable attribute
        Object GetService(Type serviceType);
	}
}
