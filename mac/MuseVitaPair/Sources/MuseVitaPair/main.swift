import Foundation
import CoreBluetooth
import Darwin

let serviceUUID = CBUUID(string: "7FDD3D1C-38EA-46CF-8B46-314ECF5F240C")
let rxUUID = CBUUID(string: "4D593029-28A2-4A6E-A1F0-3C2D5E8F9B01")
let txUUID = CBUUID(string: "D75DC4CA-7B2B-4E9C-8F0A-1D2E3F4A5B6C")

struct Options {
    var vitaURL: URL?
    var identityFile: URL?
    var sdkTokenFile: URL?
    var outputFile = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        .appendingPathComponent("musevita-pairing.json")
}

func fail(_ message: String) -> Never {
    fputs("MuseVita pairing: \(message)\n", stderr)
    exit(1)
}

func usage() -> Never {
    print("""
    Usage: musevita-pair --vita-url http://VITA-IP:8768/ [--sdk-token-file PATH] [--output PATH]
           musevita-pair --identity-file PATH [--sdk-token-file PATH] [--output PATH]

    The SDK token is read from MUSEGADGET_SDK_TOKEN, --sdk-token-file, or a hidden prompt.
    It is never printed or placed on the command line.
    """)
    exit(0)
}

func parseOptions() -> Options {
    var result = Options()
    var args = Array(CommandLine.arguments.dropFirst())
    while !args.isEmpty {
        let arg = args.removeFirst()
        switch arg {
        case "--vita-url":
            guard let value = args.first, let url = URL(string: value), url.scheme == "http", url.host != nil else { fail("--vita-url must be a local http:// URL") }
            args.removeFirst(); result.vitaURL = url
        case "--identity-file":
            guard let value = args.first else { fail("--identity-file needs a path") }
            args.removeFirst(); result.identityFile = URL(fileURLWithPath: value)
        case "--sdk-token-file":
            guard let value = args.first else { fail("--sdk-token-file needs a path") }
            args.removeFirst(); result.sdkTokenFile = URL(fileURLWithPath: value)
        case "--output":
            guard let value = args.first else { fail("--output needs a path") }
            args.removeFirst(); result.outputFile = URL(fileURLWithPath: value)
        case "--help", "-h": usage()
        default: fail("unknown option \(arg)")
        }
    }
    guard result.vitaURL != nil || result.identityFile != nil else { fail("provide --vita-url or --identity-file") }
    return result
}

