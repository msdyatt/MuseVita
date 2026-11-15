using System.Net.Http.Headers;
using System.Text.Json;

namespace MuseVitaPair.Core;

public static class MuseCredentialValidator
{
    public static async Task<bool> ValidateAsync(PairingCredentials credentials, CancellationToken cancellationToken)
    {
        var root = string.IsNullOrWhiteSpace(credentials.ApiUrlV2) ? "https://api.muse.ai" : credentials.ApiUrlV2.TrimEnd('/');
        if (!Uri.TryCreate(root + "/fetch_vms", UriKind.Absolute, out var endpoint) || endpoint.Scheme != Uri.UriSchemeHttps) return false;
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(20) }; // default handler validates hostname and certificate chain
        using var request = new HttpRequestMessage(HttpMethod.Get, endpoint); request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", credentials.AccessToken); request.Headers.Add("X-API-Version", "1.0.0"); request.Headers.UserAgent.ParseAdd("MuseVitaPair/0.1.0 (Windows)");
        using var response = await client.SendAsync(request, cancellationToken); if (!response.IsSuccessStatusCode) return false;
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken)); var rootElement = document.RootElement;
        return !rootElement.TryGetProperty("error_title", out _) && !rootElement.TryGetProperty("backend_error_code", out _) && rootElement.TryGetProperty("vm_list", out var list) && list.ValueKind == JsonValueKind.Array && list.GetArrayLength() > 0;
    }
}
