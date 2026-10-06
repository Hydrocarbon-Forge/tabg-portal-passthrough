// PatchServer — local-only TABG dedicated-server patcher.
//
// PURPOSE / SCOPE: This edits ONE method body in the *dedicated server's*
// Assembly-CSharp.dll so a LAN / single-machine self-hosted TABG server (which
// the operator owns and which already runs with AntiCheat=false in
// game_settings.txt) will accept made-up local player names without a live
// Epic Online Services token round-trip. It is for the owner's own local
// server only. It is NOT for joining official / matched servers, and the
// companion client plugin refuses to run against a client that still has an
// active EasyAntiCheat install (see docs/windows-checklist.md).
//
// What it changes in Landfall.Network.RoomInitRequestCommand.Run:
//   - Removes the Epic ConnectInterface.VerifyIdToken(...) call and its
//     VerifyIdTokenOptions setup (that call needs a real Epic backend; with no
//     Steam/Epic the token is fake and the player would be kicked EpicVerifyFail).
//   - Inserts, right after the existing AntiCheatAuthorized() block,
//     `player.SetEpicUserID("local")` so GameRoom.ValidatePlayerObjects (which
//     kicks when EpicUserName == null at match start) is satisfied.
//   - Does NOT call AssignPlayerAccountIDsCommand: with no Steam, SteamID is
//     empty and that command does ulong.Parse(SteamID) and would throw. The
//     account-ID broadcast is cosmetic (other clients' scoreboard IDs). See
//     tools/patch_server/README.md for the rationale.
//
// Design for idempotency + safety: the patcher reads from the pristine backup
// (<dll>.orig, created once on first run) and writes the real DLL. Re-running
// therefore always produces exactly one clean patch from the original — it can
// never double-patch. --check reports status without writing.

using Mono.Cecil;
using Mono.Cecil.Cil;

namespace PatchServer;

internal static class Program
{
    private const string RunType = "Landfall.Network.RoomInitRequestCommand";
    private const string RunMethod = "Run";

    private static int Main(string[] args)
    {
        string? dll = null;
        var check = false;
        foreach (var a in args)
        {
            if (a is "--check") check = true;
            else if (a is "-h" or "--help") { Usage(); return 0; }
            else if (!a.StartsWith("-")) dll = a;
            else { Console.Error.WriteLine($"Unknown option: {a}"); Usage(); return 2; }
        }

        if (dll is null) { Usage(); return 2; }
        if (!File.Exists(dll)) { Console.Error.WriteLine($"ERROR: not found: {dll}"); return 2; }

        var orig = dll + ".orig";

        if (check)
        {
            var present = VerifyCallPresent(dll);
            Console.WriteLine(present
                ? "STATUS: UNPATCHED (VerifyIdToken call present in RoomInitRequestCommand.Run)"
                : "STATUS: PATCHED (VerifyIdToken call absent in RoomInitRequestCommand.Run)");
            Console.WriteLine($"BACKUP: {(File.Exists(orig) ? orig : "(none)")}");
            return present ? 10 : 0; // 0 = patched, 10 = unpatched (for scripts)
        }

        // Backup once. After this, the pristine copy always lives at <dll>.orig.
        if (!File.Exists(orig))
        {
            File.Copy(dll, orig);
            Console.WriteLine($"Backed up original -> {orig}");
        }
        else
        {
            Console.WriteLine($"Backup already exists -> {orig} (patching from it)");
        }

        // Always patch from the pristine backup => never double-patches.
        var tmp = dll + ".patched.tmp";
        if (File.Exists(tmp)) File.Delete(tmp);

        using (var asm = AssemblyDefinition.ReadAssembly(orig, ReaderParamsFor(orig)))
        {
            var run = FindRun(asm.MainModule);
            if (run is null)
            {
                Console.Error.WriteLine($"ERROR: {RunType}.{RunMethod} not found. Wrong DLL?");
                return 3;
            }
            Patch(asm.MainModule, run);
            asm.Write(tmp);
        }

        // Verify the written output before swapping it in.
        if (VerifyCallPresent(tmp))
        {
            File.Delete(tmp);
            Console.Error.WriteLine("ERROR: post-patch verification failed (VerifyIdToken still present).");
            return 4;
        }

        File.Copy(tmp, dll, overwrite: true);
        File.Delete(tmp);
        Console.WriteLine($"Patched -> {dll}");
        Console.WriteLine("OK: VerifyIdToken call removed; SetEpicUserID(\"local\") inserted.");
        return 0;
    }

