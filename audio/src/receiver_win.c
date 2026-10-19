#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "../include/protocol.h"

#define MAX_BUFFERS 128
#define DEFAULT_BUFFERS 32
#define MAX_CHUNK_BYTES 4096
#define SILENCE_TIMEOUT_MS 2500  // Release audio device after 2.5s of digital silence
#define DEVICE_CHECK_INTERVAL_MS 1500 // Check if Windows default audio endpoint switched

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
static volatile DWORD g_last_completion_time = 0;
static WCHAR g_current_device_id[256] = {0};

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
        g_last_completion_time = GetTickCount();
    }
}

static BOOL get_default_device_id(WCHAR *out_id, size_t max_len) {
    IMMDeviceEnumerator *pEnum = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnum);
    if (FAILED(hr)) return FALSE;

    IMMDevice *pDev = NULL;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(pEnum, eRender, eMultimedia, &pDev);
    if (FAILED(hr)) {
        IMMDeviceEnumerator_Release(pEnum);
        return FALSE;
    }

    LPWSTR devId = NULL;
    hr = IMMDevice_GetId(pDev, &devId);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr) && devId) {
        wcsncpy(out_id, devId, max_len - 1);
        out_id[max_len - 1] = L'\0';
        CoTaskMemFree(devId);
        ok = TRUE;
    }
    IMMDevice_Release(pDev);
    IMMDeviceEnumerator_Release(pEnum);
    return ok;
}

static void close_audio_device(void) {
    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        if (g_buffers) {
            for (int i = 0; i < g_num_buffers; i++) {
                waveOutUnprepareHeader(g_hWaveOut, &g_buffers[i].hdr, sizeof(WAVEHDR));
                g_buffers[i].in_use = 0;
            }
        }
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }
    g_buffers_queued = 0;
    g_current_device_id[0] = L'\0';
}

static BOOL open_audio_device(int verbose) {
    close_audio_device();

    // Query active default audio endpoint
    get_default_device_id(g_current_device_id, sizeof(g_current_device_id) / sizeof(WCHAR));

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
        if (verbose) {
            fprintf(stderr, "[Audio] waveOutOpen failed (code %d), waiting for valid audio device...\n", mr);
        }
        g_hWaveOut = NULL;
        return FALSE;
    }

    for (int i = 0; i < g_num_buffers; i++) {
        ZeroMemory(&g_buffers[i].hdr, sizeof(WAVEHDR));
        g_buffers[i].hdr.lpData = g_buffers[i].data;
        g_buffers[i].hdr.dwBufferLength = DEFAULT_PAYLOAD_BYTES;
        g_buffers[i].hdr.dwUser = (DWORD_PTR)&g_buffers[i];
        g_buffers[i].in_use = 0;
        waveOutPrepareHeader(g_hWaveOut, &g_buffers[i].hdr, sizeof(WAVEHDR));
    }

    g_last_completion_time = GetTickCount();
    if (verbose) {
        printf("[Audio] Audio output successfully opened (WAVE_MAPPER, 48kHz 16-bit stereo)\n");
    }
    return TRUE;
}

