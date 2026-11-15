namespace MuseVitaPair.Core;

public sealed class ChunkAssembler
{
    private readonly int _maximum;
    private readonly MemoryStream _buffer = new();
    private byte _total;
    private byte _next;
    public ChunkAssembler(int maximum = 16 * 1024) => _maximum = maximum;

    public byte[]? Feed(ReadOnlySpan<byte> packet)
    {
        if (packet.Length < 3 || packet[0] != 0xfe)
            return _buffer.Length == 0 ? packet.ToArray() : Reset();
        var index = packet[1]; var total = packet[2];
        if (index == 0 || total != _total) { _buffer.SetLength(0); _total = total; _next = 0; }
        if (total == 0 || index != _next || index >= total || _buffer.Length + packet.Length - 3 > _maximum) return Reset();
        _buffer.Write(packet[3..]); _next++;
        if (_next != _total) return null;
        var complete = _buffer.ToArray(); _buffer.SetLength(0); _total = _next = 0; return complete;
    }
    private byte[]? Reset() { _buffer.SetLength(0); _total = _next = 0; return null; }
}

public static class BleFraming
{
    public static IReadOnlyList<byte[]> Encode(ReadOnlySpan<byte> message, int mtu = 185)
    {
        var payload = Math.Min(Math.Max(mtu - 3, 20), 160) - 3;
        var count = Math.Max(1, (message.Length + payload - 1) / payload);
        if (count > byte.MaxValue) throw new ArgumentException("Message requires too many BLE chunks.");
        var result = new List<byte[]>(count);
        for (var i = 0; i < count; i++)
        {
            var length = Math.Min(payload, message.Length - i * payload);
            var output = new byte[length + 3]; output[0] = 0xfe; output[1] = (byte)i; output[2] = (byte)count;
            message.Slice(i * payload, length).CopyTo(output.AsSpan(3)); result.Add(output);
        }
        return result;
    }
}
