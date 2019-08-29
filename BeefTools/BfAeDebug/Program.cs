using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace BfAeDebug
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        /// 
        public static String sProcessId;
        public static String sEventId;

        [STAThread]
        static void Main(String[] args)
        {   
            if (args.Length < 2)
            {
                String argStr = "Args: ";
                foreach (var arg in args)
                    argStr += arg;
                MessageBox.Show(argStr);
                return;
            }

            sProcessId = args[args.Length - 2];
            sEventId = args[args.Length - 1];

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1(args));
        }
    }
}