static int is_silent_chunk(const char *data, int bytes) {
    const int16_t *samples = (const int16_t *)data;
    int num_samples = bytes / 2;
    for (int i = 0; i < num_samples; i++) {
        if (abs(samples[i]) > 32) { // 32 / 32768 is negligible noise floor (~ -60dB)
            return 0; // Contains sound
        }
    }
    return 1; // Pure silence
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
    CoInitialize(NULL);

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
            CoUninitialize();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            CoUninitialize();
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
        CoUninitialize();
        return 1;
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        free(g_buffers);
        CoUninitialize();
        return 1;
    }

    // Create UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        free(g_buffers);
        CoUninitialize();
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
        WSACleanup();
        free(g_buffers);
        CoUninitialize();
        return 1;
    }

    // 500ms timeout for select/recv
    DWORD timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    printf("===================================================\n");
    printf(" LAN Audio Receiver (Windows Host, Auto-Detect & Sleep-Safe)\n");
    printf(" Listening on: UDP 0.0.0.0:%d\n", port);
    printf(" Audio Output: Dynamic WAVE_MAPPER (Auto-Endpoint-Follow)\n");
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
    DWORD last_sound_time = 0;
    DWORD last_device_check = 0;

    int max_queue_drift_threshold = (g_num_buffers * 3) / 4; // Drop 1 if queue > 75% full

    while (g_running) {
        struct sockaddr_in senderAddr;
        int senderLen = sizeof(senderAddr);
        int bytes = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&senderAddr, &senderLen);

        DWORD now = GetTickCount();

        if (bytes <= 0) {
            // Socket timed out (no packets from Linux): close audio device to release sound card
            if (g_hWaveOut && (now - last_sound_time >= SILENCE_TIMEOUT_MS)) {
                if (verbose) printf("[Audio] Network idle timeout, releasing audio device\n");
                close_audio_device();
            }
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

        // Silence Gating: Check if current audio chunk is silence
        int silent = is_silent_chunk(pcm_payload, payload_len);
        if (!silent) {
            last_sound_time = now;
        }

        // If continuous silence has lasted > SILENCE_TIMEOUT_MS, release device to allow sleep / device changes
        if (g_hWaveOut && (now - last_sound_time >= SILENCE_TIMEOUT_MS)) {
            if (verbose) printf("[Audio] Silence detected (>2.5s), closing audio handle to save power\n");
            close_audio_device();
            continue;
        }

        // If sound arrived but device is closed, open device on the CURRENT active default endpoint
        if (!g_hWaveOut) {
            if (!silent) {
                if (!open_audio_device(verbose)) {
                    continue; // Device cannot be opened right now
                }
            } else {
                continue; // Skip rendering silence when device is closed
            }
        }

        // Periodic Check: Did Windows default audio endpoint switch (e.g. headphones plugged in / out)?
        if (now - last_device_check >= DEVICE_CHECK_INTERVAL_MS) {
            last_device_check = now;
            WCHAR active_id[256] = {0};
            if (get_default_device_id(active_id, sizeof(active_id) / sizeof(WCHAR))) {
                if (wcscmp(active_id, g_current_device_id) != 0) {
                    if (verbose) {
                        printf("[Audio] Windows default audio endpoint changed, switching output device...\n");
                    }
                    open_audio_device(verbose);
                    if (!g_hWaveOut) continue;
                }
            }
        }

        // Watchdog: If buffers are queued but no callback has fired in > 1200ms, driver may have stalled after sleep
        if (g_buffers_queued > 4 && (now - g_last_completion_time > 1200)) {
            if (verbose) {
                printf("[Audio] Stalled audio pipeline detected, resetting waveOut...\n");
            }
            open_audio_device(verbose);
            if (!g_hWaveOut) continue;
        }

        // Clock drift control: If the queue is growing towards capacity, drop this packet
        if (g_buffers_queued > max_queue_drift_threshold) {
            drift_drops++;
            continue;
        }

        // Find available buffer in ring
        AudioBuffer *ab = &g_buffers[write_idx];
        if (ab->in_use) {
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
        MMRESULT write_res = waveOutWrite(g_hWaveOut, &ab->hdr, sizeof(WAVEHDR));
        if (write_res != MMSYSERR_NOERROR) {
            // Device became invalid (e.g. unplugged or sleep)
            InterlockedDecrement(&g_buffers_queued);
            ab->in_use = 0;
            open_audio_device(verbose);
        }

        write_idx = (write_idx + 1) % g_num_buffers;

        if (verbose && (now - last_stat_time >= 5000)) {
            printf("[Stats] Pkts: %llu, NetDrops: %llu, DriftDrops: %llu, OutOfOrder: %llu, Queued: %ld\n",
                   total_packets, dropped_packets, drift_drops, out_of_order, g_buffers_queued);
            last_stat_time = now;
        }
    }

    printf("\nShutting down LAN Audio Receiver...\n");
    closesocket(sock);
    close_audio_device();
    WSACleanup();
    free(g_buffers);
    CoUninitialize();
    return 0;
}
