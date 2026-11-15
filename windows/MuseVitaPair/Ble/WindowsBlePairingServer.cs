using MuseVitaPair.Core;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace MuseVitaPair.Ble;

/// <summary>Windows Runtime GATT peripheral implementation. It needs package identity and the bluetooth DeviceCapability.</summary>
public sealed class WindowsBlePairingServer : IAsyncDisposable
{
    public static readonly Guid ServiceUuid = Guid.Parse("7FDD3D1C-38EA-46CF-8B46-314ECF5F240C");
    public static readonly Guid RxUuid = Guid.Parse("4D593029-28A2-4A6E-A1F0-3C2D5E8F9B01");
    public static readonly Guid TxUuid = Guid.Parse("D75DC4CA-7B2B-4E9C-8F0A-1D2E3F4A5B6C");
    private readonly PairingSession _session;
    private readonly ChunkAssembler _assembler = new();
    private GattServiceProvider? _provider; private GattLocalCharacteristic? _rx; private GattLocalCharacteristic? _tx; private BluetoothLEAdvertisementPublisher? _publisher;

    public WindowsBlePairingServer(PairingSession session) => _session = session;
    public async Task StartAsync(string name)
    {
        var provider = await GattServiceProvider.CreateAsync(ServiceUuid);
        if (provider.Error != BluetoothError.Success) throw new InvalidOperationException($"Windows could not create a GATT server ({provider.Error}). This adapter may not support peripheral mode.");
        _provider = provider.ServiceProvider;
        var rxResult = await _provider.Service.CreateCharacteristicAsync(RxUuid, new GattLocalCharacteristicParameters { CharacteristicProperties = GattCharacteristicProperties.Write | GattCharacteristicProperties.WriteWithoutResponse, WriteProtectionLevel = GattProtectionLevel.Plain });
        var txResult = await _provider.Service.CreateCharacteristicAsync(TxUuid, new GattLocalCharacteristicParameters { CharacteristicProperties = GattCharacteristicProperties.Read | GattCharacteristicProperties.Notify, ReadProtectionLevel = GattProtectionLevel.Plain });
        if (rxResult.Error != BluetoothError.Success || txResult.Error != BluetoothError.Success) throw new InvalidOperationException("Windows could not create the Muse Gadget characteristics.");
        _rx = rxResult.Characteristic; _tx = txResult.Characteristic; _rx.SubscribedClientsChanged += (_, _) => { }; _rx.WriteRequested += WriteRequested; _tx.ReadRequested += ReadRequested;
        _provider.StartAdvertising(new GattServiceProviderAdvertisingParameters { IsConnectable = true, IsDiscoverable = true });
        // The service provider advertises the connectable GATT service. This publisher supplies the SDK's informational 0xffff:00 manufacturer record.
        _publisher = new BluetoothLEAdvertisementPublisher(); _publisher.Advertisement.LocalName = name; _publisher.Advertisement.ServiceUuids.Add(ServiceUuid);
        _publisher.Advertisement.ManufacturerData.Add(new BluetoothLEManufacturerData { CompanyId = 0xffff, Data = new byte[] { 0 }.AsBuffer() }); _publisher.Start();
    }
    private async void WriteRequested(GattLocalCharacteristic sender, GattWriteRequestedEventArgs args)
    {
        var deferral = args.GetDeferral();
        try
        {
            var request = await args.GetRequestAsync(); if (request is null) return;
            var packet = request.Value.ToArray(); request.Respond(); var message = _assembler.Feed(packet); if (message is null || _tx is null) return;
            foreach (var response in await _session.HandleAsync(message)) await _tx.NotifyValueAsync(response.AsBuffer());
        }
        catch { /* Invalid peer packets intentionally produce no OS-level details or secret-bearing logs. */ }
        finally { deferral.Complete(); }
    }
    private async void ReadRequested(GattLocalCharacteristic sender, GattReadRequestedEventArgs args)
    {
        var deferral = args.GetDeferral(); try { var request = await args.GetRequestAsync(); request?.RespondWithValue(Array.Empty<byte>().AsBuffer()); } finally { deferral.Complete(); }
    }
    public ValueTask DisposeAsync()
    {
        _publisher?.Stop(); _provider?.StopAdvertising(); if (_rx is not null) _rx.WriteRequested -= WriteRequested; if (_tx is not null) _tx.ReadRequested -= ReadRequested; return ValueTask.CompletedTask;
    }
}
