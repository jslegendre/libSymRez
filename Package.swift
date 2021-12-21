// swift-tools-version:5.5
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "SymRez",
    platforms: [
        .macOS(.v10_10),
    ],
    products: [
        .library(
            name: "SymRez",
            targets: ["SymRez"]),
    ],
    targets: [
        .target(
            name: "SymRez",
            dependencies: [],
            path: "Sources"),
        .testTarget(
            name: "SymRezTests",
            dependencies: ["SymRez"],
            path: "Tests"),
    ]
)
