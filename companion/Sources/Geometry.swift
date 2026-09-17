import Foundation
import CoreGraphics

struct DesktopGeometry {
    let screens: [CGRect]
    var bounds: CGRect { screens.reduce(CGRect.null) { $0.union($1) } }
    // Only the outside perimeter counts. Shared monitor seams do not switch hosts.
    func edge(at point: CGPoint) -> UInt8 {
        for screen in screens where screen.contains(point) {
            if point.x <= screen.minX + 1 && !screens.contains(where: {
                $0.contains(CGPoint(x: screen.minX - 1, y: point.y))
            }) { return 1 }
            if point.x >= screen.maxX - 2 && !screens.contains(where: {
                $0.contains(CGPoint(x: screen.maxX, y: point.y))
            }) { return 2 }
        }
        return 0
    }
    func normalizedY(_ point: CGPoint) -> UInt16 {
        guard !screens.isEmpty, bounds.height > 1 else { return 32768 }
        return UInt16(max(0, min(65535, ((point.y - bounds.minY) / (bounds.height - 1) * 65535).rounded())))
    }
    func landing(edge: UInt8, height: UInt16) -> CGPoint? {
        guard edge == 1 || edge == 2, !screens.isEmpty else { return nil }
        let y = bounds.minY + Double(height) / 65535 * (bounds.height - 1)
        // Resolve the nearest real display at this height, then its outer side.
        let distance = screens.map { max($0.minY - y, y - ($0.maxY - 1), 0) }.min()!
        let candidates = screens.filter { max($0.minY - y, y - ($0.maxY - 1), 0) <= distance + 0.01 }
        let screen = candidates.sorted { edge == 1 ? $0.minX < $1.minX : $0.maxX > $1.maxX }.first!
        let inset = min(12, screen.width / 4)
        return CGPoint(x: edge == 1 ? screen.minX + inset : screen.maxX - 1 - inset,
                       y: max(screen.minY, min(screen.maxY - 1, y)))
    }
}

struct EdgeStatus {
    let slot: UInt8, selected: UInt8, entry: UInt8, height: UInt16, epoch: UInt32, warp: Bool
    init?(_ data: Data) {
        let p = [UInt8](data)
        guard p.count == 12, p[0] == 1, p[1] < 3, p[2] < 3, p[3] <= 2,
              p[10] <= 1, p[11] == 0, (p[10] == 0 || p[3] != 0) else { return nil }
        slot = p[1]; selected = p[2]; entry = p[3]
        height = UInt16(p[4]) | UInt16(p[5]) << 8
        epoch = (0..<4).reduce(UInt32(0)) { $0 | UInt32(p[6 + $1]) << (8 * $1) }
        warp = p[10] == 1
    }
}
func edgeSample(edge: UInt8, dragging: Bool, enabled: Bool, height: UInt16, epoch: UInt32) -> Data {
    var p: [UInt8] = [1, edge, (dragging ? 1 : 0) | (enabled ? 2 : 0), 0,
                      UInt8(truncatingIfNeeded: height), UInt8(height >> 8), 0, 0, 0, 0, 0, 0]
    for i in 0..<4 { p[6 + i] = UInt8(truncatingIfNeeded: epoch >> (8 * i)) }
    return Data(p)
}
