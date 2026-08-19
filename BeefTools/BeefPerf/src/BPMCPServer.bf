using System;
using System.Collections;
using System.Diagnostics;
using System.Net;
using Beefy.utils;

namespace BeefPerf
{
	static class BPJson
	{
		static char8 HexDigit(int32 val)
		{
			return (val < 10) ? (char8)('0' + val) : (char8)('A' + val - 10);
		}

		// Appends str as a quoted JSON string. Bytes >= 0x80 pass through untouched: the payload is
		// already UTF-8 and JSON is defined over UTF-8, so re-encoding them as \u escapes would only
		// mangle multi-byte zone names.
		public static void Escape(StringView str, String outStr)
		{
			outStr.Append('"');
			for (int i < str.Length)
			{
				char8 c = str[i];
				switch (c)
				{
				case '"': outStr.Append("\\\"");
				case '\\': outStr.Append("\\\\");
				case '\n': outStr.Append("\\n");
				case '\r': outStr.Append("\\r");
				case '\t': outStr.Append("\\t");
				default:
					if ((uint8)c < 0x20)
					{
						outStr.Append("\\u00");
						outStr.Append(HexDigit(((uint8)c >> 4) & 0xF));
						outStr.Append(HexDigit((uint8)c & 0xF));
					}
					else
						outStr.Append(c);
				}
			}
			outStr.Append('"');
		}

		public static void AddStr(String outStr, StringView name, StringView value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			Escape(value, outStr);
		}

		public static void AddNum(String outStr, StringView name, int64 value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			value.ToString(outStr);
		}

		public static void AddBool(String outStr, StringView name, bool value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			outStr.Append(value ? "true" : "false");
		}

		// Tool schemas below are written with ' where JSON wants ", so they stay readable instead of
		// becoming a wall of backslashes. Nothing passed through here may contain a literal apostrophe.
		public static void Quote(StringView str, String outStr)
		{
			for (int i < str.Length)
				outStr.Append((str[i] == '\'') ? '"' : str[i]);
		}
	}

