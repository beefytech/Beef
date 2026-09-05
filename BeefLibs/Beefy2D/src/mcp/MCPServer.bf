using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Reflection;
using Beefy.utils;

namespace Beefy.mcp
{
	// An MCP server over Streamable HTTP, bound to localhost, for a long-lived Beefy application.
	//
	// HTTP rather than stdio because the app is already running with state worth asking about (an
	// open workspace, a debug session, a capture) -- the opposite of the launch-on-demand lifetime a
	// stdio server has. Register with:
	//
	//   claude mcp add --transport http <name> http://127.0.0.1:<port>/mcp
	//
	// Everything runs on the main thread, pumped from the app's Update, so tools see the same state
	// the UI does with no locking. Tools come from MCPToolSet objects: any method tagged [MCPTool] is
	// discovered by reflection and its input schema generated from its parameters. A tool that needs
	// the app to do work over several frames (finish a compile, reach a breakpoint) defers its reply
	// and is polled each frame; the HTTP response is held open until it completes.
	//
	// Ported and generalized from BeefPerf's BPMCPServer.
	public class MCPServer
	{
		const int32 cMaxRequestSize = 8 * 1024 * 1024;
		const String cProtocolVersion = "2025-06-18";

		class Conn
		{
			public Socket mSocket ~ delete _;
			public String mRecvBuffer = new String() ~ delete _;
			public String mSendBuffer = new String() ~ delete _;
			public bool mClosed;
			public bool mCloseAfterSend;
			public int32 mPendingCount;
		}

		public enum ParamKind
		{
			String,
			Int,
			Float,
			Bool
		}

		public class Tool
		{
			public class Param
			{
				public String mName ~ delete _;
				public String mDescription ~ delete _;
				public ParamKind mKind;
				public Type mType;
				public bool mRequired;
			}

			public String mName ~ delete _;
			public String mDescription ~ delete _;
			public String mSchema ~ delete _; // Explicit inputSchema JSON, or null to generate from mParams
			public MCPToolSet mToolSet;
			public MethodInfo mMethodInfo;
			public bool mHasCallParam;
			public List<Param> mParams = new List<Param>() ~ DeleteContainerAndItems!(_);
		}

		class PendingCall
		{
			public Conn mConn;
			public String mIdJson ~ delete _;
			public MCPCall mCall ~ delete _;
		}

		public String mServerName = new String("beefy") ~ delete _;
		public String mServerVersion = new String("1.0.0") ~ delete _;
		public int32 mPort;
		// Incremented once per Update; lets a tool wait for a number of frames to pass
		public int mFrameCount;

		// Runtime error catching. A Beef assert or fatal error during an automated session would
		// otherwise end in a modal crash dialog nobody is there to click. With mCatchErrors the
		// server records the error (mLastError, the error count, and a file beside the exe that
		// survives the process), attaches it to the tool call in flight, and -- for asserts, when
		// mIgnoreAsserts is set -- lets execution continue so the session can report and recover.
		public bool mCatchErrors = true;
		public bool mIgnoreAsserts = true;
		public String mLastError ~ delete _;
		public int mErrorCount;
		public String mLastErrorPath ~ delete _;
		MCPCall mActiveCall;

		// Client presence, for a UI indicator: a connection is open, or a request arrived recently
		// (clients that open a connection per request would otherwise flicker between calls)
		Stopwatch mSinceRequest ~ delete _;
		public int32 mClientActiveGraceMS = 15000;
		public bool IsClientActive
		{
			get
			{
				if (!mConns.IsEmpty)
					return true;
				return (mSinceRequest != null) && (mSinceRequest.ElapsedMilliseconds < mClientActiveGraceMS);
			}
		}

		// The runtime's handler list holds its delegate type privately, so one static handler is
		// installed for the life of the process and forwards to whichever server is live
		static MCPServer sErrorServer;
		static bool sErrorHandlerInstalled;

		Socket mListenSocket ~ delete _;
		List<Conn> mConns = new List<Conn>() ~ DeleteContainerAndItems!(_);
		List<PendingCall> mPendingCalls = new List<PendingCall>() ~ DeleteContainerAndItems!(_);
		public List<MCPToolSet> mToolSets = new List<MCPToolSet>() ~ DeleteContainerAndItems!(_);
		public List<Tool> mTools = new List<Tool>() ~ DeleteContainerAndItems!(_);
		Dictionary<String, Tool> mToolMap = new Dictionary<String, Tool>() ~ delete _; // Keys owned by Tool.mName
		String mToolsJson ~ delete _;

