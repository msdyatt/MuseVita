// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "MuseVitaPair",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "musevita-pair", targets: ["MuseVitaPair"]),
    ],
    targets: [
        .executableTarget(name: "MuseVitaPair"),
        .testTarget(name: "MuseVitaPairTests", dependencies: ["MuseVitaPair"]),
    ]
)