	// An MCP server over Streamable HTTP, bound to localhost.
	//
	// HTTP rather than stdio because BeefPerf is a long-lived window that programs connect to on their
	// own schedule: the profiler has to already be running and holding a capture before anyone asks it
	// a question, which is the opposite of the launch-on-demand lifetime a stdio server has. It is
	// registered with:
	//
	//   claude mcp add --transport http beefperf http://127.0.0.1:<mcpPort>/mcp
	//
	// Everything runs on the main thread, pumped from BPApp.Update, so queries see the same session
	// data the UI does with no locking -- BpClient.TryRecv only ever fills a byte buffer on the socket
	// thread, and it is BpClient.Update on the main thread that turns those bytes into stream data.
	// A query therefore cannot observe a half-parsed zone.
	class BPMCPServer
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
		}

		public int32 mPort;

		Socket mListenSocket ~ delete _;
		List<Conn> mConns = new List<Conn>() ~ DeleteContainerAndItems!(_);
		String mToolsJson ~ delete _;

		// A selection waiting to be shown in the window, see RequestShow
		int32 mShowSessionId;
		BPSelection mShowSelection;
		int32 mShowRetriesLeft;

		public this(int32 port)
		{
			mPort = port;
		}

		public bool IsListening => (mListenSocket != null) && (mListenSocket.IsOpen);

		// Socket.Init (WSAStartup) is already done by BPApp.Init before this runs.
		public Result<void> Start()
		{
			mListenSocket = new Socket();
			// Localhost only. An MCP endpoint that answers on a public interface hands anyone on the
			// network the contents of whatever this machine is profiling.
			if (mListenSocket.ListenLocal(mPort) case .Err)
			{
				DeleteAndNullify!(mListenSocket);
				return .Err;
			}
			return .Ok;
		}

		public void Stop()
		{
			if (mListenSocket != null)
				mListenSocket.Close();
			ClearAndDeleteItems(mConns);
		}

		public void Update()
		{
			UpdateShowRequest();

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

				if ((conn.mCloseAfterSend) && (conn.mSendBuffer.IsEmpty))
					conn.mClosed = true;

				if (conn.mClosed)
				{
					mConns.RemoveAt(connIdx);
					delete conn;
				}
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

				var body = scope String(conn.mRecvBuffer, headerEnd + 4, contentLength);

				var connectionStr = scope String();
				GetHeader(headerBlock, "Connection", connectionStr);
				if (String.Compare(connectionStr, "close", true) == 0)
					conn.mCloseAfterSend = true;

				// DNS-rebinding guard: a browser page can reach 127.0.0.1, and without this check any
				// site the user has open could read their profiling data. Non-browser clients (which
				// is what actually talks to this) send no Origin at all.
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
			var response = scope String(body.Length + 256);
			response.AppendF("HTTP/1.1 {0}\r\n", status);
			response.AppendF("Content-Type: {0}\r\n", contentType);
			response.AppendF("Content-Length: {0}\r\n", body.Length);
			response.AppendF("Connection: {0}\r\n", conn.mCloseAfterSend ? "close" : "keep-alive");
			response.Append("Cache-Control: no-store\r\n");
			response.Append("\r\n");
			response.Append(body);
			conn.mSendBuffer.Append(response);
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
				// The spec expects 405 on the MCP endpoint from a server that offers no SSE stream.
				// Anything else gets a plain status page, which makes it easy to confirm by hand that
				// the right BeefPerf is listening where you think it is.
				if (path.Contains("/mcp"))
				{
					SendResponse(conn, "405 Method Not Allowed", "text/plain", "This endpoint accepts POST only");
					return;
				}

				var status = scope String();
				status.AppendF("BeefPerf MCP server\nProfiler port: {0}\nMCP port: {1}\nSessions: {2}\nConnected clients: {3}\n",
					gApp.mListenPort, mPort, gApp.mSessions.Count, gApp.mClients.Count);
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
				BPJson.Escape(idStr, outStr);
			}
			else
				sd.GetLong("id").ToString(outStr);
		}

		void SendRpcResult(Conn conn, StringView idJson, StringView resultJson)
		{
			var body = scope String(resultJson.Length + 64);
			body.Append("{\"jsonrpc\":\"2.0\",\"id\":");
			body.Append(idJson);
			body.Append(",\"result\":");
			body.Append(resultJson);
			body.Append("}");
			SendResponse(conn, "200 OK", "application/json", body);
		}

		void SendRpcError(Conn conn, StringView idJson, int32 code, StringView message)
		{
			var body = scope String(message.Length + 128);
			body.Append("{\"jsonrpc\":\"2.0\",\"id\":");
			body.Append(idJson);
			body.Append(",\"error\":{\"code\":");
			code.ToString(body);
			body.Append(",\"message\":");
			BPJson.Escape(message, body);
			body.Append("}}");
			SendResponse(conn, "200 OK", "application/json", body);
		}

		void HandleRpc(Conn conn, StringView body)
		{
			var sd = scope StructuredData();
			if (sd.LoadFromString(body) case .Err(let loadErr))
			{
				var message = scope String();
				message.AppendF("Parse error: {0}", loadErr);
				var errBody = scope String();
				errBody.Append("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":");
				BPJson.Escape(message, errBody);
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
				// Echoing the client's version back is the most compatible answer for any version we
				// can actually serve, and this server only uses the parts common to all of them.
				if (clientVersion.IsEmpty)
					clientVersion.Set(cProtocolVersion);

				var result = scope String();
				result.Append("{\"protocolVersion\":");
				BPJson.Escape(clientVersion, result);
				result.Append(",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"beefperf\",\"version\":\"1.0.0\"}}");
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
			var toolName = scope String();
			var resultText = scope String(4096);
			bool isError = false;

			using (sd.Open("params"))
			{
				sd.GetString("name", toolName);
				using (sd.Open("arguments"))
					isError = !HandleTool(toolName, sd, resultText);
			}

			var result = scope String(resultText.Length + 128);
			result.Append("{\"content\":[{\"type\":\"text\",\"text\":");
			BPJson.Escape(resultText, result);
			result.Append("}]");
			if (isError)
				result.Append(",\"isError\":true");
			result.Append("}");
			SendRpcResult(conn, idJson, result);
		}

		// ---------------------------------------------------------------------------------------
		// Tool definitions
		// ---------------------------------------------------------------------------------------

		// The tool list is a fixed blob, built once on first request. Schemas are written with ' where
		// JSON wants " (see BPJson.Quote) so they stay legible; keep apostrophes out of the text.
		StringView GetToolsJson()
		{
			if (mToolsJson != null)
				return mToolsJson;

			mToolsJson = new String(8192);
			String json = mToolsJson;

			void Add(StringView line)
			{
				BPJson.Quote(line, json);
			}

			Add("{'tools':[");

			Add("{'name':'status',");
			Add("'description':'BeefPerf server state: the address instrumented programs should connect to, how many are connected right now, and how many capture sessions are held. Call this first to confirm the profiler is up and to get the connect string to pass to BpInit.',");
			Add("'inputSchema':{'type':'object','properties':{}}},");

			Add("{'name':'list_sessions',");
			Add("'description':'List every capture session BeefPerf holds, live and finished, with client name, session name, start time, duration, thread count and zone count. The id returned here is what every other tool takes as its session argument.',");
			Add("'inputSchema':{'type':'object','properties':{}}},");

			Add("{'name':'session_info',");
			Add("'description':'Detail for one capture session: its full time range, totals, and every thread in it with the thread index that find, profile and time_slice use.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'session':{'type':'integer','description':'Session id from list_sessions. Defaults to the session selected in the BeefPerf window, or the newest one.'}}}},");

			Add("{'name':'select_session',");
			Add("'description':'Show a session in the BeefPerf window, the same as clicking it in the Workspace panel. Useful so a human watching the window sees the same capture being queried.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'session':{'type':'integer','description':'Session id from list_sessions.'}},");
			Add("'required':['session']}},");

			Add("{'name':'clear_session',");
			Add("'description':'Throw away everything captured in a session so far, keeping the session and its connection alive so a client that is still attached keeps recording into a clean timeline. Use this between repeated runs of the same test so each run is measured on its own, rather than being buried in the accumulated capture. Timestamps restart from zero.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'session':{'type':'integer','description':'Session id from list_sessions. Defaults to the selected or newest session.'}}}},");

			Add("{'name':'find',");
			Add("'description':'Search a session for zones and events by name, the same search the Find panel runs. Sorted by duration descending by default, so leaving text empty is how you ask for the slowest zones in a session or in a time range. Search syntax: space separated terms all have to match, -term excludes, =term matches the whole name exactly, and quotes group a phrase. Matching is case insensitive. Returns startTick and depth for each hit, which is what profile takes to drill into one of them.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'text':{'type':'string','description':'What to search for. Empty matches everything, which combined with the default sort gives the slowest zones.'},");
			Add("'thread':{'type':'string','description':'Only search threads whose name matches this, using the same syntax as text.'},");
			Add("'session':{'type':'integer','description':'Session id from list_sessions. Defaults to the selected or newest session.'},");
			Add("'startUs':{'type':'integer','description':'Start of the time range, in microseconds from the beginning of the session. Omit for no lower bound.'},");
			Add("'endUs':{'type':'integer','description':'End of the time range, in microseconds from the beginning of the session. Omit for no upper bound.'},");
			Add("'zones':{'type':'boolean','description':'Include timed zones. Default true.'},");
			Add("'events':{'type':'boolean','description':'Include point events. Default true.'},");
			Add("'sort':{'type':'string','enum':['length','start','name','thread'],'description':'Sort column. Default length.'},");
			Add("'ascending':{'type':'boolean','description':'Sort ascending instead of descending. Default false.'},");
			Add("'max':{'type':'integer','description':'How many results to return, 1 to 1000. Default 50.'}}}},");

			Add("{'name':'profile',");
			Add("'description':'Aggregate everything under a zone by name, the same breakdown the Profile panel shows when you click an entry in the timeline. Gives count, total (inclusive) and self (exclusive) time per name. Two ways to select what to profile: pass thread plus startTick plus depth from a find result to profile that one zone and its whole subtree, or pass thread plus startUs plus endUs to profile a time range on that thread.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'session':{'type':'integer','description':'Session id from list_sessions. Defaults to the selected or newest session.'},");
			Add("'thread':{'type':'integer','description':'Thread index from session_info or a find result.'},");
			Add("'startTick':{'type':'integer','description':'startTick of the zone to profile, from a find result. Requires depth.'},");
			Add("'depth':{'type':'integer','description':'depth of the zone to profile, from a find result. Requires startTick.'},");
			Add("'startUs':{'type':'integer','description':'Start of a time range to profile instead of one zone, in microseconds from the beginning of the session.'},");
			Add("'endUs':{'type':'integer','description':'End of that time range, in microseconds from the beginning of the session.'},");
			Add("'max':{'type':'integer','description':'How many rows to return, 1 to 1000. Default 50.'},");
			Add("'show':{'type':'boolean','description':'Also select the zone in the BeefPerf window so a human can see it. Default true.'}},");
			Add("'required':['thread']}},");

			Add("{'name':'time_slice',");
			Add("'description':'What every thread was doing across one span of time. For each thread it reports the zones that were already running when the slice began, the zones overlapping the slice down to a depth limit, any point events inside it, and how much of the slice that thread spent inside a top level zone. This is the view for questions about threads waiting on each other, uneven work distribution, or a stall whose cause is on a different thread than the symptom.',");
			Add("'inputSchema':{'type':'object','properties':{");
			Add("'session':{'type':'integer','description':'Session id from list_sessions. Defaults to the selected or newest session.'},");
			Add("'startUs':{'type':'integer','description':'Start of the slice, in microseconds from the beginning of the session.'},");
			Add("'endUs':{'type':'integer','description':'End of the slice, in microseconds from the beginning of the session.'},");
			Add("'maxDepth':{'type':'integer','description':'How deep to report nested zones. Default 3. Raise it to see inside the work, lower it for an overview.'},");
			Add("'maxZones':{'type':'integer','description':'Cap on zones reported per thread, 1 to 1000. Default 60.'},");
			Add("'events':{'type':'boolean','description':'Include point events. Default true.'}},");
			Add("'required':['startUs','endUs']}}");

			Add("]}");

			return mToolsJson;
		}

		// ---------------------------------------------------------------------------------------
		// Session helpers
		// ---------------------------------------------------------------------------------------

		// Absolute ticks are what the stream records, but they only mean something relative to the
		// session's first tick, so everything crossing the API is microseconds from session start.
		static int64 TickToUs(BpSession session, int64 tick)
		{
			return (int64)((tick - session.mFirstTick) * session.GetTicksToUSScale());
		}

		static int64 TicksToDurUs(BpSession session, int64 ticks)
		{
			return (int64)(ticks * session.GetTicksToUSScale());
		}

		static int64 UsToTick(BpSession session, int64 timeUS)
		{
			double scale = session.GetTicksToUSScale();
			if (scale == 0)
				return timeUS + session.mFirstTick; // Clock info has not arrived yet
			return (int64)(timeUS / scale) + session.mFirstTick;
		}

		BpSession FindSession(StructuredData sd)
		{
			if (sd.Contains("session"))
			{
				int32 wantId = sd.GetInt("session", -1);
				for (var session in gApp.mSessions)
				{
					if (session.mId == wantId)
						return session;
				}
				return null;
			}

			if (gApp.mCurSession != null)
				return gApp.mCurSession;
			if (!gApp.mSessions.IsEmpty)
				return gApp.mSessions.Back;
			return null;
		}

		static void AppendSessionSummary(BpSession session, String outStr)
		{
			outStr.Append('{');
			BPJson.AddNum(outStr, "id", session.mId, false);

			var name = scope String();
			if (session.mSessionName != null)
				name.Append(session.mSessionName);
			BPJson.AddStr(outStr, "sessionName", name);

			name.Clear();
			if (session.mClientName != null)
				name.Append(session.mClientName);
			BPJson.AddStr(outStr, "clientName", name);

			var timeStr = scope String();
			session.mConnectTime.ToString(timeStr, "yyyy-MM-dd HH:mm:ss");
			BPJson.AddStr(outStr, "startTime", timeStr);

			BPJson.AddBool(outStr, "live", !session.mSessionOver);
			BPJson.AddNum(outStr, "durationUs", TicksToDurUs(session, session.mCurTick - session.mFirstTick));

			timeStr.Clear();
			BpClient.ElapsedTimeToStr(session.TicksToUS(session.mCurTick - session.mFirstTick), timeStr);
			BPJson.AddStr(outStr, "duration", timeStr);

			BPJson.AddNum(outStr, "threads", session.mThreads.Count);
			BPJson.AddNum(outStr, "zones", session.mNumZones);
			BPJson.AddNum(outStr, "bytes", session.mStatTotalBytesReceived);
			outStr.Append('}');
		}

		// ---------------------------------------------------------------------------------------
		// Tools
		// ---------------------------------------------------------------------------------------

		// Returns false to mark the call as an error; outText carries the message either way.
		bool HandleTool(StringView toolName, StructuredData sd, String outText)
		{
			switch (toolName)
			{
			case "status":
				return ToolStatus(outText);
			case "list_sessions":
				return ToolListSessions(outText);
			case "session_info":
				return ToolSessionInfo(sd, outText);
			case "select_session":
				return ToolSelectSession(sd, outText);
			case "clear_session":
				return ToolClearSession(sd, outText);
			case "find":
				return ToolFind(sd, outText);
			case "profile":
				return ToolProfile(sd, outText);
			case "time_slice":
				return ToolTimeSlice(sd, outText);
			default:
				outText.AppendF("Unknown tool: {0}", toolName);
				return false;
			}
		}

		bool ToolStatus(String outText)
		{
			outText.Append('{');
			BPJson.AddNum(outText, "profilerPort", gApp.mListenPort, false);
			BPJson.AddBool(outText, "listening", gApp.Listening);
			BPJson.AddStr(outText, "connectString", scope $"127.0.0.1:{gApp.mListenPort}");
			BPJson.AddNum(outText, "mcpPort", mPort);
			BPJson.AddNum(outText, "connectedClients", gApp.mClients.Count);
			BPJson.AddNum(outText, "sessionCount", gApp.mSessions.Count);
			BPJson.AddNum(outText, "currentSession", (gApp.mCurSession != null) ? gApp.mCurSession.mId : -1);
			BPJson.AddNum(outText, "bytesPerSec", gApp.mStatBytesPerSec);
			outText.Append('}');
			return true;
		}

		bool ToolListSessions(String outText)
		{
			outText.Append("{\"sessions\":[");
			for (var session in gApp.mSessions)
			{
				if (@session.Index > 0)
					outText.Append(',');
				AppendSessionSummary(session, outText);
			}
			outText.Append("]}");
			return true;
		}

		bool ToolSessionInfo(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			outText.Append("{\"session\":");
			AppendSessionSummary(session, outText);
			BPJson.AddNum(outText, "firstTick", session.mFirstTick);
			BPJson.AddNum(outText, "curTick", session.mCurTick);
			BPJson.AddNum(outText, "zoneNameCount", session.mZoneNames.Count);

			outText.Append(",\"threads\":[");
			for (int32 trackIdx < (int32)session.mThreads.Count)
			{
				var track = session.mThreads[trackIdx];
				if (trackIdx > 0)
					outText.Append(',');

				var trackName = scope String(64);
				track.GetName(trackName);

				outText.Append('{');
				BPJson.AddNum(outText, "index", trackIdx, false);
				BPJson.AddStr(outText, "name", trackName);
				BPJson.AddNum(outText, "nativeThreadId", track.mNativeThreadId);
				BPJson.AddNum(outText, "startUs", TickToUs(session, track.mCreatedTick));
				if (track.mRemoveTick != 0)
					BPJson.AddNum(outText, "endUs", TickToUs(session, track.mRemoveTick));
				BPJson.AddNum(outText, "streamBuffers", track.mStreamDataList.Count);
				outText.Append('}');
			}
			outText.Append("]}");
			return true;
		}

		bool ToolSelectSession(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			gApp.SetSession(session);
			outText.Append("{\"selected\":");
			AppendSessionSummary(session, outText);
			outText.Append("}");
			return true;
		}

		// Throws away everything captured so far but keeps the session and its connection, so a client
		// that is still attached goes on recording into a clean timeline. That is what makes repeated
		// runs of the same test comparable: clear, run, read, clear again. Mirrors the Ctrl+X path in
		// PerfView.KeyDown, panel resets included.
		bool ToolClearSession(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			int64 clearedZones = session.mNumZones;
			session.ClearData();

			if (gApp.mCurSession == session)
			{
				if (gApp.mBoard?.mPerfView != null)
					gApp.mBoard.mPerfView.mGotFirstTick = false;
				gApp.mFindPanel.Clear();
				gApp.mProfilePanel.Clear();
				gApp.MarkDirty();
			}

			outText.Append('{');
			BPJson.AddNum(outText, "session", session.mId, false);
			BPJson.AddNum(outText, "clearedZones", clearedZones);
			BPJson.AddBool(outText, "stillConnected", !session.mSessionOver);
			BPJson.AddStr(outText, "note", "Capture restarts from zero. Times reported from here on are measured from this point.");
			outText.Append('}');
			return true;
		}

		static int32 ClampMax(int32 val, int32 defaultVal)
		{
			if (val <= 0)
				return defaultVal;
			return Math.Min(val, 1000);
		}

		// Reads startUs/endUs into absolute ticks. Either bound may be left out, in which case its tick
		// stays 0, which the queries read as "no bound on this side".
		static void GetTickRange(StructuredData sd, BpSession session, out int64 startTick, out int64 endTick)
		{
			startTick = 0;
			endTick = 0;
			if (sd.Contains("startUs"))
				startTick = UsToTick(session, sd.GetLong("startUs"));
			if (sd.Contains("endUs"))
				endTick = UsToTick(session, sd.GetLong("endUs"));
		}

		void AppendFoundEntry(BpSession session, BPFoundEntry entry, String outStr, bool withThread)
		{
			outStr.Append('{');
			BPJson.AddStr(outStr, "name", entry.mName, false);
			if (withThread)
			{
				var trackName = scope String(64);
				if (entry.mTrackIdx < session.mThreads.Count)
					session.mThreads[entry.mTrackIdx].GetName(trackName);
				BPJson.AddStr(outStr, "thread", trackName);
				BPJson.AddNum(outStr, "threadIdx", entry.mTrackIdx);
			}
			BPJson.AddNum(outStr, "startUs", TickToUs(session, entry.mStartTick));
			if (entry.mEndTick != 0)
			{
				BPJson.AddNum(outStr, "durUs", TicksToDurUs(session, entry.Length));
				BPJson.AddNum(outStr, "depth", entry.mDepth);
				// Carried so profile can address this exact zone without a lossy round trip
				BPJson.AddNum(outStr, "startTick", entry.mStartTick);
			}
			else
				BPJson.AddBool(outStr, "isEvent", true);
			outStr.Append('}');
		}

		bool ToolFind(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			var query = scope BPFindQuery();
			var error = scope String();

			var text = scope String();
			sd.GetString("text", text);
			if (query.mTextSearch.Init(text, error) case .Err)
			{
				outText.AppendF("Bad search text: {0}", error);
				return false;
			}

			var threadFilter = scope String();
			sd.GetString("thread", threadFilter);
			if (query.mTrackSearch.Init(threadFilter, error) case .Err)
			{
				outText.AppendF("Bad thread filter: {0}", error);
				return false;
			}

			GetTickRange(sd, session, out query.mStartTick, out query.mEndTick);
			query.mIncludeZones = sd.GetBool("zones", true);
			query.mIncludeEvents = sd.GetBool("events", true);
			query.mMaxResults = ClampMax(sd.GetInt("max", 50), 50);
			query.mSortReverse = !sd.GetBool("ascending", false);

			var sortStr = scope String();
			sd.GetString("sort", sortStr, "length");
			switch (sortStr)
			{
			case "name": query.mSortColumn = BPFindQuery.cSortName;
			case "start": query.mSortColumn = BPFindQuery.cSortStart;
			case "thread": query.mSortColumn = BPFindQuery.cSortTrack;
			case "length": query.mSortColumn = BPFindQuery.cSortLength;
			default:
				outText.AppendF("Unknown sort '{0}'. Use length, start, name or thread.", sortStr);
				return false;
			}

			query.Run(session);

			outText.Append('{');
			BPJson.AddNum(outText, "session", session.mId, false);
			BPJson.AddNum(outText, "matches", query.mTotalMatches);
			BPJson.AddNum(outText, "returned", query.mResults.Count);
			if (query.mTimedOut)
			{
				BPJson.AddBool(outText, "partial", true);
				BPJson.AddStr(outText, "note", "The scan hit its time limit, so this covers only part of the session. Narrow it with startUs and endUs or a thread filter.");
			}
			outText.Append(",\"entries\":[");
			for (var entry in query.mResults)
			{
				if (@entry.Index > 0)
					outText.Append(',');
				AppendFoundEntry(session, entry, outText, true);
			}
			outText.Append("]}");
			return true;
		}

		bool ToolProfile(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			int32 threadIdx = sd.GetInt("thread", -1);
			if ((threadIdx < 0) || (threadIdx >= session.mThreads.Count))
			{
				outText.AppendF("Thread index {0} is out of range; this session has {1} threads. Call session_info for the list.", threadIdx, session.mThreads.Count);
				return false;
			}

			var query = scope BPProfileQuery();
			query.mThreadIdx = threadIdx;
			query.mMaxResults = ClampMax(sd.GetInt("max", 50), 50);

			bool isEntryMode = (sd.Contains("startTick")) && (sd.Contains("depth"));
			if (isEntryMode)
			{
				query.mStartTick = sd.GetLong("startTick");
				query.mDepth = sd.GetInt("depth", 0);
				query.mEndTick = 0;
			}
			else
			{
				if ((!sd.Contains("startUs")) || (!sd.Contains("endUs")))
				{
					outText.Append("Pass either startTick and depth from a find result, or startUs and endUs for a time range.");
					return false;
				}
				GetTickRange(sd, session, out query.mStartTick, out query.mEndTick);
				query.mDepth = -1;
				if (query.mEndTick <= query.mStartTick)
				{
					outText.Append("endUs has to be greater than startUs.");
					return false;
				}
			}

			query.Run(session);

			if ((isEntryMode) && (!query.mFoundSelection))
			{
				outText.Append("No zone starts at that startTick and depth on that thread. Those values have to come from a find result on this same session; a zone that is still running at the live edge of a capture is not reported until it closes.");
				return false;
			}

			if (sd.GetBool("show", true))
				RequestShow(session, threadIdx, query.mStartTick, isEntryMode ? query.mStartTick + query.mSelectionTicks : query.mEndTick, query.mDepth);

			var trackName = scope String(64);
			session.mThreads[threadIdx].GetName(trackName);

			outText.Append('{');
			BPJson.AddNum(outText, "session", session.mId, false);
			BPJson.AddStr(outText, "thread", trackName);
			BPJson.AddNum(outText, "threadIdx", threadIdx);
			BPJson.AddStr(outText, "mode", isEntryMode ? "zone" : "range");
			BPJson.AddNum(outText, "startUs", TickToUs(session, query.mStartTick));
			BPJson.AddNum(outText, "totalUs", TicksToDurUs(session, query.mSelectionTicks));
			BPJson.AddNum(outText, "rows", query.mTotalRows);
			BPJson.AddNum(outText, "returned", query.mResults.Count);
			if (query.mTimedOut)
			{
				BPJson.AddBool(outText, "partial", true);
				BPJson.AddStr(outText, "note", "The scan hit its time limit, so these totals cover only part of the selection.");
			}

			outText.Append(",\"entries\":[");
			for (var row in query.mResults)
			{
				if (@row.Index > 0)
					outText.Append(',');
				outText.Append('{');
				BPJson.AddStr(outText, "name", row.mName, false);
				BPJson.AddNum(outText, "count", row.mCount);
				BPJson.AddNum(outText, "totalUs", TicksToDurUs(session, row.mTicks));
				BPJson.AddNum(outText, "selfUs", TicksToDurUs(session, row.SelfTicks));
				outText.Append('}');
			}
			outText.Append("]}");
			return true;
		}

		// Mirrors the click a human would make: selects the zone in the timeline and points the Profile
		// panel at it, so the window follows along with whatever is being queried.
		//
		// This cannot be applied inline. Showing a session that is not already current rebuilds the
		// timeline view from scratch (Board.ShowSession), and the new view does not build its track
		// tree until its own next Update -- so a scroll issued right now would be aiming at a tree
		// that does not exist. The request is parked and retried from Update until the view can take
		// it, or until it has clearly missed its chance.
		void RequestShow(BpSession session, int32 threadIdx, int64 startTick, int64 endTick, int32 depth)
		{
			if (gApp.mCurSession != session)
				gApp.SetSession(session);

			// Held by id, not by reference: the session can be cleared away before this is applied
			mShowSessionId = session.mId;
			mShowSelection.mThreadIdx = threadIdx;
			mShowSelection.mTickStart = startTick;
			mShowSelection.mTickEnd = endTick;
			mShowSelection.mDepth = depth;
			mShowRetriesLeft = 60; // About a second of frames, then give up quietly
		}

		void UpdateShowRequest()
		{
			if (mShowRetriesLeft <= 0)
				return;
			mShowRetriesLeft--;

			// Somebody changed the view out from under us, so the request is stale
			if ((gApp.mCurSession == null) || (gApp.mCurSession.mId != mShowSessionId))
			{
				mShowRetriesLeft = 0;
				return;
			}

			var perfView = gApp.mBoard?.mPerfView;
			if ((perfView == null) || (perfView.mWidth <= 0))
				return;

			// False means the track tree has not been built yet -- wait for the next frame
			if ((mShowSelection.mDepth >= 0) && (!perfView.EnsureVisible(mShowSelection.mThreadIdx, mShowSelection.mDepth)))
				return;

			perfView.mSelection = .Entry(mShowSelection);
			if (mShowSelection.mTickEnd > mShowSelection.mTickStart)
				perfView.ZoomTo(mShowSelection.mTickStart, mShowSelection.mTickEnd);
			gApp.mProfilePanel.Show(perfView, mShowSelection);
			gApp.MarkDirty();
			mShowRetriesLeft = 0;
		}

		bool ToolTimeSlice(StructuredData sd, String outText)
		{
			var session = FindSession(sd);
			if (session == null)
			{
				outText.Append("No such session. Call list_sessions to see what is available.");
				return false;
			}

			var query = scope BPSliceQuery();
			GetTickRange(sd, session, out query.mStartTick, out query.mEndTick);
			if (query.mEndTick <= query.mStartTick)
			{
				outText.Append("endUs has to be greater than startUs.");
				return false;
			}

			query.mMaxDepth = Math.Max(sd.GetInt("maxDepth", 3), 1);
			query.mMaxZonesPerTrack = ClampMax(sd.GetInt("maxZones", 60), 60);
			query.mIncludeEvents = sd.GetBool("events", true);

			query.Run(session);

			int64 sliceUs = TicksToDurUs(session, query.mEndTick - query.mStartTick);

			outText.Append('{');
			BPJson.AddNum(outText, "session", session.mId, false);
			BPJson.AddNum(outText, "startUs", TickToUs(session, query.mStartTick));
			BPJson.AddNum(outText, "endUs", TickToUs(session, query.mEndTick));
			BPJson.AddNum(outText, "durUs", sliceUs);
			if (query.mTimedOut)
			{
				BPJson.AddBool(outText, "partial", true);
				BPJson.AddStr(outText, "note", "The scan hit its time limit, so some threads may be missing or incomplete. Try a shorter slice.");
			}

			outText.Append(",\"threads\":[");
			for (var sliceTrack in query.mTracks)
			{
				if (@sliceTrack.Index > 0)
					outText.Append(',');
				outText.Append('{');
				BPJson.AddStr(outText, "name", sliceTrack.mName, false);
				BPJson.AddNum(outText, "threadIdx", sliceTrack.mTrackIdx);
				BPJson.AddNum(outText, "nativeThreadId", sliceTrack.mNativeThreadId);
				BPJson.AddNum(outText, "zoneCount", sliceTrack.mTotalZones);
				BPJson.AddNum(outText, "eventCount", sliceTrack.mTotalEvents);
				// How much of the slice this thread had a top level zone running -- the rest is time
				// the thread was idle or doing uninstrumented work
				BPJson.AddNum(outText, "busyUs", TicksToDurUs(session, sliceTrack.mCoveredTicks));
				if (sliceTrack.mTrimmed)
					BPJson.AddBool(outText, "trimmed", true);

				outText.Append(",\"openAtStart\":[");
				for (var entry in sliceTrack.mOpenStack)
				{
					if (@entry.Index > 0)
						outText.Append(',');
					AppendFoundEntry(session, entry, outText, false);
				}

				outText.Append("],\"zones\":[");
				for (var entry in sliceTrack.mZones)
				{
					if (@entry.Index > 0)
						outText.Append(',');
					AppendFoundEntry(session, entry, outText, false);
				}

				outText.Append("],\"events\":[");
				for (var entry in sliceTrack.mEvents)
				{
					if (@entry.Index > 0)
						outText.Append(',');
					AppendFoundEntry(session, entry, outText, false);
				}
				outText.Append("]}");
			}
			outText.Append("]}");
			return true;
		}
	}
}
