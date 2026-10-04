using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

static class Startup {
    static readonly JavaScriptSerializer json = new JavaScriptSerializer();
    static string Hash(string path) { using(var input=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(input)).Replace("-",""); }
    static bool Running(string name) { return Process.GetProcessesByName(name).Length>0; }
    static string Setting(Dictionary<string,object> settings,string key,string fallback) {
        return settings.ContainsKey(key) && !string.IsNullOrWhiteSpace(Convert.ToString(settings[key])) ? Convert.ToString(settings[key]) : fallback;
    }
    static void CheckJava(string java) {
        var info=new ProcessStartInfo {FileName=Path.Combine(java,"bin","java.exe"),Arguments="-version",UseShellExecute=false,CreateNoWindow=true,RedirectStandardError=true,RedirectStandardOutput=true};
        using(var process=Process.Start(info)) {
            string version=process.StandardError.ReadToEnd()+process.StandardOutput.ReadToEnd();
            process.WaitForExit();
            if(process.ExitCode!=0 || !System.Text.RegularExpressions.Regex.IsMatch(version,@"version ""25(?:\.|"")"))
                throw new IOException("JDK 25 is required. Set javaHome in launcher.local.json or JAVA_HOME to a JDK 25 installation.");
        }
    }
    static byte[] ReadBytes(Stream stream,int count) {
        var data=new byte[count]; int offset=0;
        while(offset<count) { int read=stream.Read(data,offset,count-offset); if(read==0) throw new EndOfStreamException(); offset+=read; }
        return data;
    }
    // None: not running. Loading: correct client, world not loaded yet.
    static string Bridge() {
        // .NET Framework ClientWebSocket rejects Java-WebSocket's valid "Upgrade,Keep-Alive"
        // response. This local hello probe accepts HTTP token lists and validates the RFC6455 accept key.
        using(var socket=new TcpClient()) {
            try {
                var connecting=socket.BeginConnect("127.0.0.1",25599,null,null);
                using(connecting.AsyncWaitHandle) { if(!connecting.AsyncWaitHandle.WaitOne(2000)) return "None"; }
                socket.EndConnect(connecting); socket.ReceiveTimeout=2000; socket.SendTimeout=2000;
                Stream stream=socket.GetStream(); byte[] nonce=new byte[16];
                using(var random=RandomNumberGenerator.Create()) random.GetBytes(nonce);
                string key=Convert.ToBase64String(nonce);
                byte[] request=Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\nHost: 127.0.0.1:25599\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\n\r\n");
                stream.Write(request,0,request.Length);
                var header=new StringBuilder();
                while(!header.ToString().EndsWith("\r\n\r\n",StringComparison.Ordinal)) {
                    int value=stream.ReadByte(); if(value<0) throw new EndOfStreamException();
                    header.Append((char)value); if(header.Length>8192) throw new InvalidDataException("Minecraft handshake is too large.");
                }
                string[] lines=header.ToString().Split(new[]{"\r\n"},StringSplitOptions.None);
                if(!lines[0].StartsWith("HTTP/1.1 101 ",StringComparison.Ordinal)) throw new InvalidDataException("Minecraft bridge did not upgrade the connection.");
                var headers=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
                foreach(string line in lines.Skip(1)) { int colon=line.IndexOf(':'); if(colon>0) headers[line.Substring(0,colon)]=line.Substring(colon+1).Trim(); }
                string accept; using(var sha=SHA1.Create()) accept=Convert.ToBase64String(sha.ComputeHash(Encoding.ASCII.GetBytes(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
                if(!headers.ContainsKey("Sec-WebSocket-Accept") || headers["Sec-WebSocket-Accept"]!=accept || !headers.ContainsKey("Upgrade") || !headers["Upgrade"].Equals("websocket",StringComparison.OrdinalIgnoreCase) || !headers.ContainsKey("Connection") || !headers["Connection"].Split(',').Any(token=>token.Trim().Equals("Upgrade",StringComparison.OrdinalIgnoreCase)))
                    throw new InvalidDataException("Invalid Minecraft bridge handshake.");
                byte[] prefix=ReadBytes(stream,2); int length=prefix[1]&127;
                if(length==127) throw new InvalidDataException("Minecraft hello frame is too large.");
                if(length==126) { byte[] extended=ReadBytes(stream,2); length=(extended[0]<<8)|extended[1]; }
                if(prefix[0]!=0x81 || (prefix[1]&128)!=0 || length>16384) throw new InvalidDataException("Invalid Minecraft hello frame.");
                var hello=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadBytes(stream,length)));
                if(!hello.ContainsKey("t") || (string)hello["t"]!="hello") throw new InvalidDataException("Minecraft did not send its hello message.");
                if(!hello.ContainsKey("features") || !hello["features"].ToString().Contains("inventory-ui-v3") || !hello["features"].ToString().Contains("movement-v4") || !hello["features"].ToString().Contains("achroma-world-v2")) return "Old";
                return hello.ContainsKey("ready") && Convert.ToBoolean(hello["ready"]) ? "Ready" : "Loading";
            } catch(InvalidDataException) { throw; } catch(SocketException) { return "None"; } catch(IOException) { return "None"; }
        }
    }
    static string Under(string directory,string relative) {
        string basePath=Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
        string path=Path.GetFullPath(Path.Combine(directory,relative));
        if(!path.StartsWith(basePath,StringComparison.OrdinalIgnoreCase)) throw new IOException("Invalid package path: "+relative);
        return path;
    }
    static string IniPath(string path) { return path.Replace(",",",,"); }
    static void PrepareReShade(string root,string game,string package) {
        // ReShade's supported BasePath redirects its writable files out of Program Files.
        string runtime=Path.Combine(root,"runtime","reshade");
        Directory.CreateDirectory(runtime);
        string config=Path.Combine(runtime,"ReShade.ini"), preset=Path.Combine(runtime,"ReShadePreset.ini");
        if(!File.Exists(config)) {
            string contents=File.ReadAllText(Path.Combine(root,"launcher","ReShade.ini.template"))
                .Replace("{{SHADERS}}",IniPath(Path.Combine(game,"reshade-shaders","Shaders")))
                .Replace("{{TEXTURES}}",IniPath(Path.Combine(game,"reshade-shaders","Textures")))
                .Replace("{{PRESET}}",IniPath(preset)).Replace("{{RUNTIME}}",IniPath(runtime));
            File.WriteAllText(config,contents);
        }
        if(!File.Exists(preset)) File.Copy(Path.Combine(root,"launcher","ReShadePreset.ini"),preset);
        if(!Running("GTA5_Enhanced") && !Running("GTA5")) {
            // ReShade saves this runtime gate in the preset. A saved true value
            // must not composite unbound textures before the Story Mode script starts.
            string contents=File.ReadAllText(preset);
            string reset=System.Text.RegularExpressions.Regex.Replace(contents,@"(?m)^McActive=[^\r\n]*","McActive=0");
            if(reset!=contents) File.WriteAllText(preset,reset);
        }
        string redirect="[INSTALL]"+Environment.NewLine+"BasePath="+IniPath(runtime)+Environment.NewLine;
        string packaged=Path.Combine(package,"ReShade.ini");
        if(!File.Exists(packaged) || File.ReadAllText(packaged)!=redirect) File.WriteAllText(packaged,redirect);
    }
    public static void Run(string root,bool check,Action<string> output) {
        Directory.CreateDirectory(Path.Combine(root,"logs"));
        Action<string> stage=message=> { output(message); File.AppendAllText(Path.Combine(root,"logs","launcher.log"),DateTime.Now.ToString("s")+": "+message+Environment.NewLine); };
        string manifestPath=Path.Combine(root,"diagnostics","install-Enhanced.json");
        stage("Checking the Story Mode installation...");
        var manifest=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(manifestPath));
        string game=(string)manifest["game"], package=Path.Combine(root,".cache","gta-package");
        string settingsPath=Path.Combine(root,"launcher.local.json");
        var settings=File.Exists(settingsPath) ? json.Deserialize<Dictionary<string,object>>(File.ReadAllText(settingsPath)) : new Dictionary<string,object>();
        string steam=Setting(settings,"steamExecutable",Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam","steam.exe"));
        string java=Setting(settings,"javaHome",Environment.GetEnvironmentVariable("JAVA_HOME") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"Java","jdk-25.0.2"));
        string gradle=Path.Combine(root,"mc","gradlew.bat");
        foreach(string file in new[]{steam,Path.Combine(game,"GTA5_Enhanced.exe"),Path.Combine(java,"bin","java.exe"),gradle,Path.Combine(root,"mc","build","libs","passthrough-0.1.0.jar"),Path.Combine(package,"MCPassthrough.asi"),Path.Combine(root,"launcher","ReShade.ini.template"),Path.Combine(root,"launcher","ReShadePreset.ini")})
            if(!File.Exists(file)) throw new FileNotFoundException("Missing required project file: "+file);
        CheckJava(java);
        if(!check) PrepareReShade(root,game,package);
        var entries=(ArrayList)manifest["files"];
        var owned=entries.Cast<Dictionary<string,object>>().ToDictionary(x=>(string)x["relative"],StringComparer.OrdinalIgnoreCase);
        var changed=new List<string>();
        foreach(string source in Directory.GetFiles(package,"*",SearchOption.AllDirectories)) {
            string relative=source.Substring(package.Length+1), target=Under(game,relative);
            if(!File.Exists(target) || Hash(source)!=Hash(target)) changed.Add(relative);
        }
        string bridge=Bridge();
        if(check) { stage("Checks passed. Mod update needed: "+(changed.Count>0)+". Minecraft: "+bridge+"."); return; }
        if(changed.Count>0) {
            if(Running("GTA5_Enhanced") || Running("GTA5")) throw new IOException("Close GTA normally, then run the launcher again to install the update.");
            // Never overwrite files that were not installed by this project or were edited later.
            foreach(string relative in changed) {
                string target=Under(game,relative);
                if(File.Exists(target) && (!owned.ContainsKey(relative) || Hash(target)!=(string)owned[relative]["sha256"]))
                    throw new IOException("Preserving modified or unowned GTA file: "+relative);
            }
            string backup=Path.Combine(root,"diagnostics","backups","launcher-update-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(backup); File.Copy(manifestPath,Path.Combine(backup,"install-Enhanced.json"));
            foreach(string relative in changed) {
                string target=Under(game,relative), save=Under(backup,relative);
                if(File.Exists(target)) { Directory.CreateDirectory(Path.GetDirectoryName(save)); File.Copy(target,save); }
            }
            var copied=new List<string>();
            stage("Installing the prepared mod update. Backup: "+backup);
            try {
                foreach(string relative in changed) {
                    string target=Under(game,relative); Directory.CreateDirectory(Path.GetDirectoryName(target));
                    copied.Add(relative); File.Copy(Under(package,relative),target,true);
                    if(Hash(target)!=Hash(Under(package,relative))) throw new IOException("Update verification failed: "+relative);
                    if(!owned.ContainsKey(relative)) { var entry=new Dictionary<string,object>{{"relative",relative},{"sha256",Hash(target)}}; entries.Add(entry); owned.Add(relative,entry); }
                    else owned[relative]["sha256"]=Hash(target);
                }
                manifest["updatedAt"]=DateTime.UtcNow.ToString("o");
                string temporary=manifestPath+".launcher.tmp"; File.WriteAllText(temporary,json.Serialize(manifest)); File.Replace(temporary,manifestPath,null);
            } catch {
                foreach(string relative in copied) { string saved=Under(backup,relative), target=Under(game,relative); if(File.Exists(saved)) File.Copy(saved,target,true); else if(File.Exists(target)) File.Delete(target); }
                File.Copy(Path.Combine(backup,"install-Enhanced.json"),manifestPath,true); throw;
            }
        }
        if(bridge=="Old") throw new IOException("Close the existing Minecraft client normally, then run this launcher to load the updated bridge mod.");
        if(bridge=="None") {
            stage("Starting the dedicated Minecraft world...");
            string log=Path.Combine(root,"logs","minecraft-launcher-runtime.log");
            var info=new ProcessStartInfo {
                FileName=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe"),
                Arguments="/d /s /c \"\""+gradle+"\" -p \""+Path.Combine(root,"mc")+"\" runClient --no-daemon --console=plain > \""+log+"\" 2>&1\"",
                WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true
            };
            info.EnvironmentVariables["JAVA_HOME"]=java;
            info.EnvironmentVariables["GRADLE_USER_HOME"]=Path.Combine(root,".cache","gradle");
            using(var client=Process.Start(info)) {
                DateTime deadline=DateTime.Now.AddMinutes(4);
                while((bridge=Bridge())!="Ready") {
                    if(bridge=="Old") throw new IOException("A different Minecraft bridge is running. Close it normally before retrying.");
                    if(client.HasExited) throw new IOException("Minecraft stopped during startup. See logs\\minecraft-launcher-runtime.log.");
                    if(DateTime.Now>deadline) throw new IOException("Minecraft did not become ready. See logs\\minecraft-launcher-runtime.log.");
                    Thread.Sleep(1000);
                }
            }
        } else {
            stage("Using the Minecraft client already running.");
            DateTime deadline=DateTime.Now.AddMinutes(2);
            while(bridge=="Loading" && DateTime.Now<deadline) { Thread.Sleep(1000); bridge=Bridge(); }
            if(bridge!="Ready") throw new IOException("Minecraft world has not finished loading. Try again after it is ready.");
        }
        stage("Dedicated Minecraft world is ready.");
        if(!Running("GTA5_Enhanced")) {
            stage("Starting GTA Enhanced through Steam...");
            Process.Start(new ProcessStartInfo {FileName=steam,Arguments="-applaunch 3240220",UseShellExecute=true,WindowStyle=ProcessWindowStyle.Hidden});
        } else stage("GTA Enhanced is already running.");
        stage("Ready. Enter Story Mode, hold Left Alt / controller D-pad Down, select STEVE, and release.");
        stage("GTA movement and jumping. E: inventory. Double-tap Space: elytra. Fireworks: boost.");
        stage("Your existing Steam launch options are preserved; use the configured Story Mode setup with BattlEye disabled.");
    }
}