		public this(int32 port)
		{
			mPort = port;
		}

		public bool IsListening => (mListenSocket != null) && (mListenSocket.IsOpen);

		public Result<void> Start()
		{
			Socket.Init();

			mListenSocket = new Socket();
			// Localhost only. An MCP endpoint that answers on a public interface hands anyone on the
			// network control of the application.
			if (mListenSocket.ListenLocal(mPort) case .Err)
			{
				DeleteAndNullify!(mListenSocket);
				return .Err;
			}

			if (mCatchErrors)
			{
				if (mLastErrorPath == null)
				{
					String exePath = scope .();
					Environment.GetExecutableFilePath(exePath);
					String exeDir = scope .();
					Path.GetDirectoryPath(exePath, exeDir);
					mLastErrorPath = new String();
					Path.Combine(mLastErrorPath, exeDir, "mcp_last_error.txt");
				}
				sErrorServer = this;
				if (!sErrorHandlerInstalled)
				{
					Runtime.AddErrorHandler(new => StaticHandleRuntimeError);
					sErrorHandlerInstalled = true;
				}
			}
			return .Ok;
		}

		public void Stop()
		{
			if (sErrorServer == this)
				sErrorServer = null;
			if (mListenSocket != null)
				mListenSocket.Close();
			ClearAndDeleteItems(mPendingCalls);
			ClearAndDeleteItems(mConns);
		}

		public ~this()
		{
			if (sErrorServer == this)
				sErrorServer = null;
		}

		static Runtime.ErrorHandlerResult StaticHandleRuntimeError(Runtime.ErrorStage stage, Runtime.Error error)
		{
			if (sErrorServer == null)
				return .ContinueFailure;
			return sErrorServer.HandleRuntimeError(stage, error);
		}

		Runtime.ErrorHandlerResult HandleRuntimeError(Runtime.ErrorStage stage, Runtime.Error error)
		{
			if (stage != .PreFail)
				return .ContinueFailure;

			String message = scope .();
			bool isAssert = false;
			if (var assertError = error as Runtime.AssertError)
			{
				isAssert = true;
				message.AppendF("Assert failed: {} at line {} in {}", assertError.mError, assertError.mLineNum, assertError.mFilePath);
			}
			else if (var fatalError = error as Runtime.FatalError)
				message.AppendF("Fatal error: {}", fatalError.mError);
			else
			{
				String typeName = scope .();
				error.GetType().GetName(typeName);
				message.AppendF("Runtime error: {}", typeName);
			}
			if (mActiveCall != null)
				message.AppendF(" (during tool '{}')", mActiveCall.mToolName);

			bool ignore = (isAssert) && (mIgnoreAsserts);
			message.Append(ignore ? " [ignored, execution continued]" : " [fatal, the process is going down]");
			RecordError(message);
			return ignore ? .Ignore : .ContinueFailure;
		}

		void RecordError(StringView message)
		{
			mErrorCount++;
			String.NewOrSet!(mLastError, message);
			if (mActiveCall != null)
				String.NewOrSet!(mActiveCall.mRuntimeError, message);

			if (mLastErrorPath != null)
			{
				String text = scope .();
				DateTime.Now.ToString(text, "yyyy-MM-dd HH:mm:ss");
				text.Append("  ");
				text.Append(message);
				text.Append("\n");
				File.WriteAllText(mLastErrorPath, text).IgnoreError();
			}
		}

		// ---------------------------------------------------------------------------------------
		// Tool registry
		// ---------------------------------------------------------------------------------------

		static bool GetParamKind(Type type, out ParamKind kind)
		{
			kind = .String;
			if (type == typeof(String))
				kind = .String;
			else if ((type == typeof(int)) || (type == typeof(int32)) || (type == typeof(int64)))
				kind = .Int;
			else if ((type == typeof(float)) || (type == typeof(double)))
				kind = .Float;
			else if (type == typeof(bool))
				kind = .Bool;
			else
				return false;
			return true;
		}

		static StringView GetJsonTypeName(ParamKind kind)
		{
			switch (kind)
			{
			case .Int: return "integer";
			case .Float: return "number";
			case .Bool: return "boolean";
			default: return "string";
			}
		}

