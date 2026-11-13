// SPDX-License-Identifier: MIT
// Regenerate on macOS: xcrun swift ps4/resources/make_icon.swift
import AppKit

let size = 512
let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                             bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                             isPlanar: false, colorSpaceName: .deviceRGB,
                             bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
let rect = NSRect(x: 0, y: 0, width: size, height: size)
NSColor(srgbRed: 0.035, green: 0.055, blue: 0.095, alpha: 1).setFill()
rect.fill()

let border = NSBezierPath(roundedRect: rect.insetBy(dx: 25, dy: 25), xRadius: 30, yRadius: 30)
border.lineWidth = 2
NSColor(srgbRed: 0.17, green: 0.23, blue: 0.32, alpha: 1).setStroke()
border.stroke()

let star = NSBezierPath()
for point in 0..<10 {
    let angle = Double.pi / 2 + Double(point) * Double.pi / 5
    let radius = point % 2 == 0 ? 139.0 : 61.0
    let position = NSPoint(x: 256 + cos(angle) * radius, y: 295 + sin(angle) * radius)
    if point == 0 { star.move(to: position) } else { star.line(to: position) }
}
star.close()
NSColor(srgbRed: 0.97, green: 0.72, blue: 0.28, alpha: 1).setFill()
star.fill()

func label(_ value: String, size: CGFloat, y: CGFloat, color: NSColor) {
    let attributes: [NSAttributedString.Key: Any] = [
        .font: NSFont.monospacedSystemFont(ofSize: size, weight: .semibold),
        .foregroundColor: color,
    ]
    let text = value as NSString
    let bounds = text.size(withAttributes: attributes)
    text.draw(at: NSPoint(x: (512 - bounds.width) / 2, y: y), withAttributes: attributes)
}
label("kstar_gfx", size: 44, y: 91, color: .white)
label("GRAPHICS DEMO", size: 17, y: 62,
      color: NSColor(srgbRed: 0.56, green: 0.65, blue: 0.77, alpha: 1))
NSGraphicsContext.restoreGraphicsState()

// AppKit draws into RGBA, but the PS4 shell requires an opaque 24-bit RGB PNG.
let rgb = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
                          bitsPerSample: 8, samplesPerPixel: 3, hasAlpha: false,
                          isPlanar: false, colorSpaceName: .deviceRGB,
                          bytesPerRow: size * 3, bitsPerPixel: 24)!
for y in 0..<size {
    for x in 0..<size {
        let source = bitmap.bitmapData! + y * bitmap.bytesPerRow + x * 4
        let destination = rgb.bitmapData! + y * rgb.bytesPerRow + x * 3
        precondition(source[3] == 255, "PS4 icon pixels must be opaque")
        for component in 0..<3 { destination[component] = source[component] }
    }
}
let output = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("icon0.png")
try rgb.representation(using: .png, properties: [:])!.write(to: output, options: .atomic)
print(output.path)
