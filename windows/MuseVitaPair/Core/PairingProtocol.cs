using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace MuseVitaPair.Core;

public sealed record KeySchedule(byte[] TranscriptHash, byte[] MobileTxKey, byte[] MobileRxKey, byte[] SessionId);

public static class PairingProtocol
{
    public const string RecordLabel = "hatch-link ble setup v1";
    private const int TagSize = 16;

    public static string BuildTranscript(PairingIdentity identity, string firmwareVersion, string mobilePub, string devicePub, string mobileNonce, string deviceNonce) => string.Join('\n', new[]
    {
        "hatch-link-pairing-v5", "version=5", "initiator_role=mobile", "responder_role=link",
        $"device_id={identity.DeviceId}", $"node_id={identity.NodeId}", $"mac={identity.Mac.ToLowerInvariant()}", "model=hatch_link",
        $"firmware_version={firmwareVersion}", "selected_cipher_suite=p256-hkdf-sha256-aes-gcm-v1", "pairing_auth=none", "pairing_auth_epoch=0",
        "pairing_policy=confirm_app", "confirm_timeout_seconds=0", $"mobile_pub={mobilePub}", $"device_pub={devicePub}",
        $"mobile_nonce={mobileNonce}", $"device_nonce={deviceNonce}",
    });

    public static KeySchedule DeriveKeySchedule(byte[] rawSecret, PairingIdentity identity, string firmwareVersion, byte[] mobilePub, byte[] devicePub, byte[] mobileNonce, byte[] deviceNonce)
    {
        var transcript = BuildTranscript(identity, firmwareVersion, Base64Url.Encode(mobilePub), Base64Url.Encode(devicePub), Base64Url.Encode(mobileNonce), Base64Url.Encode(deviceNonce));
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(transcript));
        var salt = SHA256.HashData(Concat(mobileNonce, deviceNonce, hash));
        var secret = HkdfExtractAndExpand(rawSecret, salt, Encoding.UTF8.GetBytes(RecordLabel));
        var mobileTx = HkdfExpand(secret, Encoding.UTF8.GetBytes("mobile->device"));
        var mobileRx = HkdfExpand(secret, Encoding.UTF8.GetBytes("device->mobile"));
        var sessionId = SHA256.HashData(Concat(Encoding.UTF8.GetBytes("hatch-link session id v1"), hash, rawSecret))[..16];
        CryptographicOperations.ZeroMemory(secret);
        return new(hash, mobileTx, mobileRx, sessionId);
    }

    public static byte[] Nonce(byte direction, ulong counter)
    {
        var nonce = new byte[12]; nonce[0] = direction;
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(nonce.AsSpan(4), counter); return nonce;
    }
    public static byte[] Aad(string sessionId, byte direction, ulong counter) => Encoding.UTF8.GetBytes($"{RecordLabel}|{sessionId}|{(direction == 0 ? "m2d" : "d2m")}|{counter}");

    public static (byte[] Ciphertext, byte[] Tag) Encrypt(byte[] key, string sessionId, byte direction, ulong counter, byte[] plaintext)
    {
        var cipher = new byte[plaintext.Length]; var tag = new byte[TagSize];
        using var aes = new AesGcm(key, TagSize); aes.Encrypt(Nonce(direction, counter), plaintext, cipher, tag, Aad(sessionId, direction, counter));
        return (cipher, tag);
    }
    public static byte[] Decrypt(byte[] key, string sessionId, byte direction, ulong counter, byte[] ciphertext, byte[] tag)
    {
        if (tag.Length != TagSize) throw new CryptographicException("Bad tag length.");
        var plain = new byte[ciphertext.Length]; using var aes = new AesGcm(key, TagSize);
        aes.Decrypt(Nonce(direction, counter), ciphertext, tag, plain, Aad(sessionId, direction, counter)); return plain;
    }
    public static byte[] X963PublicKey(ECDiffieHellman key)
    {
        var p = key.ExportParameters(false); return Concat(new byte[] { 4 }, p.Q.X!, p.Q.Y!);
    }
    public static ECDiffieHellmanPublicKey ParseX963PublicKey(byte[] data)
    {
        if (data.Length != 65 || data[0] != 4) throw new FormatException("Invalid P-256 point.");
        using var imported = ECDiffieHellman.Create(new ECParameters { Curve = ECCurve.NamedCurves.nistP256, Q = new ECPoint { X = data[1..33], Y = data[33..65] } });
        return imported.PublicKey;
    }
    public static byte[] RawSecret(ECDiffieHellman privateKey, ECDiffieHellmanPublicKey publicKey) => ECDiffieHellman.DeriveRawSecretAgreement(privateKey.PrivateKey, publicKey);

    private static byte[] HkdfExtractAndExpand(byte[] ikm, byte[] salt, byte[] info) => HkdfExpand(HMACSHA256.HashData(salt, ikm), info);
    private static byte[] HkdfExpand(byte[] prk, byte[] info)
    {
        var input = new byte[info.Length + 1]; info.CopyTo(input, 0); input[^1] = 1;
        return HMACSHA256.HashData(prk, input);
    }
    public static byte[] Concat(params byte[][] values) { var output = new byte[values.Sum(x => x.Length)]; var pos = 0; foreach (var value in values) { value.CopyTo(output, pos); pos += value.Length; } return output; }
}
public sealed class PairingSession
{
    private readonly PairingIdentity _identity; private readonly string _firmware; private readonly string _sdkToken;
    private byte[]? _rx; private byte[]? _tx; private string _sessionId = ""; private ulong _rxCounter; private ulong _txCounter;
    private bool _waiting; private bool _confirmed;
    public PairingSession(PairingIdentity identity, string firmware, string sdkToken) => (_identity, _firmware, _sdkToken) = (identity, firmware, sdkToken);
    public bool Confirmed => _confirmed;
    public Func<PairingCredentials, Task<bool>>? OnProvisionAsync { get; set; }

