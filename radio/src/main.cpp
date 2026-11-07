// godwit-radio: an FM tuner on an RTL-SDR dongle.
//
// Raw s16le stereo audio at 57 kHz goes to stdout, for pw-play. Commands come
// in on stdin, a line each, and the tuner's state goes out on stderr, a line
// each. The HMI runs it only while the radio is what is heard, and stdin
// closing ends it.
//
//   in:   tune <hz>         listen to this frequency
//         seek <1|-1>       the next station up or down, wrapping around
//         scan              report every station in the band
//
//   out:  ready             the tuner is open
//         error no-tuner    no dongle, or no permission to open it
//         tuned <hz>        now on this frequency; everything below is reset
//         status <snr dB> <rms> <pilot> <stereo 0..1>
//         call <KXYZ>       from RDS: the call sign, the name the station sends,
//         ps <name>           the kind of programme, and what is on
//         pty <n>
//         rt <text>
//         seekdone <hz>     landed; the same frequency when the band is empty
//         station <hz> <snr dB>
//         scandone

#include "fmreceiver.h"
#include "samplewatch.h"

#include <rtl-sdr.h>

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

// The North American FM band's channels
constexpr int kBandLo = 87900000;
constexpr int kBandHi = 107900000;
constexpr int kBandStep = 200000;

// Ignored after a retune: samples in the USB queue from the old frequency,
// then the tuner's settling. The queue is 6 buffers of 16 KB, about 43 ms.
constexpr int kDiscard = fm::kIqRate * 60 / 1000;
constexpr uint32_t kBufferBytes = 16384;
constexpr uint32_t kBuffers = 6;

// What counts as a station when seeking. An empty channel reads 0 to 3 dB and
// even a weak station 9 or more. A spur from the board is steady too, but has
// no modulation on it.
constexpr float kStationSnrDb = 9.0f;
constexpr float kStationRms = 0.03f;

struct Shared
{
    rtlsdr_dev_t *dev = nullptr;
    fm::Receiver rx;
    std::mutex lock;                  // guards the receiver between the threads
    std::atomic<unsigned> retune { 0 };
    unsigned seen = 0;
    std::atomic<bool> silent { false };
    SampleWatch watch;
    std::vector<int16_t> pcm;
    std::vector<char> carry;          // audio a full pipe would not take yet
};

// Audio is written without blocking: the USB callback must not stall, and if
// the player falls behind, a little audio is lost rather than the tuner's data
void writeAudio(Shared &s)
{
    const char *data = reinterpret_cast<const char *>(s.pcm.data());
    size_t left = s.pcm.size() * sizeof(int16_t);

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
    // A partial frame has to be finished, or left and right swap from then on
    const size_t rest = left - size_t(n);
    const size_t partial = rest % 4;
    if (partial)
        s.carry.assign(data + n, data + n + partial);
}

