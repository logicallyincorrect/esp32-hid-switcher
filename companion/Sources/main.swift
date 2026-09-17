import AppKit
import CoreBluetooth
import ApplicationServices

let serviceID = CBUUID(string: "e963a000-8f4d-4a7b-9b65-62485c81a320")
let sampleID = CBUUID(string: "e963a001-8f4d-4a7b-9b65-62485c81a320")
let statusID = CBUUID(string: "e963a002-8f4d-4a7b-9b65-62485c81a320")
func log(_ message: String) { FileHandle.standardError.write(Data((message + "\n").utf8)) }
func geometry() -> DesktopGeometry {
    var ids = [CGDirectDisplayID](repeating: 0, count: 32), count: UInt32 = 0
    guard CGGetActiveDisplayList(32, &ids, &count) == .success else { return DesktopGeometry(screens: []) }
    return DesktopGeometry(screens: ids.prefix(Int(count)).map { CGDisplayBounds($0) }.filter { !$0.isEmpty })
}
func dragging() -> Bool {
    (0..<8).contains { CGEventSource.buttonState(.combinedSessionState, button: CGMouseButton(rawValue: UInt32($0))!) }
}

final class Companion: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var sample: CBCharacteristic?
    private var statusCharacteristic: CBCharacteristic?
    private var status: EdgeStatus?
    private var timer: Timer?
    private var observers: [NSObjectProtocol] = []
    private var lastWrite: TimeInterval = 0, lastStatus: TimeInterval = 0, lastDiscovery: TimeInterval = -10
    private var writing = false, writeStarted: TimeInterval = 0
    private var lastPacket = Data(), lastWarpEpoch: UInt32?
    private var systemAwake = true, screensAwake = true, sessionActive = true
    private var inhibitUntil: TimeInterval = 0
    private var desktop = geometry(), lastLayout: TimeInterval = 0
    private var warnedPermission = false
    private(set) var device: UUID?
    var onDevice: ((UUID, String) -> Void)?
    private var message = "Looking for HID Switcher…"
    var statusText: String {
        guard central.state == .poweredOn else { return central.state == .unauthorized ? "Allow Bluetooth in System Settings" : "Bluetooth is unavailable" }
        guard device != nil else { return "Choose your HID Switcher below" }
        guard let status else { return message }
        guard enabled else { return AXIsProcessTrusted() ? "Edge switching paused" : "Accessibility permission required" }
        return "Connected · this Mac: slot \(status.slot + 1) · selected: \(status.selected + 1)"
    }
    func selectDevice(_ id: UUID) {
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        clear(); device = id; message = "Looking for HID Switcher…"; discover()
    }
    let listing: Bool
    private var listed = Set<UUID>()
    init(device: UUID?, listing: Bool) {
        self.device = device; self.listing = listing
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
        let nc = NSWorkspace.shared.notificationCenter
        func watch(_ name: Notification.Name, _ action: @escaping () -> Void) {
            observers.append(nc.addObserver(forName: name, object: nil, queue: .main) { _ in action() })
        }
        watch(NSWorkspace.willSleepNotification) { self.systemAwake = false; self.send(force: true) }
        watch(NSWorkspace.didWakeNotification) { self.systemAwake = true; self.resumed() }
        watch(NSWorkspace.screensDidSleepNotification) { self.screensAwake = false; self.send(force: true) }
        watch(NSWorkspace.screensDidWakeNotification) { self.screensAwake = true; self.resumed() }
        watch(NSWorkspace.sessionDidResignActiveNotification) { self.sessionActive = false; self.send(force: true) }
        watch(NSWorkspace.sessionDidBecomeActiveNotification) { self.sessionActive = true; self.resumed() }
        timer = Timer.scheduledTimer(withTimeInterval: 0.04, repeats: true) { _ in self.tick() }
        if listing { DispatchQueue.main.asyncAfter(deadline: .now() + 8) { exit(0) } }
    }
    private var now: TimeInterval { ProcessInfo.processInfo.systemUptime }
    private func resumed() { inhibitUntil = now + 1; lastPacket = Data(); desktop = geometry() }
    private var enabled: Bool { systemAwake && screensAwake && sessionActive && now >= inhibitUntil && AXIsProcessTrusted() && !desktop.screens.isEmpty }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else { clear(); log("Bluetooth unavailable (state \(central.state.rawValue))."); return }
        discover()
    }
    private func clear() {
        peripheral = nil; sample = nil; statusCharacteristic = nil; status = nil
        writing = false; lastPacket = Data(); lastWarpEpoch = nil
    }
    private func discover() {
        guard central.state == .poweredOn, peripheral == nil else { return }
        lastDiscovery = now
        // The HID connection may already be held by macOS, even with advertising stopped.
        let connected = central.retrieveConnectedPeripherals(withServices: [serviceID, CBUUID(string: "1812")])
        for candidate in connected { found(candidate) }
        if peripheral == nil { central.scanForPeripherals(withServices: [serviceID]) }
    }
    private func found(_ candidate: CBPeripheral) {
        onDevice?(candidate.identifier, candidate.name ?? "Unnamed HID device")
        if listing {
            if listed.insert(candidate.identifier).inserted { print("\(candidate.identifier.uuidString)  \(candidate.name ?? "Unnamed HID device")") }
            return
        }
        guard let device, candidate.identifier == device, peripheral == nil else { return }
        peripheral = candidate; candidate.delegate = self
        central.stopScan(); central.connect(candidate)
        lastStatus = now
        message = "Connecting to \(candidate.name ?? device.uuidString)…"
        log(message)
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) { found(peripheral) }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { guard peripheral === self.peripheral else { return }; peripheral.discoverServices([serviceID]) }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) { guard peripheral === self.peripheral else { return }; log("Connection failed: \(error?.localizedDescription ?? "unknown error")"); clear() }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) { guard peripheral === self.peripheral else { return }; log("Disconnected; retrying."); clear() }
    private func failed(_ reason: String) {
        message = reason
        log(reason)
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        clear()
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard peripheral === self.peripheral else { return }
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == serviceID }) else {
            failed("Edge service missing. Install edge-switching firmware on the ESP32 first."); return
        }
        peripheral.discoverCharacteristics([sampleID, statusID], for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard peripheral === self.peripheral else { return }
        guard error == nil else { failed("Could not discover edge characteristics."); return }
        sample = service.characteristics?.first { $0.uuid == sampleID }
        statusCharacteristic = service.characteristics?.first { $0.uuid == statusID }
        guard let characteristic = statusCharacteristic, sample != nil else { failed("Incomplete edge service."); return }
        peripheral.setNotifyValue(true, for: characteristic)
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === self.peripheral else { return }
        if error != nil || !characteristic.isNotifying { failed("Cannot subscribe to edge status; pair the keyboard in Bluetooth settings first.") }
    }
    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        guard peripheral === self.peripheral else { return }
        if invalidatedServices.contains(where: { $0.uuid == serviceID }) { failed("Firmware services changed; reconnecting.") }
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === self.peripheral else { return }
        guard error == nil, characteristic.uuid == statusID, let data = characteristic.value, let next = EdgeStatus(data) else { return }
        lastStatus = now
        if status?.selected != next.selected { log("Selected computer \(next.selected + 1); this computer is slot \(next.slot + 1).") }
        status = next
        if next.warp && next.epoch != lastWarpEpoch {
            // Fail closed: acknowledge only after successful placement on an awake host.
            guard enabled, !dragging(), let point = desktop.landing(edge: next.entry, height: next.height),
                  CGWarpMouseCursorPosition(point) == .success,
                  let actual = CGEvent(source: nil)?.location,
                  abs(actual.x - point.x) <= 2, abs(actual.y - point.y) <= 2 else { send(force: true, deny: true); return }
            lastWarpEpoch = next.epoch
        }
        send(force: true)
    }
    private func send(force: Bool = false, deny: Bool = false) {
        guard let peripheral, peripheral.state == .connected, let sample, let status, !writing else { return }
        guard let point = CGEvent(source: nil)?.location else { return }
        let active = enabled && !deny && (!status.warp || lastWarpEpoch == status.epoch)
        let edge = status.warp && lastWarpEpoch == status.epoch ? UInt8(0) : desktop.edge(at: point)
        let packet = edgeSample(edge: edge, dragging: dragging(), enabled: active,
                                height: desktop.normalizedY(point), epoch: status.epoch)
        // Interior coordinates need only a heartbeat; send edge/drag transitions promptly.
        let changed = lastPacket.count != 12 || packet[1] != lastPacket[1] || packet[2] != lastPacket[2] || packet[6..<10] != lastPacket[6..<10]
        guard force || changed || now - lastWrite >= 0.2 else { return }
        writing = true; writeStarted = now; lastWrite = now; lastPacket = packet
        peripheral.writeValue(packet, for: sample, type: .withResponse)
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral === self.peripheral else { return }
        writing = false
        if let error { failed("Edge write failed: \(error.localizedDescription)") }
        else if status?.warp == true { send() }
    }
    private func tick() {
        if now - lastLayout >= 1 { desktop = geometry(); lastLayout = now }
        if listing { if peripheral == nil && now - lastDiscovery > 3 { discover() }; return }
        if !AXIsProcessTrusted() && !warnedPermission { warnedPermission = true; log("Enable Accessibility for HID Switcher Companion in System Settings. Edge switching is paused.") }
        if peripheral == nil { if now - lastDiscovery > 3 { discover() }; return }
        if (writing && now - writeStarted > 2) || now - lastStatus > 5 { failed("Companion link stalled; reconnecting."); return }
        send()
    }
}

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments.first == "check" {
    print("Accessibility: \(AXIsProcessTrusted() ? "granted" : "required")")
    print("Displays: \(geometry().screens.count)")
    exit(0)
}
if arguments.first == "permissions" {
    let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
    print("Accessibility: \(AXIsProcessTrustedWithOptions(options) ? "granted" : "enable in System Settings")")
    exit(0)
}
let listing = arguments.first == "list"
var device: UUID?
if arguments.count == 3 && arguments[0] == "run" && arguments[1] == "--device" { device = UUID(uuidString: arguments[2]) }
if arguments.isEmpty {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    let delegate = MenuBarApp()
    app.delegate = delegate
    withExtendedLifetime(delegate) { app.run() }
    exit(0)
}
guard listing || device != nil else {
    print("Usage: hid-switcher-companion list | check | permissions | run --device UUID")
    exit(2)
}
let companion = Companion(device: device, listing: listing)
RunLoop.main.run()
