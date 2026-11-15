using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MuseVitaPair.Core;
using Xunit;

namespace MuseVitaPair.Tests;

public sealed class PairingProtocolTests
{
    private static readonly PairingIdentity Identity = new("02:00:00:00:00:01");
    private const string MobilePublic = "BGsX0fLhLEJH-Lzm5WOkQPJ3A32BLeszoPShOUXYmMKWT-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU";
    private const string DevicePublic = "BHzyexiNA09-ilI4AwS1GsPAiWnid_IbNaYLSPxHZpl4B3dVENuO0EApPZrGn3Qw27p9reY86YIpngS3nSJ4c9E";

    [Fact] public void IdentityDerivationMatchesMuseGadgetConvention() { Assert.Equal("homelink-000001", Identity.NodeId); Assert.Equal("hatch-link:02:00:00:00:00:01", Identity.DeviceId); Assert.Equal("MuseGadget000001", Identity.BleName); }
    [Fact] public void Base64UrlIsUnpaddedAndStrict() { Assert.Equal("-_8", Base64Url.Encode(new byte[] { 0xfb, 0xff })); Assert.Equal(new byte[] { 0xfb, 0xff }, Base64Url.Decode("-_8")); Assert.Throws<FormatException>(() => Base64Url.Decode("ab=c")); }
    [Fact] public void BleFramingRoundTripsAndRejectsOutOfOrderChunks()
    {
        var data = Enumerable.Range(0, 600).Select(x => (byte)x).ToArray(); var chunks = BleFraming.Encode(data, 23); var reader = new ChunkAssembler();
        Assert.Equal(data, chunks.Select(reader.Feed).Last()); var bad = new ChunkAssembler(); Assert.Null(bad.Feed(chunks[0])); Assert.Null(bad.Feed(chunks[2]));
    }
    [Fact] public void CommunityAppV5PublishedKeyScheduleMatches()
    {
        var mobilePub = Base64Url.Decode(MobilePublic); var devicePub = Base64Url.Decode(DevicePublic); var mobileNonce = Base64Url.Decode("AAECAwQFBgcICQoLDA0ODw"); var deviceNonce = Base64Url.Decode("EBESExQVFhcYGRobHB0eHw");
        using var device = ECDiffieHellman.Create(new ECParameters { Curve = ECCurve.NamedCurves.nistP256, D = Scalar(2), Q = new ECPoint { X = devicePub[1..33], Y = devicePub[33..] } });
        using var mobile = PairingProtocol.ParseX963PublicKey(mobilePub); var schedule = PairingProtocol.DeriveKeySchedule(PairingProtocol.RawSecret(device, mobile), Identity, "1.0.0", mobilePub, devicePub, mobileNonce, deviceNonce);
        Assert.Equal("18Rs196tzddPGuj18pZ8rWObWYpwoDUIwocjnUASj_0", Base64Url.Encode(schedule.TranscriptHash));
        Assert.Equal("ee25e7f1eb3c05cc8465634e5f634deed858862b1c4c35fa434dc926f7facaeb", Convert.ToHexString(schedule.MobileTxKey).ToLowerInvariant());
        Assert.Equal("9abb2954b05e2f5f7112e9bfd41fd970602e27ed07453c1cff51f575de5d0b4e", Convert.ToHexString(schedule.MobileRxKey).ToLowerInvariant()); Assert.Equal("z0eNLGw5mczvD4a1F2ubSQ", Base64Url.Encode(schedule.SessionId));
    }
    [Fact] public void CommunityAppV5EncryptedRecordDecrypts()
    {
        var key = Convert.FromHexString("ee25e7f1eb3c05cc8465634e5f634deed858862b1c4c35fa434dc926f7facaeb");
        var plaintext = PairingProtocol.Decrypt(key, "z0eNLGw5mczvD4a1F2ubSQ", 0, 0, Base64Url.Decode("uwNX3KEkix5T6nWce3s2SKPPh-5E31x-UIHFSK6MpMUg562B"), Base64Url.Decode("zT4kU6XK-rM7rvt3VEITLg"));
        Assert.Equal("{\"action\":\"pairing_client_finished\"}", Encoding.UTF8.GetString(plaintext));
    }
    [Fact] public void PairingBundleHasExactOneFileFormat()
    {
        var credentials = new PairingCredentials("access", "refresh", "device", "", "", "", "", 1, "mgst_test"); var json = JsonSerializer.Serialize(new PairingBundle("musevita-pairing-v1", JsonSerializer.Serialize(credentials), "mgst_test"));
        using var doc = JsonDocument.Parse(json); Assert.Equal("musevita-pairing-v1", doc.RootElement.GetProperty("format").GetString()); Assert.Equal(JsonValueKind.String, doc.RootElement.GetProperty("pairing").ValueKind);
    }
    [Theory][InlineData("")][InlineData("nope")][InlineData("02:00:00:00:00:zz")]
    public void InvalidIdentityIsRejected(string mac) => Assert.Throws<InvalidDataException>(() => new PairingIdentity(mac).Validate());
    private static byte[] Scalar(byte value) { var bytes = new byte[32]; bytes[^1] = value; return bytes; }
}
