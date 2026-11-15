using MuseVitaPair.Core;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Devices.Bluetooth;
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
    private GattServiceProvider? _provider; private GattLocalCharacteristic? _rx; private GattLocalCharacteristic? _tx;
    private bool _serviceAdvertising;

    public WindowsBlePairingServer(PairingSession session) => _session = session;
    public async Task StartAsync()
    {
        var stage = "creating the local GATT service";
        try
        {
            var provider = await GattServiceProvider.CreateAsync(ServiceUuid);
            if (provider.Error != BluetoothError.Success) throw new InvalidOperationException($"Windows returned {provider.Error}.");
            _provider = provider.ServiceProvider;
            stage = "adding the writable pairing characteristic";
            var rxResult = await _provider.Service.CreateCharacteristicAsync(RxUuid, new GattLocalCharacteristicParameters { CharacteristicProperties = GattCharacteristicProperties.Write | GattCharacteristicProperties.WriteWithoutResponse, WriteProtectionLevel = GattProtectionLevel.Plain });
            if (rxResult.Error != BluetoothError.Success) throw new InvalidOperationException($"Windows returned {rxResult.Error}.");
            stage = "adding the notify pairing characteristic";
            var txResult = await _provider.Service.CreateCharacteristicAsync(TxUuid, new GattLocalCharacteristicParameters { CharacteristicProperties = GattCharacteristicProperties.Read | GattCharacteristicProperties.Notify, ReadProtectionLevel = GattProtectionLevel.Plain });
            if (txResult.Error != BluetoothError.Success) throw new InvalidOperationException($"Windows returned {txResult.Error}.");
            _rx = rxResult.Characteristic; _tx = txResult.Characteristic; _rx.SubscribedClientsChanged += (_, _) => { }; _rx.WriteRequested += WriteRequested; _tx.ReadRequested += ReadRequested;
            stage = "starting connectable GATT advertising";
            _provider.StartAdvertising(new GattServiceProviderAdvertisingParameters { IsConnectable = true, IsDiscoverable = true }); _serviceAdvertising = true;
        }
        catch (Exception error)
        {
            await DisposeAsync();
            throw new InvalidOperationException($"Windows BLE setup failed while {stage}: {error.Message}", error);
        }
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
        try { if (_serviceAdvertising) _provider?.StopAdvertising(); } catch { }
        if (_rx is not null) _rx.WriteRequested -= WriteRequested; if (_tx is not null) _tx.ReadRequested -= ReadRequested; return ValueTask.CompletedTask;
    }
}
