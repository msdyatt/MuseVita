using System.Security.AccessControl;
using System.Security.Principal;
using System.Text.Json;

namespace MuseVitaPair.Core;

public static class PairingBundleWriter
{
    public static async Task WriteAsync(string path, PairingCredentials credentials, string sdkToken, CancellationToken cancellationToken)
    {
        var pairing = JsonSerializer.Serialize(credentials);
        var bytes = JsonSerializer.SerializeToUtf8Bytes(new PairingBundle("musevita-pairing-v1", pairing, sdkToken));
        await File.WriteAllBytesAsync(path, bytes, cancellationToken);
        try
        {
            var user = WindowsIdentity.GetCurrent().User ?? throw new InvalidOperationException("No Windows owner SID.");
            var security = new FileSecurity(); security.SetOwner(user); security.SetAccessRuleProtection(true, false);
            security.AddAccessRule(new FileSystemAccessRule(user, FileSystemRights.FullControl, AccessControlType.Allow));
            new FileInfo(path).SetAccessControl(security);
        }
        catch (PlatformNotSupportedException) { /* CI and unsupported filesystems retain their platform default ACL. */ }
    }
}