		// Takes ownership of toolSet and registers every [MCPTool] method on it
		public void AddToolSet(MCPToolSet toolSet)
		{
			toolSet.mServer = this;
			mToolSets.Add(toolSet);
			DeleteAndNullify!(mToolsJson);

			var toolSetType = toolSet.GetType();
			for (var methodInfo in toolSetType.GetMethods(.Instance | .Public | .NonPublic))
			{
				var toolAttrResult = methodInfo.GetCustomAttribute<MCPToolAttribute>();
				if (toolAttrResult case .Err)
					continue;
				let toolAttr = toolAttrResult.Get();

				var tool = new Tool();
				tool.mName = new String(toolAttr.mName);
				tool.mDescription = new String(toolAttr.mDescription);
				if (toolAttr.mSchema != null)
				{
					tool.mSchema = new String();
					MCPJson.Quote(toolAttr.mSchema, tool.mSchema);
				}
				tool.mToolSet = toolSet;
				tool.mMethodInfo = methodInfo;

				bool isValid = true;
				for (int paramIdx < methodInfo.ParamCount)
				{
					var paramType = methodInfo.GetParamType(paramIdx);
					if (paramType == typeof(MCPCall))
					{
						tool.mHasCallParam = true;
						continue;
					}

					var param = new Tool.Param();
					param.mName = new String(methodInfo.GetParamName(paramIdx));
					param.mType = paramType;
					if (!GetParamKind(paramType, out param.mKind))
					{
						Debug.FatalError(scope $"MCP tool '{tool.mName}': parameter '{param.mName}' has an unsupported type. Use String, int, float, double or bool, or read MCPCall.mArgs directly.");
						isValid = false;
					}
					if (methodInfo.GetParamCustomAttribute<MCPParamAttribute>(paramIdx) case .Ok(let paramAttr))
					{
						param.mDescription = new String(paramAttr.mDescription);
						param.mRequired = paramAttr.mRequired;
					}
					tool.mParams.Add(param);
				}

				if ((!isValid) || (!tool.mHasCallParam))
				{
					Debug.Assert(tool.mHasCallParam, "MCP tool methods take an MCPCall as their first parameter");
					delete tool;
					continue;
				}

				if (mToolMap.ContainsKey(tool.mName))
				{
					Debug.FatalError(scope $"Duplicate MCP tool name '{tool.mName}'");
					delete tool;
					continue;
				}

				mToolMap[tool.mName] = tool;
				mTools.Add(tool);
			}
		}

		public Tool FindTool(StringView name)
		{
			Tool tool;
			if (mToolMap.TryGetValue(scope String(name), out tool))
				return tool;
			return null;
		}

		// The tool list is built once and cached until a tool set is added
		StringView GetToolsJson()
		{
			if (mToolsJson != null)
				return mToolsJson;

			mToolsJson = new String(8192);
			String json = mToolsJson;

			json.Append("{\"tools\":[");
			for (var tool in mTools)
			{
				if (@tool.Index > 0)
					json.Append(',');
				json.Append('{');
				MCPJson.AddStr(json, "name", tool.mName, false);
				MCPJson.AddStr(json, "description", tool.mDescription);
				json.Append(",\"inputSchema\":");
				if (tool.mSchema != null)
					json.Append(tool.mSchema);
				else
				{
					json.Append("{\"type\":\"object\",\"properties\":{");
					for (var param in tool.mParams)
					{
						if (@param.Index > 0)
							json.Append(',');
						MCPJson.Escape(param.mName, json);
						json.Append(":{");
						MCPJson.AddStr(json, "type", GetJsonTypeName(param.mKind), false);
						if (param.mDescription != null)
							MCPJson.AddStr(json, "description", param.mDescription);
						json.Append('}');
					}
					json.Append('}');

					bool hasRequired = false;
					for (var param in tool.mParams)
					{
						if (!param.mRequired)
							continue;
						json.Append(hasRequired ? "," : ",\"required\":[");
						MCPJson.Escape(param.mName, json);
						hasRequired = true;
					}
					if (hasRequired)
						json.Append(']');
					json.Append('}');
				}
				json.Append('}');
			}
			json.Append("]}");

			return mToolsJson;
		}

