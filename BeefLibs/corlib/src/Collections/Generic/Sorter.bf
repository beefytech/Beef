// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Collections.Generic
{
	struct Sorter<T, T2>
	{
		// This is the threshold where Introspective sort switches to Insertion sort.
		// Empirically, 16 seems to speed up most cases without slowing down others, at least for integers.
		// Large value types may benefit from a smaller number.
		const int IntrosortSizeThreshold = 16;

	    private T* keys;
	    private T2* items;
		private int mCount;
	    private Comparison<T> comparer;    

	    internal this(T* keys, T2* items, int count, Comparison<T> comparer) 
	    {
	        this.keys = keys;
	        this.items = items;
			mCount = count;
	        this.comparer = comparer;
	    }

		internal static int FloorLog2(int n)
		{
		    int result = 0;
			int val = n;
		    while (val >= 1)
		    {
		        result++;
		        val = val / 2;
		    }
		    return result;
		}

		private static int GetMedian(int low, int hi) 
	    {
		    // Note both may be negative, if we are dealing with arrays w/ negative lower bounds.
		    //Contract.Requires(low <= hi);
		    //Contract.Assert( hi - low >= 0, "Length overflow!");
		    return low + ((hi - low) >> 1);
		}

	    internal void SwapIfGreaterWithItems(int a, int b)
	    {
	        if (a != b)
	        {
	            if (comparer(keys[a], keys[b]) > 0)
	            {
	                T temp = keys[a];
	                keys[a] = keys[b];
	                keys[b] = temp;
	                if ((items != null) && (sizeof(T2) != 0))
	                {
	                    T2 item = items[a];
	                    items[a] = items[b];
	                    items[b] = item;
	                }
	            }
	        }
	    }

	    private void Swap(int i, int j)
	    {
	        T t = keys[i];
	        keys[i] = keys[j];
	        keys[j] = t;

	        if (items != null)
	        {
	            T2 item = items[i];
	            items[i] = items[j];
	            items[j] = item;
	        }
	    }

	    internal void Sort(int left, int length)
	    {
	        IntrospectiveSort(left, length);
	    }

	    private void DepthLimitedQuickSort(int left, int right, int depthLimit)
	    {
	        // Can use the much faster jit helpers for array access.
	        repeat
	        {
	            if (depthLimit == 0)
	            {
					Heapsort(left, right);
					return;
	            }

				int curLeft = left;
				int curRight = right;
				int curDepthLimit = depthLimit;

	            int i = curLeft;
	            int j = curRight;

	            // pre-sort the low, middle (pivot), and high values in place.
	            // this improves performance in the face of already sorted data, or 
	            // data that is made up of multiple sorted runs appended together.
	            int middle = GetMedian(i, j);

				SwapIfGreaterWithItems(i, middle); // swap the low with the mid point
				SwapIfGreaterWithItems(i, j);      // swap the low with the high
				SwapIfGreaterWithItems(middle, j); // swap the middle with the high
	            
	            T x = keys[middle];
	            repeat
	            {
					while (comparer(keys[i], x) < 0) i++;
					while (comparer(x, keys[j]) < 0) j--;
	                
	                //Contract.Assert(i >= left && j <= right, "(i>=left && j<=right)  Sort failed - Is your IComparer bogus?");
	                if (i > j) break;
	                if (i < j)
	                {
	                    T key = keys[i];
	                    keys[i] = keys[j];
	                    keys[j] = key;
	                    if (items != null)
	                    {
	                        T2 item = items[i];
	                        items[i] = items[j];
	                        items[j] = item;
	                    }
	                }
	                i++;
	                j--;
	            } while (i <= j);

	            // The next iteration of the while loop is to "recursively" sort the larger half of the array and the
	            // following calls recrusively sort the smaller half.  So we subtrack one from depthLimit here so
	            // both sorts see the new value.
	            curDepthLimit--;

	            if (j - curLeft <= curRight - i)
	            {
	                if (curLeft < j) DepthLimitedQuickSort(curLeft, j, curDepthLimit);
	                curLeft = i;
	            }
	            else
	            {
	                if (i < curRight) DepthLimitedQuickSort(i, curRight, curDepthLimit);
	                curRight = j;
	            }
	        } while (left < right);
	    }

	    private void IntrospectiveSort(int left, int length)
	    {
	        if (length < 2)
	            return;

			IntroSort(left, length + left - 1, 2 * FloorLog2(mCount));
	    }

	    private void IntroSort(int lo, int hi, int depthLimit)
	    {
			int curHi = hi;
			int curDepthLimit = depthLimit;

	        while (curHi > lo)
	        {
	            int partitionSize = curHi - lo + 1;
	            if (partitionSize <= IntrosortSizeThreshold)
	            {
	                if (partitionSize == 1)
	                {
	                    return;
	                }
	                if (partitionSize == 2)
	                {
	                    SwapIfGreaterWithItems(lo, curHi);
	                    return;
	                }
	                if (partitionSize == 3)
	                {
	                    SwapIfGreaterWithItems(lo, curHi-1);
	                    SwapIfGreaterWithItems(lo, curHi);
	                    SwapIfGreaterWithItems(curHi-1, curHi);
	                    return;
	                }

	                InsertionSort(lo, curHi);
	                return;
	            }

	            if (curDepthLimit == 0)
	            {
	                Heapsort(lo, curHi);
	                return;
	            }
	            curDepthLimit--;

	            int p = PickPivotAndPartition(lo, curHi);
	            IntroSort(p + 1, curHi, curDepthLimit);
	            curHi = p - 1;
	        }
	    }

	    private int PickPivotAndPartition(int lo, int hi)
	    {
	        // Compute median-of-three.  But also partition them, since we've done the comparison.
	        int mid = lo + (hi - lo) / 2;
	        // Sort lo, mid and hi appropriately, then pick mid as the pivot.
	        SwapIfGreaterWithItems(lo, mid);
	        SwapIfGreaterWithItems(lo, hi);
	        SwapIfGreaterWithItems(mid, hi);

	        T pivot = keys[mid];
	        Swap(mid, hi - 1);
	        int left = lo, right = hi - 1;  // We already partitioned lo and hi and put the pivot in hi - 1.  And we pre-increment & decrement below.

	        while (left < right)
	        {
	            while (comparer(keys[++left], pivot) < 0) {}
	            while (comparer(pivot, keys[--right]) < 0) {}

	            if(left >= right)
	                break;

	            Swap(left, right);
	        }

	        // Put pivot in the right location.
	        Swap(left, (hi - 1));
	        return left;
	    }

	    private void Heapsort(int lo, int hi)
	    {
	        int n = hi - lo + 1;
	        for (int i = n / 2; i >= 1; i = i - 1)
	        {
	            DownHeap(i, n, lo);
	        }
	        for (int i = n; i > 1; i = i - 1)
	        {
	            Swap(lo, lo + i - 1);

	            DownHeap(1, i - 1, lo);
	        }
	    }

	    private void DownHeap(int i, int n, int lo)
	    {
			int curI = i;

	        T d = keys[lo + curI - 1];
	        //T dt = (items != null) ? items[lo + i - 1] : null;
			T2* dt = (items != null) ? &items[lo + curI - 1] : null;
	        int child;
	        while (curI <= n / 2)
	        {
	            child = 2 * curI;
	            if (child < n && comparer(keys[lo + child - 1], keys[lo + child]) < 0)
	            {
	                child++;
	            }
	            if (!(comparer(d, keys[lo + child - 1]) < 0))
	                break;
	            keys[lo + curI - 1] = keys[lo + child - 1];
	            if(items != null)
	                items[lo + curI - 1] = items[lo + child - 1];
	            curI = child;
	        }
	        keys[lo + curI - 1] = d;
	        if (items != null)
	            items[lo + curI - 1] = *dt;
	    }

		private void InsertionSort(int lo, int hi)
		{
		    int i, j;
		    T t;
			T2 ti = ?;
		    for (i = lo; i < hi; i++)
		    {
		        j = i;
		        t = keys[i + 1];
				if (items != null)
					ti = items[i + 1];
		        while (j >= lo && comparer(t, keys[j]) < 0)
		        {
		            keys[j + 1] = keys[j];
		            if(items != null)
		                items[j + 1] = items[j];
		            j--;
		        }
		        keys[j + 1] = t;
		        if (items != null)
		            items[j + 1] = ti;
		    }
		}
	}
}
