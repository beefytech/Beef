using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace BfAeDebug
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();

            try
            {
                Process process = Process.GetProcessById(int.Parse(Program.sProcessId));
                mLabel.Text = String.Format("Process {0} ({1})", Program.sProcessId, process.ProcessName);
                process.Dispose();

                //var mainWindowHandle = process.MainWindowHandle;
                //NativeWindow.FromHandle(mainWindowHandle).
            }
            catch (Exception)
            {                
            }

            CenterToScreen();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            Directory.SetCurrentDirectory(@"C:\Beef\IDE\dist");
            var process = Process.Start(@"C:\Beef\IDE\dist\BeefIDE_d.exe", String.Format("-attachId={0} -attachHandle={1}", Program.sProcessId, Program.sEventId));
            Hide();
            process.WaitForExit();
            Close();
        }

        private void mVsButton_Click(object sender, EventArgs e)
        {
//             ProcessStartInfo psi = new ProcessStartInfo();
//             psi.FileName = @"c:\\windows\\system32\\vsjitdebugger.exe";
//             psi.CreateNoWindow = false;
//             psi.WorkingDirectory = "C:\\";
//             psi.WindowStyle = ProcessWindowStyle.Normal;
//             psi.Arguments = String.Format("-p {0} -e {1}", Program.sProcessId, Program.sEventId);
//             psi.UseShellExecute = false;
//             psi.ErrorDialog = true;
//             psi.RedirectStandardError = true;
//             psi.RedirectStandardInput = true;
//             psi.RedirectStandardOutput = true;
//             var process = Process.Start(psi);
            //MessageBox.Show(@"C:\Windows\system32\vsjitdebugger.exe" + " " + String.Format("-p {0} -e {1}", Program.sProcessId, Program.sEventId));            
            try
            {
                var process = Process.Start(@"C:\Windows\system32\vsjitdebugger.exe\", String.Format("-p {0} -e {1}", Program.sProcessId, Program.sEventId));
                Hide();
                process.WaitForExit();
                int exitCode = process.ExitCode;
                if (exitCode != 0)
                    MessageBox.Show("vsjitdebugger exit code: " + exitCode);
            }
            catch (Exception ex)
            {
                MessageBox.Show("vsjitdebugger exception: " + ex.ToString());
            }
            //MessageBox.Show("Done");
            Close();
        }

        private void mCancelButton_Click(object sender, EventArgs e)
        {
            Close();
        }

        private void label1_Click(object sender, EventArgs e)
        {

        }
    }
}