		// Marshals the call's arguments onto the tool method's parameters and invokes it
		void InvokeTool(Tool tool, MCPCall call)
		{
			Object[] args = scope Object[tool.mMethodInfo.ParamCount];
			int argIdx = 0;
			if (tool.mHasCallParam)
				args[argIdx++] = call;

			for (var param in tool.mParams)
			{
				bool hasArg = call.HasArg(param.mName);
				if ((!hasArg) && (param.mRequired))
				{
					call.Error(scope $"Missing required argument '{param.mName}'");
					return;
				}

				Object arg = null;
				switch (param.mKind)
				{
				case .String:
					var str = scope:: String();
					if (hasArg)
						call.mArgs.GetString(param.mName, str);
					arg = str;
				case .Int:
					int64 val = hasArg ? call.mArgs.GetLong(scope String(param.mName)) : 0;
					if (param.mType == typeof(int32))
						arg = scope:: box (int32)val;
					else if (param.mType == typeof(int64))
						arg = scope:: box val;
					else
						arg = scope:: box (int)val;
				case .Float:
					double val = hasArg ? (double)call.mArgs.GetFloat(scope String(param.mName)) : 0;
					if (param.mType == typeof(float))
						arg = scope:: box (float)val;
					else
						arg = scope:: box val;
				case .Bool:
					arg = scope:: box (hasArg ? call.mArgs.GetBool(scope String(param.mName)) : false);
				}
				args[argIdx++] = arg;
			}

			mActiveCall = call;
			switch (tool.mMethodInfo.Invoke(tool.mToolSet, params args))
			{
			case .Err(let err):
				call.Error(scope $"Failed to invoke tool '{tool.mName}': {err}");
			case .Ok(var result):
				result.Dispose();
			}
			mActiveCall = null;
		}

		// ---------------------------------------------------------------------------------------
		// Pump
		// ---------------------------------------------------------------------------------------

		public void Update()
		{
			mFrameCount++;

			for (var toolSet in mToolSets)
				toolSet.Update();

			UpdatePendingCalls();

			if ((mListenSocket == null) || (!mListenSocket.IsOpen))
				return;

			Socket newSocket = new Socket();
			if (newSocket.AcceptFrom(mListenSocket) case .Ok)
			{
				var conn = new Conn();
				conn.mSocket = newSocket;
				mConns.Add(conn);
			}
			else
				delete newSocket;

			for (int connIdx = mConns.Count - 1; connIdx >= 0; connIdx--)
			{
				var conn = mConns[connIdx];
				PumpConn(conn);
				FlushSend(conn);

				if ((conn.mCloseAfterSend) && (conn.mSendBuffer.IsEmpty) && (conn.mPendingCount == 0))
					conn.mClosed = true;

				if (conn.mClosed)
				{
					DropPendingCalls(conn);
					mConns.RemoveAt(connIdx);
					delete conn;
				}
			}
		}

		void UpdatePendingCalls()
		{
			for (int idx = mPendingCalls.Count - 1; idx >= 0; idx--)
			{
				var pending = mPendingCalls[idx];
				var call = pending.mCall;

				bool done = true;
				mActiveCall = call;
				if (call.mPoll != null)
					done = call.mPoll(call);

				if ((!done) && (call.mTimeoutMS > 0) && (call.ElapsedMS >= call.mTimeoutMS))
				{
					// One last poll with mTimedOut set lets the tool clean up and report what it
					// has; a tool that reports nothing gets the generic error.
					call.mTimedOut = true;
					call.mText.Clear();
					call.mIsError = false;
					if (call.mPoll != null)
						call.mPoll(call);
					if ((call.mText.IsEmpty) && (!call.mIsError))
						call.Error(scope $"Timed out after {call.mTimeoutMS} ms");
					done = true;
				}
				mActiveCall = null;

				if (!done)
					continue;

				SendToolResult(pending.mConn, pending.mIdJson, call);
				pending.mConn.mPendingCount--;
				FlushSend(pending.mConn);

				mPendingCalls.RemoveAt(idx);
				delete pending;
			}
		}

		// The client went away; its deferred calls have nowhere to report to
		void DropPendingCalls(Conn conn)
		{
			for (int idx = mPendingCalls.Count - 1; idx >= 0; idx--)
			{
				var pending = mPendingCalls[idx];
				if (pending.mConn != conn)
					continue;
				mPendingCalls.RemoveAt(idx);
				delete pending;
			}
		}

