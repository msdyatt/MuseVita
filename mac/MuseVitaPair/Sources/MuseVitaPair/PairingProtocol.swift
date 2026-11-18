import Foundation
import CryptoKit

enum PairingProtocolError: Error {
    case invalidMessage
    case invalidHello
    case invalidRecord
    case decryptFailed
    case expired
    case notReady
}

struct PairingIdentity: Codable {
    let mac: String

    var compactSuffix: String {
        mac.replacingOccurrences(of: ":", with: "").suffix(6).lowercased()
    }

    var nodeID: String { "homelink-\(compactSuffix)" }
    var deviceID: String { "hatch-link:\(mac.lowercased())" }
    var bleName: String { "MuseGadget\(compactSuffix.uppercased())" }
}

struct PairingCredentials: Codable {
    let access_token: String
    let refresh_token: String
    let token_type: String
    let username: String
    let api_url: String
    let api_url_v2: String
    let noise_host: String
    let access_token_saved_at: Int
    let sdk_token_hint: String
}

struct PairingBundle: Codable {
    let format: String
    let pairing: String
    let sdk_token: String
}

enum Base64URL {
    static func encode(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }

    static func decode(_ text: String) -> Data? {
        guard !text.isEmpty, !text.contains("=") else { return nil }
        let converted = text.replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        let padded = converted + String(repeating: "=", count: (4 - converted.count % 4) % 4)
        return Data(base64Encoded: padded)
    }
}

private func concat(_ values: Data...) -> Data {
    values.reduce(into: Data()) { $0.append($1) }
}

private func bigEndianCounter(_ value: UInt64) -> Data {
    var n = value.bigEndian
    return withUnsafeBytes(of: &n) { Data($0) }
}

private func recordNonce(direction: UInt8, counter: UInt64) -> Data {
    concat(Data([direction, 0, 0, 0]), bigEndianCounter(counter))
}

private func recordAAD(sessionID: String, direction: UInt8, counter: UInt64) -> Data {
    let arrow = direction == 0 ? "m2d" : "d2m"
    return Data("hatch-link ble setup v1|\(sessionID)|\(arrow)|\(counter)".utf8)
}

private func hkdf(_ input: Data, salt: Data, info: Data) -> Data {
    let key = HKDF<SHA256>.deriveKey(
        inputKeyMaterial: SymmetricKey(data: input),
        salt: salt,
        info: info,
        outputByteCount: 32
    )
    return key.withUnsafeBytes { Data($0) }
}

private func hkdfExpand(_ input: Data, info: Data) -> Data {
    let key = HKDF<SHA256>.expand(
        pseudoRandomKey: SymmetricKey(data: input),
        info: info,
        outputByteCount: 32
    )
    return key.withUnsafeBytes { Data($0) }
}

private func sha256(_ data: Data) -> Data {
    Data(SHA256.hash(data: data))
}

struct PairingKeySchedule {
    let transcriptHash: Data
    let mobileTxKey: Data
    let mobileRxKey: Data
    let sessionID: Data
}

func derivePairingKeySchedule(
    deviceKey: P256.KeyAgreement.PrivateKey,
    identity: PairingIdentity,
    firmwareVersion: String,
    mobilePubData: Data,
    mobileNonce: Data,
    deviceNonce: Data,
    pairingPolicy: String = "confirm_app"
) throws -> PairingKeySchedule {
    let mobileKey = try P256.KeyAgreement.PublicKey(x963Representation: mobilePubData)
    let devicePubData = deviceKey.publicKey.x963Representation
    let transcript = [
        "hatch-link-pairing-v5",
        "version=5",
        "initiator_role=mobile",
        "responder_role=link",
        "device_id=\(identity.deviceID)",
        "node_id=\(identity.nodeID)",
        "mac=\(identity.mac.lowercased())",
        "model=hatch_link",
        "firmware_version=\(firmwareVersion)",
        "selected_cipher_suite=p256-hkdf-sha256-aes-gcm-v1",
        "pairing_auth=none",
        "pairing_auth_epoch=0",
        "pairing_policy=\(pairingPolicy)",
        "confirm_timeout_seconds=\(pairingPolicy == "confirm_press" ? 60 : 0)",
        "mobile_pub=\(Base64URL.encode(mobilePubData))",
        "device_pub=\(Base64URL.encode(devicePubData))",
        "mobile_nonce=\(Base64URL.encode(mobileNonce))",
        "device_nonce=\(Base64URL.encode(deviceNonce))",
    ].joined(separator: "\n")
    let transcriptHash = sha256(Data(transcript.utf8))
    let sharedSecret = try deviceKey.sharedSecretFromKeyAgreement(with: mobileKey).withUnsafeBytes { Data($0) }
    let salt = sha256(concat(mobileNonce, deviceNonce, transcriptHash))
    let sessionSecret = hkdf(sharedSecret, salt: salt, info: Data("hatch-link ble setup v1".utf8))
    let mobileTx = hkdfExpand(sessionSecret, info: Data("mobile->device".utf8))
    let mobileRx = hkdfExpand(sessionSecret, info: Data("device->mobile".utf8))
    let sessionID = sha256(concat(Data("hatch-link session id v1".utf8), transcriptHash, sharedSecret)).prefix(16)
    return PairingKeySchedule(transcriptHash: transcriptHash, mobileTxKey: mobileTx, mobileRxKey: mobileRx, sessionID: Data(sessionID))
}

