using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

sealed class Launcher : Form {
    readonly TextBox output = new TextBox();
    readonly Button start = new Button();
    bool running;
    readonly string root = AppDomain.CurrentDomain.BaseDirectory;
    Launcher() {
        Text = "Minecraft in GTA - Story Mode";
        ClientSize = new Size(700, 420); MinimumSize = new Size(600, 380);
        BackColor = Color.FromArgb(21,25,31); ForeColor = Color.White;
        Font = new Font("Segoe UI", 10);
        var title = new Label { Text="Minecraft in GTA", Font=new Font("Segoe UI",22,FontStyle.Bold), AutoSize=true, Location=new Point(24,20) };
        var help = new Label { Text="Select Steve from the character wheel. E: inventory. Jump, then jump again for elytra.", AutoSize=true, Location=new Point(24,75) };
        output.Multiline=true; output.ReadOnly=true; output.ScrollBars=ScrollBars.Vertical;
        output.BackColor=Color.FromArgb(31,37,45); output.ForeColor=Color.White;
        output.BorderStyle=BorderStyle.FixedSingle; output.SetBounds(24,110,652,235);
        output.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;
        start.Text="Start Story Mode"; start.SetBounds(24,365,180,32);
        start.Anchor=AnchorStyles.Bottom|AnchorStyles.Left; start.Click+=(s,e)=>Run();
        var logs=new Button {Text="Open launcher log",Location=new Point(220,365),Size=new Size(170,32),Anchor=AnchorStyles.Bottom|AnchorStyles.Left};
        logs.Click+=(s,e)=> { var path=Path.Combine(root,"logs","launcher.log"); if(File.Exists(path)) Process.Start("notepad.exe","\""+path+"\""); };
        Controls.AddRange(new Control[]{title,help,output,start,logs});
        Shown+=(s,e)=>Run();
    }
    void Append(string line) { if(line==null || IsDisposed) return; BeginInvoke((Action)(()=>output.AppendText(line+Environment.NewLine))); }
    void Run() {
        if(running) return;
        running=true;
        start.Enabled=false; output.Clear();
        Task.Run(()=> {
            try { Startup.Run(root,false,Append); }
            catch(Exception e) { Append("ERROR: "+e.Message); }
            finally { running=false; if(!IsDisposed) BeginInvoke((Action)(()=>start.Enabled=true)); }
        });
    }
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        using(var app=new Launcher()) {
            if(args.Length>0 && args[0]=="--check") {
                Directory.CreateDirectory(Path.Combine(app.root,"logs"));
                using(var log=new StreamWriter(Path.Combine(app.root,"logs","launcher-check.log"))) {
                    try { Startup.Run(app.root,true,log.WriteLine); return 0; }
                    catch(Exception e) { log.WriteLine("ERROR: "+e.Message); return 1; }
                }
            }
            Application.Run(app); return 0;
        }
    }
}
