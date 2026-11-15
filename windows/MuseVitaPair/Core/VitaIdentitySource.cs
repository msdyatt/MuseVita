using System.Net;
using System.Text.Json;

namespace MuseVitaPair.Core;

public static class VitaIdentitySource
{
    // The Vita endpoint intentionally redirects to a one-time /pair session. Never follow a redirect to another host.
    public static async Task<PairingIdentity> FetchAsync(Uri setupUrl, CancellationToken cancellationToken)
    {
        if (setupUrl.Scheme != Uri.UriSchemeHttp || string.IsNullOrEmpty(setupUrl.Host)) throw new ArgumentException("--vita-url must be an http:// Vita setup URL.");
        using var handler = new HttpClientHandler { AllowAutoRedirect = false };
        using var client = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(15) };
        using var setup = await client.GetAsync(setupUrl, cancellationToken);
        if (setup.StatusCode is not (HttpStatusCode.Found or HttpStatusCode.SeeOther or HttpStatusCode.TemporaryRedirect) || setup.Headers.Location is null) throw new InvalidDataException("The Vita setup URL did not create a pairing session.");
        var pair = setup.Headers.Location.IsAbsoluteUri ? setup.Headers.Location : new Uri(setupUrl, setup.Headers.Location);
        if (!string.Equals(pair.Host, setupUrl.Host, StringComparison.OrdinalIgnoreCase) || pair.Port != setupUrl.Port || pair.AbsolutePath != "/pair") throw new InvalidDataException("Rejected a non-Vita pairing redirect.");
        var session = pair.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries)
            .Select(x => x.Split('=', 2)).FirstOrDefault(x => x[0] == "s")?.ElementAtOrDefault(1);
        session = session is null ? null : Uri.UnescapeDataString(session);
        if (string.IsNullOrEmpty(session)) throw new InvalidDataException("The Vita pairing session is missing.");
        var identityEndpoint = new UriBuilder(setupUrl) { Path = "/pair/identity", Query = "s=" + Uri.EscapeDataString(session) }.Uri;
        using var response = await client.GetAsync(identityEndpoint, cancellationToken); response.EnsureSuccessStatusCode();
        var identity = await JsonSerializer.DeserializeAsync<PairingIdentity>(await response.Content.ReadAsStreamAsync(cancellationToken), cancellationToken: cancellationToken) ?? throw new InvalidDataException("The Vita returned no identity.");
        identity.Validate(); return identity;
    }
    public static async Task<PairingIdentity> ReadFileAsync(string path, CancellationToken cancellationToken)
    {
        await using var input = File.OpenRead(path); var identity = await JsonSerializer.DeserializeAsync<PairingIdentity>(input, cancellationToken: cancellationToken) ?? throw new InvalidDataException("Identity file is empty."); identity.Validate(); return identity;
    }
}
