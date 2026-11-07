// godwit-ais: ships nearby, from their AIS broadcasts, on an RTL-SDR dongle.
//
// What it hears goes to stdout, a line each. Takes no commands: the HMI runs it
// only while the Ships app is open, and stdin closing ends it, which hands the
// dongle back.
//
//   ready                the tuner is open
//   error no-tuner       no dongle, or no permission to open it
//   ship <mmsi> <lat> <lon> <speed kn> <course> <type> <position age s> <name> <destination>
//                        a ship heard in the last second, the fields separated
//                        by tabs. "-" for what it has not sent yet, and the last
//                        two empty.
//   gone <mmsi>          not heard for ten minutes
//   rate <frames>        frames decoded in the last second, each second

#include "ais.h"
#include "samplewatch.h"

#include <rtl-sdr.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

namespace {

constexpr uint32_t kBufferBytes = 32768;   // about 57 ms
constexpr uint32_t kBuffers = 6;

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Shared
{
    rtlsdr_dev_t *dev = nullptr;
    ais::Demodulator demod;
    std::vector<ais::Frame> frames;
    std::mutex lock;               // guards the tracker and the count
    ais::Tracker tracker;
    int count = 0;
    SampleWatch watch;
};

void onSamples(unsigned char *buf, uint32_t len, void *ctx)
{
    auto &s = *static_cast<Shared *>(ctx);
    s.watch.heard();
    s.frames.clear();
    s.demod.process(buf, len, s.frames);
    if (s.frames.empty())
        return;
    const double t = now();
    std::lock_guard<std::mutex> hold(s.lock);
    for (const ais::Frame &f : s.frames)
        s.tracker.add(f, t);
    s.count += int(s.frames.size());
}

void report(Shared &s, double &lastReport)
{
    const double t = now();
    std::lock_guard<std::mutex> hold(s.lock);
    for (const auto &entry : s.tracker.tracks()) {
        const ais::Track &a = entry.second;
        if (!ais::confirmed(a) || a.lastHeard < lastReport)
            continue;
        char lat[24] = "-", lon[24] = "-", age[16] = "-", speed[16] = "-", course[16] = "-", type[16] = "-";
        if (a.hasPosition) {
            snprintf(lat, sizeof lat, "%.5f", a.lat);
            snprintf(lon, sizeof lon, "%.5f", a.lon);
            snprintf(age, sizeof age, "%.0f", std::max(0.0, t - a.positionTime));
        }
        if (a.hasSpeed)
            snprintf(speed, sizeof speed, "%.1f", a.speedKn);
        if (a.hasCourse)
            snprintf(course, sizeof course, "%.1f", a.course);
        if (a.shipType)
            snprintf(type, sizeof type, "%d", a.shipType);
        printf("ship\t%u\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", a.mmsi, lat, lon, speed, course, type, age,
               a.name.c_str(), a.destination.c_str());
    }
    for (const uint32_t mmsi : s.tracker.expire(t))
        printf("gone %u\n", mmsi);
    printf("rate %d\n", s.count);
    s.count = 0;
    lastReport = t;
    fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    int gainTenths = -1;    // the tuner's automatic gain
    for (int i = 1; i + 1 < argc; i += 2)
        if (!strcmp(argv[i], "--gain"))
            gainTenths = atoi(argv[i + 1]);

    signal(SIGPIPE, SIG_IGN);

    Shared s;
    if (rtlsdr_get_device_count() == 0 || rtlsdr_open(&s.dev, 0) < 0) {
        printf("error no-tuner\n");
        fflush(stdout);
        return 2;
    }
    rtlsdr_set_sample_rate(s.dev, ais::kSampleRate);
    rtlsdr_set_center_freq(s.dev, ais::kCenterHz);
    rtlsdr_set_tuner_gain_mode(s.dev, gainTenths >= 0 ? 1 : 0);
    if (gainTenths >= 0)
        rtlsdr_set_tuner_gain(s.dev, gainTenths);
    rtlsdr_reset_buffer(s.dev);

    printf("ready\n");
    fflush(stdout);

    std::atomic<bool> gone { false };
    std::thread reader([&] {
        rtlsdr_read_async(s.dev, onSamples, &s, kBuffers, kBufferBytes);
        gone = true;
    });

    double lastReport = now();
    double lastTick = lastReport;
    pollfd in { STDIN_FILENO, POLLIN, 0 };
    bool open = true;
    while (open && !gone) {
        if (s.watch.stalled()) {
            printf("error no-tuner\n");
            fflush(stdout);
            _exit(2);
        }
        if (::poll(&in, 1, 100) > 0) {
            char buf[64];
            if (read(STDIN_FILENO, buf, sizeof buf) <= 0)
                open = false;
        }
        if (now() - lastTick >= 1) {
            lastTick = now();
            report(s, lastReport);
        }
    }

    rtlsdr_cancel_async(s.dev);
    reader.join();
    rtlsdr_close(s.dev);
    // Unplugged, as far as the HMI needs to know: look for it again
    if (gone && open) {
        printf("error no-tuner\n");
        fflush(stdout);
        return 2;
    }
    return 0;
}
