namespace System.Threading.Tasks;

public class Parallel
{
#if BF_PLATFORM_WINDOWS

	public static void ForEach<T>(Span<T> source, delegate void(T item) body, int maxDegreeOfParallelism = -1) {
	    int count = source.Length;
	    if (count == 0)
	        return;

		var processorCount = OperatingSystem.[Friend]GetSystemInfo(.. scope .()).dwNumberOfProcessors;
	    int degree = maxDegreeOfParallelism == -1 ? processorCount : maxDegreeOfParallelism;
	    if (degree <= 0)
	        degree = 1;

	    degree = Math.Min(degree, count);

	    if (degree == 1)
	    {
	        for (var item in source)
	            body(item);

	        return;
	    }
	    
	    Task[] tasks = new Task[degree];
	    defer delete tasks;
	    
	    int workItemIndex = -1;
	    for (var i < degree)
		{	
			tasks[i] = new Task(new [&]() => {
	            int index;
	            while ((index = Interlocked.Increment(ref workItemIndex)) < count)
					body(source[index]);
	        });

			tasks[i].Start();
		}
	    
	    for (var i < degree) {
	        tasks[i].Wait();
	        delete tasks[i];
	    }
	}

#endif
}