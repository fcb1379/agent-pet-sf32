import CoreBluetooth
import UIKit

@MainActor
final class WatchBLEManager: NSObject, ObservableObject {
    private enum ProtocolValue {
        static let linkService = CBUUID(string: "48535741-5443-485F-4C49-4E4B00000001")
        static let linkCharacteristic = CBUUID(string: "48535741-5443-485F-4C49-4E4B00000002")
        static let serialService = CBUUID(string: "7369666C-695F-7364-0000-000000000000")
        static let serialData = CBUUID(string: "7369666C-695F-7364-0002-000000000000")
        static let serialCategoryWatchface: UInt8 = 0x04
        static let backgroundFileType: UInt16 = 2
        static let phoneTypeIOS: UInt8 = 1
        static let chunkSize = 180
        static let maximumJPEGSize = 2 * 1024 * 1024 - 4
    }

    @Published private(set) var discoveredDevices: [CBPeripheral] = []
    @Published private(set) var connectedName = "未连接"
    @Published private(set) var imageStatus = "尚未选择"
    @Published private(set) var transferStatus = "等待连接"
    @Published private(set) var previewImage: UIImage?
    @Published private(set) var progress: Double = 0
    @Published private(set) var isUploading = false
    @Published var errorMessage: String?

    var isConnected: Bool { peripheral?.state == .connected && linkCharacteristic != nil && serialCharacteristic != nil }
    var isBluetoothUnavailable: Bool { central.state != .poweredOn }
    var canUpload: Bool { isConnected && preparedJPEG != nil && !isUploading }

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var linkCharacteristic: CBCharacteristic?
    private var serialCharacteristic: CBCharacteristic?
    private var preparedJPEG: Data?
    private var serialAssembly: (expected: Int, data: Data)?
    private var waitingResponses: [UInt16: (id: UUID, continuation: CheckedContinuation<Data, Error>)] = [:]

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func startScanning() {
        guard central.state == .poweredOn else { return }
        discoveredDevices.removeAll()
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func stopScanning() { central.stopScan() }

    func connect(to peripheral: CBPeripheral) {
        stopScanning()
        self.peripheral = peripheral
        peripheral.delegate = self
        central.connect(peripheral)
        transferStatus = "正在连接"
    }

    func prepareImage(from data: Data) {
        do {
            let result = try Self.makeBadgeJPEG(from: data)
            preparedJPEG = result.data
            previewImage = result.image
            imageStatus = "\(result.data.count / 1024) KB JPEG"
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func requestStatus() { sendCommand("badge") }
    func cancelTransfer() { sendCommand("badge cancel") }
    func clearBadge() { sendCommand("badge clear") }

    func uploadPreparedImage() async {
        guard let jpeg = preparedJPEG else { return }
        guard isConnected else { errorMessage = "请先连接手表"; return }

        isUploading = true
        progress = 0
        defer { isUploading = false }

        do {
            let padding = Data(repeating: 0, count: (4 - jpeg.count % 4) % 4)
            let payload = jpeg + padding
            let upload = payload + Self.u32(Self.crc32Mpeg2(payload))
            transferStatus = "建立传输"
            try sendSerial(Self.watchfaceMessage(0, Self.u16(ProtocolValue.backgroundFileType) + Data([ProtocolValue.phoneTypeIOS]) + Self.u32(UInt32(upload.count))))
            try Self.requireSuccess(try await waitForResponse(1))
            try sendSerial(Self.watchfaceMessage(2, Self.u32(UInt32(upload.count)) + Self.u16(9) + Data("badge.jpg".utf8)))
            try Self.requireSuccess(try await waitForResponse(3))

            var index: UInt32 = 0
            for offset in stride(from: 0, to: upload.count, by: ProtocolValue.chunkSize) {
                let end = min(offset + ProtocolValue.chunkSize, upload.count)
                try sendSerial(Self.watchfaceMessage(4, Self.u32(index) + Data(upload[offset..<end])))
                try Self.requireSuccess(try await waitForResponse(5))
                index += 1
                progress = Double(end) / Double(upload.count)
                transferStatus = "正在发送 \(end / 1024) KB"
            }
            try sendSerial(Self.watchfaceMessage(6, Data()))
            try Self.requireSuccess(try await waitForResponse(7))
            try sendSerial(Self.watchfaceMessage(8, Data()))
            try Self.requireSuccess(try await waitForResponse(9))
            progress = 1
            transferStatus = "已保存到手表"
        } catch {
            transferStatus = "传输失败"
            errorMessage = error.localizedDescription
        }
    }

    private func sendCommand(_ command: String) {
        guard let peripheral, let characteristic = linkCharacteristic else { return }
        peripheral.writeValue(Data(command.utf8), for: characteristic, type: .withResponse)
    }

    private func sendSerial(_ payload: Data) throws {
        guard let peripheral, let characteristic = serialCharacteristic else { throw BadgeError.notConnected }
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        let maximumPacket = peripheral.maximumWriteValueLength(for: writeType)
        let firstCapacity = max(1, maximumPacket - 4)
        let continuationCapacity = max(1, maximumPacket - 2)
        if payload.count <= firstCapacity {
            peripheral.writeValue(Data([ProtocolValue.serialCategoryWatchface, 0]) + Self.u16(UInt16(payload.count)) + payload, for: characteristic, type: writeType)
            return
        }
        var offset = firstCapacity
        peripheral.writeValue(Data([ProtocolValue.serialCategoryWatchface, 1]) + Self.u16(UInt16(payload.count)) + payload.prefix(firstCapacity), for: characteristic, type: writeType)
        while offset < payload.count {
            let end = min(offset + continuationCapacity, payload.count)
            let flag: UInt8 = end == payload.count ? 3 : 2
            peripheral.writeValue(Data([ProtocolValue.serialCategoryWatchface, flag]) + Data(payload[offset..<end]), for: characteristic, type: writeType)
            offset = end
        }
    }

    private func waitForResponse(_ id: UInt16) async throws -> Data {
        try await withCheckedThrowingContinuation { continuation in
            let token = UUID()
            waitingResponses[id] = (token, continuation)
            DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
                guard let response = self?.waitingResponses[id], response.id == token else { return }
                self?.waitingResponses[id] = nil
                response.continuation.resume(throwing: BadgeError.timeout)
            }
        }
    }

    private func handleSerial(_ value: Data) {
        guard value.count >= 2, value[0] == ProtocolValue.serialCategoryWatchface else { return }
        let flag = value[1]
        if flag == 0, value.count >= 4 {
            handleWatchfaceMessage(Data(value.dropFirst(4)))
        } else if flag == 1, value.count >= 4 {
            serialAssembly = (Int(Self.readU16(value, at: 2)), Data(value.dropFirst(4)))
        } else if var assembly = serialAssembly, flag == 2 || flag == 3 {
            assembly.data.append(contentsOf: value.dropFirst(2))
            serialAssembly = assembly
            if flag == 3 {
                serialAssembly = nil
                handleWatchfaceMessage(assembly.data.prefix(assembly.expected))
            }
        }
    }

    private func handleWatchfaceMessage(_ message: Data) {
        guard message.count >= 4 else { return }
        let id = Self.readU16(message, at: 0)
        guard let response = waitingResponses.removeValue(forKey: id) else { return }
        response.continuation.resume(returning: message)
    }

    private func resetConnection(_ message: String) {
        waitingResponses.values.forEach { $0.continuation.resume(throwing: BadgeError.disconnected) }
        waitingResponses.removeAll()
        serialAssembly = nil
        linkCharacteristic = nil
        serialCharacteristic = nil
        connectedName = "已断开"
        transferStatus = message
    }
}

extension WatchBLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state != .poweredOn { resetConnection("蓝牙不可用") }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard (peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? "").hasPrefix("Huangshan-Watch") else { return }
        if !discoveredDevices.contains(where: { $0.identifier == peripheral.identifier }) { discoveredDevices.append(peripheral) }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectedName = peripheral.name ?? "Huangshan Watch"
        peripheral.discoverServices([ProtocolValue.linkService, ProtocolValue.serialService])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        resetConnection("连接失败")
        errorMessage = error?.localizedDescription ?? "无法连接手表"
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        resetConnection("等待连接")
    }
}

