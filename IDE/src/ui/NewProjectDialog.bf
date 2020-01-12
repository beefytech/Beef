using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using Beefy;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.theme;

namespace IDE.ui
{
    public class NewProjectDialog : IDEDialog
    {
        public PathEditWidget mDirectoryEdit;
        public EditWidget mNameEdit;
		public DarkComboBox mTargetComboBox;
		static String[5] sApplicationTypeNames =
			.("Console Application",
				"Windows Application",
				"Library",
				"Dynamic Library",
				"Custom Build");
		public bool mNameChanged;
		public String mDirBase ~ delete _;

        public this()
        {
			mTitle = new String("Create New Project");
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
            String projName = scope String();
            mNameEdit.GetText(projName);
            projName.Trim();
            String projDirectory = scope String();
            mDirectoryEdit.GetText(projDirectory);
            projDirectory.Trim();

			if (projName.IsEmpty)
			{
				while ((projDirectory.EndsWith('/')) || (projDirectory.EndsWith('\\')))
					projDirectory.RemoveFromEnd(1);
				Path.GetFileName(projDirectory, projName);
			}

            bool isNameValid = projName.Length > 0;
			for (int32 i = 0; i < projName.Length; i++)
            {
				char8 c = projName[i];
                if ((!c.IsLetterOrDigit) && (c != '-') && (c != ' ') && (c != '_'))
                    isNameValid = false;
            }
            if (!isNameValid)
            {
                mNameEdit.SetFocus();
                app.Fail("Invalid project name. The project name can only consist of alphanumeric characters, spaces, dashes, and underscores.");
                return false;
            }

            var otherProject = app.mWorkspace.FindProject(projName);
            if (otherProject != null)
            {
                mNameEdit.SetFocus();
                app.Fail("A project with this name already exists in the workspace.");
                return false;                
            }

            if (!Directory.Exists(projDirectory))
			{
                if (Directory.CreateDirectory(projDirectory) case .Err)
				{
					mDirectoryEdit.SetFocus();
					app.Fail("Invalid project directory");
					return false;
				}
			}
			else
			{
				if ((!createFlags.HasFlag(.NonEmptyDirOkay)) && (!IDEUtils.IsDirectoryEmpty(projDirectory)))
				{
					var dialog = ThemeFactory.mDefault.CreateDialog("Create Project?",
						scope String()..AppendF("The selected directory '{}' is not empty. Are you sure you want to create a project there?", projDirectory), DarkTheme.sDarkTheme.mIconWarning);
					dialog.AddButton("Yes", new (evt) =>
						{
							CreateProject(createFlags | .NonEmptyDirOkay);
						});
					dialog.AddButton("No", new (evt) =>
						{
							mDirectoryEdit.SetFocus();
						});
					dialog.PopupWindow(mWidgetWindow);
					return false;
				}
			}

			String projectFilePath = scope String()..Append(projDirectory, "/BeefProj.toml");
			if (File.Exists(projectFilePath))
			{
				gApp.Fail(scope String()..AppendF("A Beef projects already exists at '{0}'", projDirectory));
				return false;
			}	

			Project.TargetType targetType = .BeefWindowsApplication;
			for (var applicationTypeName in sApplicationTypeNames)
			{
				if (applicationTypeName == mTargetComboBox.Label)
				{
					targetType = (Project.TargetType)@applicationTypeName;
				}
			}

            IDEUtils.FixFilePath(projDirectory);

			Project project = null;

			// If we don't yet have a workspace then create one now...
			if (!app.mWorkspace.IsInitialized)
			{
				DeleteAndNullify!(app.mWorkspace.mDir);
				app.mWorkspace.mDir = new String(projDirectory);
				app.mWorkspace.mName = new String(projName);
				
				app.[Friend]LoadWorkspace(.OpenOrNew);
				app.[Friend]FinishShowingNewWorkspace(false);

				project = app.mWorkspace.FindProject(projName);
				if (project != null)
				{
					project.mGeneralOptions.mTargetType = targetType;
					project.FinishCreate();
				}
			}

			if (project == null)
            	project = app.CreateProject(projName, projDirectory, targetType);

			app.mWorkspace.SetChanged();

            return true;
        }

		public void UpdateProjectName()
		{
			if (!mNameChanged)
			{
				String path = scope .();
				mDirectoryEdit.GetText(path);

				String projName = scope .();
				Path.GetFileName(path, projName);
				mNameEdit.SetText(projName);
			}
		}

        public void Init()
        {
            mDefaultButton = AddButton("Create", new (evt) => { if (!CreateProject()) evt.mCloseDialog = false; });
            mEscButton = AddButton("Cancel", new (evt) => Close());
            
			if (gApp.mWorkspace.IsInitialized)
				mDirBase = new String(gApp.mWorkspace.mDir);
			else
				mDirBase = new String();
            mDirectoryEdit = new PathEditWidget(.Folder);
			AddEdit(mDirectoryEdit);
			mDirectoryEdit.mOnContentChanged.Add(new (dlg) =>
				{
					UpdateProjectName();
				});

			mNameEdit = AddEdit("");
			mNameEdit.mOnContentChanged.Add(new (dlg) =>
				{
					if (mNameEdit.mHasFocus)
						mNameChanged = true;
				});
			UpdateProjectName();

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
            mDirectoryEdit.SetFocus();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float curY = mHeight - GS!(20) - mButtonBottomMargin;
			mTargetComboBox.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(28));

			curY -= GS!(40);            
			mNameEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(24));

			curY -= GS!(50);
            mDirectoryEdit.Resize(GS!(16), curY - GS!(36), mWidth - GS!(16) * 2, GS!(24));
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			g.DrawString("Project Directory", mDirectoryEdit.mX, mDirectoryEdit.mY - GS!(20));
            g.DrawString("Project Name", mNameEdit.mX, mNameEdit.mY - GS!(20));
        }
    }

    
}