		void PumpConn(Conn conn)
		{
			uint8[4096] buf = default;

			RecvLoop: while (true)
			{
				switch (conn.mSocket.Recv(&buf, buf.Count))
				{
				case .Ok(let received):
					// A graceful close shows up as a successful zero-byte read, not an error
					if (received <= 0)
					{
						conn.mClosed = true;
						return;
					}
					conn.mRecvBuffer.Append(StringView((char8*)&buf, received));
					if (conn.mRecvBuffer.Length > cMaxRequestSize)
					{
						conn.mClosed = true;
						return;
					}
					if (received < buf.Count)
						break RecvLoop;
				case .Err(let err):
					if (err != .WouldBlock)
					{
						conn.mClosed = true;
						return;
					}
					break RecvLoop;
				}
			}

			ProcessRequests(conn);
		}

		void FlushSend(Conn conn)
		{
			while (!conn.mSendBuffer.IsEmpty)
			{
				switch (conn.mSocket.Send(conn.mSendBuffer.Ptr, conn.mSendBuffer.Length))
				{
				case .Ok(let sent):
					if (sent <= 0)
						return;
					conn.mSendBuffer.Remove(0, sent);
				case .Err(let err):
					if (err != .WouldBlock)
						conn.mClosed = true;
					return;
				}
			}
		}

		// ---------------------------------------------------------------------------------------
		// HTTP
		// ---------------------------------------------------------------------------------------

		static bool GetHeader(StringView headerBlock, StringView name, String outValue)
		{
			for (var line in headerBlock.Split('\n'))
			{
				var trimmed = line;
				trimmed.Trim();
				int colonIdx = trimmed.IndexOf(':');
				if (colonIdx == -1)
					continue;
				var key = trimmed.Substring(0, colonIdx);
				key.Trim();
				if (!key.Equals(name, true))
					continue;
				var value = trimmed.Substring(colonIdx + 1);
				value.Trim();
				outValue.Append(value);
				return true;
			}
			return false;
		}

		void ProcessRequests(Conn conn)
		{
			while (true)
			{
				int headerEnd = conn.mRecvBuffer.IndexOf("\r\n\r\n");
				if (headerEnd == -1)
					return;

				StringView headerBlock = StringView(conn.mRecvBuffer, 0, headerEnd);

				int contentLength = 0;
				var lengthStr = scope String();
				if (GetHeader(headerBlock, "Content-Length", lengthStr))
				{
					if (int.Parse(lengthStr) case .Ok(let parsed))
						contentLength = parsed;
				}

				int totalLen = headerEnd + 4 + contentLength;
				if (conn.mRecvBuffer.Length < totalLen)
					return; // Body still arriving

				var method = scope String();
				var path = scope String();
				var firstLine = headerBlock;
				int lineEnd = firstLine.IndexOf('\r');
				if (lineEnd != -1)
					firstLine = firstLine.Substring(0, lineEnd);
				var lineEnum = firstLine.Split(' ');
				if (StringView methodView = lineEnum.GetNext())
					method.Append(methodView);
				if (StringView pathView = lineEnum.GetNext())
					path.Append(pathView);

				// Bodies can run to megabytes; a sized scope String would put them on the stack.
				String body = scope .();
				body.Append(StringView((char8*)conn.mRecvBuffer.Ptr + headerEnd + 4, contentLength));

				var connectionStr = scope String();
				GetHeader(headerBlock, "Connection", connectionStr);
				if (String.Compare(connectionStr, "close", true) == 0)
					conn.mCloseAfterSend = true;

				// DNS-rebinding guard: a browser page can reach 127.0.0.1, and without this check
				// any site the user has open could drive the application. Non-browser clients
				// (which is what actually talks to this) send no Origin at all.
				var origin = scope String();
				if ((GetHeader(headerBlock, "Origin", origin)) && (!IsLocalOrigin(origin)))
				{
					conn.mRecvBuffer.Remove(0, totalLen);
					SendResponse(conn, "403 Forbidden", "text/plain", "Origin not allowed");
					continue;
				}

				conn.mRecvBuffer.Remove(0, totalLen);
				HandleRequest(conn, method, path, body);
			}
		}

