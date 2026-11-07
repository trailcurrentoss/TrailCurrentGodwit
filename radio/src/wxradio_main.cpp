// godwit-wxradio: NOAA Weather Radio on an RTL-SDR dongle.
//
// Raw s16le mono audio at 48 kHz goes to stdout, for pw-play. Commands come in on
// stdin, a line each, and the tuner's state goes out on stderr, a line each. The HMI
// runs it only while the Weather Radio app is open, and stdin closing ends it, which
// hands the dongle back.
//
//   in:   tune <hz>         listen to this channel. Only the seven are accepted.
//
//   out:  ready             the tuner is open
//         error no-tuner    no dongle, or no permission to open it
//         tuned <hz>        now on this channel
//         status <snr dB> <modulation> <receiving 0|1>
//         levels <dB> x 7   how far each channel stands above the noise

#include "samplewatch.h"
#include "wxradio.h"

#include <rtl-sdr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

namespace {

constexpr uint32_t kBufferBytes = 16384;    // about 28 ms
constexpr uint32_t kBuffers = 6;

struct Shared
{
    rtlsdr_dev_t *dev = nullptr;
    wx::Receiver rx;
    std::mutex lock;                  // guards the receiver between the threads
    SampleWatch watch;
    std::vector<int16_t> pcm;
    std::vector<char> carry;          // half a sample a full pipe would not take
};

// Audio is written without blocking: the USB callback must not stall, and if the
// player falls behind, a little audio is lost rather than the tuner's data
void writeAudio(Shared &s)
{
    const char *data = reinterpret_cast<const char *>(s.pcm.data());
    const size_t left = s.pcm.size() * sizeof(int16_t);

    if (!s.carry.empty()) {
        const ssize_t n = write(STDOUT_FILENO, s.carry.data(), s.carry.size());
        if (n > 0)
            s.carry.erase(s.carry.begin(), s.carry.begin() + n);
        if (!s.carry.empty()) {
            if (n < 0 && errno == EPIPE)
                _exit(0);
            return;
        }
    }
    const ssize_t n = write(STDOUT_FILENO, data, left);
    if (n < 0) {
        if (errno == EPIPE)
            _exit(0);
        return;
    }
    // Half a sample left behind would turn the rest into noise
    if ((left - size_t(n)) % 2)
        s.carry.assign(data + n, data + n + 1);
}

void onSamples(unsigned char *buf, uint32_t len, void *ctx)
{
    auto &s = *static_cast<Shared *>(ctx);
    s.watch.heard();
    s.pcm.clear();
    {
        std::lock_guard<std::mutex> hold(s.lock);
        s.rx.process(buf, len, s.pcm);
    }
    writeAudio(s);
}

void line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void tune(Shared &s, int channel)
{
    {
        std::lock_guard<std::mutex> hold(s.lock);
        s.rx.setChannel(channel);
    }
    line("tuned %d", wx::kChannelHz[size_t(channel)]);
}

} // namespace

int main(int argc, char **argv)
{
    int channel = 0;
    int gainTenths = -1;    // the tuner's automatic gain
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--freq"))
            channel = std::max(0, wx::channelIndex(atoi(argv[i + 1])));
        else if (!strcmp(argv[i], "--gain"))
            gainTenths = atoi(argv[i + 1]);
    }

    signal(SIGPIPE, SIG_IGN);
    // A short pipe, so changing channel is heard promptly: about 170 ms
    fcntl(STDOUT_FILENO, F_SETPIPE_SZ, 16384);
    fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);

    Shared s;
    if (rtlsdr_get_device_count() == 0 || rtlsdr_open(&s.dev, 0) < 0) {
        line("error no-tuner");
        return 2;
    }
    rtlsdr_set_sample_rate(s.dev, wx::kIqRate);
    rtlsdr_set_center_freq(s.dev, wx::kTunerHz);
    rtlsdr_set_tuner_gain_mode(s.dev, gainTenths >= 0 ? 1 : 0);
    if (gainTenths >= 0)
        rtlsdr_set_tuner_gain(s.dev, gainTenths);
    rtlsdr_reset_buffer(s.dev);

    tune(s, channel);
    line("ready");

    std::atomic<bool> gone { false };
    std::thread reader([&] {
        rtlsdr_read_async(s.dev, onSamples, &s, kBuffers, kBufferBytes);
        gone = true;
    });

    std::string pending;
    pollfd in { STDIN_FILENO, POLLIN, 0 };
    bool open = true;
    int ticks = 0;
    int levelsSent = 0;
    while (open && !gone) {
        if (s.watch.stalled()) {
            line("error no-tuner");
            _exit(2);
        }
        if (::poll(&in, 1, 100) > 0) {
            char buf[256];
            const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n <= 0) {
                open = false;
                break;
            }
            pending.append(buf, size_t(n));
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string cmd = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                int hz = 0;
                if (sscanf(cmd.c_str(), "tune %d", &hz) == 1) {
                    const int index = wx::channelIndex(hz);
                    if (index >= 0)
                        tune(s, index);
                }
            }
        }

        float snr, modulation;
        bool receiving;
        std::array<float, wx::kChannels> levels;
        int blocks;
        {
            std::lock_guard<std::mutex> hold(s.lock);
            snr = s.rx.snrDb();
            modulation = s.rx.modulation();
            receiving = s.rx.receiving();
            levels = s.rx.levelsDb();
            blocks = s.rx.levelBlocks();
        }
        if (++ticks >= 5) {
            ticks = 0;
            line("status %.1f %.3f %d", double(snr), double(modulation), receiving ? 1 : 0);
        }
        if (blocks != levelsSent) {
            levelsSent = blocks;
            line("levels %.1f %.1f %.1f %.1f %.1f %.1f %.1f", double(levels[0]), double(levels[1]), double(levels[2]),
                 double(levels[3]), double(levels[4]), double(levels[5]), double(levels[6]));
        }
    }

    rtlsdr_cancel_async(s.dev);
    reader.join();
    rtlsdr_close(s.dev);
    // Unplugged, as far as the HMI needs to know: look for it again
    if (gone && open) {
        line("error no-tuner");
        return 2;
    }
    return 0;
}
