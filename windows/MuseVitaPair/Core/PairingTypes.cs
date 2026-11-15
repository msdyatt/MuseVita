using System.Text.Json.Serialization;

namespace MuseVitaPair.Core;

public sealed record PairingIdentity(string Mac)
{
    public string CompactSuffix => Mac.Replace(":", "", StringComparison.Ordinal).ToLowerInvariant()[^6..];
    public string NodeId => $"homelink-{CompactSuffix}";
    public string DeviceId => $"hatch-link:{Mac.ToLowerInvariant()}";
    public string BleName => $"MuseGadget{CompactSuffix.ToUpperInvariant()}";

    public void Validate()
    {
        if (!System.Text.RegularExpressions.Regex.IsMatch(Mac, "^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$"))
            throw new InvalidDataException("The Vita identity has an invalid MAC-shaped identity.");
    }
}

public sealed record PairingCredentials(
    [property: JsonPropertyName("access_token")] string AccessToken,
    [property: JsonPropertyName("refresh_token")] string RefreshToken,
    [property: JsonPropertyName("token_type")] string TokenType,
    [property: JsonPropertyName("username")] string Username,
    [property: JsonPropertyName("api_url")] string ApiUrl,
    [property: JsonPropertyName("api_url_v2")] string ApiUrlV2,
    [property: JsonPropertyName("noise_host")] string NoiseHost,
    [property: JsonPropertyName("access_token_saved_at")] long AccessTokenSavedAt,
    [property: JsonPropertyName("sdk_token_hint")] string SdkTokenHint);

public sealed record PairingBundle(
    [property: JsonPropertyName("format")] string Format,
    [property: JsonPropertyName("pairing")] string Pairing,
    [property: JsonPropertyName("sdk_token")] string SdkToken);