void onSamples(unsigned char *buf, uint32_t len, void *ctx)
{
    auto &s = *static_cast<Shared *>(ctx);
    s.watch.heard();
    s.pcm.clear();
    {
        std::lock_guard<std::mutex> hold(s.lock);
        const unsigned want = s.retune.load();
        if (want != s.seen) {
            s.seen = want;
            s.rx.retune(kDiscard);
        }
        s.rx.setSilent(s.silent.load());
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

class Tuner
{
public:
    explicit Tuner(Shared &s) : m_s(s) {}

    void tune(int hz)
    {
        m_hz = hz;
        rtlsdr_set_center_freq(m_s.dev, uint32_t(hz - fm::kOffsetHz));
        ++m_s.retune;
        // The result of this tuning has not arrived until the callback has seen
        // the new request, so measure only after it has
        m_settling = true;
        m_ps.clear();
        m_rt.clear();
        m_call.clear();
        m_pty = -1;
        line("tuned %d", hz);
    }

    void listen(int hz)
    {
        m_mode = Mode::Listen;
        m_s.silent = false;
        tune(hz);
    }

    void seek(int dir)
    {
        m_mode = Mode::Seek;
        m_dir = dir < 0 ? -1 : 1;
        m_start = m_hz;
        m_s.silent = true;
        step();
    }

    void scan()
    {
        m_mode = Mode::Scan;
        m_start = m_hz;
        m_s.silent = true;
        tune(kBandLo);
    }

    // Called about 20 times a second
    void poll()
    {
        fm::Quality q;
        int measured;
        std::string ps, rt, call;
        int pty;
        {
            std::lock_guard<std::mutex> hold(m_s.lock);
            if (m_s.seen != m_s.retune.load())
                return;
            q = m_s.rx.quality();
            measured = m_s.rx.measured();
            ps = m_s.rx.rds().ps();
            rt = m_s.rx.rds().text();
            call = m_s.rx.rds().callSign();
            pty = m_s.rx.rds().pty();
        }
        if (measured == 0)
            return;
        m_settling = false;

        const bool station = q.snrDb >= kStationSnrDb && q.mpxRms >= kStationRms;
        if (m_mode == Mode::Seek) {
            if (station || next(m_hz) == m_start) {
                const int found = station ? m_hz : m_start;
                m_mode = Mode::Listen;
                m_s.silent = false;
                if (found != m_hz)
                    tune(found);
                line("seekdone %d", found);
            } else {
                step();
            }
            return;
        }
        if (m_mode == Mode::Scan) {
            if (station)
                line("station %d %.1f", m_hz, double(q.snrDb));
            if (m_hz >= kBandHi) {
                line("scandone");
                listen(m_start);
            } else {
                tune(m_hz + kBandStep);
            }
            return;
        }

        if (++m_ticks >= 4) {
            m_ticks = 0;
            line("status %.1f %.3f %d %.2f", double(q.snrDb), double(q.mpxRms), q.pilot ? 1 : 0, double(q.blend));
        }
        if (call != m_call) {
            m_call = call;
            line("call %s", call.c_str());
        }
        if (ps != m_ps) {
            m_ps = ps;
            line("ps %s", ps.c_str());
        }
        if (pty != m_pty) {
            m_pty = pty;
            line("pty %d", pty);
        }
        if (rt != m_rt) {
            m_rt = rt;
            line("rt %s", rt.c_str());
        }
    }

private:
    enum class Mode { Listen, Seek, Scan };

    int next(int hz) const
    {
        hz += m_dir * kBandStep;
        if (hz > kBandHi)
            return kBandLo;
        if (hz < kBandLo)
            return kBandHi;
        return hz;
    }
    void step() { tune(next(m_hz)); }

    Shared &m_s;
    Mode m_mode = Mode::Listen;
    int m_hz = kBandLo;
    int m_start = kBandLo;
    int m_dir = 1;
    int m_ticks = 0;
    bool m_settling = false;
    std::string m_ps, m_rt, m_call;
    int m_pty = -1;
};

} // namespace

int main(int argc, char **argv)
{
    int hz = kBandLo;
    int gainTenths = -1;    // the tuner's automatic gain
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--freq"))
            hz = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--gain"))
            gainTenths = atoi(argv[i + 1]);
    }

    // FM broadcast and nothing else, however it was started: the tune command
    // below holds to the same band
    if (hz < kBandLo || hz > kBandHi)
        hz = kBandLo;

    signal(SIGPIPE, SIG_IGN);
    // A short pipe, so tuning is heard promptly: about 70 ms
    fcntl(STDOUT_FILENO, F_SETPIPE_SZ, 16384);
    fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);

    Shared s;
    if (rtlsdr_get_device_count() == 0 || rtlsdr_open(&s.dev, 0) < 0) {
        line("error no-tuner");
        return 2;
    }
    rtlsdr_set_sample_rate(s.dev, fm::kIqRate);
    rtlsdr_set_tuner_gain_mode(s.dev, gainTenths >= 0 ? 1 : 0);
    if (gainTenths >= 0)
        rtlsdr_set_tuner_gain(s.dev, gainTenths);
    rtlsdr_reset_buffer(s.dev);

    Tuner tuner(s);
    tuner.listen(hz);
    line("ready");

    std::atomic<bool> gone { false };
    std::thread reader([&] {
        rtlsdr_read_async(s.dev, onSamples, &s, kBuffers, kBufferBytes);
        gone = true;
    });

    std::string pending;
    pollfd in { STDIN_FILENO, POLLIN, 0 };
    bool open = true;
    while (open && !gone) {
        if (s.watch.stalled()) {
            line("error no-tuner");
            _exit(2);
        }
        const int ready = ::poll(&in, 1, 50);
        if (ready > 0) {
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
                int value = 0;
                if (sscanf(cmd.c_str(), "tune %d", &value) == 1 && value >= kBandLo && value <= kBandHi)
                    tuner.listen(value);
                else if (sscanf(cmd.c_str(), "seek %d", &value) == 1)
                    tuner.seek(value);
                else if (cmd == "scan")
                    tuner.scan();
            }
        }
        tuner.poll();
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