		static bool IsLocalOrigin(StringView origin)
		{
			return (origin.StartsWith("http://127.0.0.1", .OrdinalIgnoreCase)) ||
				(origin.StartsWith("http://localhost", .OrdinalIgnoreCase)) ||
				(origin.StartsWith("https://127.0.0.1", .OrdinalIgnoreCase)) ||
				(origin.StartsWith("https://localhost", .OrdinalIgnoreCase));
		}

		void SendResponse(Conn conn, StringView status, StringView contentType, StringView body)
		{
			String response = scope .();
			response.Reserve(body.Length + 256);
			response.AppendF("HTTP/1.1 {0}\r\n", status);
			response.AppendF("Content-Type: {0}\r\n", contentType);
			response.AppendF("Content-Length: {0}\r\n", body.Length);
			response.AppendF("Connection: {0}\r\n", conn.mCloseAfterSend ? "close" : "keep-alive");
			response.Append("Cache-Control: no-store\r\n");
			response.Append("\r\n");
			response.Append(body);
			conn.mSendBuffer.Append(response);
		}

		// Plain-text page for GET /, the quickest way to confirm by hand that the application you
		// think is listening is the one that actually is
		public virtual void GetStatusText(String outStr)
		{
			outStr.AppendF("{0} MCP server\nPort: {1}\nTools: {2}\nConnections: {3}\nPending calls: {4}\n",
				mServerName, mPort, mTools.Count, mConns.Count, mPendingCalls.Count);
			if (mLastError != null)
				outStr.AppendF("Runtime errors: {0}\nLast error: {1}\n", mErrorCount, mLastError);
		}

		void HandleRequest(Conn conn, StringView method, StringView path, StringView body)
		{
			if (method == "POST")
			{
				HandleRpc(conn, body);
				return;
			}

			if (method == "GET")
			{
				// The spec expects 405 on the MCP endpoint from a server that offers no SSE stream
				if (path.Contains("/mcp"))
				{
					SendResponse(conn, "405 Method Not Allowed", "text/plain", "This endpoint accepts POST only");
					return;
				}

				var status = scope String();
				GetStatusText(status);
				SendResponse(conn, "200 OK", "text/plain", status);
				return;
			}

			if (method == "DELETE")
			{
				// Session teardown. This server is stateless, so there is nothing to tear down.
				SendResponse(conn, "200 OK", "text/plain", "");
				return;
			}

			SendResponse(conn, "405 Method Not Allowed", "text/plain", "Unsupported method");
		}

		// ---------------------------------------------------------------------------------------
		// JSON-RPC
		// ---------------------------------------------------------------------------------------

		static void GetIdJson(StructuredData sd, String outStr)
		{
			Object idObj = sd.Get("id");
			if (idObj == null)
			{
				outStr.Append("null");
				return;
			}

			var type = idObj.GetType();
			if ((type == typeof(String)) || (type == typeof(StringView)))
			{
				var idStr = scope String();
				sd.GetString("id", idStr);
				MCPJson.Escape(idStr, outStr);
			}
			else
				sd.GetLong("id").ToString(outStr);
		}

		void SendRpcResult(Conn conn, StringView idJson, StringView resultJson)
		{
			String body = scope .();
			body.Reserve(resultJson.Length + 64);
			body.Append("{\"jsonrpc\":\"2.0\",\"id\":");
			body.Append(idJson);
			body.Append(",\"result\":");
			body.Append(resultJson);
			body.Append("}");
			SendResponse(conn, "200 OK", "application/json", body);
		}

		void SendRpcError(Conn conn, StringView idJson, int32 code, StringView message)
		{
			String body = scope .();
			body.Reserve(message.Length + 128);
			body.Append("{\"jsonrpc\":\"2.0\",\"id\":");
			body.Append(idJson);
			body.Append(",\"error\":{\"code\":");
			code.ToString(body);
			body.Append(",\"message\":");
			MCPJson.Escape(message, body);
			body.Append("}}");
			SendResponse(conn, "200 OK", "application/json", body);
		}

