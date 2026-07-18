import CoreBluetooth
import UIKit

@MainActor
final class WatchBLEManager: NSObject, ObservableObject {
    private enum ProtocolValue {
        static let linkService = CBUUID(string: "01000000-4B4E-494C-5F48-435441575348")
        static let linkCharacteristic = CBUUID(string: "02000000-4B4E-494C-5F48-435441575348")
        static let serialService = CBUUID(string: "00000000-0000-0000-6473-5F696C666973")
        static let serialData = CBUUID(string: "00000000-0000-0200-6473-5F696C666973")
        static let serialCategoryWatchface: UInt8 = 0x04
        static let controlVersion = "HWS1"
        static let backgroundFileType: UInt16 = 2
        static let phoneTypeIOS: UInt8 = 1
        static let chunkSize = 180
        static let maximumJPEGSize = 2 * 1024 * 1024 - 4
    }

    @Published private(set) var discoveredDevices: [CBPeripheral] = []
    @Published private(set) var connectedName = "未连接"
    @Published private(set) var imageStatus = "尚未选择"
    @Published private(set) var transferStatus = "等待连接"
    @Published private(set) var timeStatus = "等待同步"
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
    private var waitingControls: [Int: (id: UUID, continuation: CheckedContinuation<String, Error>)] = [:]
    private var serialReadyWaiter: (id: UUID, continuation: CheckedContinuation<Void, Error>)?
    private var serialWriteWaiter: (id: UUID, continuation: CheckedContinuation<Void, Error>)?
    private var nextControlRequestId = 1
    private var linkNotificationsEnabled = false
    private var serialNotificationsEnabled = false
    private var didBootstrapControlPlane = false

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

    func requestStatus() { Task { await requestBadgeStatus() } }
    func cancelTransfer() { Task { await runBadgeAction("CANCEL") } }
    func clearBadge() { Task { await runBadgeAction("CLEAR") } }

    func syncTime() { Task { await synchronizeTime() } }

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
            try Self.requireSuccess(try await sendSerialRequest(Self.watchfaceMessage(0, Self.u16(ProtocolValue.backgroundFileType) + Data([ProtocolValue.phoneTypeIOS]) + Self.u32(UInt32(upload.count))), responseId: 1))
            try Self.requireSuccess(try await sendSerialRequest(Self.watchfaceMessage(2, Self.u32(UInt32(upload.count)) + Self.u16(9) + Data("badge.jpg".utf8)), responseId: 3))