    private static void Usage()
    {
        Console.WriteLine(
@"PatchServer — local-only TABG dedicated-server patcher

Usage:
  PatchServer <path-to-server-Assembly-CSharp.dll>   patch in place (backs up .orig once)
  PatchServer --check <path>                          report patched/unpatched, no write

Exit codes: 0 ok/patched, 10 unpatched (--check), 2 usage, 3 method-not-found, 4 verify-failed.");
    }

    private static MethodDefinition? FindRun(ModuleDefinition module)
    {
        var type = module.GetType(RunType);
        if (type is null) return null;
        foreach (var m in type.Methods)
            if (m.Name == RunMethod && m.IsStatic && m.HasBody)
                return m;
        return null;
    }

    // True if RoomInitRequestCommand.Run still calls ConnectInterface.VerifyIdToken.
    // Cecil resolves type references (e.g. the Unity base types of
    // TABGPlayerServer) while building metadata on Write, so it needs the
    // sibling Managed/ assemblies on its search path.
    private static ReaderParameters ReaderParamsFor(string path)
    {
        var resolver = new DefaultAssemblyResolver();
        var dir = Path.GetDirectoryName(Path.GetFullPath(path));
        if (dir is not null) resolver.AddSearchDirectory(dir);
        return new ReaderParameters { AssemblyResolver = resolver };
    }

    private static bool VerifyCallPresent(string path)
    {
        using var asm = AssemblyDefinition.ReadAssembly(path, ReaderParamsFor(path));
        var run = FindRun(asm.MainModule);
        if (run is null) return false;
        foreach (var ins in run.Body.Instructions)
            if (IsVerifyIdTokenCall(ins)) return true;
        return false;
    }

    private static bool IsVerifyIdTokenCall(Instruction ins)
        => ins.OpCode == OpCodes.Callvirt
           && ins.Operand is MethodReference mr
           && mr.Name == "VerifyIdToken"
           && mr.DeclaringType.Name == "ConnectInterface";

    private static void Patch(ModuleDefinition module, MethodDefinition run)
    {
        var body = run.Body;
        var il = body.GetILProcessor();
        var instrs = body.Instructions;

        // 1. Anchor: the AntiCheatAuthorized() call, and the player load before it.
        Instruction? acAuth = null;
        foreach (var ins in instrs)
        {
            if (ins.OpCode == OpCodes.Callvirt && ins.Operand is MethodReference m
                && m.Name == "AntiCheatAuthorized")
            {
                acAuth = ins;
                break;
            }
        }
        if (acAuth is null)
            throw new Exception("AntiCheatAuthorized() call not found; method layout changed.");

        var playerVar = LoadVariable(acAuth.Previous)
            ?? throw new Exception("could not resolve the TABGPlayerServer local from the load before AntiCheatAuthorized().");

        // 2. Setup block start: first `initobj VerifyIdTokenOptions`.
        Instruction? setupStart = null;
        foreach (var ins in instrs)
        {
            if (ins.OpCode == OpCodes.Initobj && ins.Operand is TypeReference tr
                && tr.Name == "VerifyIdTokenOptions")
            {
                setupStart = ins;
                break;
            }
        }
        if (setupStart is null)
            throw new Exception("VerifyIdTokenOptions initobj not found; method layout changed.");

        // 3. End of block: the VerifyIdToken callvirt.
        Instruction? verifyCall = null;
        for (var ins = setupStart; ins is not null; ins = ins.Next)
        {
            if (IsVerifyIdTokenCall(ins)) { verifyCall = ins; break; }
        }
        if (verifyCall is null)
            throw new Exception("VerifyIdToken call not found after setup block.");

        // 4. Resolve the two methods we emit (same module, no import needed).
        var utf8Ctor = ResolveUtf8StringCtor(module);
        var setEpic = ResolveSetEpicUserID(module);

        // 5. Build injected instructions: player.SetEpicUserID(new Utf8String("local"));
        var inj = new[]
        {
            il.Create(OpCodes.Ldloc, playerVar),
            il.Create(OpCodes.Ldstr, "local"),
            il.Create(OpCodes.Newobj, utf8Ctor),
            il.Create(OpCodes.Callvirt, setEpic),
        };

        // Insert the injected block immediately before the (soon-removed) setup start.
        foreach (var n in inj) il.InsertBefore(setupStart, n);

        // 6. Redirect any branch that targeted setupStart to the first injected
        //    instruction, so the UseAntiCheat==true path also runs SetEpicUserID.
        foreach (var ins in instrs)
        {
            if (ins.Operand is Instruction target && ReferenceEquals(target, setupStart))
                ins.Operand = inj[0];
            else if (ins.Operand is Instruction[] targets)
            {
                for (var i = 0; i < targets.Length; i++)
                    if (ReferenceEquals(targets[i], setupStart)) targets[i] = inj[0];
            }
        }

        // 7. Remove the original setup + VerifyIdToken call range, inclusive.
        var toRemove = new List<Instruction>();
        for (var ins = setupStart; ins is not null; ins = ins.Next)
        {
            toRemove.Add(ins);
            if (ReferenceEquals(ins, verifyCall)) break;
        }
        foreach (var ins in toRemove) il.Remove(ins);

        // Cecil recomputes offsets on Write.
    }

    private static VariableDefinition? LoadVariable(Instruction? ins)
    {
        if (ins is null) return null;
        var op = ins.OpCode;
        if (ins.Operand is VariableDefinition vd &&
            (op == OpCodes.Ldloc || op == OpCodes.Ldloc_S))
            return vd;
        return null; // ldloc.0-3 not expected here (the player is ldloc.s)
    }

    private static MethodReference ResolveUtf8StringCtor(ModuleDefinition module)
    {
        var t = module.GetType("Epic.OnlineServices.Utf8String")
                ?? throw new Exception("Epic.OnlineServices.Utf8String type not found.");
        foreach (var m in t.Methods)
            if (m.IsConstructor && m.Parameters.Count == 1
                && m.Parameters[0].ParameterType.FullName == "System.String")
                return m;
        throw new Exception("Utf8String(string) constructor not found.");
    }

    private static MethodReference ResolveSetEpicUserID(ModuleDefinition module)
    {
        var t = module.GetType("Landfall.Network.TABGPlayerServer")
                ?? throw new Exception("TABGPlayerServer type not found.");
        foreach (var m in t.Methods)
            if (m.Name == "SetEpicUserID" && m.Parameters.Count == 1)
                return m;
        throw new Exception("SetEpicUserID method not found.");
    }
}