func validSDKToken(_ value: String) -> Bool {
    value.range(of: #"^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$"#, options: .regularExpression) != nil
}

func readSDKToken(_ options: Options) -> String {
    let value: String
    if let env = ProcessInfo.processInfo.environment["MUSEGADGET_SDK_TOKEN"], !env.isEmpty {
        value = env.trimmingCharacters(in: .whitespacesAndNewlines)
    } else if let file = options.sdkTokenFile, let text = try? String(contentsOf: file, encoding: .utf8) {
        value = text.trimmingCharacters(in: .whitespacesAndNewlines)
    } else {
        guard let hidden = getpass("SDK token (hidden): ") else { fail("could not read SDK token") }
        value = String(cString: hidden).trimmingCharacters(in: .whitespacesAndNewlines)
    }
    guard validSDKToken(value) else { fail("SDK token format is invalid; copy it from gadgets.muse.ai") }
    return value
}

func readIdentity(_ options: Options) -> PairingIdentity {
    do {
        let data: Data
        if let url = options.vitaURL {
            let redirectSemaphore = DispatchSemaphore(value: 0)
            var redirectLocation: URL?
            let delegate = RedirectCapture { redirectLocation = $0; redirectSemaphore.signal() }
            let configuration = URLSessionConfiguration.ephemeral
            let session = URLSession(configuration: configuration, delegate: delegate, delegateQueue: nil)
            session.dataTask(with: url).resume()
            redirectSemaphore.wait()
            guard let pairURL = redirectLocation,
                  pairURL.host == url.host,
                  pairURL.path == "/pair",
                  let sid = URLComponents(url: pairURL, resolvingAgainstBaseURL: false)?.queryItems?.first(where: { $0.name == "s" })?.value else {
                fail("Vita setup URL did not provide a pairing session")
            }
            var identityComponents = URLComponents(url: url, resolvingAgainstBaseURL: false)!
            identityComponents.path = "/pair/identity"
            identityComponents.queryItems = [URLQueryItem(name: "s", value: sid)]
            let endpoint = identityComponents.url!
            let semaphore = DispatchSemaphore(value: 0)
            var result: Result<Data, Error>!
            URLSession.shared.dataTask(with: endpoint) { identityData, response, error in
                defer { semaphore.signal() }
                if let error { result = .failure(error); return }
                guard let identityData,
                      (response as? HTTPURLResponse)?.statusCode == 200 else {
                    result = .failure(NSError(domain: "MuseVitaPair", code: 1, userInfo: [NSLocalizedDescriptionKey: "HTTP request failed"]))
                    return
                }
                result = .success(identityData)
            }.resume()
            semaphore.wait()
            data = try result.get()
        } else {
            data = try Data(contentsOf: options.identityFile!)
        }
        let identity = try JSONDecoder().decode(PairingIdentity.self, from: data)
        guard identity.mac.range(of: #"^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$"#, options: .regularExpression) != nil else { fail("identity has an invalid MAC-shaped identity") }
        return identity
    } catch {
        fail("could not read Vita identity: \(error.localizedDescription)")
    }
}

final class RedirectCapture: NSObject, URLSessionTaskDelegate {
    private let callback: (URL?) -> Void
    private var completed = false
    init(callback: @escaping (URL?) -> Void) { self.callback = callback }

    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        guard !completed else { completionHandler(nil); return }
        completed = true
        callback(request.url)
        completionHandler(nil)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        guard !completed else { return }
        completed = true
        callback(nil)
    }
}

final class PairingServer: NSObject, CBPeripheralManagerDelegate {
    private let options: Options
    private let identity: PairingIdentity
    private let sdkToken: String
    private let session: PairingSession
    private var manager: CBPeripheralManager!
    private let rx = CBMutableCharacteristic(type: rxUUID, properties: [.write, .writeWithoutResponse], value: nil, permissions: [.writeable])
    private let tx = CBMutableCharacteristic(type: txUUID, properties: [.read, .notify], value: nil, permissions: [.readable])
    private var central: CBCentral?
    private var queuedNotifications: [Data] = []
    private var assembler = Data()
    private var expectedChunks: UInt8 = 0
    private var nextChunk: UInt8 = 0

    init(options: Options, identity: PairingIdentity, sdkToken: String) {
        self.options = options
        self.identity = identity
        self.sdkToken = sdkToken
        self.session = PairingSession(identity: identity, firmwareVersion: "0.1.0", sdkToken: sdkToken)
        super.init()
        self.session.onProvision = { [weak self] credentials in
            self?.validateAndSave(credentials) ?? false
        }
        manager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            if peripheral.state == .unsupported { fail("this Mac cannot advertise Bluetooth LE") }
            if peripheral.state == .unauthorized { fail("Bluetooth permission is denied for this app") }
            return
        }
        let service = CBMutableService(type: serviceUUID, primary: true)
        service.characteristics = [rx, tx]
        peripheral.add(service)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        if let error { fail("could not create Muse Gadget BLE service: \(error.localizedDescription)") }
        peripheral.startAdvertising([
            CBAdvertisementDataLocalNameKey: identity.bleName,
            CBAdvertisementDataServiceUUIDsKey: [serviceUUID],
            CBAdvertisementDataManufacturerDataKey: Data([0xff, 0xff, 0x00]),
        ])
        print("Pairing is open as \(identity.bleName). In the Muse app: Settings > Devices > Developer mode > Add Device.")
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        request.value = Data()
        peripheral.respond(to: request, withResult: .success)
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        for request in requests {
            if request.characteristic.uuid == rxUUID, let value = request.value { receive(value) }
            peripheral.respond(to: request, withResult: .success)
        }
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didSubscribeTo characteristic: CBCharacteristic) {
        guard characteristic.uuid == txUUID else { return }
        self.central = central
        flush()
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom characteristic: CBCharacteristic) {
        if characteristic.uuid == txUUID { self.central = nil }
    }

    func peripheralManagerIsReady(toUpdateSubscribers peripheral: CBPeripheralManager) { flush() }

    private func receive(_ packet: Data) {
        if packet.count < 3 || packet.first != 0xFE {
            process(assembler.isEmpty ? packet : Data())
            assembler.removeAll(keepingCapacity: true)
            return
        }
        let index = packet[packet.startIndex.advanced(by: 1)]
        let total = packet[packet.startIndex.advanced(by: 2)]
        if index == 0 || total != expectedChunks { assembler.removeAll(keepingCapacity: true); expectedChunks = total; nextChunk = 0 }
        guard total > 0, index == nextChunk, index < total else { assembler.removeAll(); expectedChunks = 0; nextChunk = 0; return }
        assembler.append(packet.dropFirst(3)); nextChunk += 1
        if nextChunk == expectedChunks {
            let message = assembler
            assembler.removeAll(keepingCapacity: true); expectedChunks = 0; nextChunk = 0
            process(message)
        }
    }

    private func process(_ message: Data) {
        do {
            let packets = try session.handlePlaintext(message)
            queuedNotifications.append(contentsOf: packets)
            flush()
        } catch { }
    }

    private func flush() {
        guard let central else { return }
        while !queuedNotifications.isEmpty {
            let packet = queuedNotifications[0]
            guard manager.updateValue(packet, for: tx, onSubscribedCentrals: [central]) else { return }
            queuedNotifications.removeFirst()
        }
    }

    private func validateAndSave(_ credentials: PairingCredentials) -> Bool {
        let root = (credentials.api_url_v2.isEmpty ? "https://api.muse.ai" : credentials.api_url_v2).trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard let endpoint = URL(string: root + "/fetch_vms") else { return false }
        let semaphore = DispatchSemaphore(value: 0)
        var valid = false
        var request = URLRequest(url: endpoint)
        request.httpMethod = "GET"
        request.setValue("Bearer \(credentials.access_token)", forHTTPHeaderField: "Authorization")
        request.setValue("1.0.0", forHTTPHeaderField: "X-API-Version")
        request.setValue("MuseVitaPair/0.1.0 (macOS)", forHTTPHeaderField: "User-Agent")
        URLSession.shared.dataTask(with: request) { data, response, _ in
            defer { semaphore.signal() }
            guard let data, (response as? HTTPURLResponse)?.statusCode == 200,
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  object["error_title"] == nil,
                  object["backend_error_code"] == nil,
                  let vmList = object["vm_list"] as? [Any], !vmList.isEmpty else { return }
            valid = true
        }.resume()
        semaphore.wait()
        guard valid else {
            fputs("MuseVita pairing: Muse rejected the returned device credentials.\n", stderr)
            return false
        }
        do {
            let pairingData = try JSONEncoder().encode(credentials)
            let pairingText = String(data: pairingData, encoding: .utf8)!
            let bundle = PairingBundle(format: "musevita-pairing-v1", pairing: pairingText, sdk_token: sdkToken)
            let data = try JSONEncoder().encode(bundle)
            try data.write(to: options.outputFile, options: [.atomic])
            try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: options.outputFile.path)
            print("Pairing complete. Wrote \(options.outputFile.path)")
            print("The file contains live credentials; keep it private and import it into MuseVita only.")
            manager.stopAdvertising()
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { exit(0) }
            return true
        } catch { fail("could not write pairing file: \(error.localizedDescription)") }
    }
}

let options = parseOptions()
let identity = readIdentity(options)
let token = readSDKToken(options)
print("Using Vita identity \(identity.nodeID). The token is kept in memory and will not be printed.")
let server = PairingServer(options: options, identity: identity, sdkToken: token)
withExtendedLifetime(server) {
    RunLoop.current.run()
}
