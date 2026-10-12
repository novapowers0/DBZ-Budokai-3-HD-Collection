// App icon: a 4-star dragon ball over a night sky, drawn with CoreGraphics
// in the macOS icon layout (824 px rounded square on a 1024 px canvas).
// Usage: swift tools/macos/make_icon.swift <out.png>
import AppKit
import CoreGraphics

let size = 1024
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "icone.png"
let space = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                    space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
  CGColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
          blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

// Background: rounded square with a sky gradient.
let body = CGRect(x: 100, y: 100, width: 824, height: 824)
let squircle = CGPath(roundedRect: body, cornerWidth: 186, cornerHeight: 186, transform: nil)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
ctx.addPath(squircle)
ctx.setFillColor(rgb(0x0B1E5B))
ctx.fillPath()
ctx.restoreGState()

ctx.saveGState()
ctx.addPath(squircle)
ctx.clip()
let sky = CGGradient(colorsSpace: space, colors: [rgb(0x2A5FD0), rgb(0x0A1A52)] as CFArray,
                     locations: [0, 1])!
ctx.drawLinearGradient(sky, start: CGPoint(x: 512, y: 100), end: CGPoint(x: 512, y: 924), options: [])
// Glow behind the ball.
let glow = CGGradient(colorsSpace: space, colors: [rgb(0xFFC860, 0.55), rgb(0xFFC860, 0)] as CFArray,
                      locations: [0, 1])!
ctx.drawRadialGradient(glow, startCenter: CGPoint(x: 512, y: 512), startRadius: 0,
                       endCenter: CGPoint(x: 512, y: 512), endRadius: 430, options: [])
ctx.restoreGState()

// Ball.
let center = CGPoint(x: 512, y: 512)
let radius: CGFloat = 292
let ball = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -18), blur: 40, color: rgb(0x000000, 0.45))
ctx.setFillColor(rgb(0xE8650C))
ctx.fillEllipse(in: ball)
ctx.restoreGState()

ctx.saveGState()
ctx.addEllipse(in: ball)
ctx.clip()
let orange = CGGradient(colorsSpace: space,
                        colors: [rgb(0xFFE7A0), rgb(0xFFB338), rgb(0xF57F12), rgb(0xC24F05)] as CFArray,
                        locations: [0, 0.35, 0.75, 1])!
ctx.drawRadialGradient(orange, startCenter: CGPoint(x: center.x - 95, y: center.y + 110), startRadius: 0,
                       endCenter: center, endRadius: radius, options: [.drawsAfterEndLocation])
ctx.restoreGState()

// Five-pointed star.
func star(at c: CGPoint, outer: CGFloat, rotation: CGFloat) {
  let inner = outer * 0.42
  let path = CGMutablePath()
  for i in 0..<10 {
    let r = i % 2 == 0 ? outer : inner
    let a = rotation + CGFloat(i) * .pi / 5 + .pi / 2
    let p = CGPoint(x: c.x + r * cos(a), y: c.y + r * sin(a))
    if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
  }
  path.closeSubpath()
  ctx.saveGState()
  ctx.addPath(path)
  ctx.setFillColor(rgb(0xD7141E))
  ctx.setShadow(offset: CGSize(width: 0, height: -3), blur: 4, color: rgb(0x7A0A00, 0.5))
  ctx.fillPath()
  ctx.restoreGState()
}

// Four stars in a diamond, slightly rotated (Grandpa Gohan's ball).
let d: CGFloat = 92
star(at: CGPoint(x: center.x - 8, y: center.y + d + 6), outer: 66, rotation: 0.10)
star(at: CGPoint(x: center.x - d - 4, y: center.y - 4), outer: 66, rotation: -0.12)
star(at: CGPoint(x: center.x + d + 2, y: center.y + 8), outer: 66, rotation: 0.18)
star(at: CGPoint(x: center.x + 6, y: center.y - d - 8), outer: 66, rotation: -0.05)

// Specular highlight, top left.
ctx.saveGState()
ctx.addEllipse(in: ball)
ctx.clip()
let shine = CGGradient(colorsSpace: space, colors: [rgb(0xFFFFFF, 0.85), rgb(0xFFFFFF, 0)] as CFArray,
                       locations: [0, 1])!
let hl = CGPoint(x: center.x - 120, y: center.y + 150)
ctx.saveGState()
ctx.translateBy(x: hl.x, y: hl.y)
ctx.rotate(by: 0.6)
ctx.scaleBy(x: 1.0, y: 0.55)
ctx.drawRadialGradient(shine, startCenter: .zero, startRadius: 0, endCenter: .zero, endRadius: 120, options: [])
ctx.restoreGState()
ctx.restoreGState()

let image = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: image)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
print("icon: \(out)")
