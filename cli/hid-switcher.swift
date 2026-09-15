import Foundation
import CoreBluetooth
import Darwin

let serviceID = CBUUID(string: "4d4c0001-8a15-4b4e-9d84-891537e66000")
let statusID = CBUUID(string: "4d4c0002-8a15-4b4e-9d84-891537e66000")
let commandID = CBUUID(string: "4d4c0003-8a15-4b4e-9d84-891537e66000")
let replyID = CBUUID(string: "4d4c0004-8a15-4b4e-9d84-891537e66000")
var targetID: UUID? = nil
var requestID = UUID().uuidString
var request: [String: Any]? = nil
func shortcutLabel(_ tuple: [Any]) -> String {
    guard tuple.count == 5, let kind = tuple[1] as? Int, let mods = tuple[2] as? Int,
          let keys = tuple[3] as? [Int], let buttons = tuple[4] as? Int else { return "Invalid binding" }
    if mods == 0 && keys.isEmpty && buttons == 0 { return "Not set" }
    var parts: [String] = []
    for (i, label) in ["Ctrl", "Shift", "Alt", "Cmd"].enumerated() where mods & (1 << i) != 0 { parts.append(label) }
    if kind == 2 {
        let labels = (0..<8).filter {buttons & (1 << $0) != 0}.map {String($0 + 1)}
        parts.append((labels.count == 1 ? "Button " : "Buttons ") + labels.joined(separator: "+"))
    } else {
        for key in keys {
            if (4...29).contains(key) { parts.append(String(UnicodeScalar(65 + key - 4)!)) }
            else if (30...39).contains(key) { parts.append(String((key - 29) % 10)) }
            else { parts.append([40:"Enter",41:"Esc",43:"Tab",44:"Space"][key] ?? String(format:"0x%02X",key)) }
        }
    }
    if (tuple[0] as? Int) == 3 { parts.append("1/2/3") }
    return parts.joined(separator: " + ")
}
final class Client: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var finished = false
    var reply: CBCharacteristic?
    var commandCharacteristic: CBCharacteristic?
    var captureToken: String?
    var interruptSource: DispatchSourceSignal?
    var confirmationSource: DispatchSourceRead?
    var interrupted = false
    var hasSubmitted = false
    let probe = command == "probe"
    func start() {
        signal(SIGINT, SIG_IGN)
        interruptSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
        interruptSource?.setEventHandler {
            self.interrupted = true; self.confirmationSource?.cancel()
            if let token = self.captureToken { self.captureToken = nil; self.send(["op": "shortcut-cancel", "token": token]) }
            else if request?["op"] as? String != "shortcut-record" || !self.hasSubmitted { exit(130) }
        }
        interruptSource?.resume()
        central = CBCentralManager(delegate: self, queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + (command == "shortcut" ? 180 : 20)) { self.fail("Timed out. Ensure HID Switcher is paired and connected. A submitted command may have applied; check status before retrying.") }
    }
    func fail(_ message: String) { if !finished { finished = true; fputs(message + "\n", stderr); exit(1) } }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            if central.state == .unauthorized { fail("Allow Bluetooth access for this CLI in System Settings > Privacy & Security > Bluetooth.") }
            if central.state == .poweredOff { fail("Bluetooth is switched off on this Mac.") }
            return
        }
        let ids = [serviceID]
        let devices = central.retrieveConnectedPeripherals(withServices: ids)
        if probe {
            for d in devices { print("\(d.identifier) \(d.name ?? "unnamed")") }
            print("Connected matching peripherals: \(devices.count)")
            finished = true; exit(0)
        }
        let matches = devices.filter { targetID == nil || $0.identifier == targetID }
        if matches.count > 1 { fail("More than one HID Switcher found; device selection is required.") }
        if let d = matches.first { use(d) }
        else { central.scanForPeripherals(withServices: [serviceID]) }
    }
    func use(_ device: CBPeripheral) {
        guard peripheral == nil else { return }
        peripheral = device; central.stopScan(); device.delegate = self
        central.connect(device)
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) { if targetID == nil || peripheral.identifier == targetID { use(peripheral) } }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { peripheral.discoverServices([serviceID]) }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) { fail(error?.localizedDescription ?? "Connection failed") }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { fail(error.localizedDescription); return }
        guard let service = peripheral.services?.first(where: {$0.uuid == serviceID}) else { fail("BLE configuration service was not found."); return }
        peripheral.discoverCharacteristics(request == nil ? [statusID] : [commandID, replyID], for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error { fail(error.localizedDescription); return }
        if let request {
            guard let command = service.characteristics?.first(where: {$0.uuid == commandID}),
                  let response = service.characteristics?.first(where: {$0.uuid == replyID}) else { fail("CLI command service unavailable; update firmware."); return }
            reply = response; commandCharacteristic = command
            do { let data = try JSONSerialization.data(withJSONObject: request); guard data.count <= 191 else { fail("Command exceeds firmware size limit"); return }; hasSubmitted = true; peripheral.writeValue(data, for: command, type: .withResponse) }
            catch { fail(error.localizedDescription) }
        } else {
            guard let c = service.characteristics?.first(where: {$0.uuid == statusID}) else { fail("Status characteristic was not found."); return }
            peripheral.readValue(for: c)
        }
    }
    func send(_ payload: [String: Any]) {
        guard let peripheral, let commandCharacteristic else { fail("Command service unavailable"); return }
        requestID = UUID().uuidString; request = payload; request!["id"] = requestID
        do { let data = try JSONSerialization.data(withJSONObject: request!); hasSubmitted = true; peripheral.writeValue(data, for: commandCharacteristic, type: .withResponse) }
        catch { fail(error.localizedDescription) }
    }
    func pollRecording() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
            guard !self.finished, let token = self.captureToken else { return }
            self.send(["op": "shortcut-status", "token": token])
        }
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { fail(error.localizedDescription); return }
        if let reply { peripheral.readValue(for: reply) }
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { fail(error.localizedDescription); return }
        guard let data = characteristic.value else { fail("Empty status response"); return }
        do {
            let object = try JSONSerialization.jsonObject(with: data)
            if request != nil {
                guard let response = object as? [String: Any] else { fail("Invalid command response"); return }
                if response["id"] as? String != requestID {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { if !self.finished, let reply = self.reply { peripheral.readValue(for: reply) } }; return
                }
                if response["ok"] as? Bool != true { fail(response["error"] as? String ?? "Command failed"); return }
                let op = request?["op"] as? String
                let result = response["result"] as? [String: Any] ?? [:]
                if op == "shortcut-record" {
                    guard let token = result["token"] as? String else { fail("Missing recording session"); return }
                    captureToken = token
                    if interrupted { captureToken = nil; send(["op": "shortcut-cancel", "token": token]); return }
                    if args.last == "slot" { print("Slot is keyboard-only: press and release just the modifiers (for example Ctrl+Cmd). Numbers 1/2/3 will select the destination.") }
                    print("Release held keys/buttons, then press and release the shortcut on the USB keyboard or mouse attached to the bridge.\nInput is paused while recording. Mouse buttons may include Ctrl/Shift/Alt/Cmd. Press Escape to cancel. Recording expires after 60 seconds.")
                    pollRecording(); return
                }
                if op == "shortcut-status" {
                    let state = result["state"] as? String ?? ""
                    if state == "waiting" || state == "recording" { pollRecording(); return }
                    if state == "ready", let token = captureToken {
                        print("Captured: \(result["label"] as? String ?? "unknown"). Save? [y/N] ", terminator: ""); fflush(stdout)
                        confirmationSource = DispatchSource.makeReadSource(fileDescriptor: STDIN_FILENO, queue: .main)
                        confirmationSource?.setEventHandler {
                            let answer = readLine()?.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
                            self.confirmationSource?.cancel(); self.confirmationSource = nil
                            self.send(["op": answer == "y" || answer == "yes" ? "shortcut-save" : "shortcut-cancel", "token": token])
                        }
                        confirmationSource?.resume(); return
                    }
                    if state == "cancelled" { print("Recording cancelled; shortcut unchanged."); finished = true; exit(0) }
                    fail("Recording expired; shortcut unchanged."); return
                }
            }
            var printable = object
            if var root = object as? [String: Any], var result = root["result"] as? [String: Any], let bindings = result["bindings"] as? [[Any]] {
                result["bindings"] = bindings.map { tuple -> [String: Any] in
                    ["action": ["cycle", "next", "prev", "slot"][tuple[0] as? Int ?? 0], "type": (tuple[1] as? Int) == 2 ? "mouse" : "keyboard", "label": shortcutLabel(tuple)]
                }
                root["result"] = result; printable = root
            }
            let pretty = try JSONSerialization.data(withJSONObject: printable, options: [.prettyPrinted, .sortedKeys])
            print(String(decoding: pretty, as: UTF8.self)); finished = true; exit(interrupted ? 130 : 0)
        } catch { fail("Invalid status JSON: \(error)") }
    }
}

