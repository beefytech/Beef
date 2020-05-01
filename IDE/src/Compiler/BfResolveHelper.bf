using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using IDE.ui;
using System.IO;

namespace IDE.Compiler
{
    public class BfResolveHelper
    {
        class DeferredReparseEntry
        {
            public int32 mWaitTicks;
            public String mFilePath ~ delete _;
            public SourceViewPanel mExludeSourceViewPanel;            
        }

        List<DeferredReparseEntry> mDeferredReparses = new List<DeferredReparseEntry>() ~ DeleteContainerAndItems!(_);
        int32 mVisibleViewRefreshWaitTicks;
        SourceViewPanel mVisibleViewRefreshExcludeSourceView;

        public void SourceViewPanelClosed(SourceViewPanel sourceViewPanel)
        {
            if (mVisibleViewRefreshExcludeSourceView == sourceViewPanel)
                mVisibleViewRefreshExcludeSourceView = null;

            for (var entry in mDeferredReparses)
            {
                if (entry.mExludeSourceViewPanel == sourceViewPanel)
                    entry.mExludeSourceViewPanel = null;
            }
        }

        public void ProjectSourceRemoved(ProjectSource projectSource)
        {
            // Don't need to do anything
        }

        public void DeferRefreshVisibleViews(SourceViewPanel sourceViewPanel)
        {
            if (mVisibleViewRefreshWaitTicks == 0)
            {
                mVisibleViewRefreshWaitTicks = 30;
                mVisibleViewRefreshExcludeSourceView = sourceViewPanel;
            }
            else
            {
                mVisibleViewRefreshWaitTicks = 30;
                if (mVisibleViewRefreshExcludeSourceView != sourceViewPanel)
                {
                    // We already had another soruceView that queued up a refresh
                    //  so remove the exclusion
                    mVisibleViewRefreshExcludeSourceView = null;
                }
            }
        }

        public void DeferReparse(String fileName, SourceViewPanel excludeSourceVewPanel)
        {
            DeferRefreshVisibleViews(excludeSourceVewPanel);

            for (var entry in mDeferredReparses)
            {
                if (Path.Equals(entry.mFilePath, fileName))
                {
                    if (entry.mExludeSourceViewPanel != excludeSourceVewPanel)
                        entry.mExludeSourceViewPanel = null; // Reparse them all
                    entry.mWaitTicks = 30;
                    return;
                }
            }

            var newEntry = new DeferredReparseEntry();
            newEntry.mFilePath = new String(fileName);
            newEntry.mExludeSourceViewPanel = excludeSourceVewPanel;
            newEntry.mWaitTicks = 30;
            mDeferredReparses.Add(newEntry);
        }

        public void Update()
        {
            var app = IDEApp.sApp;

            if ((mVisibleViewRefreshWaitTicks > 0) && (--mVisibleViewRefreshWaitTicks == 0))
            {
                if (!app.mBfResolveCompiler.IsPerformingBackgroundOperation())
                {
                    app.RefreshVisibleViews(mVisibleViewRefreshExcludeSourceView);
                }
                else
                {
                    // Try again next tick
                    mVisibleViewRefreshWaitTicks = 1;
                }
            }

            if (mDeferredReparses.Count > 0)
            {
                var entry = mDeferredReparses[0];
                if ((--entry.mWaitTicks <= 0) && (!app.mBfResolveCompiler.IsPerformingBackgroundOperation()))
                {
                    bool needsResolveAll = false;
                    app.mWorkspace.WithProjectItems(scope [&] (projectItem) =>
                        {
                            var projectSource = projectItem as ProjectSource;
                            if ((projectSource != null) &&
                                ((entry.mExludeSourceViewPanel == null) || (projectSource != entry.mExludeSourceViewPanel.mProjectSource)))
                            {
								var fullPath = scope String();
								projectSource.GetFullImportPath(fullPath);
                                if (Path.Equals(fullPath, entry.mFilePath))
                                {
                                    app.mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
                                    needsResolveAll = true;
                                    DeferRefreshVisibleViews(entry.mExludeSourceViewPanel);
                                }
                            }
                        });
                    if (needsResolveAll)
                        app.mBfResolveCompiler.QueueDeferredResolveAll();
                    mDeferredReparses.RemoveAt(0);
					delete entry;
                }
            }
        }
    }
}