extension WatchBLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        for service in peripheral.services ?? [] where service.uuid == ProtocolValue.linkService || service.uuid == ProtocolValue.serialService {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == ProtocolValue.linkCharacteristic {
                linkCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            } else if characteristic.uuid == ProtocolValue.serialData {
                serialCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            }
        }
        if isConnected { transferStatus = "已连接"; requestStatus() }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let value = characteristic.value else { return }
        if characteristic.uuid == ProtocolValue.linkCharacteristic {
            let response = String(decoding: value, as: UTF8.self)
            if response.hasPrefix("badge:") { transferStatus = response }
        } else if characteristic.uuid == ProtocolValue.serialData {
            handleSerial(value)
        }
    }
}

private extension WatchBLEManager {
    enum BadgeError: LocalizedError {
        case notConnected, timeout, disconnected, invalidResponse, rejected(UInt16), invalidImage
        var errorDescription: String? {
            switch self {
            case .notConnected: return "请先连接手表"
            case .timeout: return "手表响应超时"
            case .disconnected: return "手表已断开连接"
            case .invalidResponse: return "手表响应格式错误"
            case .rejected(let code): return "手表拒绝传输，错误码 \(code)"
            case .invalidImage: return "无法处理这张图片"
            }
        }
    }

    static func u16(_ value: UInt16) -> Data { withUnsafeBytes(of: value.littleEndian, { Data($0) }) }
    static func u32(_ value: UInt32) -> Data { withUnsafeBytes(of: value.littleEndian, { Data($0) }) }
    static func readU16(_ data: Data, at offset: Int) -> UInt16 { UInt16(data[offset]) | UInt16(data[offset + 1]) << 8 }