final class PairingSession {
    private let identity: PairingIdentity
    private let firmwareVersion: String
    private let sdkToken: String
    private var rxKey: SymmetricKey?
    private var txKey: SymmetricKey?
    private var sessionID = ""
    private var rxCounter: UInt64 = 0
    private var txCounter: UInt64 = 0
    private var waitingForFinished = false
    private var confirmed = false
    var onProvision: ((PairingCredentials) -> Bool)?

    init(identity: PairingIdentity, firmwareVersion: String, sdkToken: String) {
        self.identity = identity
        self.firmwareVersion = firmwareVersion
        self.sdkToken = sdkToken
    }

    var isConfirmed: Bool { confirmed }

    func deviceInfo() -> [String: Any] {
        [
            "type": "device_info",
            "node_id": identity.nodeID,
            "version": firmwareVersion,
            "device_id": identity.deviceID,
            "mac": identity.mac.lowercased(),
            "model": "hatch_link",
            "pairing_protocol": 5,
            "pairing_auth": "none",
            "pairing_auth_epoch": 0,
            "pairing_policy": "confirm_app",
            "build_sha": "",
            "network_ready": true,
        ]
    }

    func handlePlaintext(_ data: Data) throws -> [Data] {
        guard let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let action = object["action"] as? String else {
            return []
        }

        if action == "get_device_info" {
            return try framed(object: deviceInfo(), mtu: 185)
        }
        if action == "pairing_client_hello" {
            return try handleHello(object)
        }
        if action == "pairing_encrypted" {
            return try handleEncrypted(object)
        }
        if action == "provision_v2" {
            throw PairingProtocolError.notReady
        }
        if waitingForFinished || confirmed {
            return []
        }
        return try status("error_unknown_action")
    }

    private func handleHello(_ message: [String: Any]) throws -> [Data] {
        guard let version = message["version"] as? Int, version == 5,
              message["pairing_auth"] as? String == "none",
              message["pairing_policy"] as? String == "confirm_app",
              let mobilePubText = message["mobile_pub"] as? String,
              let mobileNonceText = message["mobile_nonce"] as? String,
              let mobilePubData = Base64URL.decode(mobilePubText),
              let mobileNonce = Base64URL.decode(mobileNonceText),
              mobilePubData.count == 65, mobilePubData.first == 0x04,
              mobileNonce.count == 16 else {
            return try status("error_pairing_invalid_hello")
        }

        do { _ = try P256.KeyAgreement.PublicKey(x963Representation: mobilePubData) }
        catch { return try status("error_pairing_invalid_hello") }

        let deviceKey = P256.KeyAgreement.PrivateKey()
        let devicePubData = deviceKey.publicKey.x963Representation
        var deviceNonce = Data(count: 16)
        let randomStatus = deviceNonce.withUnsafeMutableBytes {
            SecRandomCopyBytes(kSecRandomDefault, 16, $0.baseAddress!)
        }
        guard randomStatus == errSecSuccess else { throw PairingProtocolError.invalidHello }

        let schedule = try derivePairingKeySchedule(
            deviceKey: deviceKey,
            identity: identity,
            firmwareVersion: firmwareVersion,
            mobilePubData: mobilePubData,
            mobileNonce: mobileNonce,
            deviceNonce: deviceNonce
        )

        rxKey = SymmetricKey(data: schedule.mobileTxKey)
        txKey = SymmetricKey(data: schedule.mobileRxKey)
        sessionID = Base64URL.encode(schedule.sessionID)
        rxCounter = 0
        txCounter = 0
        waitingForFinished = true
        confirmed = false

        let ready: [String: Any] = [
            "type": "pairing_ready",
            "version": 5,
            "device_id": identity.deviceID,
            "node_id": identity.nodeID,
            "mac": identity.mac.lowercased(),
            "model": "hatch_link",
            "firmware_version": firmwareVersion,
            "pairing_auth": "none",
            "pairing_auth_epoch": 0,
            "pairing_policy": "confirm_app",
            "device_pub": Base64URL.encode(devicePubData),
            "device_nonce": Base64URL.encode(deviceNonce),
            "transcript_hash": Base64URL.encode(schedule.transcriptHash),
            "session_id": sessionID,
        ]
        return try framed(object: ready, mtu: 185)
    }

