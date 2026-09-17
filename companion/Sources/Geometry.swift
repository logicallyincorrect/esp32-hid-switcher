import Foundation
import CoreGraphics

struct DesktopGeometry {
    let screens: [CGRect]
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

}

struct EdgeStatus {
    let slot: UInt8, selected: UInt8, epoch: UInt32
    init?(_ data: Data) {
        let p = [UInt8](data)
        guard p.count == 12, p[0] == 1, p[1] < 3, p[2] < 3, p[3] <= 2,
              p[10] <= 1, p[11] == 0, (p[10] == 0 || p[3] != 0) else { return nil }
        slot = p[1]; selected = p[2]
        epoch = (0..<4).reduce(UInt32(0)) { $0 | UInt32(p[6 + $1]) << (8 * $1) }
    }
}
func edgeSample(edge: UInt8, dragging: Bool, enabled: Bool, epoch: UInt32) -> Data {
    var p: [UInt8] = [1, edge, (dragging ? 1 : 0) | (enabled ? 2 : 0), 0,
                      0, 0, 0, 0, 0, 0, 0, 0]
    for i in 0..<4 { p[6 + i] = UInt8(truncatingIfNeeded: epoch >> (8 * i)) }
    return Data(p)
}
