using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Diagnostics;
using Beefy.utils;
using Beefy;
using System.IO;

namespace IDE.Compiler
{
    public abstract class CompilerBase : CommandQueueManager
    {
        public int32 mResolveAllWait;        
        protected List<String> mQueuedOutput = new List<String>() ~ DeleteContainerAndItems!(_);
        public bool mWantsActiveViewRefresh;
        
        public volatile int32 mThreadYieldCount = 0; // Whether our thread wants to be yielded to, and for how many ticks        

        protected class ResolveAllCommand : Command
        {
        }

        protected class ProjectSourceRemovedCommand : Command
        {
            public ProjectSource mProjectSource;
			public ~this()
			{
				mProjectSource.ReleaseRef();
			}
        }

        protected class ProjectSourceCommand : Command
        {
            public ProjectSource mProjectSource;
            public IdSpan mSourceCharIdData ~ _.Dispose();
            public String mSourceString ~ delete _;
        }

        protected class CompileCommand : Command
        {
            public String mOutputDirectory ~ delete _;
        }        

        public void QueueCompile(String outputDirectory)
        {
            CompileCommand command = new CompileCommand();
            command.mOutputDirectory = new String(outputDirectory);
            QueueCommand(command);
        }

        public void QueueDeferredResolveAll()
        {
            mResolveAllWait = 2;
        }

        public virtual void QueueProjectSource(ProjectSource projectSource)
        {
            ProjectSourceCommand command = new ProjectSourceCommand();
            command.mProjectSource = projectSource;
            command.mSourceString = new String();
            IDEApp.sApp.FindProjectSourceContent(projectSource, out command.mSourceCharIdData, false, command.mSourceString);
			if (gApp.mBfBuildCompiler == this)
			{
				if (gApp.mDbgVersionedCompileDir != null)
				{
					String fileName = scope .();
					projectSource.GetFullImportPath(fileName);
					
					String baseSrcDir = scope .();
 					baseSrcDir.Append(projectSource.mProject.mProjectDir);
					baseSrcDir.Append(@"\src\");

					if (fileName.StartsWith(baseSrcDir, .OrdinalIgnoreCase))
					{
						fileName.Remove(0, baseSrcDir.Length - 1);
						fileName.Insert(0, projectSource.mProject.mProjectName);
					}
					//Console.WriteLine("Hey!");
					fileName.Replace('\\', '_');
					fileName.Replace('/', '_');

					String path = scope .();
					path.Append(gApp.mDbgVersionedCompileDir);
					path.Append(fileName);
					//Utils.WriteTextFile(path, command.mSourceString);

					var filePath = scope String();
					projectSource.GetFullImportPath(filePath);

					String contents = scope .();
					contents.AppendF("//@{}|{}\n", filePath, File.GetLastWriteTime(filePath).GetValueOrDefault().ToFileTime());
					contents.Append(command.mSourceString);
					File.WriteAllText(path, contents);
				}
			}
            QueueCommand(command);
        }

        public virtual void ClearQueuedProjectSource()
        {
            using (mMonitor.Enter())
            {
                // Don't remove the head of the queue because we may already be processing it
                for (int32 i = 1; i < mCommandQueue.Count; i++)
                {
                    if (mCommandQueue[i] is ProjectSourceCommand)
                    {
                        mCommandQueue.RemoveAt(i);
                        i--;
                    }
                }
            }
        }

        public void QueueProjectSourceRemoved(ProjectSource projectSource)
        {
            ProjectSourceRemovedCommand command = new ProjectSourceRemovedCommand();
            command.mProjectSource = projectSource;
			projectSource.AddRef();
            QueueCommand(command);
        }
        
        public bool HasResolvedAll()
        {
            return (!HasQueuedCommands()) && (mResolveAllWait == 0);
        }        

        public void QueueResolveCommand()
        {
            ResolveAllCommand command = new ResolveAllCommand();
            QueueCommand(command);
        }

        public override void ProcessQueue()
        {
            mThreadYieldCount = 0;
            DoProcessQueue();
        }

        public override void Update()
        {            
            if (!ThreadRunning)
            {                
				CheckThreadDone();

                if (mWantsActiveViewRefresh)
                {
                    IDEApp.sApp.RefreshVisibleViews();
                    mWantsActiveViewRefresh = false;
                }

                if (mThreadYieldCount > 0)
                {
                    mThreadYieldCount--;
                }
                else if (mAllowThreadStart)
                {
                    if (mResolveAllWait > 0)
                    {
                        if (--mResolveAllWait == 0)
                        {
                            QueueResolveCommand();
                        }
                    }

                    if (mCommandQueue.Count > 0)
                    {                        
                        StartQueueProcessThread();                        
                    }
                }
            }
        }

        public bool PopMessage(String outMessage)
        {
            using (mMonitor.Enter())
            {
                if (mQueuedOutput.Count > 0)
                {
                    String msg = mQueuedOutput[0];
                    mQueuedOutput.RemoveAt(0);
					msg.MoveTo(outMessage);
					delete msg;
                    return true;
                }
            }
            return false;
        }

        public void ClearMessages()
        {
            using (mMonitor.Enter())
            {
				ClearAndDeleteItems(mQueuedOutput);
            }
        }        
    }
}
