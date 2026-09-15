import Foundation
import CoreBluetooth

let serviceID = CBUUID(string: "4d4c0001-8a15-4b4e-9d84-891537e66000")
let statusID = CBUUID(string: "4d4c0002-8a15-4b4e-9d84-891537e66000")
let commandID = CBUUID(string: "4d4c0003-8a15-4b4e-9d84-891537e66000")
let replyID = CBUUID(string: "4d4c0004-8a15-4b4e-9d84-891537e66000")
var targetID: UUID? = nil
let requestID = UUID().uuidString
var request: [String: Any]? = nil
final class Client: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var finished = false
    var reply: CBCharacteristic?
    let probe = command == "probe"
    func start() {
        central = CBCentralManager(delegate: self, queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + 20) { self.fail("Timed out. Ensure HID Switcher is paired and connected. A submitted command may have applied; check status before retrying.") }
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
            reply = response
            do { let data = try JSONSerialization.data(withJSONObject: request); guard data.count <= 191 else { fail("Command exceeds firmware size limit"); return }; peripheral.writeValue(data, for: command, type: .withResponse) }
            catch { fail(error.localizedDescription) }
        } else {
            guard let c = service.characteristics?.first(where: {$0.uuid == statusID}) else { fail("Status characteristic was not found."); return }
            peripheral.readValue(for: c)
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
            }
            let pretty = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
            print(String(decoding: pretty, as: UTF8.self)); finished = true; exit(0)
        } catch { fail("Invalid status JSON: \(error)") }
    }
}

var args = Array(CommandLine.arguments.dropFirst())
if args.first == "--device" {
    guard args.count >= 3, let id = UUID(uuidString: args[1]) else { usage() }
    targetID = id; args.removeFirst(2)
}
let command = args.first ?? "status"
func usage() -> Never { fputs("Usage: hid-switcher [--device UUID] status | diagnostics | select SLOT | name SLOT NAME | move SLOT TO | wifi on|off | probe | --help | --version\n", stderr); exit(2) }
func slot(_ index: Int) -> Int { guard args.count > index, let n = Int(args[index]), (1...3).contains(n) else { usage() }; return n }
switch command {
case "--help", "-h":
    print("Usage: hid-switcher [--device UUID] status | diagnostics | select SLOT | name SLOT NAME | move SLOT TO | wifi on|off | probe")
    exit(0)
case "--version":
    print("hid-switcher " + (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev")); exit(0)
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
