using System.Text;
using System.Text.RegularExpressions;

namespace MuseVitaPair.Core;

public static partial class Base64Url
{
    [GeneratedRegex("^[A-Za-z0-9_-]+$")]
    private static partial Regex ValidCharacters();

    public static string Encode(ReadOnlySpan<byte> bytes) => Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    public static byte[] Decode(string? value, int maxChars = 16_384)
    {
        if (string.IsNullOrEmpty(value) || value.Length > maxChars || value.Length % 4 == 1 || !ValidCharacters().IsMatch(value))
            throw new FormatException("Invalid unpadded base64url.");
        var padded = value.Replace('-', '+').Replace('_', '/') + new string('=', (4 - value.Length % 4) % 4);
        return Convert.FromBase64String(padded);
    }
}