		void HandleRpc(Conn conn, StringView body)
		{
			if (mSinceRequest == null)
				mSinceRequest = new Stopwatch();
			mSinceRequest.Restart();

			var sd = scope StructuredData();
			if (sd.LoadFromString(body) case .Err(let loadErr))
			{
				var message = scope String();
				message.AppendF("Parse error: {0}", loadErr);
				var errBody = scope String();
				errBody.Append("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":");
				MCPJson.Escape(message, errBody);
				errBody.Append("}}");
				SendResponse(conn, "400 Bad Request", "application/json", errBody);
				return;
			}

			var method = scope String();
			sd.GetString("method", method);

			bool isNotification = !sd.Contains("id");
			var idJson = scope String();
			GetIdJson(sd, idJson);

			if (isNotification)
			{
				// Notifications get no JSON-RPC response at all, just an HTTP acknowledgement
				SendResponse(conn, "202 Accepted", "text/plain", "");
				return;
			}

			switch (method)
			{
			case "initialize":
				var clientVersion = scope String();
				using (sd.Open("params"))
					sd.GetString("protocolVersion", clientVersion);
				// Echoing the client's version back is the most compatible answer for any version
				// we can actually serve, and this server only uses the parts common to all of them.
				if (clientVersion.IsEmpty)
					clientVersion.Set(cProtocolVersion);

				var result = scope String();
				result.Append("{\"protocolVersion\":");
				MCPJson.Escape(clientVersion, result);
				result.Append(",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":");
				MCPJson.Escape(mServerName, result);
				result.Append(",\"version\":");
				MCPJson.Escape(mServerVersion, result);
				result.Append("}}");
				SendRpcResult(conn, idJson, result);
			case "ping":
				SendRpcResult(conn, idJson, "{}");
			case "tools/list":
				SendRpcResult(conn, idJson, GetToolsJson());
			case "tools/call":
				HandleToolCall(conn, idJson, sd);
			default:
				SendRpcError(conn, idJson, -32601, scope $"Method not found: {method}");
			}
		}

		void HandleToolCall(Conn conn, StringView idJson, StructuredData sd)
		{
			var call = new MCPCall();
			call.mServer = this;

			using (sd.Open("params"))
			{
				var toolName = scope String();
				sd.GetString("name", toolName);
				call.mToolName = new String(toolName);

				Tool tool;
				if (!mToolMap.TryGetValue(toolName, out tool))
					call.Error(scope $"Unknown tool: {toolName}");
				else
				{
					using (sd.Open("arguments"))
					{
						call.mArgs = sd;
						InvokeTool(tool, call);
						call.mArgs = null;
					}
				}
			}

			if ((call.mIsDeferred) && (!call.mIsError))
			{
				// Reply once the poll says so. The connection stays open, and while it waits no
				// other response goes out on it -- clients do not pipeline MCP requests.
				var pending = new PendingCall();
				pending.mConn = conn;
				pending.mIdJson = new String(idJson);
				pending.mCall = call;
				conn.mPendingCount++;
				mPendingCalls.Add(pending);
				return;
			}

			SendToolResult(conn, idJson, call);
			delete call;
		}

		void SendToolResult(Conn conn, StringView idJson, MCPCall call)
		{
			if (call.mRuntimeError != null)
			{
				// Whatever the tool produced is suspect once an assert fired underneath it
				String text = scope String(call.mText);
				call.mText.Set("RUNTIME ERROR DURING TOOL: ");
				call.mText.Append(call.mRuntimeError);
				if (!text.IsEmpty)
				{
					call.mText.Append("\nPartial result: ");
					call.mText.Append(text);
				}
				call.mIsError = true;
			}

			var result = scope String(call.mText.Length + 256);
			result.Append("{\"content\":[");

			bool hasImages = (call.mImages != null) && (!call.mImages.IsEmpty);
			bool first = true;
			if ((!call.mText.IsEmpty) || (!hasImages))
			{
				result.Append("{\"type\":\"text\",\"text\":");
				MCPJson.Escape(call.mText.IsEmpty ? "{}" : call.mText, result);
				result.Append('}');
				first = false;
			}

			if (hasImages)
			{
				for (var image in call.mImages)
				{
					if (!first)
						result.Append(',');
					result.Append("{\"type\":\"image\",\"data\":\"");
					MCPJson.Base64Encode(image.mData, result);
					result.Append("\",\"mimeType\":");
					MCPJson.Escape(image.mMimeType, result);
					result.Append('}');
					first = false;
				}
			}

			result.Append(']');
			if (call.mIsError)
				result.Append(",\"isError\":true");
			result.Append('}');
			SendRpcResult(conn, idJson, result);
		}
	}
}