    private func handleEncrypted(_ envelope: [String: Any]) throws -> [Data] {
        guard let key = rxKey,
              let sid = envelope["session_id"] as? String, sid == sessionID,
              let counterText = envelope["counter"] as? String,
              let counter = UInt64(counterText), counter == rxCounter,
              let ciphertextText = envelope["ciphertext"] as? String,
              let tagText = envelope["tag"] as? String,
              let ciphertext = Base64URL.decode(ciphertextText),
              let tag = Base64URL.decode(tagText), tag.count == 16 else {
            return try status("error_pairing_decrypt")
        }
        do {
            let nonce = try AES.GCM.Nonce(data: recordNonce(direction: 0, counter: counter))
            let box = try AES.GCM.SealedBox(nonce: nonce, ciphertext: ciphertext, tag: tag)
            let plaintext = try AES.GCM.open(box, using: key, authenticating: recordAAD(sessionID: sid, direction: 0, counter: counter))
            rxCounter += 1
            guard let command = try JSONSerialization.jsonObject(with: plaintext) as? [String: Any],
                  let action = command["action"] as? String else { throw PairingProtocolError.invalidRecord }
            if action == "pairing_client_finished" && command.count == 1 && waitingForFinished {
                waitingForFinished = false
                confirmed = true
                return try status("pairing_confirmed", sdkToken: sdkToken)
            }
            if action == "provision_v2" && confirmed {
                return try handleProvision(command)
            }
        } catch {
            waitingForFinished = false
            confirmed = false
            return try status("error_pairing_decrypt")
        }
        return []
    }

    private func handleProvision(_ command: [String: Any]) throws -> [Data] {
        guard let access = command["access_token"] as? String, !access.isEmpty,
              let refresh = command["refresh_token"] as? String, !refresh.isEmpty,
              command["token_type"] as? String == "device" else {
            return try status("error_missing_credentials")
        }
        let output = PairingCredentials(
            access_token: access,
            refresh_token: refresh,
            token_type: "device",
            username: command["username"] as? String ?? "",
            api_url: (command["api_url"] as? String ?? "").hasPrefix("https://") ? (command["api_url"] as? String ?? "") : "",
            api_url_v2: (command["api_url_v2"] as? String ?? "").hasPrefix("https://") ? (command["api_url_v2"] as? String ?? "") : "",
            noise_host: command["noise_host"] as? String ?? "",
            access_token_saved_at: Int(Date().timeIntervalSince1970),
            sdk_token_hint: String(sdkToken.prefix(12))
        )
        guard onProvision?(output) ?? false else {
            return try status("auth_failed")
        }
        return try status("auth_ok")
    }

    private func status(_ value: String, sdkToken: String? = nil) throws -> [Data] {
        var message: [String: Any] = ["type": "status", "status": value]
        if let sdkToken, value == "pairing_confirmed" { message["sdk_token"] = sdkToken }
        return try encrypt(object: message, generationRequired: value == "auth_ok")
    }

    private func encrypt(object: [String: Any], generationRequired: Bool) throws -> [Data] {
        guard let key = txKey, !sessionID.isEmpty else { return [] }
        let plaintext = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        let counter = txCounter
        let nonce = try AES.GCM.Nonce(data: recordNonce(direction: 1, counter: counter))
        let sealed = try AES.GCM.seal(plaintext, using: key, nonce: nonce, authenticating: recordAAD(sessionID: sessionID, direction: 1, counter: counter))
        txCounter += 1
        let envelope: [String: Any] = [
            "type": "pairing_encrypted",
            "session_id": sessionID,
            "counter": String(counter),
            "ciphertext": Base64URL.encode(sealed.ciphertext),
            "tag": Base64URL.encode(sealed.tag),
        ]
        return try framed(object: envelope, mtu: 185)
    }

    private func framed(object: [String: Any], mtu: Int) throws -> [Data] {
        let data = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        let maximum = min(max(mtu - 3, 20), 160)
        let usable = maximum - 3
        let pieces = stride(from: 0, to: max(data.count, 1), by: usable).map { start in
            Data(data[start..<min(start + usable, data.count)])
        }
        let chunks = pieces.isEmpty ? [Data()] : pieces
        guard chunks.count <= 255 else { throw PairingProtocolError.invalidMessage }
        return chunks.enumerated().map { index, piece in
            Data([0xFE, UInt8(index), UInt8(chunks.count)]) + piece
        }
    }
}

