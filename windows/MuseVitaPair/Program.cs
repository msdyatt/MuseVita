using MuseVitaPair.Ble;
using MuseVitaPair.Core;

static void Usage() => Console.WriteLine("Usage: musevita-pair --vita-url http://VITA-IP:8768/ [--sdk-token-file PATH] [--output PATH]\n       musevita-pair --identity-file PATH [--sdk-token-file PATH] [--output PATH]\nThe token is read from Credential Manager, a local file, or a hidden prompt; it is never accepted as an argument.");
static string? Next(Dictionary<string, string> values, string key) => values.TryGetValue(key, out var value) ? value : null;

var options = new Dictionary<string, string>(StringComparer.Ordinal);
try
{
    for (var i = 0; i < args.Length; i++)
    {
        if (args[i] is "--help" or "-h") { Usage(); return; }
        if (args[i] is not ("--vita-url" or "--identity-file" or "--sdk-token-file" or "--output") || ++i == args.Length) throw new ArgumentException("Unknown or incomplete option.");
        options[args[i - 1]] = args[i];
    }
    if ((Next(options, "--vita-url") is null) == (Next(options, "--identity-file") is null)) throw new ArgumentException("Provide exactly one of --vita-url or --identity-file.");
    var identity = Next(options, "--vita-url") is { } vita ? await VitaIdentitySource.FetchAsync(new Uri(vita), CancellationToken.None) : await VitaIdentitySource.ReadFileAsync(Next(options, "--identity-file")!, CancellationToken.None);
    var token = await SdkToken.ReadAsync(Next(options, "--sdk-token-file"), CancellationToken.None); var output = Path.GetFullPath(Next(options, "--output") ?? "musevita-pairing.json");
    var session = new PairingSession(identity, "0.1.0", token);
    session.OnProvisionAsync = async credentials => { if (!await MuseCredentialValidator.ValidateAsync(credentials, CancellationToken.None)) { Console.Error.WriteLine("Muse rejected the returned device credentials."); return false; } await PairingBundleWriter.WriteAsync(output, credentials, token, CancellationToken.None); Console.WriteLine($"Pairing complete. Wrote {output}"); return true; };
    await using var ble = new WindowsBlePairingServer(session); await ble.StartAsync();
    Console.WriteLine($"Pairing GATT service is open. Windows advertises its system Bluetooth name rather than {identity.BleName}; in the Muse app use Settings > Devices > Developer mode > Add Device and look for this PC.");
    Console.WriteLine("Physical Muse-app discovery on Windows is unverified. Press Enter to stop advertising."); Console.ReadLine();
}
catch (Exception error) { Console.Error.WriteLine($"MuseVita pairing: {error.Message}"); Environment.ExitCode = 1; }
