#ifndef LAN_AUDIO_PROTOCOL_H
#define LAN_AUDIO_PROTOCOL_H

#include <stdint.h>

#define LAN_AUDIO_MAGIC 0x4155444F // 'AUDO'
#define DEFAULT_AUDIO_PORT 4010
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_CHANNELS 2
#define DEFAULT_BITS_PER_SAMPLE 16
#define DEFAULT_CHUNK_FRAMES 240 // 5ms per packet at 48kHz
#define DEFAULT_BYTES_PER_FRAME (DEFAULT_CHANNELS * (DEFAULT_BITS_PER_SAMPLE / 8)) // 4 bytes
#define DEFAULT_PAYLOAD_BYTES (DEFAULT_CHUNK_FRAMES * DEFAULT_BYTES_PER_FRAME) // 960 bytes

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          // LAN_AUDIO_MAGIC ('AUDO')
    uint32_t seq;            // Monotonically increasing sequence number
    uint32_t timestamp_ms;   // Sender timestamp in milliseconds
    uint16_t sample_rate;    // e.g. 48000
    uint8_t  channels;       // 2 (Stereo)
    uint8_t  bits;           // 16
    uint16_t payload_bytes;  // Length of raw PCM data following header (e.g. 960)
    uint16_t flags;          // Reserved flags (0)
} AudioPacketHeader;
#pragma pack(pop)

#endif // LAN_AUDIO_PROTOCOL_H
