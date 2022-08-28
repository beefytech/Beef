using System.Reflection;
using System.Diagnostics;
using System.Collections;
using System.Security.Cryptography;
using System.IO;

namespace System
{
	static class Compiler
	{
		public abstract class Generator
		{
			public enum Flags
			{
				None = 0,
				AllowRegenerate = 1
			}

			public String mCmdInfo = new String() ~ delete _;
			public Dictionary<StringView, StringView> mParams = new .() ~ delete _;
			public abstract String Name { get; }

			public StringView ProjectName => mParams["ProjectName"];
			public StringView ProjectDir => mParams["ProjectDir"];
			public StringView FolderDir => mParams["FolderDir"];
			public StringView Namespace => mParams["Namespace"];
			public StringView DefaultNamespace => mParams["DefaultNamespace"];
			public StringView WorkspaceName => mParams["WorkspaceName"];
			public StringView WorkspaceDir => mParams["WorkspaceDir"];
			public StringView DateTime => mParams["DateTime"];
			public bool IsRegenerating => mParams.GetValueOrDefault("Regenerating") == "True";

			public void Fail(StringView error)
			{
				mCmdInfo.AppendF("error\t");
				error.QuoteString(mCmdInfo);
				mCmdInfo.Append("\n");
			}

			public void AddEdit(StringView dataName, StringView label, StringView defaultValue)
			{
				mCmdInfo.AppendF($"addEdit\t");
				dataName.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				label.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				defaultValue.QuoteString(mCmdInfo);
				mCmdInfo.Append("\n");
			}

			public void AddFilePath(StringView dataName, StringView label, StringView defaultValue)
			{
				mCmdInfo.AppendF($"addFilePath\t");
				dataName.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				label.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				defaultValue.QuoteString(mCmdInfo);
				mCmdInfo.Append("\n");
			}

			public void AddFolderPath(StringView dataName, StringView label, StringView defaultValue)
			{
				mCmdInfo.AppendF($"addFolderPath\t");
				dataName.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				label.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				defaultValue.QuoteString(mCmdInfo);
				mCmdInfo.Append("\n");
			}

			public void AddCombo(StringView dataName, StringView label, StringView defaultValue, Span<StringView> values)
			{
				mCmdInfo.AppendF($"addCombo\t");
				dataName.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				label.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				defaultValue.QuoteString(mCmdInfo);
				for (var value in values)
				{
					mCmdInfo.Append("\t");
					value.QuoteString(mCmdInfo);
				}
				mCmdInfo.Append("\n");
			}

			public void AddCheckbox(StringView dataName, StringView label, bool defaultValue)
			{
				mCmdInfo.AppendF($"addCheckbox\t");
				dataName.QuoteString(mCmdInfo);
				mCmdInfo.Append("\t");
				label.QuoteString(mCmdInfo);
				mCmdInfo.AppendF($"\t{defaultValue}\n");
			}

			public bool GetString(StringView key, String outVal)
			{
				if (mParams.TryGetAlt(key, var matchKey, var value))
				{
					outVal.Append(value);
					return true;
				}
				return false;
			}

			public virtual void InitUI()
			{
			}

			public virtual void Generate(String outFileName, String outText, ref Flags generateFlags)
			{
			}

			static String GetName<T>() where T : Generator
			{
				T val = scope T();
				String str = val.Name;
				return str;
			}

			void HandleArgs(String args)
			{
				for (var line in args.Split('\n', .RemoveEmptyEntries))
				{
					int tabPos = line.IndexOf('\t');
					var key = line.Substring(0, tabPos);
					var value = line.Substring(tabPos + 1);

					if (key == "FolderDir")
						Directory.SetCurrentDirectory(value).IgnoreError();

					if (mParams.TryAdd(key, var keyPtr, var valuePtr))
					{
						*keyPtr = key;
						*valuePtr = value;
					}
				}
			}

			static String InitUI<T>(String args) where T : Generator
			{
				T val = scope T();
				val.HandleArgs(args);
				val.InitUI();
				return val.mCmdInfo;
			}

			static String Generate<T>(String args) where T : Generator
			{
				T val = scope T();
				val.HandleArgs(args);
				String fileName = scope .();
				String outText = scope .();
				Flags flags = .None;
				val.Generate(fileName, outText, ref flags);
				val.mCmdInfo.Append("fileName\t");
				fileName.Quote(val.mCmdInfo);
				val.mCmdInfo.Append("\n");
				val.mCmdInfo.Append("data\n");

				if (flags.HasFlag(.AllowRegenerate))
				{
					bool writeArg = false;
					for (var line in args.Split('\n', .RemoveEmptyEntries))
					{
						int tabPos = line.IndexOf('\t');
						var key = line.Substring(0, tabPos);
						var value = line.Substring(tabPos + 1);

						if (key == "Generator")
							writeArg = true;
						if (writeArg)
						{
							val.mCmdInfo.AppendF($"// {key}={value}\n");
						}
					}
					var hash = MD5.Hash(.((.)outText.Ptr, outText.Length));
					val.mCmdInfo.AppendF($"// GenHash={hash}\n\n");
				}
				val.mCmdInfo.Append(outText);
				return val.mCmdInfo;
			}
		}

