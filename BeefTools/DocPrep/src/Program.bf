using System;
using System.IO;

namespace DocPrep
{
	class Program
	{
		[StdCall, CLink]
		static extern void* BfSystem_Create();

		[StdCall, CLink]
		static extern void BfSystem_Delete(void* bfSystem);

		[StdCall, CLink]
		static extern void* BfSystem_CreateParser(void* bfSystem, void* bfProject);

		[StdCall, CLink]
		static extern void* BfSystem_CreatePassInstance(void* bfSystem);

		[StdCall, CLink]
		static extern void BfPassInstance_Delete(void* bfSystem);

		[StdCall, CLink]
		static extern void BfSystem_DeleteParser(void* bfSystem, void* bfParser);

		[StdCall, CLink]
		static extern void BfParser_SetSource(void* bfParser, char8* data, int32 length, char8* fileName);

		[StdCall, CLink]
		static extern bool BfParser_Parse(void* bfParser, void* bfPassInstance, bool compatMode);

		[StdCall, CLink]
		static extern bool BfParser_Reduce(void* bfParser, void* bfPassInstance);

		[StdCall, CLink]        
		static extern char8* BfParser_Format(void* bfParser, int32 formatEnd, int32 formatStart, out int32* outCharMapping);

		[StdCall, CLink]        
		static extern char8* BfParser_DocPrep(void* bfParser);

		/*[StdCall, CLink]        
		static extern char8* BfParser_Prep*/

		[StdCall, CLink]
		static extern void* BfSystem_CreateProject(void* bfSystem, char8* projectName);

		String mSrcDirPath;
		String mDestDirPath;

		void* mSystem;
		void* mProject;

		this()
		{
			mSystem = BfSystem_Create();
			mProject = BfSystem_CreateProject(mSystem, "main");
		}

		void GenFile(String relFilePath)
		{
			String inFilePath = scope .();
			inFilePath.Append(mSrcDirPath, "/", relFilePath);

			String outFilePath = scope .();
			outFilePath.Append(mDestDirPath, "/", relFilePath);

			String text = scope .();
			File.ReadAllText(inFilePath, text, false);

			void* parser = BfSystem_CreateParser(mSystem, mProject);
			BfParser_SetSource(parser, text.Ptr, (.)text.Length, inFilePath);

			void* passInstance = BfSystem_CreatePassInstance(mSystem);
			BfParser_Parse(parser, passInstance, false);
			BfParser_Reduce(parser, passInstance);

			char8* docText = BfParser_DocPrep(parser);

			String outDirPath = scope .();
			Path.GetDirectoryPath(outFilePath, outDirPath);
			Directory.CreateDirectory(outDirPath);

			File.WriteAllText(outFilePath, .(docText));

			BfSystem_DeleteParser(mSystem, parser);
		}

		void AddFiles(String relDirPath)
		{
			String dirPath = scope .(mSrcDirPath);
			dirPath.Append("/");
			dirPath.Append(relDirPath);

			for (let val in Directory.EnumerateDirectories(dirPath))
			{
				String subDirPath = scope .();
				subDirPath.Append(relDirPath, "/");
				val.GetFileName(subDirPath);
				AddFiles(subDirPath);
			}

			for (let val in Directory.EnumerateFiles(dirPath))
			{
				String relFilePath = scope .();
				relFilePath.Append(relDirPath, "/");
				val.GetFileName(relFilePath);
				GenFile(relFilePath);
			}
		}

		bool TestDestPath(String relDirPath)
		{
			String dirPath = scope .(mDestDirPath);
			dirPath.Append("/");
			dirPath.Append(relDirPath);

			for (let val in Directory.EnumerateDirectories(dirPath))
			{
				String subDirPath = scope .();
				subDirPath.Append(relDirPath, "/");
				val.GetFileName(subDirPath);
				if (!TestDestPath(subDirPath))
					return false;
			}

			for (let val in Directory.EnumerateFiles(dirPath))
			{
				String filePath = scope .();
				val.GetFilePath(filePath);

				if (!filePath.EndsWith(".bf"))
				{
					Console.Error.WriteLine("Dest past not empty. Invalid file found: {}", filePath);
					return false;
				}
			}

			return true;
		}

		public static int32 Main(String[] args)
		{
			Program pg = scope .();
			pg.mSrcDirPath = args[0];
			pg.mDestDirPath = args[1];
			if (!pg.TestDestPath(""))
				return 1;
			if (Directory.DelTree(pg.mDestDirPath) case .Err)
			{
				Console.Error.Write("Failed to clear dest path");
				return 2;
			}

			pg.AddFiles("");

			return 0;
		}
	}
}
