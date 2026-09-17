import Foundation
import CoreGraphics
func expect(_ condition: @autoclosure () -> Bool) { precondition(condition()) }
let side = DesktopGeometry(screens: [CGRect(x: 0,y: 0,width: 1920,height: 1080),CGRect(x: 1920,y: 0,width: 1280,height: 1024)])
expect(side.edge(at: CGPoint(x: 1919,y: 300)) == 0)
expect(side.edge(at: CGPoint(x: 1920,y: 300)) == 0)
expect(side.edge(at: CGPoint(x: 3199,y: 300)) == 2)
expect(side.edge(at: CGPoint(x: 0,y: 300)) == 1)
expect(side.edge(at: CGPoint(x: 1919,y: 1050)) == 2) // Exposed section above/below a shorter neighbor.
let above = DesktopGeometry(screens: [CGRect(x: -1600,y: -900,width: 1600,height: 900),CGRect(x: 0,y: 0,width: 1920,height: 1080)])
expect(above.edge(at: CGPoint(x: -1600,y: -500)) == 1)
expect(DesktopGeometry(screens: []).edge(at: CGPoint(x: 0,y: 0)) == 0)
let sample = edgeSample(edge: 2,dragging: false,enabled: true,epoch: 0x01020304)
expect(Array(sample) == [1,2,2,0,0,0,4,3,2,1,0,0])
let status = EdgeStatus(Data([1,1,0,1,0,128,4,3,2,1,1,0]))!
expect(status.epoch == 0x01020304 && status.slot == 1) // Legacy placement fields are ignored.
expect(EdgeStatus(Data([1,1,0])) == nil)
expect(EdgeStatus(Data([1,1,0,0,0,128,4,3,2,1,1,0])) == nil)
print("Companion geometry and protocol tests passed")