    public async Task<IReadOnlyList<byte[]>> HandleAsync(byte[] message)
    {
        using var document = JsonDocument.Parse(message); var root = document.RootElement;
        if (!root.TryGetProperty("action", out var action) || action.ValueKind != JsonValueKind.String) return Array.Empty<byte[]>();
        return action.GetString() switch
        {
            "get_device_info" => Frame(DeviceInfo()),
            "pairing_client_hello" => HandleHello(root),
            "pairing_encrypted" => await HandleEncryptedAsync(root),
            _ => Array.Empty<byte[]>(),
        };
    }
    private IReadOnlyList<byte[]> HandleHello(JsonElement hello)
    {
        try
        {
            if (!hello.TryGetProperty("version", out var v) || v.ValueKind != JsonValueKind.Number || v.GetInt32() != 5 ||
                hello.GetProperty("pairing_auth").GetString() != "none" || hello.GetProperty("pairing_policy").GetString() != "confirm_app") throw new FormatException();
            var mobilePub = Base64Url.Decode(hello.GetProperty("mobile_pub").GetString(), 4096);
            var mobileNonce = Base64Url.Decode(hello.GetProperty("mobile_nonce").GetString(), 4096);
            if (mobilePub.Length != 65 || mobilePub[0] != 4 || mobileNonce.Length != 16) throw new FormatException();
            using var device = ECDiffieHellman.Create(ECCurve.NamedCurves.nistP256);
            using var mobile = PairingProtocol.ParseX963PublicKey(mobilePub);
            var devicePub = PairingProtocol.X963PublicKey(device); var nonce = RandomNumberGenerator.GetBytes(16);
            var schedule = PairingProtocol.DeriveKeySchedule(PairingProtocol.RawSecret(device, mobile), _identity, _firmware, mobilePub, devicePub, mobileNonce, nonce);
            _rx = schedule.MobileTxKey; _tx = schedule.MobileRxKey; _sessionId = Base64Url.Encode(schedule.SessionId); _rxCounter = _txCounter = 0; _waiting = true; _confirmed = false;
            return Frame(new { type = "pairing_ready", version = 5, device_id = _identity.DeviceId, node_id = _identity.NodeId, mac = _identity.Mac.ToLowerInvariant(), model = "hatch_link", firmware_version = _firmware, pairing_auth = "none", pairing_auth_epoch = 0, pairing_policy = "confirm_app", device_pub = Base64Url.Encode(devicePub), device_nonce = Base64Url.Encode(nonce), transcript_hash = Base64Url.Encode(schedule.TranscriptHash), session_id = _sessionId });
        }
        catch { Reset(); return Frame(new { type = "status", status = "error_pairing_invalid_hello" }); }
    }
    private async Task<IReadOnlyList<byte[]>> HandleEncryptedAsync(JsonElement envelope)
    {
        try
        {
            if (_rx is null || envelope.GetProperty("session_id").GetString() != _sessionId || !TryCounter(envelope.GetProperty("counter").GetString(), out var counter) || counter != _rxCounter) throw new CryptographicException();
            var plaintext = PairingProtocol.Decrypt(_rx, _sessionId, 0, counter, Base64Url.Decode(envelope.GetProperty("ciphertext").GetString()), Base64Url.Decode(envelope.GetProperty("tag").GetString())); _rxCounter++;
            using var command = JsonDocument.Parse(plaintext); var root = command.RootElement;
            if (root.TryGetProperty("action", out var action) && action.GetString() == "pairing_client_finished" && _waiting && root.EnumerateObject().Count() == 1)
            { _waiting = false; _confirmed = true; return Encrypt(new { type = "status", status = "pairing_confirmed", sdk_token = _sdkToken }); }
            if (root.TryGetProperty("action", out action) && action.GetString() == "provision_v2" && _confirmed)
                return await ProvisionAsync(root);
            return Array.Empty<byte[]>();
        }
        catch { Reset(); return Frame(new { type = "status", status = "error_pairing_decrypt" }); }
    }
    private async Task<IReadOnlyList<byte[]>> ProvisionAsync(JsonElement root)
    {
        if (!root.TryGetProperty("access_token", out var access) || string.IsNullOrEmpty(access.GetString()) || !root.TryGetProperty("refresh_token", out var refresh) || string.IsNullOrEmpty(refresh.GetString()) || root.GetProperty("token_type").GetString() != "device") return Encrypt(new { type = "status", status = "error_missing_credentials" });
        var credentials = new PairingCredentials(access.GetString()!, refresh.GetString()!, "device", String(root, "username"), Https(root, "api_url"), Https(root, "api_url_v2"), String(root, "noise_host"), DateTimeOffset.UtcNow.ToUnixTimeSeconds(), _sdkToken[..Math.Min(12, _sdkToken.Length)]);
        return await (OnProvisionAsync?.Invoke(credentials) ?? Task.FromResult(false)) ? Encrypt(new { type = "status", status = "auth_ok" }) : Encrypt(new { type = "status", status = "auth_failed" });
    }
    private IReadOnlyList<byte[]> Frame(object value) => BleFraming.Encode(JsonSerializer.SerializeToUtf8Bytes(value, JsonOptions));
    private IReadOnlyList<byte[]> Encrypt(object value)
    {
        if (_tx is null) return Array.Empty<byte[]>(); var counter = _txCounter++;
        var sealedRecord = PairingProtocol.Encrypt(_tx, _sessionId, 1, counter, JsonSerializer.SerializeToUtf8Bytes(value, JsonOptions));
        return Frame(new { type = "pairing_encrypted", session_id = _sessionId, counter = counter.ToString(System.Globalization.CultureInfo.InvariantCulture), ciphertext = Base64Url.Encode(sealedRecord.Ciphertext), tag = Base64Url.Encode(sealedRecord.Tag) });
    }
    private object DeviceInfo() => new { type = "device_info", node_id = _identity.NodeId, version = _firmware, device_id = _identity.DeviceId, mac = _identity.Mac.ToLowerInvariant(), model = "hatch_link", pairing_protocol = 5, pairing_auth = "none", pairing_auth_epoch = 0, pairing_policy = "confirm_app", build_sha = "", network_ready = true };
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = null };
    private static string String(JsonElement e, string name) => e.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() ?? "" : "";
    private static string Https(JsonElement e, string name) { var value = String(e, name); return value.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ? value : ""; }
    private static bool TryCounter(string? value, out ulong counter) => !string.IsNullOrWhiteSpace(value) && value.All(char.IsAsciiDigit) && ulong.TryParse(value, out counter);
    private void Reset() { _rx = _tx = null; _sessionId = ""; _rxCounter = _txCounter = 0; _waiting = _confirmed = false; }
}