var args = Array(CommandLine.arguments.dropFirst())
if args.first == "--device" {
    guard args.count >= 3, let id = UUID(uuidString: args[1]) else { usage() }
    targetID = id; args.removeFirst(2)
}
let command = args.first ?? "status"
func usage() -> Never { fputs("Usage: hid-switcher [--device UUID] status | shortcuts [reset] | shortcut record cycle|next|prev|slot | shortcut clear ACTION keyboard|mouse | diagnostics | select SLOT | name SLOT NAME | ble-name NAME | move SLOT TO | wifi on|off | probe | --help | --version\n", stderr); exit(2) }
func slot(_ index: Int) -> Int { guard args.count > index, let n = Int(args[index]), (1...3).contains(n) else { usage() }; return n }
switch command {
case "shortcuts":
    guard args.count == 1 || (args.count == 2 && args[1] == "reset") else { usage() }
    request = ["op": args.count == 1 ? "shortcuts" : "shortcuts-reset"]
case "shortcut":
    guard args.count >= 3, let action = ["cycle", "next", "prev", "slot"].firstIndex(of: args[2]) else { usage() }
    if args[1] == "record" {
        guard args.count == 3, isatty(STDIN_FILENO) != 0 else { usage() }
        request = ["op": "shortcut-record", "action": action]
    } else if args[1] == "clear" {
        guard args.count == 4, let kind = ["keyboard", "mouse"].firstIndex(of: args[3]), action != 3 || kind == 0 else { usage() }
        request = ["op": "shortcut-clear", "action": action, "kind": kind + 1]
    } else { usage() }
case "--help", "-h":
    print("Usage: hid-switcher [--device UUID] status | shortcuts [reset] | shortcut record cycle|next|prev|slot | shortcut clear ACTION keyboard|mouse | diagnostics | select SLOT | name SLOT NAME | ble-name NAME | move SLOT TO | wifi on|off | probe")
    exit(0)
case "--version":
    print("hid-switcher " + (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev")); exit(0)
case "ble-name":
    guard args.count == 2, !args[1].trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, args[1].utf8.count <= 29, !args[1].unicodeScalars.contains(where: {$0.value < 32 || (127...159).contains($0.value)}) else { usage() }
    request = ["op": command, "name": args[1]]
case "status", "probe": guard args.count <= 1 else { usage() }
case "diagnostics": guard args.count == 1 else { usage() }; request = ["op": command]
case "select": guard args.count == 2 else { usage() }; request = ["op": command, "slot": slot(1)]
case "name":
    guard args.count == 3, !args[2].isEmpty, args[2].utf8.count <= 32, !args[2].unicodeScalars.contains(where: {$0.value < 32 || $0.value == 127}) else { usage() }
    request = ["op": command, "slot": slot(1), "name": args[2]]
case "move": guard args.count == 3 else { usage() }; request = ["op": command, "slot": slot(1), "to": slot(2)]
case "wifi": guard args.count == 2, ["on", "off"].contains(args[1]) else { usage() }; request = ["op": command, "enabled": args[1] == "on"]
default: usage()
}
if request != nil { request!["id"] = requestID }
let client = Client(); client.start(); RunLoop.main.run()
