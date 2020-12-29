using System.IO;
namespace System
{
	class Test
	{
		public class TestStream : Stream
		{
			public override int64 Position
			{
				get
				{
					Runtime.FatalError();
				}

				set
				{
				}
			}

			public override int64 Length
			{
				get
				{
					Runtime.FatalError();
				}
			}

			public override bool CanRead
			{
				get
				{
					return false;
				}
			}

			public override bool CanWrite
			{
				get
				{
					return true;
				}
			}

			public override Result<int> TryRead(Span<uint8> data)
			{
				return default;
			}

			public override Result<int> TryWrite(Span<uint8> data)
			{
				String str = scope String();
				str.Append((char8*)data.Ptr, data.Length);
				Internal.[Friend]Test_Write(str.CStr());
				return .Ok(data.Length);
			}

			public override void Close()
			{

			}
		}

		public static void FatalError(String msg = "Test fatal error encountered", String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum)
		{
			String failStr = scope .()..AppendF("{} at line {} in {}", msg, line, filePath);
			Internal.[Friend]Test_Error(failStr);
		}

		public static void Assert(bool condition, String error = Compiler.CallerExpression[0], String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum) 
		{
			if (!condition)
			{
				String failStr = scope .()..AppendF("Assert failed: {} at line {} in {}", error, line, filePath);
				Internal.[Friend]Test_Error(failStr);
			}
		}
	}
}
