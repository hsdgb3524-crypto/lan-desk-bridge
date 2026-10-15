#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../include/protocol.h"

#define MAX_BUFFERS 128
#define DEFAULT_BUFFERS 32
#define MAX_CHUNK_BYTES 4096

typedef struct {
    WAVEHDR hdr;
    char data[MAX_CHUNK_BYTES];
    volatile LONG in_use;
} AudioBuffer;

static volatile BOOL g_running = TRUE;
static AudioBuffer *g_buffers = NULL;
static int g_num_buffers = DEFAULT_BUFFERS;
static HWAVEOUT g_hWaveOut = NULL;
static volatile LONG g_buffers_queued = 0;

static BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        g_running = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hwo; (void)dwInstance; (void)dwParam2;
    if (uMsg == WOM_DONE) {
        WAVEHDR *pHdr = (WAVEHDR *)dwParam1;
        if (pHdr) {
            AudioBuffer *ab = (AudioBuffer *)pHdr->dwUser;
            if (ab) {
                InterlockedExchange(&ab->in_use, 0);
            }
        }
        InterlockedDecrement(&g_buffers_queued);
    }
}

static void print_usage(const char *progname) {
    printf("LAN Audio Receiver (Windows Host)\n");
    printf("Receives low-latency audio stream via UDP and outputs to default audio device.\n\n");
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -p <port>       UDP listen port (default: %d)\n", DEFAULT_AUDIO_PORT);
    printf("  -b <buffers>    Number of ring buffers, 8-128 (default: %d)\n", DEFAULT_BUFFERS);
    printf("  -v              Enable verbose stats logging\n");
    printf("  -h              Show this help message\n");
}

