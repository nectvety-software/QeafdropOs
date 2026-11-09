using System;
using System.Runtime.InteropServices;
class Cred {
    [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    static extern bool CredRead(string t, int ty, int r, out IntPtr p);
    [DllImport("advapi32.dll")]
    static extern void CredFree(IntPtr p);
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct C { int Flags; int Type; string TargetName; string Comment; int Persist; int Attr; string TargetAlias; string UserName; string CredBlob; }
    static void Main(){
        foreach(var target in new[]{"git:https://github.com","GitHub - https://api.github.com/nectvety-software"}){
            IntPtr p;
            if(!CredRead(target,1,0,out p)){ Console.WriteLine(target+" FAIL "+Marshal.GetLastWin32Error()); continue; }
            var c=(C)Marshal.PtrToStructure(p,typeof(C));
            Console.WriteLine(target);
            Console.WriteLine("User: "+c.UserName);
            Console.WriteLine("BlobLen: "+(c.CredBlob==null?0:c.CredBlob.Length));
            if(c.CredBlob!=null) Console.WriteLine("TokenPrefix: "+c.CredBlob.Substring(0, Math.Min(8,c.CredBlob.Length))+"...");
            CredFree(p);
        }
    }
}
