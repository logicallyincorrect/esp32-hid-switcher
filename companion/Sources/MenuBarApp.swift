import AppKit
import ApplicationServices

// Finder launches stay visible in the menu bar; CLI runs remain headless.
final class MenuBarApp: NSObject, NSApplicationDelegate {
    private var item: NSStatusItem!
    private var companion: Companion!
    private var devices: [UUID: String] = [:]
    private var timer: Timer?
    private let menu = NSMenu()
    private let status = NSMenuItem(title: "Starting…", action: nil, keyEquivalent: "")
    private let deviceMenu = NSMenu()
    private let preference = "edge-companion-device"

    func applicationDidFinishLaunching(_ notification: Notification) {
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.image = NSImage(systemSymbolName: "computermouse", accessibilityDescription: "HID Switcher")
        if item.button?.image == nil { item.button?.title = "HID" }
        item.button?.toolTip = "HID Switcher"
        menu.addItem(NSMenuItem(title: "HID Switcher", action: nil, keyEquivalent: ""))
        menu.addItem(status)
        menu.addItem(.separator())
        let computers = NSMenuItem(title: "Choose device", action: nil, keyEquivalent: "")
        computers.submenu = deviceMenu; menu.addItem(computers)
        add("Allow Accessibility…", #selector(permissions))
        add("Bluetooth Settings…", #selector(bluetoothSettings))
        menu.addItem(.separator())
        add("Quit HID Switcher", #selector(quit))
        item.menu = menu
        let saved = UserDefaults.standard.string(forKey: preference).flatMap(UUID.init(uuidString:))
        companion = Companion(device: saved, listing: false)
        companion.onDevice = { [weak self] id, name in
            guard let self, self.devices[id] != name else { return }
            self.devices[id] = name; self.updateDevices()
        }
        updateDevices()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.status.title = self.companion.statusText
            self.item.button?.toolTip = "HID Switcher — " + self.companion.statusText
        }
        // Show the menu on launch so a first-time user can see where the app lives.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.item.button?.performClick(nil) }
    }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        item.button?.performClick(nil); return true
    }
    private func add(_ title: String, _ action: Selector) {
        let entry = NSMenuItem(title: title, action: action, keyEquivalent: "")
        entry.target = self; menu.addItem(entry)
    }
    private func updateDevices() {
        deviceMenu.removeAllItems()
        if devices.isEmpty { deviceMenu.addItem(NSMenuItem(title: "Searching for devices…", action: nil, keyEquivalent: "")) }
        for (id, name) in devices.sorted(by: { $0.value == $1.value ? $0.key.uuidString < $1.key.uuidString : $0.value < $1.value }) {
            let entry = NSMenuItem(title: "\(name) (\(id.uuidString.suffix(6)))", action: #selector(choose(_:)), keyEquivalent: "")
            entry.target = self; entry.representedObject = id.uuidString
            entry.state = companion.device == id ? .on : .off
            deviceMenu.addItem(entry)
        }
    }
    @objc private func choose(_ sender: NSMenuItem) {
        guard let value = sender.representedObject as? String, let id = UUID(uuidString: value) else { return }
        UserDefaults.standard.set(value, forKey: preference)
        companion.selectDevice(id); updateDevices(); status.title = companion.statusText
    }
    @objc private func permissions() {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
        _ = AXIsProcessTrustedWithOptions(options)
        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
    }
    @objc private func bluetoothSettings() {
        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.BluetoothSettings")!)
    }
    @objc private func quit() { NSApplication.shared.terminate(nil) }
}