    static func watchfaceMessage(_ id: UInt16, _ data: Data) -> Data { u16(id) + u16(UInt16(data.count)) + data }

    static func requireSuccess(_ message: Data) throws {
        guard message.count >= 6 else { throw BadgeError.invalidResponse }
        let status = readU16(message, at: 4)
        if status != 0 { throw BadgeError.rejected(status) }
    }

    static func crc32Mpeg2(_ data: Data) -> UInt32 {
        data.reduce(UInt32(0xffff_ffff)) { crc, byte in
            (0..<8).reduce(crc ^ UInt32(byte) << 24) { partial, _ in
                partial & 0x8000_0000 != 0 ? (partial << 1) ^ 0x04c1_1db7 : partial << 1
            }
        }
    }

    static func makeBadgeJPEG(from source: Data) throws -> (data: Data, image: UIImage) {
        guard let original = UIImage(data: source) else { throw BadgeError.invalidImage }
        let side = min(original.size.width, original.size.height)
        let drawSize = CGSize(width: 240 * original.size.width / side, height: 240 * original.size.height / side)
        let drawOrigin = CGPoint(x: (240 - drawSize.width) / 2, y: (240 - drawSize.height) / 2)
        let renderer = UIGraphicsImageRenderer(size: CGSize(width: 240, height: 240))
        let result = renderer.image { _ in
            original.draw(in: CGRect(origin: drawOrigin, size: drawSize), blendMode: .copy, alpha: 1)
        }
        var quality: CGFloat = 0.9
        var jpeg = result.jpegData(compressionQuality: quality) ?? Data()
        while jpeg.count > ProtocolValue.maximumJPEGSize && quality > 0.35 {
            quality -= 0.1
            jpeg = result.jpegData(compressionQuality: quality) ?? Data()
        }
        guard !jpeg.isEmpty, jpeg.count <= ProtocolValue.maximumJPEGSize else { throw BadgeError.invalidImage }
        return (jpeg, result)
    }
}
