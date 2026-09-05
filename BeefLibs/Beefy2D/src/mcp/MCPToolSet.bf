using System;
using System.Collections;
using System.Diagnostics;
using Beefy.utils;

namespace Beefy.mcp
{
	// Marks a method on an MCPToolSet as an MCP tool. The method's first parameter must be an
	// MCPCall; any further parameters become the tool's arguments and are marshalled from the
	// request by name (String, int/int32/int64, float/double, bool). The input schema is generated
	// from those parameters unless an explicit schema is given, written with ' for " (see
	// MCPJson.Quote) for tools that want nested or array arguments and read them from MCPCall.mArgs.
	[AttributeUsage(.Method, .ReflectAttribute | .AlwaysIncludeTarget, ReflectUser = .All)]
	public struct MCPToolAttribute : Attribute
	{
		public String mName;
		public String mDescription;
		public String mSchema;

		public this(String name, String description, String schema = null)
		{
			mName = name;
			mDescription = description;
			mSchema = schema;
		}
	}

	// Describes one tool argument. Without it the argument is optional and undocumented.
	[AttributeUsage(.Parameter, .ReflectAttribute)]
	public struct MCPParamAttribute : Attribute
	{
		public String mDescription;
		public bool mRequired;

		public this(String description, bool required = false)
		{
			mDescription = description;
			mRequired = required;
		}
	}

	// One tools/call in flight. A tool either fills in its result before returning, or calls Defer
	// to be polled every frame until it finishes -- that is how a tool waits for a compile to end or
	// the debugger to pause without ever blocking the frame.
	public class MCPCall
	{
		public class Image
		{
			public uint8[] mData ~ delete _;
			public String mMimeType ~ delete _;
		}

		public MCPServer mServer;
		public String mToolName ~ delete _;

		// The request's arguments object. Only valid while the tool method itself is running: a
		// deferred poll must have copied out anything it needs.
		public StructuredData mArgs;

		public String mText = new String() ~ delete _;
		public bool mIsError;
		public List<Image> mImages ~ DeleteContainerAndItems!(_);
		// A Beef assert or fatal error raised while this call was running (see MCPServer's error
		// catcher); reported with the result so the client learns the tool tripped over something
		public String mRuntimeError ~ delete _;

		public bool mIsDeferred;
		public delegate bool(MCPCall call) mPoll ~ delete _;
		public Stopwatch mStopwatch ~ delete _;
		public int mTimeoutMS;
		// Set before the final poll once the timeout has passed, so the poll can clean up and choose
		// its own result. If it sets none, the server reports a generic timeout error.
		public bool mTimedOut;

		public int ElapsedMS => (mStopwatch != null) ? (int)mStopwatch.ElapsedMilliseconds : 0;

		// Sets the result text. By convention this is JSON.
		public void Result(StringView text)
		{
			mText.Set(text);
			mIsError = false;
		}

		public void Error(StringView message)
		{
			mText.Set(message);
			mIsError = true;
		}

		public void AddImage(Span<uint8> data, StringView mimeType = "image/png")
		{
			if (mImages == null)
				mImages = new List<Image>();
			var image = new Image();
			image.mData = new uint8[data.Length];
			data.CopyTo(image.mData);
			image.mMimeType = new String(mimeType);
			mImages.Add(image);
		}

		// The poll runs once per frame and returns true when the call has finished. The delegate is
		// owned by the call; capture by value, never by reference to locals of the tool method.
		public void Defer(int timeoutMS, delegate bool(MCPCall call) poll)
		{
			mIsDeferred = true;
			mPoll = poll;
			mTimeoutMS = timeoutMS;
			mStopwatch = new Stopwatch(true);
		}

		public bool HasArg(StringView name)
		{
			return (mArgs != null) && (mArgs.Contains(name));
		}

		public void GetString(StringView name, String outStr, StringView defaultVal = default)
		{
			if (!HasArg(name))
			{
				outStr.Append(defaultVal);
				return;
			}
			mArgs.GetString(name, outStr);
		}

		public int64 GetInt(StringView name, int64 defaultVal = 0)
		{
			if (!HasArg(name))
				return defaultVal;
			return mArgs.GetLong(scope String(name), defaultVal);
		}

		public double GetFloat(StringView name, double defaultVal = 0)
		{
			if (!HasArg(name))
				return defaultVal;
			return mArgs.GetFloat(scope String(name), (float)defaultVal);
		}

		public bool GetBool(StringView name, bool defaultVal = false)
		{
			if (!HasArg(name))
				return defaultVal;
			return mArgs.GetBool(scope String(name), defaultVal);
		}
	}

	// A group of related tools. Subclass, add [MCPTool] methods, and hand an instance to
	// MCPServer.AddToolSet; the server owns it from then on. Update runs once per frame for tool
	// sets that drive their own queues.
	public class MCPToolSet
	{
		public MCPServer mServer;

		public virtual void Update()
		{
		}
	}
}