		public class NewClassGenerator : Generator
		{
			public override String Name => "New Class";
			
			public override void InitUI()
			{
				AddEdit("name", "Class Name", "");
			}

			public override void Generate(String outFileName, String outText, ref Flags generateFlags)
			{
				var name = mParams["name"];
				if (name.EndsWith(".bf", .OrdinalIgnoreCase))
					name.RemoveFromEnd(3);
				outFileName.Append(name);
				outText.AppendF(
					$"""
					namespace {Namespace};
					
					class {name}
					{{
					}}
					""");
			}
		}

		public struct MethodBuilder
		{
			void* mNative;

			public void Emit(String str)
			{
				Comptime_MethodBuilder_EmitStr(mNative, str);
			}

			public void Emit(Type type)
			{

			}
		}

		public static class Options
		{
			[LinkName("#AllocStackCount")]
			public static extern int32 AllocStackCount;
		}

		[LinkName("#CallerLineNum")]
		public static extern int CallerLineNum;

		[LinkName("#CallerFilePath")]
		public static extern String CallerFilePath;

		[LinkName("#CallerFileName")]
		public static extern String CallerFileName;

		[LinkName("#CallerFileDir")]
		public static extern String CallerFileDir;

		[LinkName("#CallerType")]
		public static extern Type CallerType;

		[LinkName("#CallerTypeName")]
		public static extern String CallerTypeName;

		[LinkName("#CallerMemberName")]
		public static extern String CallerMemberName;

		[LinkName("#CallerProject")]
		public static extern String CallerProject;

		[LinkName("#CallerExpression")]
		public static extern String[0x00FFFFFF] CallerExpression;

		[LinkName("#OrigCalleeType")]
		public static extern Type OrigCalleeType;

		[LinkName("#ProjectName")]
		public static extern String ProjectName;

		[LinkName("#ModuleName")]
		public static extern String ModuleName;

		[LinkName("#TimeLocal")]
		public static extern String TimeLocal;

		[LinkName("#IsComptime")]
		public static extern bool IsComptime;

		[LinkName("#IsBuilding")]
		public static extern bool IsBuilding;

		[LinkName("#IsReified")]
		public static extern bool IsReified;

		[LinkName("#CompileRev")]
		public static extern int32 CompileRev;

		[LinkName("#NextId")]
		public static extern int64 NextId;

		[Comptime(ConstEval=true)]
		public static void Assert(bool cond)
		{
			if (!cond)
				Runtime.FatalError("Assert failed");
		}

		[Comptime(ConstEval = true)]
		public static void Assert(bool cond, String message)
		{
		    if (!cond)
		        Runtime.FatalError(message);
		}

		static extern void Comptime_SetReturnType(int32 typeId);
		static extern void Comptime_Align(int32 typeId, int32 align);
		static extern void* Comptime_MethodBuilder_EmitStr(void* native, StringView str);
		static extern void* Comptime_CreateMethod(int32 typeId, StringView methodName, Type returnType, MethodFlags methodFlags);
		static extern void Comptime_EmitTypeBody(int32 typeId, StringView text);
		static extern void Comptime_EmitAddInterface(int32 typeId, int32 ifaceTypeId);
		static extern void Comptime_EmitMethodEntry(int64 methodHandle, StringView text);
		static extern void Comptime_EmitMethodExit(int64 methodHandle, StringView text);
		static extern void Comptime_EmitMixin(StringView text);

		[Comptime(OnlyFromComptime=true)]
		public static MethodBuilder CreateMethod(Type owner, StringView methodName, Type returnType, MethodFlags methodFlags)
		{
			MethodBuilder builder = .();
			builder.[Friend]mNative = Comptime_CreateMethod((.)owner.TypeId, methodName, returnType, methodFlags);
			return builder;
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitTypeBody(Type owner, StringView text)
		{
			Comptime_EmitTypeBody((.)owner.TypeId, text);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void SetReturnType(Type type)
		{
			Comptime_SetReturnType((.)type.TypeId);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void Align(Type type, int align)
		{
			Comptime_Align((.)type.TypeId, (.)align);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitAddInterface(Type owner, Type iface)
		{
			Comptime_EmitAddInterface((.)owner.TypeId, (.)iface.TypeId);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitMethodEntry(MethodInfo methodHandle, StringView text)
		{
			Comptime_EmitMethodEntry(methodHandle.[Friend]mData.mComptimeMethodInstance, text);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitMethodExit(MethodInfo methodHandle, StringView text)
		{
			Comptime_EmitMethodExit(methodHandle.[Friend]mData.mComptimeMethodInstance, text);
		}

		[Comptime(ConstEval=true)]
		public static void Mixin(StringView text)
		{
			if (Compiler.IsComptime)
				Comptime_EmitMixin(text);
		}

		[Comptime]
		public static void MixinRoot(StringView text)
		{
			if (Compiler.IsComptime)
				Comptime_EmitMixin(text);
		}

		[Comptime]
		public static Span<uint8> ReadBinary(StringView path)
		{
			List<uint8> data = scope .();
			File.ReadAll(path, data);
			return data;
		}

		[Comptime]
		public static String ReadText(StringView path)
		{
			String data = scope .();
			File.ReadAllText(path, data);
			return data;
		}
	}
}