int main(int argc, char *argv[]) {
    SetConsoleCtrlHandler(console_handler, TRUE);

    int port = DEFAULT_AUDIO_PORT;
    int num_buffers = DEFAULT_BUFFERS;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            num_buffers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (num_buffers < 8) num_buffers = 8;
    if (num_buffers > MAX_BUFFERS) num_buffers = MAX_BUFFERS;
    g_num_buffers = num_buffers;

    // Allocate ring buffers
    g_buffers = (AudioBuffer *)calloc(g_num_buffers, sizeof(AudioBuffer));
    if (!g_buffers) {
        fprintf(stderr, "Failed to allocate memory for audio buffers\n");
        return 1;
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        free(g_buffers);
        return 1;
    }

    // Default audio format: 48kHz, 16-bit, Stereo
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(WAVEFORMATEX));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = DEFAULT_CHANNELS;
    wfx.nSamplesPerSec = DEFAULT_SAMPLE_RATE;
    wfx.wBitsPerSample = DEFAULT_BITS_PER_SAMPLE;
    wfx.nBlockAlign = DEFAULT_BYTES_PER_FRAME;
    wfx.nAvgBytesPerSec = DEFAULT_SAMPLE_RATE * DEFAULT_BYTES_PER_FRAME;
    wfx.cbSize = 0;

    MMRESULT mr = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)waveOutProc, 0, CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
        fprintf(stderr, "waveOutOpen failed (code %d)\n", mr);
        WSACleanup();
        free(g_buffers);
        return 1;
    }

    // Prepare ring buffer headers
    for (int i = 0; i < g_num_buffers; i++) {
        ZeroMemory(&g_buffers[i].hdr, sizeof(WAVEHDR));
        g_buffers[i].hdr.lpData = g_buffers[i].data;
        g_buffers[i].hdr.dwBufferLength = DEFAULT_PAYLOAD_BYTES;
        g_buffers[i].hdr.dwUser = (DWORD_PTR)&g_buffers[i];
        g_buffers[i].in_use = 0;
        waveOutPrepareHeader(g_hWaveOut, &g_buffers[i].hdr, sizeof(WAVEHDR));
    }

    // Create UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        waveOutClose(g_hWaveOut);
        WSACleanup();
        free(g_buffers);
        return 1;
    }

    // Enlarge socket receive buffer to 512 KB
    int rcvbuf = 512 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&rcvbuf, sizeof(rcvbuf));

    // Bind to 0.0.0.0:port
    struct sockaddr_in bindAddr;
    ZeroMemory(&bindAddr, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&bindAddr, sizeof(bindAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed on port %d: %d\n", port, WSAGetLastError());
        closesocket(sock);
        waveOutClose(g_hWaveOut);
        WSACleanup();
        free(g_buffers);
        return 1;
    }

    // 1-second timeout for select/recv so clean exit check works
    DWORD timeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    printf("===================================================\n");
    printf(" LAN Audio Receiver (Windows Host)\n");
    printf(" Listening on: UDP 0.0.0.0:%d\n", port);
    printf(" Audio Device: Default (WAVE_MAPPER)\n");
    printf(" Ring Buffers: %d slots (~%d ms buffer ceiling)\n",
           g_num_buffers, g_num_buffers * 5);
    printf(" Output Format: %d Hz, 16-bit, %d ch (Stereo)\n",
           DEFAULT_SAMPLE_RATE, DEFAULT_CHANNELS);
    printf("===================================================\n");

    int write_idx = 0;
    char recv_buf[sizeof(AudioPacketHeader) + MAX_CHUNK_BYTES];
    uint32_t last_seq = 0;
    uint64_t total_packets = 0;
    uint64_t dropped_packets = 0;
    uint64_t drift_drops = 0;
    uint64_t out_of_order = 0;
    DWORD last_stat_time = GetTickCount();

    int max_queue_drift_threshold = (g_num_buffers * 3) / 4; // Drop 1 if queue > 75% full

    while (g_running) {
        struct sockaddr_in senderAddr;
        int senderLen = sizeof(senderAddr);
        int bytes = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&senderAddr, &senderLen);

        if (bytes <= 0) {
            continue;
        }

        const char *pcm_payload = NULL;
        int payload_len = 0;

        // Check for AudioPacketHeader
        if (bytes >= (int)sizeof(AudioPacketHeader)) {
            AudioPacketHeader *hdr = (AudioPacketHeader *)recv_buf;
            if (hdr->magic == LAN_AUDIO_MAGIC) {
                if (bytes == (int)(sizeof(AudioPacketHeader) + hdr->payload_bytes)) {
                    pcm_payload = recv_buf + sizeof(AudioPacketHeader);
                    payload_len = hdr->payload_bytes;

                    // Sequence tracking
                    if (last_seq != 0) {
                        if (hdr->seq <= last_seq && (last_seq - hdr->seq) < 100000) {
                            out_of_order++;
                            continue; // Drop outdated packet
                        } else if (hdr->seq > last_seq + 1) {
                            dropped_packets += (hdr->seq - last_seq - 1);
                        }
                    }
                    last_seq = hdr->seq;
                }
            }
        }

        // Fallback for legacy raw PCM packets (e.g. 960 bytes)
        if (!pcm_payload) {
            if (bytes == DEFAULT_PAYLOAD_BYTES) {
                pcm_payload = recv_buf;
                payload_len = bytes;
            } else {
                continue; // Ignore unrecognized packet
            }
        }

        total_packets++;

        // Clock drift control: If the queue is growing towards capacity, drop this packet
        // to keep delay minimal and prevent buffer overrun
        if (g_buffers_queued > max_queue_drift_threshold) {
            drift_drops++;
            continue;
        }

        // Find available buffer in ring
        AudioBuffer *ab = &g_buffers[write_idx];
        if (ab->in_use) {
            // Buffer collision; advance
            write_idx = (write_idx + 1) % g_num_buffers;
            ab = &g_buffers[write_idx];
        }

        if (payload_len > MAX_CHUNK_BYTES) {
            payload_len = MAX_CHUNK_BYTES;
        }

        memcpy(ab->data, pcm_payload, payload_len);
        ab->hdr.dwBufferLength = payload_len;
        ab->in_use = 1;
        InterlockedIncrement(&g_buffers_queued);

        // Feed to WinMM waveOut
        waveOutWrite(g_hWaveOut, &ab->hdr, sizeof(WAVEHDR));

        write_idx = (write_idx + 1) % g_num_buffers;

        if (verbose) {
            DWORD now = GetTickCount();
            if (now - last_stat_time >= 5000) {
                printf("[Stats] Pkts: %llu, NetDrops: %llu, DriftDrops: %llu, OutOfOrder: %llu, Queued: %ld\n",
                       total_packets, dropped_packets, drift_drops, out_of_order, g_buffers_queued);
                last_stat_time = now;
            }
        }
    }

    printf("\nShutting down LAN Audio Receiver...\n");
    printf("Total packets: %llu, Lost: %llu, Drift drops: %llu\n",
           total_packets, dropped_packets, drift_drops);

    closesocket(sock);
    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        for (int i = 0; i < g_num_buffers; i++) {
            waveOutUnprepareHeader(g_hWaveOut, &g_buffers[i].hdr, sizeof(WAVEHDR));
        }
        waveOutClose(g_hWaveOut);
    }
    WSACleanup();
    free(g_buffers);
    return 0;
}
