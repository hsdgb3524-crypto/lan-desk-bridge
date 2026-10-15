#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#include "../include/protocol.h"

static volatile sig_atomic_t g_running = 1;

static void handle_sig(int sig) {
    (void)sig;
    g_running = 0;
}

static uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void print_usage(const char *progname) {
    printf("LAN Audio Sender (Linux -> Windows / LAN)\n");
    printf("Captures desktop audio via PulseAudio/PipeWire and streams via UDP.\n\n");
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -t, --target <ip>      Target IP address (default: 169.254.123.131)\n");
    printf("  -p, --port <port>      Target UDP port (default: %d)\n", DEFAULT_AUDIO_PORT);
    printf("  -d, --device <name>    PulseAudio capture device (default: @DEFAULT_MONITOR@)\n");
    printf("  -r, --rate <hz>        Sample rate in Hz (default: %d)\n", DEFAULT_SAMPLE_RATE);
    printf("  -f, --frames <count>   Frames per UDP packet (default: %d, ~5ms)\n", DEFAULT_CHUNK_FRAMES);
    printf("  -v, --verbose          Enable verbose output\n");
    printf("  -h, --help             Show this help message\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    const char *target_ip = "169.254.123.131";
    int target_port = DEFAULT_AUDIO_PORT;
    const char *device_name = "@DEFAULT_MONITOR@";
    int sample_rate = DEFAULT_SAMPLE_RATE;
    int chunk_frames = DEFAULT_CHUNK_FRAMES;
    int verbose = 0;

    static struct option long_options[] = {
        {"target",  required_argument, 0, 't'},
        {"port",    required_argument, 0, 'p'},
        {"device",  required_argument, 0, 'd'},
        {"rate",    required_argument, 0, 'r'},
        {"frames",  required_argument, 0, 'f'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:p:d:r:f:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': target_ip = optarg; break;
            case 'p': target_port = atoi(optarg); break;
            case 'd': device_name = optarg; break;
            case 'r': sample_rate = atoi(optarg); break;
            case 'f': chunk_frames = atoi(optarg); break;
            case 'v': verbose = 1; break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (sample_rate <= 0 || sample_rate > 192000) {
        fprintf(stderr, "Error: Invalid sample rate: %d\n", sample_rate);
        return 1;
    }
    if (chunk_frames <= 0 || chunk_frames > 4800) {
        fprintf(stderr, "Error: Invalid chunk frames: %d\n", chunk_frames);
        return 1;
    }

    size_t bytes_per_frame = DEFAULT_CHANNELS * (DEFAULT_BITS_PER_SAMPLE / 8);
    size_t payload_bytes = chunk_frames * bytes_per_frame;
    size_t total_packet_bytes = sizeof(AudioPacketHeader) + payload_bytes;

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Set send buffer size to 256 KB
    int sndbuf = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    // Request low-delay DSCP/TOS
    int tos = IPTOS_LOWDELAY;
    setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_port);
    if (inet_pton(AF_INET, target_ip, &target_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid target IP: %s\n", target_ip);
        close(sock);
        return 1;
    }

    printf("===================================================\n");
    printf(" LAN Audio Sender (Linux -> Windows / LAN)\n");
    printf(" Target:      %s:%d\n", target_ip, target_port);
    printf(" Device:      %s\n", device_name);
    printf(" Audio Spec:  %d Hz, 16-bit, %d channels (Stereo)\n", sample_rate, DEFAULT_CHANNELS);
    printf(" Chunk Size:  %d frames (~%.2f ms, %zu payload bytes)\n",
           chunk_frames, (double)chunk_frames * 1000.0 / sample_rate, payload_bytes);
    printf(" Total Packet: %zu bytes (Header: %zu bytes)\n",
           total_packet_bytes, sizeof(AudioPacketHeader));
    printf("===================================================\n");

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = (uint32_t)sample_rate;
    ss.channels = DEFAULT_CHANNELS;

    pa_buffer_attr ba;
    ba.maxlength = (uint32_t)-1;
    ba.tlength = (uint32_t)-1;
    ba.prebuf = (uint32_t)-1;
    ba.minreq = (uint32_t)-1;
    ba.fragsize = (uint32_t)payload_bytes;

    uint8_t *packet_buffer = malloc(total_packet_bytes);
    if (!packet_buffer) {
        fprintf(stderr, "Out of memory\n");
        close(sock);
        return 1;
    }

    AudioPacketHeader *hdr = (AudioPacketHeader *)packet_buffer;
    uint8_t *pcm_data = packet_buffer + sizeof(AudioPacketHeader);

    uint32_t seq = 0;
    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;

    while (g_running) {
        int error = 0;
        if (verbose) {
            printf("Connecting to PulseAudio source '%s'...\n", device_name);
        }

        pa_simple *s = pa_simple_new(NULL, "LanAudioSender", PA_STREAM_RECORD,
                                     device_name, "LAN Audio Stream", &ss, NULL, &ba, &error);
        if (!s) {
            fprintf(stderr, "PulseAudio connection to '%s' failed: %s\n", device_name, pa_strerror(error));
            if (strcmp(device_name, "@DEFAULT_MONITOR@") == 0) {
                // Fallback to default sink/source
                fprintf(stderr, "Trying fallback to NULL default source...\n");
                s = pa_simple_new(NULL, "LanAudioSender", PA_STREAM_RECORD,
                                  NULL, "LAN Audio Stream", &ss, NULL, &ba, &error);
            }
            if (!s) {
                fprintf(stderr, "Retrying in 2 seconds...\n");
                sleep(2);
                continue;
            }
        }

        printf("Streaming desktop audio to %s:%d...\n", target_ip, target_port);

        while (g_running) {
            if (pa_simple_read(s, pcm_data, payload_bytes, &error) < 0) {
                fprintf(stderr, "pa_simple_read() error: %s (will reconnect)\n", pa_strerror(error));
                break;
            }

            hdr->magic = LAN_AUDIO_MAGIC;
            hdr->seq = ++seq;
            hdr->timestamp_ms = get_time_ms();
            hdr->sample_rate = (uint16_t)sample_rate;
            hdr->channels = DEFAULT_CHANNELS;
            hdr->bits = DEFAULT_BITS_PER_SAMPLE;
            hdr->payload_bytes = (uint16_t)payload_bytes;
            hdr->flags = 0;

            ssize_t sent = sendto(sock, packet_buffer, total_packet_bytes, 0,
                                  (struct sockaddr *)&target_addr, sizeof(target_addr));
            if (sent < 0 && g_running) {
                perror("sendto");
            } else {
                total_packets++;
                total_bytes += sent;
            }

            if (verbose && (total_packets % 1000 == 0)) {
                printf("[Sender Stats] Sent %lu packets (%.2f MB)\n",
                       total_packets, (double)total_bytes / (1024.0 * 1024.0));
            }
        }

        pa_simple_free(s);
        if (g_running) {
            sleep(1);
        }
    }

    printf("\nShutting down LAN Audio Sender...\n");
    printf("Total packets sent: %lu, Total bytes: %.2f MB\n",
           total_packets, (double)total_bytes / (1024.0 * 1024.0));

    free(packet_buffer);
    close(sock);
    return 0;
}
