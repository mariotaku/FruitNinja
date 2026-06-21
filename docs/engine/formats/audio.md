# .wav.pcm Audio Format

## File Layout

```
+0x00: int32  channels       (always 1 = mono)
+0x04: int32  sampleRate     (always 16000 = 16kHz)
+0x08: int32  bitsPerSample  (always 16)
+0x0C: int32  numSamples     (total sample count)
+0x10: int32  reserved       (always 0)
+0x14: byte[] pcmData        (signed 16-bit little-endian PCM)
```

**Header size: 20 bytes.** PCM data starts at offset 0x14.

Total file size = 20 + numSamples * 2.

## Properties

- **Mono**, 16-bit signed little-endian PCM
- **16kHz** sample rate (all files)
- Duration = numSamples / 16000 seconds
- 133 sound effects in `Data/sfx/`

## Conversion to WAV

Add a standard 44-byte RIFF/WAV header in front of the raw PCM data:

```c
// WAV header for FruitNinja PCM
struct WavHeader {
    char     riff[4]    = "RIFF";
    uint32_t fileSize   = 36 + numSamples * 2;
    char     wave[4]    = "WAVE";
    char     fmt[4]     = "fmt ";
    uint32_t fmtSize    = 16;
    uint16_t format     = 1;          // PCM
    uint16_t channels   = 1;          // mono
    uint32_t sampleRate = 16000;
    uint32_t byteRate   = 32000;      // sampleRate * channels * 2
    uint16_t blockAlign = 2;          // channels * 2
    uint16_t bitsPerSample = 16;
    char     data[4]    = "data";
    uint32_t dataSize   = numSamples * 2;
};
// Then append raw PCM from offset 0x14
```

## Sound File Examples

| File | Samples | Duration |
|------|---------|----------|
| bomb-explode.wav.pcm | 70,310 | 4.4s |
| applause-light.wav.pcm | 49,150 | 3.1s |
| angel-combo-1.wav.pcm | 17,600 | 1.1s |
| bamboo-impact-1.wav.pcm | 6,093 | 0.4s |

---

