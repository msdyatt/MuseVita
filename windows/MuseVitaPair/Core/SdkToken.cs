using System.Text.RegularExpressions;
using Windows.Security.Credentials;

namespace MuseVitaPair.Core;

public static partial class SdkToken
{
    private const string Resource = "MuseVitaPair";
    [GeneratedRegex("^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$")]
    private static partial Regex ValidFormat();

    public static async Task<string> ReadAsync(string? file, CancellationToken cancellationToken)
    {
        string? token = null;
        if (!string.IsNullOrEmpty(file)) token = (await File.ReadAllTextAsync(file, cancellationToken)).Trim();
        if (string.IsNullOrEmpty(token))
        {
            try { var credential = new PasswordVault().Retrieve(Resource, Environment.UserName); credential.RetrievePassword(); token = credential.Password; } catch { }
        }
        if (string.IsNullOrEmpty(token)) token = ReadHidden("SDK token (hidden): ");
        if (string.IsNullOrEmpty(token) || !ValidFormat().IsMatch(token)) throw new InvalidDataException("SDK token format is invalid; copy it from gadgets.muse.ai.");
        try { var vault = new PasswordVault(); try { vault.Remove(vault.Retrieve(Resource, Environment.UserName)); } catch { } vault.Add(new PasswordCredential(Resource, Environment.UserName, token)); } catch { }
        return token;
    }
    private static string ReadHidden(string prompt)
    {
        Console.Write(prompt); var value = new System.Text.StringBuilder(); ConsoleKeyInfo key;
        while ((key = Console.ReadKey(intercept: true)).Key != ConsoleKey.Enter) { if (key.Key == ConsoleKey.Backspace && value.Length > 0) value.Length--; else if (!char.IsControl(key.KeyChar)) value.Append(key.KeyChar); }
        Console.WriteLine(); return value.ToString().Trim();
    }
}
