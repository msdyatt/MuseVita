import XCTest
import CryptoKit
@testable import MuseVitaPair

final class PairingProtocolTests: XCTestCase {
    func testIdentityDerivationMatchesMuseGadgetConventions() {
        let identity = PairingIdentity(mac: "02:00:00:00:00:01")
        XCTAssertEqual(identity.nodeID, "homelink-000001")
        XCTAssertEqual(identity.deviceID, "hatch-link:02:00:00:00:00:01")
        XCTAssertEqual(identity.bleName, "MuseGadget000001")
    }

    func testBase64URLRoundTrip() {
        let value = Data([0xfb, 0xff, 0x00, 0x01])
        XCTAssertEqual(Base64URL.decode(Base64URL.encode(value)), value)
    }

    func testPublishedCommunityV5KeyScheduleVector() throws {
        let identity = PairingIdentity(mac: "02:00:00:00:00:01")
        let mobilePublic = try XCTUnwrap(Base64URL.decode("BGsX0fLhLEJH-Lzm5WOkQPJ3A32BLeszoPShOUXYmMKWT-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU"))
        let mobileNonce = try XCTUnwrap(Base64URL.decode("AAECAwQFBgcICQoLDA0ODw"))
        let deviceNonce = try XCTUnwrap(Base64URL.decode("EBESExQVFhcYGRobHB0eHw"))
        let deviceKey = try P256.KeyAgreement.PrivateKey(rawRepresentation: Data(repeating: 0, count: 31) + Data([2]))
        let schedule = try derivePairingKeySchedule(
            deviceKey: deviceKey,
            identity: identity,
            firmwareVersion: "1.0.0",
            mobilePubData: mobilePublic,
            mobileNonce: mobileNonce,
            deviceNonce: deviceNonce,
            pairingPolicy: "confirm_app"
        )
        XCTAssertEqual(Base64URL.encode(schedule.transcriptHash), "18Rs196tzddPGuj18pZ8rWObWYpwoDUIwocjnUASj_0")
        XCTAssertEqual(schedule.mobileTxKey.map { String(format: "%02x", $0) }.joined(), "ee25e7f1eb3c05cc8465634e5f634deed858862b1c4c35fa434dc926f7facaeb")
        XCTAssertEqual(schedule.mobileRxKey.map { String(format: "%02x", $0) }.joined(), "9abb2954b05e2f5f7112e9bfd41fd970602e27ed07453c1cff51f575de5d0b4e")
        XCTAssertEqual(Base64URL.encode(schedule.sessionID), "z0eNLGw5mczvD4a1F2ubSQ")
    }
}
