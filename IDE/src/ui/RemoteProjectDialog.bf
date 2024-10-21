#pragma warning disable 168
using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using Beefy;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.theme;
using IDE.Util;

namespace IDE.ui
{
    public class RemoteProjectDialog : IDEDialog
    {
        public EditWidget mURLEdit;
        public EditWidget mVersionEdit;
		public DarkComboBox mTargetComboBox;
		static String[1] sApplicationTypeNames =
			.("Git");
		public bool mNameChanged;
		public String mDirBase ~ delete _;

        public this()
        {
			mTitle = new String("Add Remote Project");
        }

        public override void CalcSize()
        {
            mWidth = GS!(320);
            mHeight = GS!(200);
        }

		enum CreateFlags
		{
			None,
			NonEmptyDirOkay = 1,
		}

		bool CreateProject(CreateFlags createFlags = .None)
		{
		    var app = IDEApp.sApp;
		    String url = scope String();
		    mURLEdit.GetText(url);
		    url.Trim();

			if (url.IsEmpty)
			{
				mURLEdit.SetFocus();
			    app.Fail("Invalid URL");
			    return false;
			}

			var projName = Path.GetFileName(url, .. scope .());

			var version = mVersionEdit.GetText(.. scope .())..Trim();
			
		    var otherProject = app.mWorkspace.FindProject(projName);
		    if (otherProject != null)
		    {
		        mURLEdit.SetFocus();
		        app.Fail("A project with this name already exists in the workspace.");
		        return false;                
		    }

			VerSpec verSpec = .Git(url, scope .(version));
			if (var project = gApp.AddProject(projName, verSpec))
			{
				//gApp.ProjectCreated(project);
				app.mWorkspace.SetChanged();

				gApp.[Friend]FlushDeferredLoadProjects(true);
				//gApp.RetryProjectLoad(project, false);
				//gApp.AddProjectToWorkspace(project);

				var projectSpec = new Workspace.ProjectSpec();
				projectSpec.mProjectName = new .(project.mProjectName);
				projectSpec.mVerSpec = .Git(new .(url), new .(version));
				gApp.mWorkspace.mProjectSpecs.Add(projectSpec);
			}

		    return true;
		}

		public void UpdateProjectName()
		{
			if (!mNameChanged)
			{
				String path = scope .();
				mURLEdit.GetText(path);
				path.Trim();
				if ((path.EndsWith('\\')) || (path.EndsWith('/')))
					path.RemoveFromEnd(1);

				String projName = scope .();
				Path.GetFileName(path, projName);
				mVersionEdit.SetText(projName);
			}
		}

        public void Init()
        {
            mDefaultButton = AddButton("Create", new (evt) =>
				{
					if (!CreateProject()) evt.mCloseDialog = false;
				});
            mEscButton = AddButton("Cancel", new (evt) => Close());
            
			if (gApp.mWorkspace.IsInitialized)
				mDirBase = new String(gApp.mWorkspace.mDir);
			else
				mDirBase = new String();
            mURLEdit = new DarkEditWidget();
			
			AddEdit(mURLEdit);
			mURLEdit.mOnContentChanged.Add(new (dlg) =>
				{
					
				});

			mVersionEdit = AddEdit("");
			mVersionEdit.mOnContentChanged.Add(new (dlg) =>
				{
					if (mVersionEdit.mHasFocus)
						mNameChanged = true;
				});

			mTargetComboBox = new DarkComboBox();
			mTargetComboBox.Label = sApplicationTypeNames[0];
			mTargetComboBox.mPopulateMenuAction.Add(new (dlg) =>
				{
					for (var applicationTypeName in sApplicationTypeNames)
					{
						var item = dlg.AddItem(applicationTypeName);
						item.mOnMenuItemSelected.Add(new (item) =>
							{
								mTargetComboBox.Label = item.mLabel;
								MarkDirty();
							});
					}
				});
			AddWidget(mTargetComboBox);
			mTabWidgets.Add(mTargetComboBox);
        }

        public override void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
        {
            base.PopupWindow(parentWindow, offsetX, offsetY);
            mURLEdit.SetFocus();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float curY = mHeight - GS!(30) - mButtonBottomMargin;
			mVersionEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(24));

			curY -= GS!(50);
			mURLEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(24));

			curY -= GS!(60);
			mTargetComboBox.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			g.DrawString("Remote Project URL", mURLEdit.mX, mURLEdit.mY - GS!(20));
            g.DrawString("Version Constraint (Blank for HEAD)", mVersionEdit.mX, mVersionEdit.mY - GS!(20));
        }
    }

    
}
