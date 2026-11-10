import AVFoundation
import CoreVideo
import Foundation

let destination = URL(fileURLWithPath: CommandLine.arguments[1])
try? FileManager.default.removeItem(at: destination)
let writer = try AVAssetWriter(outputURL: destination, fileType: .mov)
let settings: [String: Any] = [AVVideoCodecKey: AVVideoCodecType.h264,
                              AVVideoWidthKey: 160, AVVideoHeightKey: 90]
let input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
input.expectsMediaDataInRealTime = false
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input,
    sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32ARGB,
                                  kCVPixelBufferWidthKey as String: 160,
                                  kCVPixelBufferHeightKey as String: 90])
writer.add(input)
writer.startWriting()
writer.startSession(atSourceTime: .zero)
for index in 0..<30 {
    while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.005) }
    var buffer: CVPixelBuffer?
    CVPixelBufferCreate(kCFAllocatorDefault, 160, 90, kCVPixelFormatType_32ARGB, nil, &buffer)
    let pixels = buffer!
    CVPixelBufferLockBaseAddress(pixels, [])
    let base = CVPixelBufferGetBaseAddress(pixels)!.assumingMemoryBound(to: UInt8.self)
    let stride = CVPixelBufferGetBytesPerRow(pixels)
    for y in 0..<90 {
        for x in 0..<160 {
            let pixel = base + y * stride + x * 4
            pixel[0] = 255
            pixel[1] = index < 15 ? 220 : 20
            pixel[2] = index < 15 ? 20 : 220
            pixel[3] = 35
        }
    }
    CVPixelBufferUnlockBaseAddress(pixels, [])
    if !adaptor.append(pixels, withPresentationTime: CMTime(value: Int64(index), timescale: 10)) {
        fatalError(writer.error?.localizedDescription ?? "Unable to append test frame")
    }
}
input.markAsFinished()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
if writer.status != .completed { fatalError(writer.error?.localizedDescription ?? "Unable to write test movie") }
print(destination.path)