            var index: UInt32 = 0
            for offset in stride(from: 0, to: upload.count, by: ProtocolValue.chunkSize) {
                let end = min(offset + ProtocolValue.chunkSize, upload.count)
                try Self.requireSuccess(try await sendSerialRequest(Self.watchfaceMessage(4, Self.u32(index) + Data(upload[offset..<end])), responseId: 5))
                index += 1
                progress = Double(end) / Double(upload.count)
                transferStatus = "正在发送 \(end / 1024) KB"
            }
            try Self.requireSuccess(try await sendSerialRequest(Self.watchfaceMessage(6, Data()), responseId: 7))
            try Self.requireSuccess(try await sendSerialRequest(Self.watchfaceMessage(8, Data()), responseId: 9))
            progress = 1
            transferStatus = "已保存到手表"
        } catch {
            transferStatus = "传输失败"
            errorMessage = error.localizedDescription
        }
    }

    private func sendCommand(_ command: String) throws {
        guard let peripheral, let characteristic = linkCharacteristic else { throw BadgeError.notConnected }
        peripheral.writeValue(Data(command.utf8), for: characteristic, type: .withResponse)
    }

    private func sendControl(_ operation: String, payload: String = "") async throws -> String {
        let requestId = nextControlRequestId
        nextControlRequestId = nextControlRequestId % 65535 + 1
        let suffix = payload.isEmpty ? "" : "|\(payload)"
        return try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<String, Error>) in
            let token = UUID()
            waitingControls[requestId] = (token, continuation)
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { [weak self] in
                guard let response = self?.waitingControls[requestId], response.id == token else { return }
                self?.waitingControls[requestId] = nil
                response.continuation.resume(throwing: BadgeError.timeout)
            }
            do {
                try sendCommand("\(ProtocolValue.controlVersion)|\(requestId)|\(operation)\(suffix)")
            } catch {
                self.waitingControls[requestId] = nil
                continuation.resume(throwing: error)
            }
        }
    }

    private func synchronizeTime() async {
        guard isConnected else { return }
        do {
            transferStatus = "正在同步时间"
            let epoch = Int(Date().timeIntervalSince1970)
            let timezoneMinutes = TimeZone.current.secondsFromGMT() / 60
            let response = try await sendControl("TIME", payload: "\(epoch),\(timezoneMinutes)")
            timeStatus = response
            transferStatus = "时间已同步"
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func requestBadgeStatus() async {
        do {
            transferStatus = try await sendControl("BADGE", payload: "STATUS")
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func runBadgeAction(_ action: String) async {
        do {
            transferStatus = try await sendControl("BADGE", payload: action)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func bootstrapControlPlane() async {
        do {
            let hello = try await sendControl("HELLO")
            transferStatus = "已连接 \(hello)"
            await synchronizeTime()
            await requestBadgeStatus()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func sendSerial(_ payload: Data) async throws {
        guard let peripheral, let characteristic = serialCharacteristic else { throw BadgeError.notConnected }
        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        let maximumPacket = peripheral.maximumWriteValueLength(for: writeType)
        let firstCapacity = max(1, maximumPacket - 4)
        let continuationCapacity = max(1, maximumPacket - 2)
        if payload.count <= firstCapacity {
            try await writeSerialPacket(Data([ProtocolValue.serialCategoryWatchface, 0]) + Self.u16(UInt16(payload.count)) + payload,
                                        peripheral: peripheral, characteristic: characteristic, type: writeType)
            return
        }
        var offset = firstCapacity
        try await writeSerialPacket(Data([ProtocolValue.serialCategoryWatchface, 1]) + Self.u16(UInt16(payload.count)) + payload.prefix(firstCapacity),
                                    peripheral: peripheral, characteristic: characteristic, type: writeType)
        while offset < payload.count {
            let end = min(offset + continuationCapacity, payload.count)
            let flag: UInt8 = end == payload.count ? 3 : 2
            try await writeSerialPacket(Data([ProtocolValue.serialCategoryWatchface, flag]) + Data(payload[offset..<end]),
                                        peripheral: peripheral, characteristic: characteristic, type: writeType)
            offset = end
        }
    }

    private func writeSerialPacket(_ packet: Data, peripheral: CBPeripheral, characteristic: CBCharacteristic,
                                   type: CBCharacteristicWriteType) async throws {
        if type == .withoutResponse {
            while !peripheral.canSendWriteWithoutResponse {
                try await waitForSerialWriteAvailability(peripheral)
            }
            peripheral.writeValue(packet, for: characteristic, type: .withoutResponse)
            return
        }

        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            let token = UUID()
            serialWriteWaiter = (token, continuation)
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { [weak self] in
                guard let response = self?.serialWriteWaiter, response.id == token else { return }
                self?.serialWriteWaiter = nil
                response.continuation.resume(throwing: BadgeError.timeout)
            }
            peripheral.writeValue(packet, for: characteristic, type: .withResponse)
        }
    }

    private func waitForSerialWriteAvailability(_ peripheral: CBPeripheral) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            let token = UUID()
            serialReadyWaiter = (token, continuation)
            if peripheral.canSendWriteWithoutResponse {
                serialReadyWaiter = nil
                continuation.resume()
                return
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { [weak self] in
                guard let response = self?.serialReadyWaiter, response.id == token else { return }
                self?.serialReadyWaiter = nil
                response.continuation.resume(throwing: BadgeError.timeout)
            }
        }
    }

    private func sendSerialRequest(_ payload: Data, responseId: UInt16) async throws -> Data {
        return try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Data, Error>) in
            let token = UUID()
            waitingResponses[responseId] = (token, continuation)
            DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
                guard let response = self?.waitingResponses[responseId], response.id == token else { return }
                self?.waitingResponses[responseId] = nil
                response.continuation.resume(throwing: BadgeError.timeout)
            }
            Task {
                do {
                    try await self.sendSerial(payload)
                } catch {
                    guard let response = self.waitingResponses[responseId], response.id == token else { return }
                    self.waitingResponses[responseId] = nil
                    response.continuation.resume(throwing: error)
                }
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

    private func handleControlResponse(_ response: String) {
        let fields = response.split(separator: "|", omittingEmptySubsequences: false)
        guard fields.count >= 3, String(fields[0]) == ProtocolValue.controlVersion,
              let requestId = Int(fields[1]), let waiter = waitingControls.removeValue(forKey: requestId) else { return }
        if fields[2] == "OK" {
            waiter.continuation.resume(returning: fields.dropFirst(3).joined(separator: "|"))
        } else {
            waiter.continuation.resume(throwing: BadgeError.controlRejected(String(fields.dropFirst(3).joined(separator: "|"))))
        }
    }

    private func resetConnection(_ message: String) {
        waitingResponses.values.forEach { $0.continuation.resume(throwing: BadgeError.disconnected) }
        waitingResponses.removeAll()
        waitingControls.values.forEach { $0.continuation.resume(throwing: BadgeError.disconnected) }
        waitingControls.removeAll()
        serialReadyWaiter?.continuation.resume(throwing: BadgeError.disconnected)
        serialReadyWaiter = nil
        serialWriteWaiter?.continuation.resume(throwing: BadgeError.disconnected)
        serialWriteWaiter = nil
        serialAssembly = nil
        linkCharacteristic = nil
        serialCharacteristic = nil
        linkNotificationsEnabled = false
        serialNotificationsEnabled = false
        didBootstrapControlPlane = false
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
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, characteristic.isNotifying else {
            errorMessage = error?.localizedDescription ?? "无法订阅手表通知"
            return
        }
        if characteristic.uuid == ProtocolValue.linkCharacteristic {
            linkNotificationsEnabled = true
        } else if characteristic.uuid == ProtocolValue.serialData {
            serialNotificationsEnabled = true
        }
        guard linkNotificationsEnabled, serialNotificationsEnabled, !didBootstrapControlPlane else { return }
        didBootstrapControlPlane = true
        transferStatus = "已连接"
        Task { await bootstrapControlPlane() }
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        guard let response = serialReadyWaiter else { return }
        serialReadyWaiter = nil
        response.continuation.resume()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == ProtocolValue.serialData, let response = serialWriteWaiter else { return }
        serialWriteWaiter = nil
        if let error {
            response.continuation.resume(throwing: error)
        } else {
            response.continuation.resume()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let value = characteristic.value else { return }
        if characteristic.uuid == ProtocolValue.linkCharacteristic {
            let response = String(decoding: value, as: UTF8.self)
            if response.hasPrefix(ProtocolValue.controlVersion + "|") {
                handleControlResponse(response)
            } else if response.hasPrefix("badge:") {
                transferStatus = response
            }
        } else if characteristic.uuid == ProtocolValue.serialData {
            handleSerial(value)
        }
    }
}

private extension WatchBLEManager {
    enum BadgeError: LocalizedError {
        case notConnected, timeout, disconnected, invalidResponse, rejected(UInt16), invalidImage, controlRejected(String)
        var errorDescription: String? {
            switch self {
            case .notConnected: return "请先连接手表"
            case .timeout: return "手表响应超时"
            case .disconnected: return "手表已断开连接"
            case .invalidResponse: return "手表响应格式错误"
            case .rejected(let code): return "手表拒绝传输，错误码 \(code)"
            case .invalidImage: return "无法处理这张图片"
            case .controlRejected(let code): return "手表协议错误 \(code)"
            }
        }
    }

    static func u16(_ value: UInt16) -> Data { withUnsafeBytes(of: value.littleEndian, { Data($0) }) }
    static func u32(_ value: UInt32) -> Data { withUnsafeBytes(of: value.littleEndian, { Data($0) }) }
    static func readU16(_ data: Data, at offset: Int) -> UInt16 { UInt16(data[offset]) | UInt16(data[offset + 1]) << 8 }

    static func watchfaceMessage(_ id: UInt16, _ data: Data) -> Data { u16(id) + u16(UInt16(data.count)) + data }

    static func requireSuccess(_ message: Data) throws {
        guard message.count >= 4 else { throw BadgeError.invalidResponse }
        let status = readU16(message, at: 2)
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
